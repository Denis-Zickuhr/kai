#pragma once

#include <QTreeWidget>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRect>

#include "utils/logger.h"

namespace kai::ui {

// QTreeWidget que sinaliza de forma CONFIÁVEL quando um drag-and-drop
// interno termina. Motivo (bug real corrigido: "ordenação via drag não
// funciona"): o QTreeWidget em modo InternalMove implementa o drop como
// remove+insert das linhas, disparando rowsRemoved/rowsInserted — e NÃO
// rowsMoved. Conectar em QAbstractItemModel::rowsMoved (como era feito)
// portanto quase nunca chamava o handler, e a nova ordem nunca era
// persistida. Aqui sobrescrevemos dropEvent: deixamos o QTreeWidget
// reordenar visualmente (base), e SÓ ENTÃO emitimos itemsDropped(), quando
// a árvore já reflete o resultado final — o chamador lê a árvore e persiste
// a ordem/hierarquia.
//
// INDICADOR VISUAL DE DESTINO (feedback do usuário): o indicador nativo do
// Qt é quase invisível no tema escuro. Desenhamos um próprio: uma LINHA
// grossa de acento entre itens (drop "no meio") ou um RETÂNGULO destacando
// a pasta/item alvo (drop "sobre"), atualizado em tempo real no
// dragMoveEvent.
class DraggableTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    explicit DraggableTreeWidget(QWidget *parent = nullptr) : QTreeWidget(parent)
    {
        // IMPORTANTE: manter o indicador nativo LIGADO. O QTreeWidget só
        // calcula a posição correta do drop (dropIndicatorPosition) quando
        // ele está ativo; desligá-lo fazia o drop cair sempre no fim (bug
        // reportado "joga a linha por último"). O indicador nativo é quase
        // invisível no tema escuro, então pintamos o NOSSO por cima
        // (paintEvent) — sem prejudicar o cálculo de posição do Qt.
        setDropIndicatorShown(true);
    }

    // Cor de acento do indicador (ajustada pelo tema ativo).
    void setIndicatorColor(const QColor &color) { m_indicatorColor = color; update(); }

signals:
    void itemsDropped();

protected:
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        QTreeWidget::dragMoveEvent(event);
        // Recalcula onde o drop cairia e guarda para pintar.
        const QPoint pos = event->position().toPoint();
        m_dropRow = QRect();
        m_dropOnItem = QRect();
        QTreeWidgetItem *target = itemAt(pos);
        if (!target) {
            m_hasIndicator = false;
            viewport()->update();
            return;
        }
        const QRect r = visualItemRect(target);
        const DropIndicatorPosition dip = dropIndicatorPosition();
        switch (dip) {
        case QAbstractItemView::AboveItem:
            m_dropRow = QRect(r.left(), r.top() - 1, r.width(), 2);
            break;
        case QAbstractItemView::BelowItem:
            m_dropRow = QRect(r.left(), r.bottom() - 1, r.width(), 2);
            break;
        case QAbstractItemView::OnItem:
            m_dropOnItem = r; // destaca a pasta/item alvo inteiro
            break;
        case QAbstractItemView::OnViewport:
        default:
            break;
        }
        m_hasIndicator = m_dropRow.isValid() || m_dropOnItem.isValid();
        viewport()->update();
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        m_hasIndicator = false;
        viewport()->update();
        QTreeWidget::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        m_hasIndicator = false;
        viewport()->update();
        const DropIndicatorPosition dip = dropIndicatorPosition();
        QTreeWidget::dropEvent(event);
        // Log bruto do drop (diagnóstico do bug de order no Windows): diz
        // se o Qt aceitou o movimento e qual a posição de inserção.
        utils::Logger::info("DraggableTree",
            QStringLiteral("dropEvent: aceito=%1 posicao=%2")
                .arg(event->isAccepted() ? QStringLiteral("sim") : QStringLiteral("nao"))
                .arg(static_cast<int>(dip)));
        // Só sinaliza se o drop foi de fato aceito (movimento interno
        // efetivado), evitando reordenações espúrias em drops rejeitados.
        if (event->isAccepted()) {
            emit itemsDropped();
        }
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
            // Destaque da pasta/item alvo: retângulo de acento translúcido
            // + borda de acento.
            QColor fill = m_indicatorColor;
            fill.setAlpha(60);
            p.fillRect(m_dropOnItem, fill);
            p.setPen(QPen(m_indicatorColor, 2));
            p.drawRect(m_dropOnItem.adjusted(1, 1, -1, -1));
        } else if (m_dropRow.isValid()) {
            // Linha grossa de acento entre itens.
            p.fillRect(m_dropRow.adjusted(0, -1, 0, 1), m_indicatorColor);
            // Pequenos "cantos" para dar aparência de cursor de inserção.
            p.setBrush(m_indicatorColor);
            p.setPen(Qt::NoPen);
            const int y = m_dropRow.center().y();
            p.drawEllipse(QPoint(m_dropRow.left() + 3, y), 3, 3);
        }
    }

private:
    QColor m_indicatorColor{189, 147, 249}; // accent Dracula por padrão
    bool m_hasIndicator = false;
    QRect m_dropRow;
    QRect m_dropOnItem;
};

} // namespace kai::ui
