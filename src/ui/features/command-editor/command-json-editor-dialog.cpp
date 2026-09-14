#include "ui/features/command-editor/command-json-editor-dialog.h"
#include "ui/shared/dialog-utils.h"
#include "ui/features/ai/ai-assist-dialog.h"
#include "ui/shared/lucide-icons.h"
#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/shared/foldable-json-view.h"
#include "ui/shared/json-syntax-highlighter.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QFont>
#include <QColor>

namespace kai::ui {

CommandJsonEditorDialog::CommandJsonEditorDialog(const core::Command *existingCommand,
                                                 const QString &targetFolderId,
                                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(existingCommand ? utils::tr(QStringLiteral("command_json_editor.title.edit"))
                                   : utils::tr(QStringLiteral("command_json_editor.title.new")));
    setSizeGripEnabled(true);
    resize(760, 600);
    setupUi(existingCommand, targetFolderId);
    centerOnParent(this);
}

QString CommandJsonEditorDialog::templateJsonFor(const QString &targetFolderId)
{
    // Esqueleto de comando shell mínimo com o folder_id de destino.
    QJsonObject obj;
    obj["name"] = QString();
    obj["folder_id"] = targetFolderId;
    obj["type"] = QStringLiteral("shell");
    obj["icon"] = QString();
    obj["command"] = QString();
    obj["working_dir"] = QString();
    obj["is_background"] = false;
    obj["params"] = QJsonArray();
    QJsonObject hooks;
    hooks["pre"] = QJsonArray();
    hooks["post"] = QJsonArray();
    obj["hooks"] = hooks;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

void CommandJsonEditorDialog::setupUi(const core::Command *existingCommand, const QString &targetFolderId)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Cabeçalho (voltar ao modo simples/Formatar/Minificar) pelo helper de dialog-utils.h, igual ao JsonEditorDialog.
    auto *headerLayout = new QHBoxLayout();
    const JsonEditorHeaderButtons buttons = buildJsonEditorHeaderRow(this, headerLayout,
        utils::tr(QStringLiteral("command_json_editor.raw_label")),
        utils::tr(QStringLiteral("command_json_editor.simple_mode")));
    mainLayout->addLayout(headerLayout);
    m_folderId = targetFolderId;
    if (aiAssistAvailable()) {
        auto *aiButton = new QToolButton(this);
        aiButton->setObjectName(QStringLiteral("aiAssistButton"));
        aiButton->setText(utils::tr(QStringLiteral("ai.editor.button")));
        aiButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        aiButton->setIcon(LucideIcons::icon(QStringLiteral("sparkles"), QColor(utils::tokens::accent()), 16));
        aiButton->setToolTip(utils::tr(QStringLiteral("ai.editor.tip")));
        aiButton->setCursor(Qt::PointingHandCursor);
        aiButton->setStyleSheet(buttons.backButton->styleSheet());
        headerLayout->insertWidget(2, aiButton);
        connect(aiButton, &QToolButton::clicked, this, [this]() {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(m_jsonField->toPlainText().toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.invalid_title")),
                    utils::tr(QStringLiteral("command_json_editor.switch_invalid")).arg(parseError.errorString()));
                return;
            }
            core::Command current = core::Command::fromJson(doc.object());
            if (current.folderId.isEmpty()) current.folderId = m_folderId;
            AiAssistDialog assist(current, current.folderId, m_commandsForAi, this);
            if (assist.exec() == QDialog::Accepted && assist.hasResult()) {
                m_result = assist.result();
                m_hasValidResult = true;
                m_assistApplied = true;
                accept();
            }
        });
    }

    // Voltar ao modo simples: converte o JSON em Command (o formulário precisa dos campos
    // estruturados) e fecha com Accepted; se inválido, avisa sem perder o texto.
    connect(buttons.backButton, &QToolButton::clicked, this, [this]() {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(m_jsonField->toPlainText().toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.invalid_title")),
                utils::tr(QStringLiteral("command_json_editor.switch_invalid"))
                    .arg(parseError.errorString()));
            return;
        }
        m_result = core::Command::fromJson(doc.object());
        if (m_result.id.trimmed().isEmpty()) {
            m_result.id = CommandEditorDialog::generateCommandId(m_result.folderId, m_result.name);
        }
        m_hasValidResult = true;
        m_switchToSimple = true;
        accept();
    });
    installEditModeToggleShortcut(this, buttons.backButton);
    connect(buttons.formatButton, &QToolButton::clicked, this, &CommandJsonEditorDialog::handleFormatJsonRequested);
    connect(buttons.minifyButton, &QToolButton::clicked, this, &CommandJsonEditorDialog::handleMinifyJsonRequested);

    // FoldableJsonView: realce de sintaxe + dobras (o mesmo do visualizador de resposta HTTP).
    m_jsonField = new FoldableJsonView(this);
    m_jsonField->setFont(utils::tokens::monoFont());
    m_jsonField->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_jsonField->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; }")
        .arg(utils::tokens::codeBg(), utils::tokens::codeFg(), utils::tokens::codeBorder())
        .arg(utils::tokens::radiusMd()));
    m_jsonField->setGutterColors(QColor(utils::tokens::codeBg()), QColor(utils::tokens::mutedFg()));
    new JsonSyntaxHighlighter(m_jsonField->document());
    if (existingCommand) {
        m_jsonField->setFoldableJsonText(
            QString::fromUtf8(QJsonDocument(existingCommand->toJson()).toJson(QJsonDocument::Indented)));
    } else {
        m_jsonField->setFoldableJsonText(templateJsonFor(targetFolderId));
    }
    mainLayout->addWidget(m_jsonField);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CommandJsonEditorDialog::handleAcceptRequested);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void CommandJsonEditorDialog::handleFormatJsonRequested()
{
    const QString raw = m_jsonField->toPlainText();
    if (raw.trimmed().isEmpty()) {
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.format_title")),
            utils::tr(QStringLiteral("json.error.invalid_format"))
                .arg(parseError.errorString()));
        return;
    }
    m_jsonField->setFoldableJsonText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}

void CommandJsonEditorDialog::handleMinifyJsonRequested()
{
    const QString raw = m_jsonField->toPlainText();
    if (raw.trimmed().isEmpty()) {
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.minify_title")),
            utils::tr(QStringLiteral("json.error.invalid_format"))
                .arg(parseError.errorString()));
        return;
    }
    m_jsonField->setRawText(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void CommandJsonEditorDialog::handleAcceptRequested()
{
    // JSON precisa ser objeto válido com nome e campo principal (comando shell ou URL).
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(m_jsonField->toPlainText().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.invalid_title")),
            utils::tr(QStringLiteral("command_json_editor.parse_error")).arg(parseError.errorString()));
        return;
    }

    const core::Command parsed = core::Command::fromJson(doc.object());
    if (parsed.name.trimmed().isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.name_required.title")),
            utils::tr(QStringLiteral("json_editor.error.name_required.body")));
        return;
    }
    if (parsed.type == core::CommandType::Http) {
        if (!parsed.httpConfig.has_value() || parsed.httpConfig->url.trimmed().isEmpty()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.url_required.title")),
                utils::tr(QStringLiteral("command_json_editor.error.url_required.body")));
            return;
        }
    } else {
        if (parsed.command.trimmed().isEmpty()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.command_required.title")),
                utils::tr(QStringLiteral("command_json_editor.error.command_required.body")));
            return;
        }
    }

    m_result = parsed;
    if (m_result.id.trimmed().isEmpty()) {
        m_result.id = CommandEditorDialog::generateCommandId(m_result.folderId, m_result.name);
    }
    m_hasValidResult = true;
    accept();
}

core::Command CommandJsonEditorDialog::buildCommand() const
{
    return m_result;
}

} // namespace kai::ui
