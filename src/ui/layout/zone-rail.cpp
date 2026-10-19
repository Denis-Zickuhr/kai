#include "ui/layout/zone-rail.h"

#include "ui/layout/view-mime.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDrag>
#include <QHelpEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace kai::ui::layout {

namespace tk = kai::utils::tokens;

namespace {
constexpr int kIcon = 16;
constexpr int kSlot = 28;
constexpr int kPad = 9; // respiro entre a borda da pílula e o primeiro/último botão
constexpr double kGap = 6.0; // o vão do divisor entre os cartões: a faixa o repete do lado do conteúdo
} // namespace

ZoneRail::ZoneRail(Edge edge, ViewRegistry *registry, QWidget *parent)
    : QWidget(parent)
    , m_edge(edge)
    , m_registry(registry)
{
    setObjectName(QStringLiteral("zoneRail"));
    setMouseTracking(true);
    if (vertical()) {
        setFixedWidth(kThickness);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    } else {
        setFixedHeight(kThickness);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
}

QSize ZoneRail::sizeHint() const
{
    const int length = int(m_ids.size() + 1) * kSlot + 2 * kPad;
    return vertical() ? QSize(kThickness, length) : QSize(length, kThickness);
}

void ZoneRail::setViews(const QStringList &ids, const QStringList &open)
{
    m_ids = ids;
    m_open = open;
    m_hover = -2;
    updateGeometry();
    update();
}

QRectF ZoneRail::pillRect() const
{
    // Ao longo da faixa ela vai de ponta a ponta, como os cartões ao lado; na outra direção só o vão do divisor do lado do
    // conteúdo (a borda da janela não ganha respiro: os cartões também a encostam).
    QRectF pill = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    switch (m_edge) {
    case Edge::Left:
        pill.setRight(pill.right() - kGap);
        break;
    case Edge::Right:
        pill.setLeft(pill.left() + kGap);
        break;
    case Edge::Top:
        pill.setBottom(pill.bottom() - kGap);
        break;
    case Edge::Bottom:
        pill.setTop(pill.top() + kGap);
        break;
    }
    return pill;
}

// O slot 0 é o botão de expandir; os painéis vêm depois.
QRect ZoneRail::slot(int index) const
{
    const int offset = kPad + index * kSlot;
    const QRectF pill = pillRect();
    const int cross = vertical() ? int(pill.center().x() - kSlot / 2.0) : int(pill.center().y() - kSlot / 2.0);
    return vertical() ? QRect(cross, offset, kSlot, kSlot) : QRect(offset, cross, kSlot, kSlot);
}

int ZoneRail::insertionIndexAt(const QPoint &pos) const
{
    const int along = vertical() ? pos.y() : pos.x();
    for (int i = 0; i < m_ids.size(); ++i) {
        const QRect box = slot(i + 1);
        if (along < (vertical() ? box.center().y() : box.center().x())) {
            return i;
        }
    }
    return int(m_ids.size());
}

QRect ZoneRail::expandRect() const
{
    return slot(0);
}

QRect ZoneRail::iconRect(const QString &id) const
{
    const int index = int(m_ids.indexOf(id));
    return index < 0 ? QRect() : slot(index + 1);
}

int ZoneRail::indexAt(const QPoint &pos) const
{
    if (slot(0).contains(pos)) {
        return -1;
    }
    for (int i = 0; i < m_ids.size(); ++i) {
        if (slot(i + 1).contains(pos)) {
            return i;
        }
    }
    return -2;
}

void ZoneRail::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Uma "pílula" com a moldura dos painéis (borda + raio escolhido), alinhada aos cartões vizinhos.
    const QRectF pill = pillRect();
    const double radius = qMin(double(tk::radiusMd()), qMin(pill.width(), pill.height()) / 2.0);
    p.setPen(QPen(QColor(tk::borderColor()), 1));
    p.setBrush(QColor(tk::altBg()));
    p.drawRoundedRect(pill, radius, radius);

    const auto drawSlot = [&](const QRect &box, const QString &icon, bool active, bool hover) {
        if (active || hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(active ? tk::selBg() : tk::hoverBg()));
            p.drawRoundedRect(box.adjusted(2, 2, -2, -2), tk::radiusSm(), tk::radiusSm());
        }
        const QIcon pixmap = LucideIcons::icon(icon, QColor(active ? tk::accent() : tk::mutedFg()), kIcon);
        if (!pixmap.isNull()) {
            pixmap.paint(&p, QRect(box.center().x() - kIcon / 2, box.center().y() - kIcon / 2, kIcon, kIcon));
        }
    };
    // Expandir: o chevron aponta para onde a zona cresce.
    const QString chevron = m_edge == Edge::Left ? QStringLiteral("chevron-right")
                            : m_edge == Edge::Right ? QStringLiteral("chevron-left")
                            : m_edge == Edge::Top ? QStringLiteral("chevron-down") : QStringLiteral("chevron-up");
    drawSlot(slot(0), chevron, false, m_hover == -1);
    for (int i = 0; i < m_ids.size(); ++i) {
        drawSlot(slot(i + 1), m_registry->icon(m_ids.at(i)), m_open.contains(m_ids.at(i)), m_hover == i);
    }
}

void ZoneRail::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressIndex = indexAt(event->pos());
        m_pressPos = event->pos();
    }
    QWidget::mousePressEvent(event);
}

void ZoneRail::startDrag(int index)
{
    const QString id = m_ids.at(index);
    auto *mime = new QMimeData;
    mime->setData(kViewMimeType, id.toUtf8());
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    const QRect box = slot(index + 1);
    QPixmap pixmap(box.size() * devicePixelRatioF());
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    pixmap.fill(Qt::transparent);
    render(&pixmap, QPoint(), QRegion(box));
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(box.width() / 2, box.height() / 2));
    emit dragStarted(id);
    const Qt::DropAction action = drag->exec(Qt::MoveAction);
    emit dragFinished(id, action, QCursor::pos());
}

void ZoneRail::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = indexAt(event->pos());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
    if ((event->buttons() & Qt::LeftButton) && m_pressIndex >= 0
        && (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        const int index = m_pressIndex;
        m_pressIndex = -2;
        startDrag(index);
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ZoneRail::leaveEvent(QEvent *event)
{
    m_hover = -2;
    update();
    QWidget::leaveEvent(event);
}

void ZoneRail::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = indexAt(event->pos());
        // Só vale como clique se o botão foi apertado no mesmo ícone (um arrasto cancela).
        const bool sameSpot = index == m_pressIndex;
        m_pressIndex = -2;
        if (sameSpot && index == -1) {
            emit expandRequested();
        } else if (sameSpot && index >= 0) {
            emit viewClicked(m_ids.at(index));
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void ZoneRail::contextMenuEvent(QContextMenuEvent *event)
{
    const int index = indexAt(event->pos());
    if (index >= 0) {
        emit viewMenuRequested(m_ids.at(index), event->globalPos());
    }
}

bool ZoneRail::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const int index = indexAt(help->pos());
        if (index >= 0) {
            QToolTip::showText(help->globalPos(), m_registry->title(m_ids.at(index)), this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

} // namespace kai::ui::layout
