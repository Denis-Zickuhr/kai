#pragma once

#include <QTableWidget>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QHeaderView>
#include <QRect>
#include <QApplication>

namespace kai::ui {

// QTableWidget com reorder de linhas por arrastar — pedido do usuário
// ("parâmetros dinâmicos são fortamente posicionais, preciso de uma
// estratégia de reorder, podemos usar a mesma estratégia da saída de
// terminal") — a estratégia VISUAL é a mesma da árvore de comandos (ver
// DraggableTreeWidget): indicador de linha de destino, mesma cor de
// acento. A MECÂNICA é bem diferente de propósito, e não por acaso:
//
// Duas tentativas anteriores usaram o D&D NATIVO do Qt
// (QAbstractItemView::InternalMove + QDrag), e as DUAS tiveram bugs reais
// reportados pelo usuário que nunca foram eliminados por completo mexendo
// em detalhes do mecanismo (Qt::MoveAction vs CopyAction, clearOrRemove()
// automático de QAbstractItemView::startDrag()). O bug final, decisivo,
// que matou de vez a abordagem nativa: "é como se o drag colocasse o
// param dentro do outro componente ao invés de na tabela" — o D&D nativo
// do Qt é uma operação de VERDADE a nível de sistema operacional, com um
// payload de MIME DATA (que por padrão inclui o texto da linha) que
// QUALQUER outro widget sob o cursor no momento do release pode aceitar
// — um QLineEdit vizinho (ex: campo Nome) engolindo o texto do parâmetro
// arrastado se o ponto de soltar saísse um pixel da área da tabela. Não
// tem como restringir esse escape de forma confiável só ajustando a ação
// de drop; o problema é estrutural ao usar D&D nativo pra um reorder que
// só deveria acontecer DENTRO do próprio widget.
//
// Esta versão não usa QDrag/mimeData NENHUM — é rastreamento de mouse
// MANUAL (pressionar numa linha, mover, soltar), 100% contido dentro
// deste widget: mousePressEvent grava a linha de origem, mouseMoveEvent
// (só enquanto o botão está pressionado) desenha o indicador e calcula o
// alvo, mouseReleaseEvent emite rowMoved(from, to). Sem loop de eventos
// nativo do SO envolvido, nada pode escapar pra outro widget — e, bônus
// real: dá pra TESTAR de verdade em headless com QMouseEvent sintético
// (D&D nativo precisa de um drag loop do SO que não reproduz direito
// offscreen, o que impediu qualquer teste anterior de pegar esse bug).
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
    // `toRow` é a posição de inserção no modelo ORIGINAL (antes de remover
    // `fromRow`) — o receptor faz o ajuste de índice de praxe ao remover e
    // reinserir (ver comentário em ParameterEditorWidget::handleRowMoved).
    void rowMoved(int fromRow, int toRow);

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        // Deixa a base tratar PRIMEIRO (seleção normal da linha clicada) —
        // o gesto de arrastar é só uma CONTINUAÇÃO possível de um clique
        // normal, não um substituto dele.
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
            // Só vira "arrastar" depois de um deslocamento mínimo — um
            // clique simples (sem mover) não deve disparar reorder nenhum.
            if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
                QTableWidget::mouseMoveEvent(event);
                return;
            }
            m_dragging = true;
        }
        // Enquanto arrasta, a tabela NÃO trata mais o movimento como
        // seleção/hover normal — só atualiza o indicador de destino.
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
            // Achado real (bug relatado: "arrasto, alguns some, outros vao
            // errados" — persistindo mesmo depois de tirar o D&D nativo):
            // pular QTableWidget::mouseReleaseEvent() aqui deixava o
            // estado INTERNO de QAbstractItemView (índice pressionado,
            // máquina de estado de seleção) preso na linha antiga — o
            // dono (rowMoved -> handleRowMoved -> rebuildTable()) destrói
            // TODAS as linhas/itens/cell widgets logo em seguida, e sem a
            // base ter fechado o próprio ciclo de press/release ANTES
            // disso, a base ficava referenciando itens que não existem
            // mais. Isso não quebrava o drag ATUAL (from/to já foram
            // capturados em variáveis locais acima), mas corrompia a
            // PRÓXIMA interação — exatamente "alguns vão errados" (o
            // drag seguinte, não o atual). Chamar a base ANTES de emitir
            // rowMoved deixa QAbstractItemView fechar seu próprio estado
            // enquanto a tabela ainda existe do jeito que ele espera.
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
        // pos pode estar FORA da área visível (usuário arrastou até a
        // borda) — indexAt() devolve inválido nesse caso; clampa pro
        // topo/fundo da tabela em vez de simplesmente desistir do
        // indicador (senão arrastar até o começo/fim da lista não
        // funciona, exatamente o caso de uso mais comum de reorder).
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

    QColor m_indicatorColor{189, 147, 249}; // accent Dracula por padrão
    bool m_hasIndicator = false;
    QRect m_dropRow;
    int m_targetRow = -1;
    bool m_dragging = false;
    int m_pressRow = -1;
    QPoint m_pressPos;
};

} // namespace kai::ui
