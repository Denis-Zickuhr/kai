#pragma once

#include <QWidget>
#include <QString>
#include <QColor>
#include <QVector>
#include <QPair>
#include <QBoxLayout>

class QToolButton;

namespace kai::ui {

// Formas de ícone desenhadas vetorialmente para os botões da sidebar
// (sem depender de arquivos de imagem — nítidos em qualquer DPI e
// recoloríveis conforme o tema).
enum class ActionIconShape {
    Play, Stop, ForceStop, Reset, EditBody
};

// Grupo de ações "Execução", sempre referentes ao item atualmente
// selecionado na árvore: Executar (play), Parar (stop), Forçar Parada
// (force stop), Resetar e Editar body (rápido — só comando HTTP). Os
// botões ficam SEMPRE carregados/visíveis, mas são habilitados apenas
// quando a linha selecionada permite a ação (feedback do usuário
// original: mover botões inline para a sidebar como row actions,
// habilitando conforme a linha). O chamador informa o estado da seleção
// via setRowContext() e recebe as ações via sinais.
//
// Reusável em qualquer um dos dois containers de posicionamento
// (ActionGroupContainer horizontal na upper bar, ou vertical na side bar —
// ver Settings "Onde exibir cada grupo de ações") via setOrientation().
//
// As ações de CRIAÇÃO/edição de item (editar pasta, nova pasta, novo
// comando, nova coleção, editar/excluir comando) viraram o grupo "Item"
// (ItemActionsBar); expandir/colapsar/ocultar viraram o grupo "Exibição"
// (ExpandCollapseBar) — pedido do usuário: os 3 grupos são posicionáveis
// independentemente, na ordem Execução, Exibição, Item.
class ActionSidebar : public QWidget {
    Q_OBJECT

public:
    explicit ActionSidebar(QWidget *parent = nullptr);

    // Atualiza o contexto da linha selecionada, habilitando/desabilitando
    // as row actions conforme o tipo e estado do item. `hasSelection`
    // false => tudo desabilitado.
    void setRowContext(bool hasSelection, bool isCommand, bool isRunning, bool hasFailed, bool isHttp = false);

    // Recolori os ícones NEUTROS (reset) com a cor de destaque do tema
    // ativo. Os ícones de execução (play/stop/kill) mantêm cores
    // semânticas fixas. Chamado pelo MainWindow ao carregar ou trocar o
    // tema.
    void applyAccentColor(const QColor &accent);

    // Alterna o layout interno entre coluna (side bar) e linha (upper bar).
    void setOrientation(Qt::Orientation orientation);

signals:
    // Row actions (referem-se ao item atualmente selecionado na árvore —
    // o MainWindow resolve o id via CommandTreeWidget::currentSelectionId).
    void playSelectedRequested();
    void stopSelectedRequested();
    void forceStopSelectedRequested();
    void resetSelectedRequested();
    // Editar SÓ o body (rápido) do comando HTTP selecionado.
    void editBodySelectedRequested();

private:
    void setupUi();

    QBoxLayout *m_layout{nullptr};
    QColor m_accent{189, 147, 249}; // accent padrão (Dracula) até o tema carregar
    QVector<QPair<QToolButton *, ActionIconShape>> m_neutralButtons;

    QToolButton *m_playButton = nullptr;
    QToolButton *m_stopButton = nullptr;
    QToolButton *m_forceStopButton = nullptr;
    QToolButton *m_resetButton = nullptr;
    QToolButton *m_editBodyButton = nullptr;
};

} // namespace kai::ui
