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
#include "core/note-files.h"
#include "core/sync-drift.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/features/command-editor/folder-actions-view.h"

class QMenu;
class QAction;

namespace kai::ui {

// Colocação de um nó após drag-and-drop: novo pai (pasta ou outro comando; vazio = raiz da aba)
// e nova ordem sequencial dentro do tipo.
struct TreeNodePlacement {
    QString id;
    bool isCommand = false;
    // Nota (não-executável, como a coleção): quando isCommand==false e isCollection==false.
    bool isNote = false;
    // Coleção (não-executável) vs Pasta quando isCommand==false.
    bool isCollection = false;
    QString parentId;
    int order = 0;
};

// Abas dinâmicas: cada pasta raiz (parent_id == null) vira uma aba com o ícone da pasta;
// subpastas e comandos ficam aninhados na árvore da aba.
class CommandTreeWidget : public QWidget {
    Q_OBJECT

public:
    explicit CommandTreeWidget(QWidget *parent = nullptr);

    // Repopula as abas; toda pasta raiz (inclusive importada) é tratada igual.
    void setData(const QVector<core::Folder> &folders, const QVector<core::Command> &commands);
    // Também recebe coleções (itens não-executáveis sob sua folderId; editar/duplicar/excluir pelo menu).
    void setData(const QVector<core::Folder> &folders, const QVector<core::Command> &commands,
                 const QVector<core::Collection> &collections);

    // Sobrecarga que também recebe as NOTAS (itens não-executáveis, com ícone de caderno, sob a pasta delas).
    void setData(const QVector<core::Folder> &folders, const QVector<core::Command> &commands,
                 const QVector<core::Collection> &collections, const QVector<core::Note> &notes);

    // Id da pasta padrão sintética ("Geral") que hospeda órfãos (folderId vazio ou inexistente);
    // não persistida. Exposta para o MainWindow tratar a exclusão dessa aba.
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
    // Documentos com alterações não salvas (rascunhos do leitor de documentos): as pastas que os têm (e, para a árvore
    // fechada não esconder o aviso, as pastas-pai delas) e as notas. Ganham um selo no ícone e uma linha no balão.
    void setDraftMarks(const QSet<QString> &folderIds, const QSet<QString> &noteIds);
    // Ícone "focado" (o que está mostrando a saída): id virtual, ou vazio.
    // Selecionar qualquer linha da árvore desfoca sozinho.
    void setFocusedAction(const QString &virtualId);
    // Ações que já rodaram (têm saída guardada pra carregar): muda a dica e o que o
    // clique faz no ícone (carrega em vez de rodar).
    void setActionsWithOutput(const QSet<QString> &virtualIds);
    QString focusedAction() const { return m_focusedActionId; }
    // Sem o foco inteligente, o menu do item oferece "Mostrar saída" (comando com saída) e "Abrir documento" (item com
    // documentação): quem responde é o MainWindow. Sem predicado, nenhum dos dois aparece.
    void setFocusPredicates(std::function<bool(const QString &commandId)> hasOutput,
                            std::function<bool(const QString &itemId)> hasDocument)
    {
        m_commandHasOutput = std::move(hasOutput);
        m_itemHasDocument = std::move(hasDocument);
    }
    // O comando que está na Saída (marca em destaque na linha dele; os outros com saída ganham a marca discreta).
    void setShownCommand(const QString &commandId);
    QString shownCommand() const { return m_shownCommandId; }
    // Redesenha as marcas (o que elas dizem mudou fora da árvore, como o foco inteligente).
    void refreshRowMarks() { refreshActionViews(); }
    // Com as marcas ligadas (sem o foco inteligente) a coluna de status ganha o espaço delas, ao lado do tempo.
    void setOutputMarksEnabled(bool enabled);
    // Ações inline: botões do item logo depois do nome, na linha sob o mouse e na selecionada.
    void setInlineActionsEnabled(bool enabled);

    // Reinicia o cronômetro no instante em que uma execução começa: setRunningCommandIds() só o
    // reinicia na transição ausente->presente, e um re-run rápido pode nunca ser visto ausente pelo poll.
    void resetRunTimer(const QString &commandId);
    // Quando a execução em curso começou (inválido: não está rodando). O mesmo relógio do tempo na linha.
    QDateTime runStartedAt(const QString &commandId) const { return m_runStartedAt.value(commandId); }

    // "0s" -> "0m 0s" -> "0h 0m 0s" (o próximo token só aparece quando alcançado).
    static QString formatElapsed(qint64 seconds);

    // Comandos cuja última execução falhou: mantêm o botão de reset até uma nova execução limpar a entrada.
    void setFailedCommandIds(const QSet<QString> &failedCommandIds);

    // Filtro fuzzy aplicado em todas as abas (a busca acha comandos de qualquer pasta raiz).
    void setFilterQuery(const QString &query);

    // Comandos com `hidden == true` somem de todas as abas; true os traz de volta (meio apagados).
    // Reconstrói a árvore.
    void setShowHidden(bool show);
    bool showHidden() const { return m_showHidden; }

    // Só comandos rodando agora (e as pastas que os contêm), em todas as abas; combina com a busca.
    // Não reconstrói a árvore.
    void setShowRunningOnly(bool runningOnly);
    bool showRunningOnly() const { return m_showRunningOnly; }

    QString currentSelectionId() const;
    bool currentSelectionIsFolder() const;
    bool currentSelectionIsCollection() const;
    bool currentSelectionIsNote() const;

    // Id da pasta raiz da aba ativa (a aba é a pasta, não um item da árvore). Vazio sem abas.
    QString currentRootFolderId() const;

    void focusFirstVisibleItem();

    // Imagem pintada atrás da árvore ("cover"; caminho vazio remove) e opacidade 0-100 do fundo dos itens.
    void setBackground(const QString &imagePath, int opacity);

    // Altura da barra de abas das pastas raiz e reaplicação do estilo único de
    // abas (ver ui/shared/tab-bar-style.h): mesma altura/recuo/fonte das abas da
    // Saída. Chamado no boot e quando o tema/aparência muda.
    void setTabBarHeight(int height);

    // Linhas de conexão: 0=nativa, 1=nenhuma, 2=contínua (estilo `tree -d`). Vale para as árvores
    // existentes e futuras; `lineColor` opcional recolore com a cor do tema.
    void setTreeConnectorStyle(int style, const QColor &lineColor = QColor());
    // Só recolore (live reload do tema muda a cor do accent, não a preferência de estilo).
    void refreshTreeConnectorColor(const QColor &lineColor);

    void selectNextTab();
    void selectPreviousTab();

    // Menu de contexto do item selecionado (ou de criação, sem seleção); usado pelo atalho remapeável (Insert).
    void openContextMenuForCurrent();

    // Expande/colapsa o item selecionado ou todos, na árvore da aba ativa (ExpandCollapseBar).
    void expandCurrentItem();
    void collapseCurrentItem();
    void expandAll();
    void collapseAll();

    // Ativa a aba que contém o comando `id` e o seleciona (a Saída acompanha a
    // seleção). false se o comando não existe ou está oculto. Usado pelo
    // histórico de notificações ("ir para o comando").
    bool selectCommand(const QString &id) { return selectItemById(id); }
    // Pasta RAIZ não é um item da árvore, é uma aba: ativa essa aba e tira a seleção de dentro dela.
    bool showRootTab(const QString &rootFolderId);

signals:
    void commandActivated(const QString &commandId);
    void editRequested(const QString &itemId, bool isFolder);
    void deleteRequested(const QString &itemId, bool isFolder);
    // Menu de contexto da ABA (pasta raiz): único jeito de ocultar uma raiz, já que o form de pasta
    // não tem campo "oculta".
    void toggleFolderHiddenRequested(const QString &folderId);
    // Clique num ícone de ação: executar (ou, se já roda, mostrar a saída) esse
    // comando com `folderId` como contexto.
    void folderActionActivated(const QString &commandId, const QString &folderId);
    // Botão direito num ícone de ação.
    void folderActionContextRequested(const QString &commandId, const QString &folderId, const QPoint &globalPos);
    // Menu do item: mostrar a saída do comando / o documento do item (sem executar nem selecionar).
    void commandOutputRequested(const QString &commandId);
    void documentRequested(const QString &itemId);
    // "Sincronizar com o arquivo" (kai.yml) de uma pasta-projeto.
    // `kaiToFile` true = Kai -> Arquivo; false = Arquivo -> Kai.
    void projectSyncRequested(const QString &folderId, bool kaiToFile);
    void exportRequested(const QString &itemId, bool isFolder);
    // "Fixar como painel" (comando KIP): vira um mini-app permanente do quadro de painéis.
    void pinAsPanelRequested(const QString &commandId);
    // "Duplicar": pede ao MainWindow uma cópia (novo id, nome "X (cópia)").
    void duplicateRequested(const QString &itemId, bool isFolder);

    // Ações de COLEÇÃO (item não-executável); editar abre o grid da coleção.
    void collectionEditRequested(const QString &collectionId);
    void collectionDuplicateRequested(const QString &collectionId);
    void collectionDeleteRequested(const QString &collectionId);
    // Ações de NOTA: abrir (duplo clique/Enter) no editor do leitor, propriedades (nome/ícone/tipo/local), duplicar,
    // excluir e criar.
    void noteActivated(const QString &noteId);
    void noteEditRequested(const QString &noteId);
    void noteDuplicateRequested(const QString &noteId);
    void noteDeleteRequested(const QString &noteId);
    // Salvar como / abrir cópia / copiar o conteúdo da nota; e importar um arquivo de texto como nota nova.
    void noteFileActionRequested(const QString &noteId, core::NoteFileAction action);
    void newNoteRequested();
    void quickEditBodyRequested(const QString &commandId);
    void quickEditFolderRequested(const QString &folderId);
    void selectionChanged(const QString &itemId, bool isFolder);
    // Clique num COMANDO (também quando ele já era o item atual, caso em que o Qt não emite currentItemChanged).
    // A saída pode estar mostrando outro comando mesmo com este selecionado (um KIP iniciado pela busca ou por um
    // atalho, por exemplo): quem ouve reconecta a saída se for o caso.
    void commandClicked(const QString &commandId);
    // Idem para uma PASTA (ou nota): clicar nela de novo, já selecionada, não emite selectionChanged, mas o painel dela
    // (Documento) precisa voltar à frente.
    void folderClicked(const QString &itemId);
    void killRequested(const QString &commandId);
    void playRequested(const QString &commandId);

    // Botão inline de reset (comando rodando ou última execução com erro): encerra o processo, se
    // rodando, e reexecuta.
    void resetRequested(const QString &commandId);

    // Criação (menu de contexto ou Insert); o MainWindow resolve a pasta destino pela seleção/aba atual.
    void newFolderRequested();
    void newCommandRequested();
    void newCollectionRequested();

    // Force-stop pelo menu de contexto; mesmo caminho de kill do MainWindow (terminate->timeout->kill).
    void forceStopRequested(const QString &commandId);

    // Após drag-and-drop: colocação (pai + ordem) de todos os itens do nível afetado, para persistir de uma vez.
    void structureChanged(const QVector<TreeNodePlacement> &placements);
    // Reordenação de abas (pastas-raiz): persistir SEM reconstruir a árvore.
    void tabsReordered(const QVector<TreeNodePlacement> &placements);

private slots:
    void handleItemActivated(QTreeWidgetItem *item, int column);
    void handleTreeContextMenuRequested(const QPoint &pos);
    void handleTreeCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    // Após um drop interno: recalcula `order` dos itens afetados e emite structureChanged.
    void handleItemsReordered();
    // Reordenação das ABAS (pastas-raiz) via drag no QTabBar: recalcula o
    // order das pastas-raiz na nova ordem visual e emite structureChanged.
    void handleTabsReordered();

private:
    void setupUi();
    void rebuildTabs();
    // Menu de contexto de `item` (nullptr = área vazia: só criação).
    void showContextMenu(QTreeWidgetItem *item, const QPoint &globalPos);
    bool isProjectFolder(const QString &folderId) const;
    // Menu de contexto da ABA (pasta raiz), botão direito na aba.
    void showTabContextMenu(int tabIndex, const QPoint &globalPos);
    void addCreationActions(
        QMenu *menu,
        const std::function<QAction *(QMenu *, const QString &, const QColor &, const QString &)> &addIconAction,
        const QColor &accent);
    void handleCreationChoice(QAction *chosen);
    // Ordena por ordem manual e reconstrói as abas (chamado pelas sobrecargas de setData).
    void applyDataAndRebuild();
    void refreshCommandVisuals();
    QTreeWidget *createTreeForRoot(const core::Folder &rootFolder);
    void applyFilterToTree(QTreeWidget *tree, const QString &query);
    bool applyFilterToItem(QTreeWidgetItem *item, const QString &query);
    void focusFirstVisibleItem(QTreeWidget *tree);
    void selectFirstVisible(QTreeWidget *tree);
    // Estado de UI preservado entre reconstruções (rebuildTabs): ids de pastas expandidas e seleção por id.
    QSet<QString> collectExpandedIds() const;
    void restoreExpandedIds(const QSet<QString> &expandedIds);
    bool selectItemById(const QString &id);
    static QString formatCommandLabel(const core::Command &command);
    static QString rootIdForFolder(const core::Folder &folder,
                                   const QMap<QString, core::Folder> &foldersById);

    QTabWidget *m_tabWidget = nullptr;
    QMap<QString, QTreeWidget *> m_treesByRootId;
    // Guardados para aplicar também às árvores criadas depois (createTreeForRoot).
    int m_treeConnectorStyle = 0;
    QColor m_treeConnectorLineColor;
    QVector<core::Folder> m_folders;
    QVector<core::Command> m_commands;
    QVector<core::Collection> m_collections;
    QVector<core::Note> m_notes;
    QSet<QString> m_runningCommandIds;
    bool m_inlineActions = false;
    QVector<core::GlobalAction> m_globalActions;
    QMap<QString, core::SyncDrift> m_syncDrift;
    QSet<QString> m_draftFolderIds; // já com as pastas-pai
    QSet<QString> m_draftNoteIds;
    QIcon noteIconFor(const core::Note &note) const;
    QString noteTooltipFor(const core::Note &note) const;
    QIcon folderIconFor(const core::Folder &folder) const;
    QString syncTooltipFor(const QString &folderId) const;
    void refreshSyncMarks();
    QString m_focusedActionId;
    QSet<QString> m_actionsWithOutput;
    std::function<bool(const QString &)> m_commandHasOutput;
    std::function<bool(const QString &)> m_itemHasDocument;
    QString m_shownCommandId;
    bool m_outputMarks = false;
    QMap<QString, core::Command> m_commandIndex; // id -> comando, pra montar as ações sem varrer a lista a cada pintura
    bool m_rebuilding = false;                   // rebuildTabs em curso: restaurar a seleção não desfoca
    QVector<FolderActionIcon> folderActionIcons(const QString &folderId) const;
    void refreshActionViews();
    // Início de cada execução em andamento, para calcular o tempo decorrido.
    QMap<QString, QDateTime> m_runStartedAt;
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

    QPixmap m_backgroundImage;
    // GIF/animação de fundo (opcional): quando o arquivo é animado, o frame
    // atual é usado no lugar de m_backgroundImage. nullptr = imagem estática.
    QMovie *m_backgroundMovie = nullptr;
    int m_backgroundOpacity = 70; // 0-100; opacidade do fundo dos itens
    void applyBackgroundStyle();

protected:
    // Pinta o fundo (cover) no viewport de cada árvore, atrás dos itens; via filtro de evento para
    // acertar a área exata (conteúdo, não a tab bar).
    bool eventFilter(QObject *watched, QEvent *event) override;
};

} // namespace kai::ui
