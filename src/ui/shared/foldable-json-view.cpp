#include "ui/shared/foldable-json-view.h"
#include "ui/shared/lucide-icons.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <utility>

namespace kai::ui {
namespace {

// Largura fixa do gutter, sempre reservada: evita o texto pular quando a resposta alterna entre JSON válido e inválido.
constexpr int kGutterWidth = 20;
constexpr int kMarkerSize = 12;

// Widget filho que só repassa pintura/clique ao dono (mesmo padrão do LineNumberArea de CodeOutputView).
class FoldGutter : public QWidget {
public:
    explicit FoldGutter(FoldableJsonView *editor)
        : QWidget(editor), m_editor(editor)
    {
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override { return QSize(kGutterWidth, 0); }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->paintGutter(event); }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_editor->handleGutterClick(event->pos());
        }
    }

private:
    FoldableJsonView *m_editor;
};

} // namespace

FoldableJsonView::FoldableJsonView(QWidget *parent)
    : QPlainTextEdit(parent)
{
    m_gutter = new FoldGutter(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        updateGutterArea(rect, dy);
    });
    updateGutterWidth();
}

void FoldableJsonView::setGutterColors(const QColor &background, const QColor &markerColor)
{
    m_gutterBg = background;
    m_gutterMarkerColor = markerColor;
    m_gutter->update();
}

void FoldableJsonView::setRawText(const QString &text)
{
    // JSON inválido: preserva o texto cru; sem pares de colapso, o gutter fica presente mas mudo.
    m_ranges.clear();
    setPlainText(text);
    updateGutterWidth();
    viewport()->update();
    m_gutter->update();
}

void FoldableJsonView::setFoldableJsonText(const QString &prettyJson, int autoCollapseLineThreshold)
{
    m_ranges.clear();
    setPlainText(prettyJson);

    // O texto é o pretty-print do QJsonDocument::Indented: a linha de abertura termina em '{' ou '[' e a de
    // fechamento começa com '}' ou ']'. Como o JSON é bem formado, uma pilha basta para parear, sem reparsear
    // nem contar indentação.
    struct Open { int block; QChar ch; };
    QVector<Open> stack;
    QTextBlock block = document()->firstBlock();
    while (block.isValid()) {
        const QString trimmed = block.text().trimmed();
        if (!trimmed.isEmpty()) {
            const QChar first = trimmed.front();
            if ((first == QLatin1Char('}') || first == QLatin1Char(']')) && !stack.isEmpty()) {
                const Open open = stack.takeLast();
                FoldRange range;
                range.startBlock = open.block;
                range.endBlock = block.blockNumber();
                m_ranges.append(range);
            }
            const QChar last = (trimmed.back() == QLatin1Char(',') && trimmed.size() > 1)
                ? trimmed.at(trimmed.size() - 2)
                : trimmed.back();
            if (last == QLatin1Char('{') || last == QLatin1Char('[')) {
                stack.append({block.blockNumber(), last});
            }
        }
        block = block.next();
    }
    std::sort(m_ranges.begin(), m_ranges.end(), [](const FoldRange &a, const FoldRange &b) {
        return a.startBlock < b.startBlock;
    });

    // Por padrão tudo fica aberto; só pares cujo span (linhas ocultas) passa de `autoCollapseLineThreshold` nascem colapsados.
    if (autoCollapseLineThreshold > 0) {
        for (FoldRange &range : m_ranges) {
            if (range.endBlock - range.startBlock - 1 > autoCollapseLineThreshold) {
                range.collapsed = true;
            }
        }
        for (const FoldRange &range : std::as_const(m_ranges)) {
            if (range.collapsed) {
                applyVisibility(range);
            }
        }
    }

    updateGutterWidth();
    viewport()->update();
    m_gutter->update();
}

int FoldableJsonView::rangeIndexStartingAt(int startBlockNumber) const
{
    for (int i = 0; i < m_ranges.size(); ++i) {
        if (m_ranges.at(i).startBlock == startBlockNumber) {
            return i;
        }
    }
    return -1;
}

bool FoldableJsonView::foldStartAtBlock(int blockNumber) const
{
    return rangeIndexStartingAt(blockNumber) >= 0;
}

bool FoldableJsonView::isFoldCollapsed(int startBlockNumber) const
{
    const int idx = rangeIndexStartingAt(startBlockNumber);
    return idx >= 0 && m_ranges.at(idx).collapsed;
}

void FoldableJsonView::toggleFold(int startBlockNumber)
{
    const int idx = rangeIndexStartingAt(startBlockNumber);
    if (idx < 0) {
        return;
    }
    m_ranges[idx].collapsed = !m_ranges[idx].collapsed;
    applyVisibility(m_ranges.at(idx));
}

void FoldableJsonView::expandAllFolds()
{
    // De fora para dentro (span maior primeiro): aplicar um range externo por último reesconderia os filhos reabertos.
    QVector<int> order;
    for (int i = 0; i < m_ranges.size(); ++i) {
        order.append(i);
    }
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return (m_ranges.at(a).endBlock - m_ranges.at(a).startBlock)
             > (m_ranges.at(b).endBlock - m_ranges.at(b).startBlock);
    });
    for (int i : std::as_const(order)) {
        m_ranges[i].collapsed = false;
    }
    for (int i : std::as_const(order)) {
        applyVisibility(m_ranges.at(i));
    }
}

void FoldableJsonView::collapseAllFolds()
{
    for (FoldRange &range : m_ranges) {
        range.collapsed = true;
    }
    for (const FoldRange &range : std::as_const(m_ranges)) {
        applyVisibility(range);
    }
}

void FoldableJsonView::applyVisibility(const FoldRange &range)
{
    if (range.startBlock < 0 || range.endBlock <= range.startBlock) {
        return;
    }
    QTextBlock block = document()->findBlockByNumber(range.startBlock + 1);
    if (range.collapsed) {
        // Colapsando esconde até o fim inclusive: a linha de fechamento também some, porque o overlay de paintEvent()
        // já sintetiza o "⋯}" logo após a abertura.
        while (block.isValid() && block.blockNumber() <= range.endBlock) {
            block.setVisible(false);
            block = block.next();
        }
    } else {
        // Expandindo: reexibe todo o intervalo do par...
        while (block.isValid() && block.blockNumber() <= range.endBlock) {
            block.setVisible(true);
            block = block.next();
        }
        // ...e reaplica o colapso dos pares aninhados que já estavam fechados antes do toggle.
        for (const FoldRange &nested : std::as_const(m_ranges)) {
            if (nested.collapsed
                && nested.startBlock > range.startBlock
                && nested.endBlock < range.endBlock) {
                QTextBlock nb = document()->findBlockByNumber(nested.startBlock + 1);
                while (nb.isValid() && nb.blockNumber() <= nested.endBlock) {
                    nb.setVisible(false);
                    nb = nb.next();
                }
            }
        }
    }
    // setVisible() sozinho não faz o QPlainTextEdit refazer o layout: invalida o documento.
    document()->markContentsDirty(0, document()->characterCount());
    viewport()->update();
    m_gutter->update();
}

int FoldableJsonView::gutterWidth() const
{
    return kGutterWidth;
}

void FoldableJsonView::updateGutterWidth()
{
    setViewportMargins(gutterWidth(), 0, 0, 0);
    m_gutter->setGeometry(QRect(contentsRect().left(), contentsRect().top(),
                                 gutterWidth(), contentsRect().height()));
}

void FoldableJsonView::updateGutterArea(const QRect &rect, int dy)
{
    if (dy != 0) {
        m_gutter->scroll(0, dy);
    } else {
        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateGutterWidth();
    }
}

void FoldableJsonView::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void FoldableJsonView::paintGutter(QPaintEvent *event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), m_gutterBg.isValid() ? m_gutterBg : palette().window().color());

    const QColor markerColor = m_gutterMarkerColor.isValid() ? m_gutterMarkerColor
                                                               : palette().windowText().color();

    QTextBlock block = firstVisibleBlock();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()
            && foldStartAtBlock(block.blockNumber())) {
            // Chevron para baixo = expandido; para a direita = colapsado.
            const QString iconName = isFoldCollapsed(block.blockNumber())
                ? QStringLiteral("chevron-right")
                : QStringLiteral("chevron-down");
            const QIcon icon = LucideIcons::icon(iconName, markerColor, kMarkerSize);
            const int rowHeight = static_cast<int>(bottom - top);
            const int y = static_cast<int>(top) + (rowHeight - kMarkerSize) / 2;
            const int x = (kGutterWidth - kMarkerSize) / 2;
            icon.paint(&painter, x, y, kMarkerSize, kMarkerSize);
        }
        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
    }
}

void FoldableJsonView::handleGutterClick(const QPoint &gutterPos)
{
    QTextBlock block = firstVisibleBlock();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();

    while (block.isValid() && top <= viewport()->rect().bottom()) {
        if (block.isVisible() && gutterPos.y() >= top && gutterPos.y() < bottom) {
            if (foldStartAtBlock(block.blockNumber())) {
                toggleFold(block.blockNumber());
            }
            return;
        }
        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
    }
}

void FoldableJsonView::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);

    // Dica inline nos pares colapsados ("⋯" após a abertura): overlay pintado, o texto do documento fica intacto.
    if (m_ranges.isEmpty()) {
        return;
    }
    QPainter painter(viewport());
    painter.setPen(m_gutterMarkerColor.isValid() ? m_gutterMarkerColor : palette().mid().color());
    painter.setFont(font());
    const QFontMetrics fm(font());

    for (const FoldRange &range : std::as_const(m_ranges)) {
        if (!range.collapsed) {
            continue;
        }
        const QTextBlock block = document()->findBlockByNumber(range.startBlock);
        if (!block.isValid() || !block.isVisible()) {
            continue;
        }
        const QRectF geo = blockBoundingGeometry(block).translated(contentOffset());
        if (geo.bottom() < event->rect().top() || geo.top() > event->rect().bottom()) {
            continue;
        }
        const QString text = block.text();
        const QString trimmed = text.trimmed();
        const QChar openCh = trimmed.isEmpty() ? QLatin1Char('{') : trimmed.back();
        const QChar closeCh = (openCh == QLatin1Char('[')) ? QLatin1Char(']') : QLatin1Char('}');
        const QString hint = QStringLiteral(" ⋯%1").arg(closeCh);
        const qreal x = geo.left() + fm.horizontalAdvance(text);
        painter.drawText(QRectF(x, geo.top(), 120, geo.height()), Qt::AlignVCenter | Qt::AlignLeft, hint);
    }
}

} // namespace kai::ui
