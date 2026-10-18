#include "ui/layout/drop-overlay.h"

#include "ui/layout/view-zone.h"
#include "utils/design-tokens.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>

namespace kai::ui::layout {

namespace tk = kai::utils::tokens;

DropOverlay::DropOverlay(QWidget *host)
    : QWidget(host)
{
    setObjectName(QStringLiteral("dropOverlay"));
    setAcceptDrops(true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
}

void DropOverlay::setTargetProvider(TargetFn target, EdgesFn edges)
{
    m_targetFn = std::move(target);
    m_edgesFn = std::move(edges);
}

void DropOverlay::setTarget(const DropTarget &target)
{
    if (target.valid == m_target.valid && target.zone == m_target.zone && target.index == m_target.index
        && target.rect == m_target.rect) {
        return;
    }
    m_target = target;
    update();
}

void DropOverlay::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(kViewMimeType)) {
        event->acceptProposedAction();
        setTarget(m_targetFn ? m_targetFn(event->position().toPoint()) : DropTarget());
    } else {
        event->ignore();
    }
}

void DropOverlay::dragMoveEvent(QDragMoveEvent *event)
{
    const DropTarget target = m_targetFn ? m_targetFn(event->position().toPoint()) : DropTarget();
    setTarget(target);
    if (target.valid) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DropOverlay::dragLeaveEvent(QDragLeaveEvent *event)
{
    setTarget(DropTarget());
    event->accept();
}

void DropOverlay::dropEvent(QDropEvent *event)
{
    const DropTarget target = m_targetFn ? m_targetFn(event->position().toPoint()) : DropTarget();
    const QString id = QString::fromUtf8(event->mimeData()->data(kViewMimeType));
    if (target.valid && !id.isEmpty()) {
        event->acceptProposedAction();
        emit dropped(id, target);
    } else {
        event->ignore();
    }
    setTarget(DropTarget());
}

void DropOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent(tk::accent());
    const int radius = tk::radiusMd();

    // As bordas vazias onde dá para soltar: sempre à vista durante o arrasto.
    if (m_edgesFn) {
        QColor faint(tk::mutedFg());
        faint.setAlpha(110);
        QPen dashed(faint, 1, Qt::DashLine);
        for (const auto &[rect, label] : m_edgesFn()) {
            p.setPen(dashed);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect.adjusted(4, 4, -4, -4), radius, radius);
            QColor text(tk::mutedFg());
            text.setAlpha(190);
            QFont font = p.font();
            font.setBold(true);
            font.setPointSizeF(font.pointSizeF() + 2);
            p.setFont(font);
            p.setPen(text);
            p.drawText(rect, Qt::AlignCenter, label);
        }
    }
    if (m_target.valid) {
        QColor fill = accent;
        fill.setAlpha(55);
        p.setPen(QPen(accent, 2));
        p.setBrush(fill);
        p.drawRoundedRect(m_target.rect.adjusted(3, 3, -3, -3), radius, radius);
    }
}

} // namespace kai::ui::layout
