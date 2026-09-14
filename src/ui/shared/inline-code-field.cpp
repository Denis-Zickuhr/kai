#include "ui/shared/inline-code-field.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/json-syntax-highlighter.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontMetrics>
#include <QTextDocument>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace kai::ui {
namespace tk = kai::utils::tokens;

InlineCodeField::InlineCodeField(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("inlineCodeField"));
    // Container TRANSPARENTE. A regra global "QWidget { background-color: bg }"
    // (theme-manager) pinta o fundo ESCURO em TODA a área deste widget —
    // ao redor do editor de código e do botão de expandir —, criando uma
    // faixa/moldura "fora do campo" (relatado). Transparente, só o editor
    // (m_edit, com seu codeAreaQss próprio) pinta o fundo, no tamanho do
    // campo, e o resto mostra o surface2 do card por trás. Qualificado por
    // objectName pra não vazar a transparência a outros QWidget.
    setStyleSheet(QStringLiteral("QWidget#inlineCodeField { background: transparent; }"));
    // O widget acompanha o conteúdo e NÃO estica: sem isto o QFormLayout
    // esticava a linha e sobrava um vão grande acima e abaixo do campo (bug
    // reportado: "ficou muito espaço em cima e em baixo").
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(1));

    m_edit = new QPlainTextEdit(this);
    m_edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_edit->setFrameShape(QFrame::NoFrame);
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // FONTE APLICADA NO WIDGET, não apenas via QSS. O cálculo de altura usa
    // QFontMetrics(m_edit->font()), e o font-family do QSS NÃO altera
    // widget->font() — então a altura era calculada com a fonte errada e o texto
    // ficava desalinhado/cortado dentro do campo ("texto torto", relatado).
    {
        QFont mono = m_edit->font();
        mono.setFamilies({QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Code"),
                          QStringLiteral("Fira Code"), QStringLiteral("Ubuntu Mono"),
                          QStringLiteral("Consolas"), QStringLiteral("monospace")});
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(tk::fontSizePt());
        m_edit->setFont(mono);
    }
    m_editPadding = tk::space(1);
    m_edit->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 padding: %2px; }")
                              .arg(tk::codeAreaQss())
                              .arg(m_editPadding));
    // Sem margem interna extra do documento: ela some da conta da altura e
    // empurra a primeira linha para baixo.
    m_edit->document()->setDocumentMargin(0);
    layout->addWidget(m_edit, 1);

    // Botão de expandir alinhado ao TOPO: o campo cresce para baixo, então
    // manter o botão no topo evita que ele "pule" conforme o usuário digita.
    m_expandButton = new QToolButton(this);
    m_expandButton->setObjectName(QStringLiteral("inlineExpandButton"));
    m_expandButton->setAutoRaise(true);
    m_expandButton->setCursor(Qt::PointingHandCursor);
    m_expandButton->setIcon(LucideIcons::icon(QStringLiteral("maximize-2"),
                                              QColor(tk::mutedFg()), 14));
    m_expandButton->setToolTip(utils::tr(QStringLiteral("inline_code_field.expand_tooltip")));
    // Botão compacto, do tamanho de uma linha de texto — antes usava um
    // tamanho maior que a própria altura mínima do campo.
    const int btn = tk::space(5);
    m_expandButton->setFixedSize(btn, btn);
    layout->addWidget(m_expandButton, 0, Qt::AlignTop);

    connect(m_expandButton, &QToolButton::clicked, this, &InlineCodeField::expand);
    connect(m_edit, &QPlainTextEdit::textChanged, this, [this]() {
        updateHeightForContent();
        emit textChanged();
    });

    auto *sc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+E")), m_edit);
    sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc, &QShortcut::activated, this, &InlineCodeField::expand);

    updateHeightForContent();
}

QString InlineCodeField::toPlainText() const
{
    return m_edit->toPlainText();
}

void InlineCodeField::setPlainText(const QString &text)
{
    m_edit->setPlainText(text);
    updateHeightForContent();
}

void InlineCodeField::setPlaceholderText(const QString &text)
{
    m_edit->setPlaceholderText(text);
}

void InlineCodeField::setEditorTitle(const QString &title)
{
    m_editorTitle = title;
}

void InlineCodeField::setLineRange(int minLines, int maxLines)
{
    m_minLines = qMax(1, minLines);
    m_maxLines = qMax(m_minLines, maxLines);
    updateHeightForContent();
}

void InlineCodeField::setJsonSyntax(bool enabled)
{
    if (m_jsonSyntax == enabled) {
        return;
    }
    m_jsonSyntax = enabled;
    if (enabled) {
        new JsonSyntaxHighlighter(m_edit->document());
    }
}

void InlineCodeField::setPlainField(bool plain)
{
    // Fundo de campo NORMAL (bg do tema) em vez do fundo de editor de código
    // (codeBg). Mantém o mesmo padding calculado. Segue a diretriz de bordas:
    // border-radius pelo token da preferência de canto.
    namespace tk = kai::utils::tokens;
    if (plain) {
        m_edit->setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3;"
            " border-radius: %4px; padding: %5px; }")
            .arg(tk::bg(), tk::fg(), tk::borderColor())
            .arg(tk::radiusMd()).arg(m_editPadding));
    } else {
        m_edit->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 padding: %2px; }")
            .arg(tk::codeAreaQss()).arg(m_editPadding));
    }
}

// Cresce com o conteúdo entre minLines e maxLines. Passando do teto, aparece a
// barra de rolagem (e o usuário pode expandir para editar com folga).
void InlineCodeField::updateHeightForContent()
{
    const QFontMetrics fm(m_edit->font());
    const int lineHeight = fm.lineSpacing();
    const int contentLines = qMax(1, m_edit->document()->blockCount());
    const int lines = qBound(m_minLines, contentLines, m_maxLines);
    // A altura precisa somar o padding do QSS DAS DUAS pontas (topo e base) e a
    // borda de 1px de cada lado; contar só uma vez cortava a última linha e
    // deslocava o texto verticalmente.
    const int chrome = (m_editPadding * 2) + 2;
    const int h = lineHeight * lines + chrome;
    m_edit->setFixedHeight(h);
    // Fixa a altura do CONTÊINER também, senão o layout reserva espaço extra.
    setFixedHeight(h);
    updateGeometry();
}

void InlineCodeField::expand()
{
    QDialog dialog(this);
    dialog.setWindowTitle(m_editorTitle.isEmpty()
                              ? utils::tr(QStringLiteral("inline_code_field.default_title"))
                              : m_editorTitle);
    dialog.resize(860, 600);
    centerOnParent(&dialog);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(3));
    layout->setSpacing(tk::space(3));

    auto *big = new QPlainTextEdit(&dialog);
    big->setPlainText(m_edit->toPlainText());
    big->setFrameShape(QFrame::NoFrame);
    // Herda o modo de quebra de linha do campo compacto (em vez de sempre
    // NoWrap): comando/BODY continuam sem quebra (código, uma linha lógica
    // longa é normal), mas o ParameterFormDialog passou a reusar este
    // widget para o parâmetro Textarea (texto livre), que quer WidgetWidth
    // — sem isto o popup de edição cortava o texto na borda em vez de
    // quebrar, inconsistente com o campo compacto que o abriu.
    big->setLineWrapMode(m_edit->lineWrapMode());
    big->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 padding: %2px; }")
                           .arg(tk::codeAreaQss())
                           .arg(tk::space(2)));
    if (m_jsonSyntax) {
        new JsonSyntaxHighlighter(big->document());
    }
    layout->addWidget(big, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    // Traduz Ok/Cancel (i18n §8 do AGENTS.md) — ver mesmo fix em
    // export-selection-dialog.cpp.
    kai::ui::stripDialogButtonIcons(buttons);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    big->setFocus();
    if (dialog.exec() == QDialog::Accepted) {
        m_edit->setPlainText(big->toPlainText());
        updateHeightForContent();
    }
}

} // namespace kai::ui
