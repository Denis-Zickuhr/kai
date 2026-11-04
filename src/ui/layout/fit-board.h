#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QWidget>

#include "ui/layout/layout-model.h"

#include <functional>

namespace kai::ui::layout {

class ViewZone;

// O quadro do modo Fit: uma grade de casas iguais onde cada célula ocupa um retângulo de casas. Cada célula tem uma alça no
// canto de baixo à direita; arrastá-la na direção desejada estica (ou encolhe) a célula por mais colunas/linhas.
class FitBoard : public QWidget {
public:
    struct Item {
        ViewZone *widget = nullptr;
        FitCell cell;
    };

    // Dado o pedido (colunas × linhas) de uma célula, devolve o que cabe (o modelo decide).
    using SpanLimiter = std::function<QSize(Zone, int, int)>;
    using SpanCommit = std::function<void(Zone, int, int)>;

    static constexpr int kGap = 6;

    FitBoard(int cols, int rows, QWidget *parent = nullptr);

    void setItems(const QList<Item> &items);
    void setSpanHandlers(SpanLimiter limiter, SpanCommit commit);
    QRect cellRect(int row, int col, int rowSpan, int colSpan) const;
    // A alça de uma célula (para os testes).
    QWidget *gripOf(Zone zone) const;
    // Durante o arrasto: o retângulo do pedido em casas (x=coluna, y=linha, w/h=vãos), ou vazio.
    QRect dragPreview() const { return m_previewCells; }

    // O arrasto da alça (a alça chama; público para os testes simularem sem mouse).
    void dragMoved(Zone zone, const QPoint &boardPos);
    void dragEnded(Zone zone);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void relayout();

    int m_cols;
    int m_rows;
    QList<Item> m_items;
    QList<QWidget *> m_grips;
    QWidget *m_preview = nullptr;
    SpanLimiter m_limiter;
    SpanCommit m_commit;
    QRect m_previewCells;
};

} // namespace kai::ui::layout
