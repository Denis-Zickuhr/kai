#include "ui/features/command-editor/inline-row-actions.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QToolTip>
#include <QTreeWidget>

#include <algorithm>
#include <utility>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
QColor colorFor(InlineRowActions::Kind kind)
{
    switch (kind) {
    case InlineRowActions::Kind::Play:
        return QColor(tk::successFg());
    case InlineRowActions::Kind::Stop:
        return QColor(tk::errorFg());
    default:
        return QColor(tk::accent());
    }
}
} // namespace

InlineRowActions::InlineRowActions(QTreeWidget *tree, Provider provider)
    : QStyledItemDelegate(tree)
    , m_tree(tree)
    , m_provider(std::move(provider))
{
    tree->setItemDelegateForColumn(0, this);
    tree->viewport()->installEventFilter(this);
    tree->viewport()->setMouseTracking(true);
}

void InlineRowActions::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    m_hover = {};
    m_pressed = {};
    m_hoverItem = nullptr;
    m_tree->viewport()->setCursor(Qt::ArrowCursor);
    m_tree->viewport()->update();
}

QString InlineRowActions::iconName(Kind kind)
{
    switch (kind) {
    case Kind::Play: return QStringLiteral("play");
    case Kind::Stop: return QStringLiteral("square");
    case Kind::Reset: return QStringLiteral("rotate-ccw");
    case Kind::EditBody: return QStringLiteral("pencil");
    case Kind::Output: return QStringLiteral("square-terminal");
    case Kind::Document: return QStringLiteral("file-text");
    }
    return {};
}

QString InlineRowActions::tooltipKey(Kind kind)
{
    switch (kind) {
    case Kind::Play: return QStringLiteral("tree.run");
    case Kind::Stop: return QStringLiteral("tree.stop");
    case Kind::Reset: return QStringLiteral("tree.reset");
    case Kind::EditBody: return QStringLiteral("sidebar.edit_body");
    case Kind::Output: return QStringLiteral("sidebar.show_output");
    case Kind::Document: return QStringLiteral("sidebar.show_document");
    }
    return {};
}

bool InlineRowActions::rowShowsButtons(const QTreeWidgetItem *item) const
{
    return m_enabled && item && (item == m_hoverItem || item == m_tree->currentItem());
}

// Sempre na borda direita da coluna do nome, com o primeiro botão (executar/parar) fixo no mesmo ponto em toda linha e os
// outros se somando à esquerda dele.
QVector<QRect> InlineRowActions::buttonRects(const QTreeWidgetItem *item, int count) const
{
    QVector<QRect> rects;
    const QModelIndex index = m_tree->indexFromItem(item, 0);
    const QRect row = m_tree->visualRect(index);
    if (count <= 0 || !row.isValid()) {
        return rects;
    }
    const int box = std::max(1, std::min(kBox, row.height()));
    const int right = std::min(row.right(), m_tree->columnViewportPosition(0) + m_tree->columnWidth(0) - 1) - kEdgeGap;
    const int y = row.top() + (row.height() - box) / 2;
    for (int i = 0; i < count; ++i) {
        const int x = right - (i + 1) * box - i * kGap + 1;
        if (x < row.left() + kMinNameWidth) {
            break; // sem espaço pra mais: sobra o nome
        }
        rects.append(QRect(x, y, box, box));
    }
    return rects;
}

InlineRowActions::Hit InlineRowActions::hitTest(const QPoint &viewportPos) const
{
    Hit hit;
    const QTreeWidgetItem *item = m_tree->itemAt(viewportPos);
    if (!rowShowsButtons(item) || !m_provider) {
        return hit;
    }
    const QVector<Kind> kinds = m_provider(item);
    const QVector<QRect> rects = buttonRects(item, int(kinds.size()));
    for (int i = 0; i < rects.size(); ++i) {
        if (rects.at(i).contains(viewportPos)) {
            hit.valid = true;
            hit.kind = kinds.at(i);
            hit.item = item;
            hit.rect = rects.at(i);
            return hit;
        }
    }
    return hit;
}

void InlineRowActions::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QTreeWidgetItem *item = m_tree->itemFromIndex(index);
    if (!rowShowsButtons(item) || !m_provider) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const QVector<Kind> kinds = m_provider(item);
    const QVector<QRect> rects = buttonRects(item, int(kinds.size()));

    // O nome termina antes dos botões em vez de passar por baixo deles.
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    if (!rects.isEmpty()) {
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
        const int available = rects.last().left() - kLeadGap - textRect.left();
        opt.text = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, std::max(0, available));
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < rects.size(); ++i) {
        const QColor color = colorFor(kinds.at(i));
        const bool hovered = m_hover.valid && m_hover.item == item && m_hover.kind == kinds.at(i);
        if (hovered) {
            QColor fill = color;
            fill.setAlpha(40);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(QRectF(rects.at(i)).adjusted(0.5, 0.5, -0.5, -0.5), tk::radiusSm(), tk::radiusSm());
        }
        const QRect box = rects.at(i);
        const int size = std::min({kIcon, box.width(), box.height()});
        const QRect inner(box.left() + (box.width() - size) / 2, box.top() + (box.height() - size) / 2, size, size);
        LucideIcons::icon(iconName(kinds.at(i)), color, size).paint(painter, inner);
    }
    painter->restore();
}

bool InlineRowActions::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_tree->viewport() || !m_enabled) {
        return QStyledItemDelegate::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) {
            return false;
        }
        const Hit hit = hitTest(mouse->pos());
        if (!hit.valid) {
            return false;
        }
        m_pressed = hit;
        return true; // nem seleciona a linha, nem começa a arrastar
    }
    case QEvent::MouseButtonRelease: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || !m_pressed.valid) {
            return false;
        }
        const Hit pressed = std::exchange(m_pressed, Hit{});
        const Hit hit = hitTest(mouse->pos());
        if (hit.valid && hit.item == pressed.item && hit.kind == pressed.kind) {
            emit activated(hit.item, hit.kind);
        }
        return true;
    }
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QTreeWidgetItem *item = m_tree->itemAt(mouse->pos());
        const Hit hit = hitTest(mouse->pos());
        if (item != m_hoverItem || hit.valid != m_hover.valid || hit.kind != m_hover.kind || hit.item != m_hover.item) {
            m_hoverItem = item;
            m_hover = hit;
            m_tree->viewport()->setCursor(hit.valid ? Qt::PointingHandCursor : Qt::ArrowCursor);
            m_tree->viewport()->update();
        }
        return false;
    }
    case QEvent::Leave:
        if (m_hoverItem || m_hover.valid) {
            m_hoverItem = nullptr;
            m_hover = {};
            m_tree->viewport()->setCursor(Qt::ArrowCursor);
            m_tree->viewport()->update();
        }
        return false;
    case QEvent::ToolTip: {
        auto *help = static_cast<QHelpEvent *>(event);
        const Hit hit = hitTest(help->pos());
        if (!hit.valid) {
            return false;
        }
        QToolTip::showText(help->globalPos(), utils::tr(tooltipKey(hit.kind)), m_tree->viewport(), hit.rect);
        return true;
    }
    default:
        return false;
    }
}

} // namespace kai::ui
