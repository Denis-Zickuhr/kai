#include "ui/features/output/code-output-view.h"

#include <QDesktopServices>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QUrl>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>

namespace kai::ui {
namespace {

// Widget filho que apenas repassa a pintura para o editor dono. Mantém o
// desenho dos números junto da lógica do editor, sem uma segunda classe cheia.
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeOutputView *editor)
        : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override
    {
        return QSize(m_editor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_editor->paintLineNumbers(event);
    }

private:
    CodeOutputView *m_editor;
};

} // namespace

CodeOutputView::CodeOutputView(QWidget *parent)
    : QPlainTextEdit(parent)
{
    // Necessário para receber mouseMoveEvent sem botão pressionado (hover do
    // link). Read-only não gera tracking por padrão.
    setMouseTracking(true);
    // O QTextDocument tem sua PRÓPRIA margem interna (padrão 4px),
    // independente do `padding` da stylesheet do OutputPanel — as duas
    // SOMAVAM, deixando um espaço maior que o pretendido entre o cabeçalho
    // (abas) e a primeira linha da saída (bug relatado: "espacinho bugado
    // depois da saída"). Zerada aqui: o padding da QSS já é a fonte única
    // de verdade do respiro.
    document()->setDocumentMargin(0);
    m_lineNumberArea = new LineNumberArea(this);
    m_lineNumberArea->setVisible(false);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) {
        updateLineNumberAreaWidth();
    });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        updateLineNumberArea(rect, dy);
    });
    updateLineNumberAreaWidth();
}

void CodeOutputView::setLineNumbersVisible(bool visible)
{
    if (m_showLineNumbers == visible) {
        return;
    }
    m_showLineNumbers = visible;
    m_lineNumberArea->setVisible(visible);
    updateLineNumberAreaWidth();
    viewport()->update();
}

void CodeOutputView::setGutterColors(const QColor &background, const QColor &foreground)
{
    m_gutterBg = background;
    m_gutterFg = foreground;
    m_lineNumberArea->update();
}

int CodeOutputView::lineNumberAreaWidth() const
{
    if (!m_showLineNumbers) {
        return 0;
    }
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeOutputView::updateLineNumberAreaWidth()
{
    // setViewportMargins é protegido — acessível aqui porque somos a subclasse.
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    m_lineNumberArea->setGeometry(QRect(contentsRect().left(), contentsRect().top(),
                                        lineNumberAreaWidth(), contentsRect().height()));
}

void CodeOutputView::updateLineNumberArea(const QRect &rect, int dy)
{
    if (!m_showLineNumbers) {
        return;
    }
    if (dy != 0) {
        m_lineNumberArea->scroll(0, dy);
    } else {
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth();
    }
}

void CodeOutputView::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}


// Regex de URL usada tanto para o clique quanto para o realce visual.
// Deliberadamente conservadora: http(s) e ftp, parando em espaço/aspas e
// descartando pontuação final (o "." de fim de frase não faz parte da URL).
static const QRegularExpression &urlPattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"((https?://|ftp://)[^\s<>"'\]\)]+)"));
    return re;
}

QString CodeOutputView::urlAt(const QPoint &viewportPos) const
{
    const QTextCursor cursor = cursorForPosition(viewportPos);
    const QString line = cursor.block().text();
    if (line.isEmpty()) {
        return QString();
    }
    const int col = cursor.positionInBlock();
    auto it = urlPattern().globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QString url = m.captured(0);
        // Remove pontuação final que normalmente não pertence à URL.
        while (!url.isEmpty() && QStringLiteral(".,;:!?").contains(url.back())) {
            url.chop(1);
        }
        const int start = m.capturedStart(0);
        if (col >= start && col <= start + url.length()) {
            return url;
        }
    }
    return QString();
}

void CodeOutputView::mouseMoveEvent(QMouseEvent *event)
{
    // Cursor de mãozinha sobre o link, sinalizando que é clicável.
    const bool overLink = !urlAt(event->pos()).isEmpty();
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
    QPlainTextEdit::mouseMoveEvent(event);
}

void CodeOutputView::mouseReleaseEvent(QMouseEvent *event)
{
    // Só abre em clique SEM seleção (para não atropelar seleção de texto).
    if (event->button() == Qt::LeftButton && !textCursor().hasSelection()) {
        const QString url = urlAt(event->pos());
        if (!url.isEmpty()) {
            QDesktopServices::openUrl(QUrl(url));
            event->accept();
            return;
        }
    }
    QPlainTextEdit::mouseReleaseEvent(event);
}

void CodeOutputView::paintLineNumbers(QPaintEvent *event)
{
    if (!m_showLineNumbers) {
        return;
    }
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_gutterBg.isValid() ? m_gutterBg : palette().window().color());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();

    painter.setPen(m_gutterFg.isValid() ? m_gutterFg : palette().windowText().color());
    const int width = m_lineNumberArea->width() - 6;

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(0, static_cast<int>(top), width, fontMetrics().height(),
                             Qt::AlignRight, QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
        ++blockNumber;
    }
}

} // namespace kai::ui
