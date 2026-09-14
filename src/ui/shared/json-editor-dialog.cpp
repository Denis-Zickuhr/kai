#include "ui/shared/json-editor-dialog.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/foldable-json-view.h"
#include "ui/shared/json-syntax-highlighter.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QFont>
#include <QColor>
#include <QPlainTextEdit>

namespace kai::ui {

JsonEditorDialog::JsonEditorDialog(const QString &title, const QJsonObject &initial, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setSizeGripEnabled(true);
    resize(720, 560);
    setupUi(initial);
    centerOnParent(this);
}

void JsonEditorDialog::setupUi(const QJsonObject &initial)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Cabeçalho (label + voltar ao modo simples + Formatar + Minificar)
    // construído por um helper COMPARTILHADO com CommandJsonEditorDialog
    // (ver dialog-utils.h) — os TRÊS editores de JSON cru do app (Comando/
    // Pasta/Coleção) usam a MESMA função pra criar estes botões, então
    // ficam visualmente idênticos por construção (pedido do usuário: "o
    // botão de voltar para o modo simples, deve ter o mesmo tema do outro
    // botão").
    auto *headerLayout = new QHBoxLayout();
    const JsonEditorHeaderButtons buttons = buildJsonEditorHeaderRow(this, headerLayout,
        utils::tr(QStringLiteral("json_editor.raw_label")),
        utils::tr(QStringLiteral("command_json_editor.simple_mode")));
    mainLayout->addLayout(headerLayout);

    // Botão de voltar ao modo simples: neste fluxo (coleção/pasta abertas
    // via form simples), "modo simples" É fechar este editor sem aplicar
    // nada além do que já foi aceito antes de abrir — reject() devolve o
    // controle ao formulário, exatamente o que o atalho de teclado já fazia
    // (agora com uma affordance visível também).
    connect(buttons.backButton, &QToolButton::clicked, this, &QDialog::reject);
    installEditModeToggleShortcut(this, buttons.backButton);
    connect(buttons.formatButton, &QToolButton::clicked, this, &JsonEditorDialog::handleFormat);
    connect(buttons.minifyButton, &QToolButton::clicked, this, &JsonEditorDialog::handleMinify);

    // Editor JetBrains-style (FoldableJsonView, mesmo componente do
    // visualizador de resposta HTTP): realce de sintaxe + gutter com
    // dobras — NÃO read-only aqui (diferente do viewer), este é um editor
    // de verdade.
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
    m_jsonField->setFoldableJsonText(
        QString::fromUtf8(QJsonDocument(initial).toJson(QJsonDocument::Indented)));
    mainLayout->addWidget(m_jsonField, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &JsonEditorDialog::handleAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void JsonEditorDialog::handleFormat()
{
    const QString raw = m_jsonField->toPlainText();
    if (raw.trimmed().isEmpty()) {
        return;
    }
    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.format_title")),
            utils::tr(QStringLiteral("json.error.invalid_format")).arg(e.errorString()));
        return;
    }
    // setFoldableJsonText (em vez de setPlainText) recalcula os pares de
    // dobra sobre o texto reformatado — senão o gutter ficaria desalinhado
    // com o novo texto após reindentar.
    m_jsonField->setFoldableJsonText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}

void JsonEditorDialog::handleMinify()
{
    const QString raw = m_jsonField->toPlainText();
    if (raw.trimmed().isEmpty()) {
        return;
    }
    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.minify_title")),
            utils::tr(QStringLiteral("json.error.invalid_format")).arg(e.errorString()));
        return;
    }
    // JSON minificado não tem quebras de linha pra dobrar — texto cru, sem
    // cálculo de dobras (setRawText), evitando um gutter vazio/confuso
    // sobre uma única linha gigante.
    m_jsonField->setRawText(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void JsonEditorDialog::handleAccept()
{
    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(m_jsonField->toPlainText().toUtf8(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.invalid_title")),
            utils::tr(QStringLiteral("json_editor.parse_error")).arg(e.errorString()));
        return;
    }
    m_result = doc.object();
    accept();
}

} // namespace kai::ui
