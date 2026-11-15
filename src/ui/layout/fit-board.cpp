#include "ui/layout/fit-board.h"

#include "ui/layout/view-zone.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace kai::ui::layout {

namespace tk = kai::utils::tokens;

namespace {

// O quanto os pontos recuam do canto para ficarem dentro do arco da borda arredondada do painel.
int cornerInset()
{
    return 4 + int(std::ceil(0.293 * tk::radiusMd()));
}

// A alça: pontinhos na diagonal, no canto da célula, ou uma faixa fina sobre uma das bordas.
class FitGrip : public QWidget {
public:
    FitGrip(Zone zone, FitBoard *board, FitBoard::Edge edge)
        : QWidget(board)
        , m_zone(zone)
        , m_board(board)
        , m_edge(edge)
    {
        if (edge == FitBoard::Edge::Corner) {
            const int side = cornerInset() + 14;
            setFixedSize(side, side);
            setCursor(Qt::SizeFDiagCursor);
            setToolTip(utils::tr(QStringLiteral("layout.fit.grip")));
        } else {
            setCursor(edge == FitBoard::Edge::Right ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        }
        setAttribute(Qt::WA_Hover, true);
        setObjectName(QStringLiteral("fitGrip"));
        setStyleSheet(QStringLiteral("#fitGrip { background: transparent; }"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        if (m_edge != FitBoard::Edge::Corner) {
            if (m_down || underMouse()) {
                QColor line(tk::accent());
                line.setAlpha(180);
                p.setBrush(line);
                if (m_edge == FitBoard::Edge::Right) {
                    p.drawRoundedRect(QRectF(width() - 4, 0, 3, height()), 1.5, 1.5);
                } else {
                    p.drawRoundedRect(QRectF(0, height() - 4, width(), 3), 1.5, 1.5);
                }
            }
            return;
        }
        p.setBrush(QColor(m_down || underMouse() ? tk::accent() : tk::mutedFg()));
        const double inset = cornerInset();
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j <= i; ++j) {
                p.drawEllipse(QPointF(width() - inset - j * 4.0, height() - inset - (i - j) * 4.0), 1.1, 1.1);
            }
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_down = true;
            update();
            event->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_down) {
            m_board->dragMoved(m_zone, mapTo(m_board, event->position().toPoint()), m_edge);
        }
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_down && event->button() == Qt::LeftButton) {
            m_down = false;
            update();
            m_board->dragEnded(m_zone);
        }
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    Zone m_zone;
    FitBoard *m_board;
    FitBoard::Edge m_edge;
    bool m_down = false;
};

// O retângulo do pedido durante o arrasto: por cima das células, sem pegar o mouse.
class FitPreview : public QWidget {
public:
    explicit FitPreview(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setObjectName(QStringLiteral("fitPreview"));
        setStyleSheet(QStringLiteral("#fitPreview { background: transparent; }"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor fill(tk::accent());
        fill.setAlpha(50);
        p.setPen(QPen(QColor(tk::accent()), 2, Qt::DashLine));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), tk::radiusMd(), tk::radiusMd());
    }
};

} // namespace

FitBoard::FitBoard(int cols, int rows, QWidget *parent)
    : QWidget(parent)
    , m_cols(std::max(1, cols))
    , m_rows(std::max(1, rows))
{
    setObjectName(QStringLiteral("fitBoard"));
    m_preview = new FitPreview(this);
    m_preview->hide();
}

void FitBoard::setSpanHandlers(SpanLimiter limiter, SpanCommit commit)
{
    m_limiter = std::move(limiter);
    m_commit = std::move(commit);
}

QRect FitBoard::cellRect(int row, int col, int rowSpan, int colSpan) const
{
    const double cellW = (width() - kGap * (m_cols - 1)) / double(m_cols);
    const double cellH = (height() - kGap * (m_rows - 1)) / double(m_rows);
    const int x = qRound(col * (cellW + kGap));
    const int y = qRound(row * (cellH + kGap));
    const int w = qRound(colSpan * cellW + (colSpan - 1) * kGap);
    const int h = qRound(rowSpan * cellH + (rowSpan - 1) * kGap);
    return QRect(x, y, std::max(1, w), std::max(1, h));
}

void FitBoard::setItems(const QList<Item> &items)
{
    qDeleteAll(m_grips);
    m_grips.clear();
    qDeleteAll(m_edgeGrips);
    m_edgeGrips.clear();
    m_items = items;
    for (const Item &item : std::as_const(m_items)) {
        item.widget->setParent(this);
        item.widget->show();
        auto *grip = new FitGrip(item.cell.zone, this, Edge::Corner);
        m_grips.append(grip);
        m_edgeGrips.append(new FitGrip(item.cell.zone, this, Edge::Right));
        m_edgeGrips.append(new FitGrip(item.cell.zone, this, Edge::Bottom));
    }
    setHandlesVisible(m_handlesVisible);
    relayout();
}

QWidget *FitBoard::gripOf(Zone zone) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).cell.zone == zone) {
            return m_grips.at(i);
        }
    }
    return nullptr;
}

void FitBoard::setHandlesVisible(bool visible)
{
    m_handlesVisible = visible;
    for (QWidget *grip : std::as_const(m_grips)) {
        grip->setVisible(visible);
    }
    for (QWidget *grip : std::as_const(m_edgeGrips)) {
        grip->setVisible(visible);
    }
}

void FitBoard::relayout()
{
    for (int i = 0; i < m_items.size(); ++i) {
        const FitCell &cell = m_items.at(i).cell;
        const QRect rect = cellRect(cell.row, cell.col, cell.rowSpan, cell.colSpan);
        m_items.at(i).widget->setGeometry(rect);
        QWidget *corner = m_grips.at(i);
        corner->move(rect.right() - corner->width() - 1, rect.bottom() - corner->height() - 1);
        // As faixas das bordas ficam fora da área da alça do canto e dos cantos arredondados.
        const int margin = std::max(tk::radiusMd(), 6);
        QWidget *right = m_edgeGrips.at(2 * i);
        QWidget *bottom = m_edgeGrips.at(2 * i + 1);
        right->setGeometry(rect.right() - 5, rect.top() + margin, 7, std::max(0, rect.height() - margin - corner->height() - 1));
        bottom->setGeometry(rect.left() + margin, rect.bottom() - 5, std::max(0, rect.width() - margin - corner->width() - 1), 7);
    }
    if (!m_previewCells.isNull()) {
        m_preview->setGeometry(cellRect(m_previewCells.y(), m_previewCells.x(), m_previewCells.height(), m_previewCells.width()));
        m_preview->raise();
    }
    for (QWidget *grip : std::as_const(m_edgeGrips)) {
        grip->raise();
    }
    for (QWidget *grip : std::as_const(m_grips)) {
        grip->raise();
    }
}

void FitBoard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void FitBoard::dragMoved(Zone zone, const QPoint &boardPos, Edge edge)
{
    const double cellW = (width() - kGap * (m_cols - 1)) / double(m_cols);
    const double cellH = (height() - kGap * (m_rows - 1)) / double(m_rows);
    const int col = std::clamp(int(boardPos.x() / (cellW + kGap)), 0, m_cols - 1);
    const int row = std::clamp(int(boardPos.y() / (cellH + kGap)), 0, m_rows - 1);
    int colSpan = std::max(1, col - zone.col + 1);
    int rowSpan = std::max(1, row - zone.row + 1);
    for (const Item &item : std::as_const(m_items)) {
        if (item.cell.zone == zone) {
            if (edge == Edge::Right) {
                rowSpan = item.cell.rowSpan;
            } else if (edge == Edge::Bottom) {
                colSpan = item.cell.colSpan;
            }
            break;
        }
    }
    if (m_limiter) {
        const QSize fit = m_limiter(zone, colSpan, rowSpan);
        colSpan = fit.width();
        rowSpan = fit.height();
    }
    const QRect preview(zone.col, zone.row, colSpan, rowSpan);
    if (preview != m_previewCells) {
        m_previewCells = preview;
        m_preview->show();
        relayout();
    }
}

void FitBoard::dragEnded(Zone zone)
{
    const QRect preview = m_previewCells;
    m_previewCells = QRect();
    m_preview->hide();
    if (!preview.isNull() && m_commit) {
        m_commit(zone, preview.width(), preview.height());
    }
}

} // namespace kai::ui::layout
