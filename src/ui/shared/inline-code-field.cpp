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
    // Container transparente: a regra global de QWidget pintaria o fundo escuro ao redor do editor e do botão.
    // Só o editor (m_edit) pinta o fundo. Qualificado por objectName para não vazar.
    setStyleSheet(QStringLiteral("QWidget#inlineCodeField { background: transparent; }"));
    // Acompanha o conteúdo e não estica (senão o QFormLayout deixava um vão acima e abaixo).
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
    // Fonte aplicada no widget, não só via QSS: a altura usa QFontMetrics(m_edit->font()) e o font-family do QSS
    // não altera widget->font().
    {
        QFont mono = m_edit->font();
        mono.setFamilies({QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Code"),
                          QStringLiteral("Fira Code"), QStringLiteral("Ubuntu Mono"),
                          QStringLiteral("Consolas"), QStringLiteral("monospace")});
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(tk::fontSizePt());
        m_edit->setFont(mono);
    }
    m_editPadding = tk::space(2);
    m_editPaddingX = tk::space(3);
    setPlainField(true);
    // Sem margem interna do documento: ela entraria fora da conta da altura.
    m_edit->document()->setDocumentMargin(0);
    layout->addWidget(m_edit, 1);

    // Botão no topo: o campo cresce para baixo, e assim ele não "pula" ao digitar.
    m_expandButton = new QToolButton(this);
    m_expandButton->setObjectName(QStringLiteral("inlineExpandButton"));
    m_expandButton->setAutoRaise(true);
    m_expandButton->setCursor(Qt::PointingHandCursor);
    m_expandButton->setIcon(LucideIcons::icon(QStringLiteral("maximize-2"),
                                              QColor(tk::mutedFg()), 14));
    m_expandButton->setToolTip(utils::tr(QStringLiteral("inline_code_field.expand_tooltip")));
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
    // Fundo de campo normal (bg do tema) em vez do de editor de código (codeBg), com o mesmo padding.
    namespace tk = kai::utils::tokens;
    if (plain) {
        m_edit->setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3;"
            " border-radius: %4px; padding: %5px %6px; }")
            .arg(tk::bg(), tk::fg(), tk::borderColor())
            .arg(tk::radiusMd()).arg(m_editPadding).arg(m_editPaddingX));
    } else {
        m_edit->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 padding: %2px %3px; }")
            .arg(tk::codeAreaQss()).arg(m_editPadding).arg(m_editPaddingX));
    }
}

// Cresce com o conteúdo entre minLines e maxLines; acima do teto aparece a barra de rolagem.
void InlineCodeField::updateHeightForContent()
{
    const QFontMetrics fm(m_edit->font());
    const int lineHeight = fm.lineSpacing();
    const int contentLines = qMax(1, m_edit->document()->blockCount());
    const int lines = qBound(m_minLines, contentLines, m_maxLines);
    // Soma o padding do QSS das duas pontas e a borda de 1px de cada lado.
    const int chrome = (m_editPadding * 2) + 2;
    const int h = lineHeight * lines + chrome;
    m_edit->setFixedHeight(h);
    // Fixa também a altura do contêiner, senão o layout reserva espaço extra.
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
    // Herda o modo de quebra do campo compacto: comando/BODY sem quebra, Textarea (texto livre) com WidgetWidth.
    big->setLineWrapMode(m_edit->lineWrapMode());
    big->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 padding: %2px; }")
                           .arg(tk::codeAreaQss())
                           .arg(tk::space(2)));
    if (m_jsonSyntax) {
        new JsonSyntaxHighlighter(big->document());
    }
    layout->addWidget(big, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
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
