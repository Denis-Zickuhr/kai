#include "ui/command-json-editor-dialog.h"
#include "ui/dialog-utils.h"
#include "ui/command-editor-dialog.h"
#include "ui/foldable-json-view.h"
#include "ui/json-syntax-highlighter.h"
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
    // Esqueleto pré-preenchido para criação em modo avançado:
    // um comando shell mínimo, já com o folder_id de destino resolvido —
    // o usuário experiente edita o JSON diretamente. Comentado não é
    // possível em JSON; os campos vazios servem de guia.
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

    // Cabeçalho (label + voltar ao modo simples + Formatar + Minificar),
    // construído pelo MESMO helper que JsonEditorDialog (Pasta/Coleção) usa
    // — ver dialog-utils.h. Antes este diálogo tinha seus próprios botões
    // (QPushButton avulsos, sem realce de sintaxe no campo), diferentes do
    // JsonEditorDialog (que só tinha atalho de teclado pra voltar ao modo
    // simples, sem botão visível); agora os TRÊS editores de JSON do app
    // (Comando/Pasta/Coleção) compartilham a mesma construção de botões —
    // resolve a reclamação do usuário: "o botão de voltar para o modo
    // simples, deve ter o mesmo tema do outro botão".
    auto *headerLayout = new QHBoxLayout();
    const JsonEditorHeaderButtons buttons = buildJsonEditorHeaderRow(this, headerLayout,
        utils::tr(QStringLiteral("command_json_editor.raw_label")),
        utils::tr(QStringLiteral("command_json_editor.simple_mode")));
    mainLayout->addLayout(headerLayout);

    // Item 8: alternar de volta para o modo simples (formulário). Tenta
    // parsear o JSON atual; se válido, guarda como resultado a transportar
    // e fecha com Accepted sinalizando a troca. Se inválido, avisa e não
    // troca (não perde o texto do usuário). Diferente de JsonEditorDialog
    // (onde "voltar ao simples" é só reject()): aqui precisa CONVERTER o
    // JSON pro Command antes de fechar, pois o form simples de Comando
    // precisa dos campos já estruturados.
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

    // Editor JetBrains-style (FoldableJsonView): realce de sintaxe + gutter
    // com dobras — mesmo componente usado no visualizador de resposta HTTP
    // e agora nos 3 editores de JSON avançado.
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
    // Valida antes de aceitar (nunca fecha silenciosamente com
    // dados inválidos): JSON precisa ser um objeto válido e ter nome e o
    // campo principal (comando shell ou URL http) preenchido.
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

    // Gera um id se o JSON não trouxe um (criação): reaproveita a mesma
    // regra do editor padrão para consistência.
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
