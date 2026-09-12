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
// Discreto por padrão (feedback do usuário sobre a antiga ExpandCollapseBar
// "chamar atenção demais" com fundo+borda+raio cheios): a orientação
// HORIZONTAL ganha só uma linha fina embaixo (mesmo estilo do cabeçalho da
// Saída); a VERTICAL não tem chrome próprio (mesma aparência neutra que a
// ActionSidebar sempre teve).
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

private:
    QWidget *createSeparator();

    Qt::Orientation m_orientation;
    QBoxLayout *m_layout{nullptr};
    QVector<QWidget *> m_separators;
    // Quando VERTICAL (sidebar), o conteúdo vive dentro de um QScrollArea
    // para não impor uma altura mínima que limite o crescimento do terminal:
    // se os ícones não couberem, rola em vez de esticar a coluna (pedido do
    // usuário). Nulos na orientação horizontal.
    QScrollArea *m_scroll{nullptr};
    QWidget *m_content{nullptr};
};

} // namespace kai::ui
