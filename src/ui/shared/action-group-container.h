#pragma once

#include <QWidget>
#include <QBoxLayout>
#include <QVector>

class QScrollArea;

namespace kai::ui {

// Container que hospeda 0-3 grupos de ações (ItemActionsBar, ExpandCollapseBar, ActionSidebar) lado a lado
// (horizontal) ou empilhados (vertical), com divisor fino entre grupos visíveis adjacentes. Qual grupo entra em qual
// container é decidido pelo MainWindow (applyActionGroupPlacement()).
// As barras horizontais (dentro da moldura da lista) são planas, só ícones e uma linha separadora (setEdgeSeparator);
// as colunas verticais têm moldura própria (borda 1px + radiusMd) com os ícones soltos dentro (refreshStyle()).
class ActionGroupContainer : public QWidget {
    Q_OBJECT

public:
    explicit ActionGroupContainer(Qt::Orientation orientation, QWidget *parent = nullptr);

    // Substitui os grupos hospedados, na ordem dada (cada ponteiro é reparentado para este container). Lista vazia
    // esconde o container.
    void setGroups(const QVector<QWidget *> &groups);

    // Reaplica o QSS (cor/raio do tema); chamado no live reload e na troca de "Estilo de cantos".
    void refreshStyle();

    // Só horizontal: linha de 1px na borda dada (Bottom para a barra acima da árvore, Top para a de baixo). 0 = sem linha.
    void setEdgeSeparator(Qt::Edge edge);

    // Só horizontal: altura que o conteúdo dos grupos pede e o ajuste da altura da barra (a Saída usa a mesma altura).
    int naturalBarHeight() const;
    void setBarHeight(int height);

    // Largura natural do conteúdo (a pílula de ícones). Na orientação Vertical sizeHint() não serve: o widget expõe
    // um QScrollArea, cujo sizeHint é genérico. Usado por MainWindow::applyActionGroupPlacement().
    int contentWidth() const;

    // Barra horizontal: pede o que os ícones precisam, mas cabe em qualquer largura (rola em vez de cortar).
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QWidget *createSeparator();

    Qt::Orientation m_orientation;
    Qt::Edge m_separatorEdge{};
    QBoxLayout *m_layout{nullptr};
    QVector<QWidget *> m_separators;
    // Vertical: o conteúdo vive num QScrollArea para não impor altura mínima (rola em vez de esticar a coluna).
    // Nulos na horizontal.
    QScrollArea *m_scroll{nullptr};
    QWidget *m_content{nullptr};
    // Widget visual da pílula (só Vertical): o QScrollArea estica m_content até a coluna inteira, então a pílula fica
    // DENTRO dele, com altura fixa no próprio sizeHint e ancorada no topo. nullptr na Horizontal.
    QWidget *m_pill{nullptr};
    // Orientação Horizontal: os grupos vivem num widget dentro de um QScrollArea que rola na horizontal quando a barra fica
    // estreita demais para os ícones (rolar em vez de cortar). nullptr na Vertical.
    QWidget *m_flat{nullptr};
};

} // namespace kai::ui
