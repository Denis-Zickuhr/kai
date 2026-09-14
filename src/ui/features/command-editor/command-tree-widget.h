#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVector>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QMap>
#include <QColor>
#include <QPixmap>
#include <functional>

class QPaintEvent;
class QEvent;
class QLabel;
class QMovie;

#include "core/models.h"
#include "core/sync-drift.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/features/command-editor/folder-actions-view.h"

class QMenu;
class QAction;

namespace kai::ui {

// Descreve a colocação de um nó da árvore após um drag-and-drop
// (reordenar e reparentar por arraste): id do item, se é comando, o id do
// novo pai (pasta OU outro comando — agrupar comando dentro de comando é
// permitido) e a nova ordem sequencial dentro do seu tipo. parentId vazio
// significa raiz da aba corrente.
struct TreeNodePlacement {
    QString id;
    bool isCommand = false;
    // Nota (não-executável, como a coleção): quando isCommand==false e isCollection==false.
    bool isNote = false;
    // Distingue Coleção (não-executável) de Pasta quando isCommand==false.
    // Sem isto, coleções eram tratadas como pastas e sua nova ordem/pasta
    // nunca era persistida (bug reportado: mover coleção não salvava).
    bool isCollection = false;
    QString parentId;
    int order = 0;
};

// Navegação por abas dinâmicas: cada pasta raiz (parent_id == null) vira
// uma aba com o ícone da pasta; subpastas e comandos permanecem aninhados
// na árvore da aba correspondente. Não existem abas fixas de Recentes,
// Projetos ou Customizados.
class CommandTreeWidget : public QWidget {
    Q_OBJECT

public:
    explicit CommandTreeWidget(QWidget *parent = nullptr);

    // Repopula as abas a partir das pastas e comandos carregados pelo
    // ConfigManager. Toda pasta raiz, inclusive uma pasta criada por
    // importação, é tratada exatamente da mesma forma.
    void setData(const QVector<core::Folder> &folders, const QVector<core::Command> &commands);
    // Sobrecarga que também recebe as coleções (item não-executável na
    // árvore). Coleções aparecem sob sua folderId como itens com ícone de
    // database; não podem ser executadas (play desabilitado), mas podem
    // ser editadas/duplicadas/excluídas via menu de contexto.
    void setData(const QVector<core::Folder> &folders, const QVector<core::Command> &commands,
                 const QVector<core::Collection> &collections);

    // Sobrecarga que também recebe as NOTAS (itens não-executáveis, com ícone de caderno, sob a pasta delas).
    void setData(const QVector<core::Folder> &folders, const QVector<core::Command> &commands,
                 const QVector<core::Collection> &collections, const QVector<core::Note> &notes);

    // Id da pasta PADRÃO sintética ("Geral") que hospeda comandos/coleções
    // órfãos (folderId vazio ou apontando para pasta/comando inexistente).
    // Não é persistida — existe só na árvore. Exposta para o MainWindow
    // tratar a exclusão dessa aba (remove os órfãos que ela contém).
    static QString defaultFolderId();

    // Atualiza os controles visuais inline por comando nas árvores dinâmicas:
    // comandos em execução exibem Rodando e botão Stop; os demais exibem Play.
    void setRunningCommandIds(const QSet<QString> &runningCommandIds);

    // AÇÕES de pasta (ver core/folder-actions.h): ícones na linha de cada
    // subpasta — as globais (aqui) antes das da própria pasta (Folder::actions).
    // O estado "rodando" (verde) vem de setRunningCommandIds com o id VIRTUAL da
    // ação (core::folderActionCommandId).
    void setGlobalActions(const QVector<core::GlobalAction> &actions);
    // Pastas-projeto fora de sincronia com o kai.yml (ProjectSyncManager::requestDriftCheck):
    // selo âmbar no ícone da pasta (ou da aba) e tooltip dizendo o que mudou. Pasta ausente
    // do mapa, ou InSync, volta ao normal.
    void setSyncDrift(const QMap<QString, core::SyncDrift> &driftByFolderId); // = engine::SyncDriftMap
    // Ícone "focado" (o que está mostrando a saída): id virtual, ou vazio.
    // Selecionar qualquer linha da árvore desfoca sozinho.
    void setFocusedAction(const QString &virtualId);
    // Ações que já rodaram (têm saída guardada pra carregar): muda a dica e o que o
    // clique faz no ícone (carrega em vez de rodar).
    void setActionsWithOutput(const QSet<QString> &virtualIds);
    QString focusedAction() const { return m_focusedActionId; }

    // Bug real reportado: "ao re-rodar um comando via double click ou
    // enter, esse tempo não reseta". Causa: setRunningCommandIds() só
    // reinicia o cronômetro de um id quando ele estava AUSENTE do conjunto
    // (transição parado->rodando detectada por diff contra o poll
    // periódico do MainWindow) — se o comando terminar e for disparado de
    // novo rápido o bastante, nenhum poll chega a ver o id ausente entre as
    // duas execuções, então o cronômetro antigo nunca é solto e continua
    // contando da primeira vez. Chamado pelo MainWindow no exato instante
    // (síncrono) em que uma execução começa — não depende de poll.
    void resetRunTimer(const QString &commandId);

    // TEMPO DE EXECUÇÃO: formata a duração no padrão pedido pelo usuário —
    // exibe o próximo token só quando ele é alcançado:
    //   "0s" -> "0m 0s" -> "0h 0m 0s"
    static QString formatElapsed(qint64 seconds);

    // Comandos cuja última execução terminou com erro (botão
    // inline de reset/reiniciar): exibem o botão de reset mesmo depois
    // de o processo já ter finalizado, enquanto o usuário não disparar
    // uma nova execução (que limpa a entrada correspondente).
    void setFailedCommandIds(const QSet<QString> &failedCommandIds);

    // Aplica filtro fuzzy somente à aba ativa e às suas árvores? O filtro é
    // aplicado em todas as abas para que a busca continue encontrando
    // comandos independentemente da pasta raiz selecionada.
    void setFilterQuery(const QString &query);

    // OCULTAR DA ÁRVORE: comandos com `hidden == true` somem por padrão de
    // TODAS as abas; setShowHidden(true) os traz de volta (meio apagados,
    // ver createTreeForRoot). Reconstrói a árvore para aplicar.
    void setShowHidden(bool show);
    bool showHidden() const { return m_showHidden; }

    // FILTRO "SÓ EM EXECUÇÃO": só os comandos rodando agora (e as pastas que os
    // contêm) ficam visíveis, em todas as abas. Acompanha a lista de
    // execuções em tempo real e combina com a busca. Não reconstrói a árvore.
    void setShowRunningOnly(bool runningOnly);
    bool showRunningOnly() const { return m_showRunningOnly; }

    // Retorna o item selecionado na árvore da aba ativa.
    QString currentSelectionId() const;
    bool currentSelectionIsFolder() const;
    bool currentSelectionIsCollection() const;
    bool currentSelectionIsNote() const;

    // Id da pasta raiz correspondente à aba atualmente ativa (a própria
    // aba representa essa pasta, que não é mais renderizada como item na
    // árvore). Vazio se não houver nenhuma aba.
    QString currentRootFolderId() const;

    // Move o foco para a árvore ativa e seleciona o primeiro item visível.
    void focusFirstVisibleItem();

    // PLANO DE FUNDO (feedback do usuário): define a imagem pintada atrás
    // da árvore de comandos (só neste widget) e a opacidade (0-100) do fundo
    // dos itens — quanto menor, mais a imagem transparece através da árvore.
    // Caminho vazio remove o fundo. A imagem é desenhada em "cover".
    void setBackground(const QString &imagePath, int opacity);

    // Altura da barra de abas das pastas raiz e reaplicação do estilo único de
    // abas (ver ui/shared/tab-bar-style.h): mesma altura/recuo/fonte das abas da
    // Saída. Chamado no boot e quando o tema/aparência muda.
    void setTabBarHeight(int height);

    // Estilo de renderização das linhas de conexão entre itens (Settings →
    // Aparência → "Linhas da árvore"): 0=nativa, 1=nenhuma, 2=contínua
    // (estilo `tree -d`). Aplicado a todas as árvores já criadas e às
    // futuras (rebuildTabs/createTreeForRoot). `lineColor` opcional recolori
    // as linhas com a cor do tema ativo (ver MainWindow::handleThemeReloaded).
    void setTreeConnectorStyle(int style, const QColor &lineColor = QColor());
    // Só recolori as linhas já configuradas (chamado no live reload do
    // tema, que não mexe na PREFERÊNCIA de estilo — só na cor do accent).
    void refreshTreeConnectorColor(const QColor &lineColor);

    // Navegação circular entre as abas dinâmicas.
    void selectNextTab();
    void selectPreviousTab();

    // Abre o menu de contexto sobre o item atualmente selecionado (ou o menu
    // de criação, se nada estiver selecionado). Usado pelo atalho remapeável
    // (Insert por padrão) — o menu resultante é navegável por teclado (o
    // QMenu do Qt já trata ↑/↓/Enter/Esc nativamente).
    void openContextMenuForCurrent();

    // Expande/colapsa o item atualmente selecionado, ou expande/colapsa
    // todos os itens — sempre na árvore da aba ATIVA (cada aba tem sua
    // própria QTreeWidget). Usados pela ExpandCollapseBar, acima da árvore.
    void expandCurrentItem();
    void collapseCurrentItem();
    void expandAll();
    void collapseAll();

    // Ativa a aba que contém o comando `id` e o seleciona (a Saída acompanha a
    // seleção). false se o comando não existe ou está oculto. Usado pelo
    // histórico de notificações ("ir para o comando").
    bool selectCommand(const QString &id) { return selectItemById(id); }

signals:
    void commandActivated(const QString &commandId);
    void editRequested(const QString &itemId, bool isFolder);
    void deleteRequested(const QString &itemId, bool isFolder);
    // Emitido pelo menu de contexto da ABA (pasta RAIZ) — pedido do
    // usuário: "preciso que adicione a possibilidade de ocultar pastas de
    // raiz" (achado real: uma pasta que ficou oculta enquanto ainda tinha
    // pai, e depois foi órfã pela exclusão do pai, virava aba raiz sem
    // NENHUM jeito de reverter — o form de edição de pasta não tem campo
    // "oculta", e o atalho normal de ocultar/mostrar só enxerga o item
    // ATUALMENTE selecionado dentro da árvore, nunca a aba em si).
    void toggleFolderHiddenRequested(const QString &folderId);
    // Clique num ícone de ação: executar (ou, se já roda, mostrar a saída) esse
    // comando com `folderId` como contexto.
    void folderActionActivated(const QString &commandId, const QString &folderId);
    // Botão direito num ícone de ação.
    void folderActionContextRequested(const QString &commandId, const QString &folderId, const QPoint &globalPos);
    // "Sincronizar com o arquivo" (kai.yml) de uma pasta-projeto.
    // `kaiToFile` true = Kai -> Arquivo; false = Arquivo -> Kai.
    void projectSyncRequested(const QString &folderId, bool kaiToFile);
    void exportRequested(const QString &itemId, bool isFolder);
    // Emitido pela ação "Duplicar" do menu de contexto: pede ao MainWindow
    // para criar uma cópia do comando/pasta (novo id, nome "X (cópia)").
    void duplicateRequested(const QString &itemId, bool isFolder);

    // Ações de COLEÇÃO (item não-executável). Roteadas para handlers
    // próprios no MainWindow: editar abre o grid da coleção; duplicar/
    // excluir operam no collections.json.
    void collectionEditRequested(const QString &collectionId);
    void collectionDuplicateRequested(const QString &collectionId);
    void collectionDeleteRequested(const QString &collectionId);
    // Ações de NOTA: abrir (duplo clique/Enter) no editor do leitor, propriedades (nome/ícone/tipo/local), duplicar,
    // excluir e criar.
    void noteActivated(const QString &noteId);
    void noteEditRequested(const QString &noteId);
    void noteDuplicateRequested(const QString &noteId);
    void noteDeleteRequested(const QString &noteId);
    void newNoteRequested();
    void quickEditBodyRequested(const QString &commandId);
    void selectionChanged(const QString &itemId, bool isFolder);
    // Clique num COMANDO (também quando ele já era o item atual, caso em que o Qt não emite currentItemChanged).
    // A saída pode estar mostrando outro comando mesmo com este selecionado (um KIP iniciado pela busca ou por um
    // atalho, por exemplo): quem ouve reconecta a saída se for o caso.
    void commandClicked(const QString &commandId);
    void killRequested(const QString &commandId);
    void playRequested(const QString &commandId);

    // Emitido pelo botão inline de reset (quadrado vermelho, 
    // novo botão): visível quando o comando está em execução ou quando
    // sua última execução terminou com erro. Pede para encerrar o
    // processo (se ainda rodando) e disparar uma nova execução do zero.
    void resetRequested(const QString &commandId);

    // Ações de CRIAÇÃO disparadas pelo menu de contexto (clicando numa pasta,
    // num item ou na área vazia da árvore) e pelo atalho remapeável (Insert).
    // O MainWindow resolve a pasta destino pela seleção/aba atual, então estes
    // sinais não carregam parâmetros — casam com os handlers já existentes.
    void newFolderRequested();
    void newCommandRequested();
    void newCollectionRequested();

    // Force-stop pelo menu de contexto de um comando (encerra à força). O
    // MainWindow roteia para o mesmo caminho de kill (terminate->timeout->kill).
    void forceStopRequested(const QString &commandId);

    // Emitido após o usuário reordenar/reparentar itens por drag-and-drop.
    // Carrega a colocação (novo pai + nova ordem) de todos os itens do
    // nível afetado, para o MainWindow persistir folderId/parentId e order
    // em commands.json de uma vez (corrige o bug de comando aninhado sob
    // comando que "sumia" no rebuild por o folderId não acompanhar o drop).
    void structureChanged(const QVector<TreeNodePlacement> &placements);
    // Reordenação de abas (pastas-raiz): persistir SEM reconstruir a árvore.
    void tabsReordered(const QVector<TreeNodePlacement> &placements);

private slots:
    void handleItemActivated(QTreeWidgetItem *item, int column);
    void handleTreeContextMenuRequested(const QPoint &pos);
    void handleTreeCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    // Chamado após um drop de reordenação interno na árvore: recalcula o
    // campo `order` de todos os itens afetados e emite orderChanged.
    void handleItemsReordered();
    // Reordenação das ABAS (pastas-raiz) via drag no QTabBar: recalcula o
    // order das pastas-raiz na nova ordem visual e emite structureChanged.
    void handleTabsReordered();

private:
    void setupUi();
    void rebuildTabs();
    // Monta e exibe o menu de contexto (com ícones) para `item` (pode ser
    // nullptr = área vazia, só ações de criação) na posição global dada.
    void showContextMenu(QTreeWidgetItem *item, const QPoint &globalPos);
    bool isProjectFolder(const QString &folderId) const;
    // Menu de contexto da ABA (pasta-raiz) em si — botão direito na aba,
    // não numa linha da árvore. Pedido do usuário: "quero a possibilidade
    // de editar pastas com o botão direito (pastas raiz)" — antes só dava
    // pra editar a pasta-raiz via o botão de editar da toolbar (que segue
    // a aba ATIVA), não por clique direito nela.
    void showTabContextMenu(int tabIndex, const QPoint &globalPos);
    // Adiciona as ações "Nova Pasta/Comando/Coleção" (com ícones) ao menu.
    void addCreationActions(
        QMenu *menu,
        const std::function<QAction *(QMenu *, const QString &, const QColor &, const QString &)> &addIconAction,
        const QColor &accent);
    // Emite o sinal de criação correspondente à ação escolhida (ou nada).
    void handleCreationChoice(QAction *chosen);
    // Ordena os dados (folders/commands/collections) por ordem manual e
    // reconstrói as abas. Chamado pelas sobrecargas de setData.
    void applyDataAndRebuild();
    void refreshCommandVisuals();
    QTreeWidget *createTreeForRoot(const core::Folder &rootFolder);
    void applyFilterToTree(QTreeWidget *tree, const QString &query);
    bool applyFilterToItem(QTreeWidgetItem *item, const QString &query);
    void focusFirstVisibleItem(QTreeWidget *tree);
    void selectFirstVisible(QTreeWidget *tree);
    // Preservação do estado de UI entre reconstruções da árvore
    // (rebuildTabs recria tudo). collectExpandedIds captura os ids das
    // pastas atualmente expandidas em todas as abas; restoreExpandedIds
    // reexpande esses ids após o rebuild; selectItemById reseleciona um
    // item pelo id, se ainda existir. Corrige o bug de pastas
    // autocolapsando ao persistir/editar comandos.
    QSet<QString> collectExpandedIds() const;
    void restoreExpandedIds(const QSet<QString> &expandedIds);
    bool selectItemById(const QString &id);
    static QString formatCommandLabel(const core::Command &command);
    static QString rootIdForFolder(const core::Folder &folder,
                                   const QMap<QString, core::Folder> &foldersById);

    QTabWidget *m_tabWidget = nullptr;
    QMap<QString, QTreeWidget *> m_treesByRootId;
    // Estilo/cor atuais das linhas de conexão — guardados para aplicar às
    // árvores futuras criadas por createTreeForRoot() (troca de tab, novo
    // root), não só às já existentes no momento de setTreeConnectorStyle().
    int m_treeConnectorStyle = 0;
    QColor m_treeConnectorLineColor;
    QVector<core::Folder> m_folders;
    QVector<core::Command> m_commands;
    QVector<core::Collection> m_collections;
    QVector<core::Note> m_notes;
    QSet<QString> m_runningCommandIds;
    QVector<core::GlobalAction> m_globalActions;
    QMap<QString, core::SyncDrift> m_syncDrift;
    QIcon folderIconFor(const core::Folder &folder) const;
    QString syncTooltipFor(const QString &folderId) const;
    void refreshSyncMarks();
    QString m_focusedActionId;
    QSet<QString> m_actionsWithOutput;
    QMap<QString, core::Command> m_commandIndex; // id -> comando, pra montar as ações sem varrer a lista a cada pintura
    bool m_rebuilding = false;                   // rebuildTabs em curso: restaurar a seleção não desfoca
    QVector<FolderActionIcon> folderActionIcons(const QString &folderId) const;
    void refreshActionViews();
    // Início de cada execução em andamento, para calcular o tempo decorrido.
    QMap<QString, QDateTime> m_runStartedAt;
    // Tique de 1s que atualiza os contadores dos comandos em execução.
    QTimer *m_elapsedTimer = nullptr;
    void updateElapsedLabels();
    QSet<QString> m_failedCommandIds;
    QString m_currentFilter;
    // O filtro (busca ou "só em execução") abre pastas para mostrar o que casou: o estado de expansão de ANTES é
    // guardado ao começar a filtrar e devolvido ao parar, em vez de deixar a árvore toda aberta.
    QHash<QString, bool> m_expansionBeforeFilter;
    bool m_filterSnapshotTaken = false;
    void snapshotExpansion();
    void restoreExpansion();
    // "Só em execução": a aba de uma pasta raiz sem nenhum item visível some.
    void updateRootTabVisibility();
    bool m_showHidden = false;
    bool m_showRunningOnly = false;
    // Dica sobre a aba quando o filtro "só em execução" não deixa nada visível.
    QLabel *m_runningOnlyHint = nullptr;
    void updateRunningOnlyHint();

    // Plano de fundo opcional da aba de comandos (ver setBackground).
    QPixmap m_backgroundImage;
    // GIF/animação de fundo (opcional): quando o arquivo é animado, o frame
    // atual é usado no lugar de m_backgroundImage. nullptr = imagem estática.
    QMovie *m_backgroundMovie = nullptr;
    int m_backgroundOpacity = 70; // 0-100; opacidade do fundo dos itens
    // Reaplica o estilo (transparência) do fundo às árvores/abas atuais.
    // Chamado por setBackground e após rebuildTabs (que recria as árvores).
    void applyBackgroundStyle();

protected:
    // Pinta a imagem de fundo (em cover) diretamente no viewport de cada
    // árvore, atrás dos itens, com um véu de opacidade. Feito por filtro de
    // evento para acertar a ÁREA exata (o conteúdo da árvore, não a tab bar).
    bool eventFilter(QObject *watched, QEvent *event) override;
};

} // namespace kai::ui
