#include "ui/features/docs/doc-code-editor.h"

#include "ui/features/docs/doc-highlighter.h"
#include "utils/design-tokens.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {
constexpr int kIndentWidth = 2;
constexpr int kMinZoom = 60;
constexpr int kMaxZoom = 250;
} // namespace

// A calha com os números de linha: só desenha; quem sabe os números é o editor.
class DocLineNumberArea : public QWidget {
public:
    explicit DocLineNumberArea(DocCodeEditor *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->paintLineNumbers(event); }

private:
    DocCodeEditor *m_editor;
};

DocCodeEditor::DocCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("docCodeEditor"));
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabChangesFocus(false);
    m_gutter = new DocLineNumberArea(this);
    m_highlighter = new DocHighlighter(document());
    m_validation = new QTimer(this);
    m_validation->setSingleShot(true);
    m_validation->setInterval(300);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this]() { updateGutter(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &DocCodeEditor::updateGutterRect);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() { rebuildExtraSelections(); });
    connect(this, &QPlainTextEdit::textChanged, this, [this]() { m_validation->start(); });
    connect(m_validation, &QTimer::timeout, this, &DocCodeEditor::validateNow);
    refreshStyle();
    updateGutter();
}

DocCodeEditor::~DocCodeEditor() = default;

void DocCodeEditor::setLanguage(texttools::Language language)
{
    m_language = language;
    m_highlighter->setLanguage(language);
    validateNow();
}

void DocCodeEditor::validateNow()
{
    m_validation->stop();
    texttools::Issue issue;
    const QString text = toPlainText();
    switch (m_language) {
    case texttools::Language::Json: issue = texttools::validateJson(text); break;
    case texttools::Language::Yaml: issue = texttools::validateYaml(text); break;
    case texttools::Language::Xml: issue = texttools::validateXml(text); break;
    default: break;
    }
    const bool changed = issue.ok != m_issue.ok || issue.line != m_issue.line || issue.column != m_issue.column
        || issue.message != m_issue.message;
    m_issue = issue;
    rebuildExtraSelections();
    m_gutter->update();
    if (changed) {
        emit issueChanged(m_issue);
    }
}

void DocCodeEditor::refreshStyle()
{
    QFont font(tk::monoFamily());
    font.setFixedPitch(true);
    font.setPointSizeF(tk::fontSizePt() * m_zoom / 100.0);
    setFont(font);
    setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * kIndentWidth);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Base, QColor(tk::bg()));
    palette.setColor(QPalette::Text, QColor(tk::fg()));
    palette.setColor(QPalette::Highlight, QColor(tk::selBg()));
    palette.setColor(QPalette::HighlightedText, QColor(tk::fg()));
    setPalette(palette);
    setStyleSheet(QStringLiteral("QPlainTextEdit { background-color: %1; color: %2; border: none; }").arg(tk::bg(), tk::fg()));
    m_highlighter->refresh();
    updateGutter();
    rebuildExtraSelections();
}

void DocCodeEditor::setZoomPercent(int percent)
{
    m_zoom = std::clamp(percent, kMinZoom, kMaxZoom);
    refreshStyle();
}

void DocCodeEditor::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            setZoomPercent(m_zoom + (delta > 0 ? 10 : -10));
        }
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

// ------------------------------------------------------------------------------------------------------------- calha

int DocCodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    for (int count = std::max(1, blockCount()); count >= 10; count /= 10) {
        ++digits;
    }
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * std::max(3, digits);
}

void DocCodeEditor::updateGutter()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    m_gutter->update();
}

void DocCodeEditor::updateGutterRect(const QRect &rect, int dy)
{
    if (dy != 0) {
        m_gutter->scroll(0, dy);
    } else {
        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateGutter();
    }
}

void DocCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void DocCodeEditor::paintLineNumbers(QPaintEvent *event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), QColor(tk::altBg()));
    painter.setFont(font());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = int(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + int(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber();
    const int errorLine = m_issue.ok ? -1 : m_issue.line - 1;
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const bool isError = number == errorLine;
            if (isError) {
                painter.fillRect(0, top, m_gutter->width(), bottom - top, QColor(tk::errorFg()).darker(300));
            }
            painter.setPen(isError ? QColor(tk::errorFg()) : QColor(number == current ? tk::fg() : tk::mutedFg()));
            painter.drawText(0, top, m_gutter->width() - 8, fontMetrics().height(), Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + int(blockBoundingRect(block).height());
        ++number;
    }
}

// --------------------------------------------------------------------------------------------------- realces extras

void DocCodeEditor::setSearchSelections(const QList<QTextEdit::ExtraSelection> &selections)
{
    m_searchSelections = selections;
    rebuildExtraSelections();
}

void DocCodeEditor::rebuildExtraSelections()
{
    QList<QTextEdit::ExtraSelection> all;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection line;
        QColor color(tk::selBg());
        color.setAlpha(60);
        line.format.setBackground(color);
        line.format.setProperty(QTextFormat::FullWidthSelection, true);
        line.cursor = textCursor();
        line.cursor.clearSelection();
        all.append(line);
    }
    if (!m_issue.ok && m_issue.line > 0) {
        QTextBlock block = document()->findBlockByNumber(m_issue.line - 1);
        if (block.isValid()) {
            QTextEdit::ExtraSelection error;
            QColor color(tk::errorFg());
            color.setAlpha(45);
            error.format.setBackground(color);
            error.format.setProperty(QTextFormat::FullWidthSelection, true);
            error.cursor = QTextCursor(block);
            all.append(error);
            // Sublinhado ondulado no ponto do erro (um caractere, ou o fim da linha).
            QTextEdit::ExtraSelection mark;
            mark.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            mark.format.setUnderlineColor(QColor(tk::errorFg()));
            mark.cursor = QTextCursor(block);
            const int column = std::clamp(m_issue.column - 1, 0, std::max(0, block.length() - 1));
            mark.cursor.setPosition(block.position() + column);
            mark.cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            if (!mark.cursor.hasSelection()) {
                mark.cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
            }
            all.append(mark);
        }
    }
    // Seleções dos cursores extras (multi-cursor).
    for (const QTextCursor &c : std::as_const(m_extra)) {
        if (c.hasSelection()) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = c;
            sel.format.setBackground(QColor(tk::selBg()));
            all.append(sel);
        }
    }
    // O colchete que casa com o do cursor ganha um fundo discreto (o do cursor e o par).
    if (!isReadOnly()) {
        const int here = textCursor().position();
        for (const int index : {here, here - 1}) {
            const int match = matchingBracket(index);
            if (match < 0) {
                continue;
            }
            QColor tint(tk::accent());
            tint.setAlpha(70);
            for (const int at : {index, match}) {
                QTextEdit::ExtraSelection sel;
                sel.cursor = QTextCursor(document());
                sel.cursor.setPosition(at);
                sel.cursor.setPosition(at + 1, QTextCursor::KeepAnchor);
                sel.format.setBackground(tint);
                all.append(sel);
            }
            break;
        }
    }
    all.append(m_searchSelections);
    setExtraSelections(all);
}

void DocCodeEditor::goToLine(int line, int column)
{
    QTextBlock block = document()->findBlockByNumber(std::max(0, line - 1));
    if (!block.isValid()) {
        block = document()->lastBlock();
    }
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + std::clamp(column - 1, 0, std::max(0, block.length() - 1)));
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

// -------------------------------------------------------------------------------------------------------- edição

texttools::Result DocCodeEditor::transform(const std::function<texttools::Result(const QString &)> &fn)
{
    QTextCursor cursor = textCursor();
    const bool selected = cursor.hasSelection();
    QString input = selected ? cursor.selectedText() : toPlainText();
    if (selected) {
        input.replace(QChar(0x2029), QLatin1Char('\n')); // o Qt separa parágrafos da seleção com U+2029
    }
    const texttools::Result result = fn(input);
    if (!result.ok) {
        return result;
    }
    cursor.beginEditBlock();
    if (!selected) {
        cursor.select(QTextCursor::Document);
    }
    cursor.insertText(result.text);
    cursor.endEditBlock();
    setTextCursor(cursor);
    validateNow();
    return result;
}

} // namespace kai::ui
