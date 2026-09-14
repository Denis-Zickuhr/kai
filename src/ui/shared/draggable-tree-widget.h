#pragma once

#include <QTreeWidget>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRect>
#include <QApplication>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <functional>

#include "utils/logger.h"

namespace kai::ui {

class DraggableTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    enum class ConnectorStyle { Native, None, Continuous };

    explicit DraggableTreeWidget(QWidget *parent = nullptr) : QTreeWidget(parent) {}

    void setIndicatorColor(const QColor &color) { m_indicatorColor = color; update(); }

    void setConnectorStyle(ConnectorStyle style) { m_connectorStyle = style; viewport()->update(); }
    void setConnectorLineColor(const QColor &color) { m_connectorLineColor = color; viewport()->update(); }

    void setCanAcceptChildrenPredicate(std::function<bool(QTreeWidgetItem *)> predicate)
    {
        m_canAcceptChildren = std::move(predicate);
    }

signals:
    void itemsDropped();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QTreeWidget::mousePressEvent(event);
        if (event->button() != Qt::LeftButton) {
            return;
        }
        m_pressItem = itemAt(event->pos());
        m_pressPos = event->pos();
        m_dragging = false;
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_pressItem || !(event->buttons() & Qt::LeftButton)) {
            QTreeWidget::mouseMoveEvent(event);
            return;
        }
        if (!m_dragging) {
            if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
                QTreeWidget::mouseMoveEvent(event);
                return;
            }
            m_dragging = true;
        }
        updateIndicatorForPos(event->pos());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            m_hasIndicator = false;
            viewport()->update();
            QTreeWidgetItem *sourceItem = m_pressItem;
            QTreeWidgetItem *targetItem = m_targetItem;
            const DropZone zone = m_targetZone;
            m_pressItem = nullptr;
            m_targetItem = nullptr;
            
            QTreeWidget::mouseReleaseEvent(event);
            if (performMove(sourceItem, targetItem, zone)) {
                utils::Logger::info("DraggableTree", QStringLiteral(
                    "drop manual aceito (sem D&D nativo)."));
                emit itemsDropped();
            }
            return;
        }
        m_pressItem = nullptr;
        m_dragging = false;
        QTreeWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QTreeWidget::paintEvent(event);
        if (!m_hasIndicator) {
            return;
        }
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing, false);
        if (m_dropOnItem.isValid()) {
            QColor fill = m_indicatorColor;
            fill.setAlpha(60);
            p.fillRect(m_dropOnItem, fill);
            p.setPen(QPen(m_indicatorColor, 2));
            p.drawRect(m_dropOnItem.adjusted(1, 1, -1, -1));
        } else if (m_dropRow.isValid()) {
            p.fillRect(m_dropRow.adjusted(0, -1, 0, 1), m_indicatorColor);
            p.setBrush(m_indicatorColor);
            p.setPen(Qt::NoPen);
            const int y = m_dropRow.center().y();
            p.drawEllipse(QPoint(m_dropRow.left() + 3, y), 3, 3);
        }
    }

protected:
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override
    {
        if (m_connectorStyle == ConnectorStyle::Native) {
            QTreeWidget::drawBranches(painter, rect, index);
            return;
        }

        int depth = 0;
        for (QModelIndex p = index.parent(); p.isValid(); p = p.parent()) {
            ++depth;
        }

        if (m_connectorStyle == ConnectorStyle::Continuous && model()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->setPen(QPen(m_connectorLineColor, 1));

            const int indent = indentation();
            const int rowMidY = rect.top() + rect.height() / 2;

            QModelIndex node = index.parent();
            for (int col = depth - 2; col >= 0 && node.isValid(); --col) {
                if (node.row() < model()->rowCount(node.parent()) - 1) {
                    const int x = rect.left() + col * indent + indent / 2;
                    painter->drawLine(x, rect.top(), x, rect.bottom());
                }
                node = node.parent();
            }

            if (depth > 0) {
                const bool isLastChild = index.row() == model()->rowCount(index.parent()) - 1;
                const int x = rect.left() + (depth - 1) * indent + indent / 2;
                const int vBottom = isLastChild ? rowMidY : rect.bottom();
                painter->drawLine(x, rect.top(), x, vBottom);

                // Verifica se é pasta (tem filhos) ou comando (não tem filhos)
                const bool hasChildren = model()->hasChildren(index);
                int lineEndX = rect.left() + depth * indent;

                if (hasChildren) {
                    // Para pastas: o traço vai até o meio da coluna do chevron (espaço do próprio nível)
                    lineEndX = x + indent / 2;
                } else {
                    // Para comandos: o traço vai até o início do texto/conteúdo visual
                    if (auto *item = itemFromIndex(index)) {
                        lineEndX = visualItemRect(item).left();
                    }
                }

                painter->drawLine(x, rowMidY, lineEndX, rowMidY);
            }

            painter->restore();
        }

        if (model() && model()->hasChildren(index)) {
            QStyleOptionViewItem opt;
            opt.initFrom(this);
            opt.rect = QRect(rect.left() + depth * indentation(), rect.top(), indentation(), rect.height());
            opt.state = QStyle::State_Children;
            if (isExpanded(index)) {
                opt.state |= QStyle::State_Open;
            }
            style()->drawPrimitive(QStyle::PE_IndicatorBranch, &opt, painter, this);
        }
    }

private:
    enum class DropZone { Above, On, Below };

    void updateIndicatorForPos(const QPoint &pos)
    {
        QTreeWidgetItem *target = itemAt(pos);
        m_dropRow = QRect();
        m_dropOnItem = QRect();
        m_targetItem = target;
        if (!target) {
            m_hasIndicator = false;
            viewport()->update();
            return;
        }
        const QRect r = visualItemRect(target);
        const bool acceptsChildren = !m_canAcceptChildren || m_canAcceptChildren(target);
        const int quarter = qMax(1, r.height() / 4);
        if (pos.y() < r.top() + quarter) {
            m_targetZone = DropZone::Above;
            m_dropRow = QRect(r.left(), r.top() - 1, r.width(), 2);
        } else if (pos.y() > r.bottom() - quarter) {
            m_targetZone = DropZone::Below;
            m_dropRow = QRect(r.left(), r.bottom() - 1, r.width(), 2);
        } else if (acceptsChildren) {
            m_targetZone = DropZone::On;
            m_dropOnItem = r;
        } else if (pos.y() < r.center().y()) {
            m_targetZone = DropZone::Above;
            m_dropRow = QRect(r.left(), r.top() - 1, r.width(), 2);
        } else {
            m_targetZone = DropZone::Below;
            m_dropRow = QRect(r.left(), r.bottom() - 1, r.width(), 2);
        }
        m_hasIndicator = true;
        viewport()->update();
    }

    bool performMove(QTreeWidgetItem *source, QTreeWidgetItem *target, DropZone zone)
    {
        if (!source || !target || source == target) {
            return false;
        }
        for (QTreeWidgetItem *p = target; p; p = p->parent()) {
            if (p == source) {
                return false;
            }
        }
        if (zone == DropZone::On && m_canAcceptChildren && !m_canAcceptChildren(target)) {
            return false;
        }

        QTreeWidgetItem *oldParent = source->parent();
        const int oldIndex = oldParent ? oldParent->indexOfChild(source) : indexOfTopLevelItem(source);
        QTreeWidgetItem *taken = oldParent ? oldParent->takeChild(oldIndex) : takeTopLevelItem(oldIndex);
        if (!taken) {
            return false;
        }

        if (zone == DropZone::On) {
            target->addChild(taken);
            target->setExpanded(true);
        } else {
            QTreeWidgetItem *newParent = target->parent();
            int targetIndex = newParent ? newParent->indexOfChild(target) : indexOfTopLevelItem(target);
            if (newParent == oldParent && oldIndex < targetIndex) {
                targetIndex -= 1;
            }
            const int insertAt = (zone == DropZone::Below) ? targetIndex + 1 : targetIndex;
            if (newParent) {
                newParent->insertChild(insertAt, taken);
            } else {
                insertTopLevelItem(insertAt, taken);
            }
        }
        setCurrentItem(taken);
        return true;
    }

    QColor m_indicatorColor{189, 147, 249};
    ConnectorStyle m_connectorStyle = ConnectorStyle::Native;
    QColor m_connectorLineColor{139, 233, 253}; // Alterado para um tom mais claro por padrão
    bool m_hasIndicator = false;
    QRect m_dropRow;
    QRect m_dropOnItem;
    bool m_dragging = false;
    QTreeWidgetItem *m_pressItem = nullptr;
    QTreeWidgetItem *m_targetItem = nullptr;
    DropZone m_targetZone = DropZone::On;
    QPoint m_pressPos;
    std::function<bool(QTreeWidgetItem *)> m_canAcceptChildren;
};

} // namespace kai::ui