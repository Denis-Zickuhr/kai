#pragma once

#include <QTreeWidget>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <QScrollBar>
#include <QRect>
#include <QApplication>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <functional>

#include "utils/design-tokens.h"
#include "utils/logger.h"

namespace kai::ui {

class DraggableTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    enum class ConnectorStyle { Native, None, Continuous };

    explicit DraggableTreeWidget(QWidget *parent = nullptr) : QTreeWidget(parent)
    {
        setMouseTracking(true);
    }

    void setIndicatorColor(const QColor &color) { m_indicatorColor = color; update(); }

    // Visual de explorador de pastas: sem linhas de conexão nem zebra, só o chevron, recuo maior e destaque
    // (hover/seleção) como cápsula recuada com o raio do design system.
    void setExplorerLook(bool on)
    {
        if (m_explorerLook == on) {
            return;
        }
        m_explorerLook = on;
        setIndentation(on ? 18 : 20);
        viewport()->update();
    }
    bool explorerLook() const { return m_explorerLook; }

    void setConnectorStyle(ConnectorStyle style) { m_connectorStyle = style; viewport()->update(); }
    void setConnectorLineColor(const QColor &color) { m_connectorLineColor = color; viewport()->update(); }

    // Cores de fundo da linha inteira (ver paintEvent). O QSS do item fica transparente em todo estado
    // ("CommandTreeWidget QTreeWidget::item") e este widget é o único responsável pelo fundo (zebra, hover,
    // seleção) cobrindo as duas colunas: o estado nativo é pintado por célula, e a 2ª coluna caía no fallback
    // da paleta, reabrindo um vão no hover.
    void setRowColors(const QColor &baseColor, const QColor &stripeColor,
                       const QColor &hoverColor, const QColor &selectedColor)
    {
        m_baseRowColor = baseColor;
        m_stripeRowColor = stripeColor;
        m_hoverRowColor = hoverColor;
        m_selectedRowColor = selectedColor;
        viewport()->update();
    }

    // Com várias árvores à vista só uma desenha a linha selecionada (a de quem tem o foco/marcas); as outras guardam o item
    // atual sem destacá-lo. Sem predicado, a seleção sempre aparece.
    void setSelectionVisiblePredicate(std::function<bool()> predicate)
    {
        m_selectionVisible = std::move(predicate);
        viewport()->update();
    }

    void setCanAcceptChildrenPredicate(std::function<bool(QTreeWidgetItem *)> predicate)
    {
        m_canAcceptChildren = std::move(predicate);
    }

signals:
    void itemsDropped();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        // O botão direito só abre o menu (que usa itemAt): não troca o item corrente, senão a Saída e o
        // Documento seguem o item do menu.
        if (event->button() == Qt::RightButton) {
            event->accept();
            return;
        }
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
        updateHoveredItem(itemAt(event->pos()));
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

    void leaveEvent(QEvent *event) override
    {
        QTreeWidget::leaveEvent(event);
        updateHoveredItem(nullptr);
    }

    // Zera State_HasFocus (item atual) e State_MouseOver antes do delegate/estilo desenhar: o Fusion desenha por
    // conta própria uma cápsula arredondada do tamanho do conteúdo nesses estados e nenhuma regra QSS em ::item
    // a suprime. Hover/seleção/linha atual são pintados à mão em paintEvent (ver setRowColors).
    void initViewItemOption(QStyleOptionViewItem *option) const override
    {
        QTreeWidget::initViewItemOption(option);
        option->state &= ~QStyle::State_HasFocus;
        option->state &= ~QStyle::State_MouseOver;
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
        if (event->button() == Qt::RightButton) {
            event->accept();
            return;
        }
        m_pressItem = nullptr;
        m_dragging = false;
        QTreeWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        // Fundo de toda linha visível pintado à mão antes do conteúdo nativo, na largura total do viewport. Prioridade:
        // seleção > hover > zebra/base. `visibleRow` conta só linhas visíveis (não ocultas pelo filtro), como o Qt
        // faz para alternate-row.
        if (m_baseRowColor.isValid() || m_stripeRowColor.isValid()
            || m_hoverRowColor.isValid() || m_selectedRowColor.isValid()) {
            QPainter bg(viewport());
            QTreeWidgetItem *current = (!m_selectionVisible || m_selectionVisible()) ? currentItem() : nullptr;
            int visibleRow = 0;
            std::function<void(QTreeWidgetItem *)> paintRow = [&](QTreeWidgetItem *item) {
                if (item->isHidden()) {
                    return;
                }
                const QRect r = visualItemRect(item);
                if (r.isValid()) {
                    QColor color;
                    if (item == current) {
                        color = m_selectedRowColor;
                    } else if (item == m_hoveredItem) {
                        color = m_hoverRowColor;
                    } else {
                        color = (!m_explorerLook && visibleRow % 2 == 1) ? m_stripeRowColor : m_baseRowColor;
                    }
                    if (color.isValid()) {
                        if (m_explorerLook) {
                            const bool highlighted = item == current || item == m_hoveredItem;
                            bg.fillRect(QRect(0, r.top(), viewport()->width(), r.height()), m_baseRowColor);
                            if (highlighted) {
                                bg.setRenderHint(QPainter::Antialiasing, true);
                                bg.setPen(Qt::NoPen);
                                bg.setBrush(color);
                                const int radius = utils::tokens::radiusSm();
                                bg.drawRoundedRect(QRectF(3, r.top() + 1, viewport()->width() - 6, r.height() - 2),
                                                   radius, radius);
                                bg.setRenderHint(QPainter::Antialiasing, false);
                            }
                        } else {
                            bg.fillRect(QRect(0, r.top(), viewport()->width(), r.height()), color);
                        }
                    }
                }
                ++visibleRow;
                if (item->isExpanded()) {
                    for (int i = 0; i < item->childCount(); ++i) {
                        paintRow(item->child(i));
                    }
                }
            };
            for (int i = 0; i < topLevelItemCount(); ++i) {
                paintRow(topLevelItem(i));
            }
        }

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
        if (m_connectorStyle == ConnectorStyle::Native && !m_explorerLook) {
            QTreeWidget::drawBranches(painter, rect, index);
            return;
        }

        int depth = 0;
        for (QModelIndex p = index.parent(); p.isValid(); p = p.parent()) {
            ++depth;
        }

        if (m_connectorStyle == ConnectorStyle::Continuous && !m_explorerLook && model()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            QColor lineColor = palette().color(QPalette::Text);
            lineColor.setAlpha(70);
            QPen pen(lineColor, 1.0, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);

            const int indent = indentation();
            // Centro de pixel da linha, o mesmo que o conteúdo do item usa: com height/2 a horizontal ficava 1px abaixo
            // do ícone e do texto.
            const int rowMidY = rect.top() + (rect.height() - 1) / 2;
            // +0.5: traço de 1px centrado no pixel, nítido mesmo com antialiasing. O fim da linha vai um pixel além do
            // retângulo da linha para os trechos verticais de linhas vizinhas se emendarem sem fresta.
            const qreal top = rect.top();
            const qreal bottom = rect.bottom() + 1;

            // Níveis acima: uma vertical contínua enquanto o ancestral ainda tem irmãos abaixo.
            QModelIndex node = index.parent();
            for (int col = depth - 2; col >= 0 && node.isValid(); --col) {
                if (node.row() < model()->rowCount(node.parent()) - 1) {
                    const qreal x = rect.left() + col * indent + indent / 2 + 0.5;
                    painter->drawLine(QPointF(x, top), QPointF(x, bottom));
                }
                node = node.parent();
            }

            if (depth > 0) {
                const bool isLastChild = index.row() == model()->rowCount(index.parent()) - 1;
                const qreal x = rect.left() + (depth - 1) * indent + indent / 2 + 0.5;
                const qreal y = rowMidY + 0.5;

                qreal lineEndX = rect.left() + depth * indent;
                if (model()->hasChildren(index)) {
                    // Pastas: o traço para antes do chevron, que ocupa a coluna do próprio nível.
                    lineEndX = x + indent / 2.0 - 4;
                } else if (auto *item = itemFromIndex(index)) {
                    lineEndX = visualItemRect(item).left() - 2;
                }
                lineEndX = qMax(lineEndX, x);

                // Último filho: a vertical faz uma curva suave para dentro e segue na horizontal (um "L" arredondado).
                // Os demais: a vertical segue reta até a próxima linha e a ramificação sai num "T" reto; uma curva
                // colada numa vertical que continua só parecia um gancho torto.
                if (isLastChild) {
                    const qreal radius = std::min<qreal>({5.0, y - top, lineEndX - x});
                    QPainterPath path;
                    path.moveTo(x, top);
                    path.lineTo(x, y - radius);
                    path.quadTo(QPointF(x, y), QPointF(x + radius, y));
                    path.lineTo(lineEndX, y);
                    painter->drawPath(path);
                } else {
                    painter->drawLine(QPointF(x, top), QPointF(x, bottom));
                    painter->drawLine(QPointF(x, y), QPointF(lineEndX, y));
                }
            }

            painter->restore();
        }

        if (model() && model()->hasChildren(index)) {
            const int indent = indentation();
            const QPointF c(rect.left() + depth * indent + indent / 2.0, rect.top() + rect.height() / 2.0);
            const qreal h = 3.5;
            QPolygonF chevron;
            if (isExpanded(index)) {
                chevron << QPointF(c.x() - h, c.y() - h / 2) << QPointF(c.x(), c.y() + h / 2)
                        << QPointF(c.x() + h, c.y() - h / 2);
            } else {
                chevron << QPointF(c.x() - h / 2, c.y() - h) << QPointF(c.x() + h / 2, c.y())
                        << QPointF(c.x() - h / 2, c.y() + h);
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            QPen pen(palette().color(QPalette::Text), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPolyline(chevron);
            painter->restore();
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

    void updateHoveredItem(QTreeWidgetItem *item)
    {
        if (item == m_hoveredItem) {
            return;
        }
        m_hoveredItem = item;
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
    QTreeWidgetItem *m_hoveredItem = nullptr;
    QColor m_baseRowColor;
    QColor m_stripeRowColor;
    QColor m_hoverRowColor;
    QColor m_selectedRowColor;
    std::function<bool()> m_selectionVisible;
    ConnectorStyle m_connectorStyle = ConnectorStyle::Native;
    bool m_explorerLook = false;
    QColor m_connectorLineColor{139, 233, 253};
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