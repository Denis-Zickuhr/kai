#pragma once

#include <QWidget>
#include <QBoxLayout>
#include <QVector>

class QScrollArea;

namespace kai::ui {

// Container genérico que hospeda 0-3 "grupos de ações" (ItemActionsBar,
// ExpandCollapseBar, ActionSidebar — cada um representando os grupos Item/
// Exibição/Execução) lado a lado (upper bar, horizontal) ou empilhados
// (side bar, vertical), com um divisor fino entre grupos adjacentes
// VISÍVEIS. Qual grupo entra em qual container (ou se fica oculto) é
// decidido pelo MainWindow conforme a preferência de posicionamento de
// cada grupo no Settings — ver MainWindow::applyActionGroupPlacement().
//
// Visual: as barras HORIZONTAIS (dentro da moldura da caixa de comandos) são
// planas, como o cabeçalho da Saída — sem "ilha", só os ícones e uma linha
// separadora (ver setEdgeSeparator). As colunas VERTICAIS (fora dela) têm a
// própria moldura (borda 1px + radiusMd, igual aos painéis principais), com
// os ícones soltos dentro (ver refreshStyle()).
class ActionGroupContainer : public QWidget {
    Q_OBJECT

public:
    explicit ActionGroupContainer(Qt::Orientation orientation, QWidget *parent = nullptr);

    // Substitui o conjunto de grupos hospedados, na ordem dada. Cada
    // ponteiro é reparentado para este container. Uma lista vazia esconde
    // o container inteiro (evita uma caixa vazia ocupando espaço/chamando
    // atenção à toa).
    void setGroups(const QVector<QWidget *> &groups);

    // Reaplica o QSS (cor/raio do tema) — chamado no live reload e na
    // troca de "Estilo de cantos" (ver MainWindow::applyAppearanceSettings).
    void refreshStyle();

    // Só orientação horizontal: linha de 1px na borda dada (Bottom para a
    // barra acima da árvore, Top para a de baixo), separando a barra do
    // conteúdo como no cabeçalho da Saída. 0 = sem linha.
    void setEdgeSeparator(Qt::Edge edge);

    // Só orientação horizontal. Altura que o CONTEÚDO dos grupos pede e o ajuste
    // da altura da barra: o MainWindow usa as duas para a Saída ter
    // cabeçalho/rodapé da MESMA altura.
    int naturalBarHeight() const;
    void setBarHeight(int height);

    // Largura "natural" do conteúdo (a pílula de ícones), não a do próprio
    // container. Na orientação Vertical, sizeHint() do QWidget não serve —
    // o widget expõe um QScrollArea (necessário pra não impor altura
    // mínima ao terminal), e QScrollArea::sizeHint() é um valor genérico,
    // independente do conteúdo real da pílula lá dentro. Usado pelo
    // MainWindow para dar à coluna de ícones (que não é mais
    // redimensionável por arraste) uma largura correta por padrão — ver
    // MainWindow::applyActionGroupPlacement().
    int contentWidth() const;

private:
    QWidget *createSeparator();

    Qt::Orientation m_orientation;
    Qt::Edge m_separatorEdge{};
    QBoxLayout *m_layout{nullptr};
    QVector<QWidget *> m_separators;
    // Quando VERTICAL (sidebar), o conteúdo vive dentro de um QScrollArea
    // para não impor uma altura mínima que limite o crescimento do terminal:
    // se os ícones não couberem, rola em vez de esticar a coluna (pedido do
    // usuário). Nulos na orientação horizontal.
    QScrollArea *m_scroll{nullptr};
    QWidget *m_content{nullptr};
    // Widget VISUAL da pílula (só existe na orientação Vertical): o
    // QScrollArea estica m_content pra preencher a coluna inteira do
    // splitter quando o conteúdo é menor que a viewport — se a pílula
    // fosse o próprio m_content, herdaria esse esticamento (bug relatado
    // com print: a pílula lateral virou uma faixa cobrindo a coluna
    // inteira). m_pill fica DENTRO de m_content, com altura fixa no seu
    // próprio sizeHint e ancorado no topo (ver construtor); nullptr na
    // orientação Horizontal, onde `this` já hospeda os botões direto.
    QWidget *m_pill{nullptr};
};

} // namespace kai::ui
