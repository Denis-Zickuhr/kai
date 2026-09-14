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

    explicit DraggableTreeWidget(QWidget *parent = nullptr) : QTreeWidget(parent)
    {
        setMouseTracking(true);
    }

    void setIndicatorColor(const QColor &color) { m_indicatorColor = color; update(); }

    void setConnectorStyle(ConnectorStyle style) { m_connectorStyle = style; viewport()->update(); }
    void setConnectorLineColor(const QColor &color) { m_connectorLineColor = color; viewport()->update(); }

    // Cores de fundo da LINHA INTEIRA (ver paintEvent): a árvore de
    // comandos tem 2 colunas (nome/ícone e status), e todo estado nativo do
    // Qt (normal/zebra, hover, seleção) é pintado por CÉLULA — a segunda
    // coluna, vazia na maior parte do tempo, só entra no MESMO estado da
    // primeira quando é seleção (SelectRows força as duas células a
    // "selected" juntas); hover não tem equivalente — só a célula sob o
    // cursor vira :hover — então a segunda coluna caía no fallback nativo
    // (zebra da paleta) SÓ nela, repintando por cima do que pintássemos e
    // reabrindo um vão (relatado como "bordinha" entre os comandos, e que
    // voltava especificamente no hover). A solução: o QSS do item fica
    // transparente em TODO estado (ver app-stylesheet.cpp/theme-manager.cpp,
    // seletor "CommandTreeWidget QTreeWidget::item"), e este widget passa a
    // ser o ÚNICO responsável pelo fundo — normal/zebra, hover e seleção —
    // sempre cobrindo a LARGURA TOTAL do viewport (as duas colunas de uma
    // vez), sem exceção.
    void setRowColors(const QColor &baseColor, const QColor &stripeColor,
                       const QColor &hoverColor, const QColor &selectedColor)
    {
        m_baseRowColor = baseColor;
        m_stripeRowColor = stripeColor;
        m_hoverRowColor = hoverColor;
        m_selectedRowColor = selectedColor;
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

    // Tira o item "atual" (foco de teclado/clique) do estado
    // State_HasFocus E o item sob o mouse do State_MouseOver ANTES do
    // delegate/estilo nativo desenhar. Motivo: o Fusion (e temas
    // derivados) não decide "o que desenhar no hover/foco" só a partir das
    // propriedades CSS declaradas (background/border/outline/radius) — ele
    // TAMBÉM desenha, por conta própria, uma cápsula/contorno arredondado
    // de destaque sempre que o STATE do item tem State_MouseOver ou
    // State_HasFocus, do tamanho do CONTEÚDO (ícone+texto), não da célula
    // inteira — e nenhuma regra QSS em cima do `::item` consegue suprimir
    // esse desenho específico (relatado repetidamente pelo usuário: cantos
    // arredondados e largura curta reaparecendo no hover mesmo com todo o
    // CSS já zerado). A única forma confiável de eliminar essa decoração
    // nativa é o estilo NUNCA VER esses estados: zerando os bits aqui, o
    // delegate/estilo sempre pinta como "item comum", e a ÚNICA coisa que
    // marca hover/seleção/linha atual é o preenchimento manual, linha
    // inteira, sem cantos, pintado em paintEvent (ver setRowColors).
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
        m_pressItem = nullptr;
        m_dragging = false;
        QTreeWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        // Fundo de TODA linha visível pintado à mão, ANTES do conteúdo
        // nativo (ícones, texto, indicador de status) — normal/zebra,
        // hover e seleção, cobrindo sempre a largura TOTAL do viewport (as
        // duas colunas de uma vez, sem vão entre elas — ver setRowColors).
        // A prioridade visual é seleção > hover > zebra/base. `visibleRow`
        // conta só linhas visíveis (não ocultas pelo filtro) na ordem de
        // exibição, igual à contagem nativa do Qt para alternate-row.
        if (m_baseRowColor.isValid() || m_stripeRowColor.isValid()
            || m_hoverRowColor.isValid() || m_selectedRowColor.isValid()) {
            QPainter bg(viewport());
            QTreeWidgetItem *current = currentItem();
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
                        color = (visibleRow % 2 == 1) ? m_stripeRowColor : m_baseRowColor;
                    }
                    if (color.isValid()) {
                        bg.fillRect(QRect(0, r.top(), viewport()->width(), r.height()), color);
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