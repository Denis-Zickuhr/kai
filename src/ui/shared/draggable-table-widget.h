#pragma once

#include <QTableWidget>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QHeaderView>
#include <QRect>
#include <QApplication>

namespace kai::ui {

// QTableWidget com reorder de linhas por arrastar, com o indicador visual da DraggableTreeWidget.
// Não usa o D&D nativo do Qt (QDrag/mimeData): o payload nativo pode ser aceito por qualquer widget sob o cursor
// no release (ex.: um QLineEdit vizinho engolia o texto da linha). O rastreamento do mouse é manual e fica
// contido neste widget: press grava a origem, move (botão pressionado) desenha o indicador, release emite
// rowMoved(from, to). Também permite testar com QMouseEvent sintético em headless.
class DraggableTableWidget : public QTableWidget {
    Q_OBJECT

public:
    explicit DraggableTableWidget(int rows, int columns, QWidget *parent = nullptr)
        : QTableWidget(rows, columns, parent)
    {
        setSelectionBehavior(QAbstractItemView::SelectRows);
    }

    // Cor de acento do indicador (ajustada pelo tema ativo).
    void setIndicatorColor(const QColor &color) { m_indicatorColor = color; update(); }

signals:
    // `toRow` é a posição de inserção no modelo ORIGINAL (antes de remover `fromRow`); o receptor ajusta o índice
    // ao remover e reinserir (ver ParameterEditorWidget::handleRowMoved).
    void rowMoved(int fromRow, int toRow);

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        // A base trata primeiro (seleção normal da linha clicada): arrastar é só continuação possível do clique.
        QTableWidget::mousePressEvent(event);
        if (event->button() != Qt::LeftButton) {
            return;
        }
        const QModelIndex idx = indexAt(event->pos());
        m_pressRow = idx.isValid() ? idx.row() : -1;
        m_pressPos = event->pos();
        m_dragging = false;
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_pressRow < 0 || !(event->buttons() & Qt::LeftButton)) {
            QTableWidget::mouseMoveEvent(event);
            return;
        }
        if (!m_dragging) {
            // Só vira arrastar depois de um deslocamento mínimo.
            if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
                QTableWidget::mouseMoveEvent(event);
                return;
            }
            m_dragging = true;
        }
        // Arrastando: só atualiza o indicador de destino.
        updateIndicatorForPos(event->pos());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            m_hasIndicator = false;
            viewport()->update();
            const int fromRow = m_pressRow;
            const int toRow = m_targetRow;
            m_pressRow = -1;
            m_targetRow = -1;
            // A base fecha seu ciclo press/release ANTES do rowMoved: o dono reconstrói todas as linhas/itens logo em
            // seguida, e sem isso o estado interno do QAbstractItemView ficava preso na linha antiga e corrompia o
            // próximo arrasto.
            QTableWidget::mouseReleaseEvent(event);
            if (fromRow >= 0 && toRow >= 0) {
                emit rowMoved(fromRow, toRow);
            }
            return;
        }
        m_pressRow = -1;
        m_dragging = false;
        QTableWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QTableWidget::paintEvent(event);
        if (!m_hasIndicator || !m_dropRow.isValid()) {
            return;
        }
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(m_dropRow.adjusted(0, -1, 0, 1), m_indicatorColor);
        p.setBrush(m_indicatorColor);
        p.setPen(Qt::NoPen);
        const int y = m_dropRow.center().y();
        p.drawEllipse(QPoint(m_dropRow.left() + 3, y), 3, 3);
    }

private:
    void updateIndicatorForPos(const QPoint &pos)
    {
        // pos pode estar fora da área visível (arrasto até a borda): indexAt() devolve inválido, então clampa
        // para o topo/fundo em vez de desistir do indicador.
        QModelIndex idx = indexAt(pos);
        if (!idx.isValid() && rowCount() > 0) {
            const int clampedY = qBound(rowViewportPosition(0) + 1, pos.y(),
                rowViewportPosition(rowCount() - 1) + rowHeight(rowCount() - 1) - 1);
            idx = indexAt(QPoint(pos.x(), clampedY));
        }
        if (!idx.isValid() || columnCount() == 0) {
            m_hasIndicator = false;
            viewport()->update();
            return;
        }
        const QRect first = visualRect(idx.siblingAtColumn(0));
        const QRect last = visualRect(idx.siblingAtColumn(columnCount() - 1));
        const QRect r = first.united(last);
        const bool below = pos.y() > r.center().y();
        m_dropRow = below ? QRect(r.left(), r.bottom() - 1, r.width(), 2)
                           : QRect(r.left(), r.top() - 1, r.width(), 2);
        m_targetRow = below ? idx.row() + 1 : idx.row();
        m_hasIndicator = true;
        viewport()->update();
    }

    QColor m_indicatorColor{189, 147, 249};
    bool m_hasIndicator = false;
    QRect m_dropRow;
    int m_targetRow = -1;
    bool m_dragging = false;
    int m_pressRow = -1;
    QPoint m_pressPos;
};

} // namespace kai::ui
