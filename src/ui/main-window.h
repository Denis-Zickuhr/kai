#pragma once

#include "ui/features/docs/doc-note.h"
#include <QJsonArray>
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMap>
#include <QSet>
#include <map>
#include <memory>
#include <QIcon>
#include <QDateTime>
#include <QElapsedTimer>
#include <memory>

#include "core/config-manager.h"
#include "core/environment-manager.h"
#include "core/run-history.h"
#include "core/notification-history.h"
#include "core/models.h"
#include "core/note-files.h"
#include "engine/execution-pipeline.h"
#include "engine/process-manager.h"
#include "engine/cron-scheduler.h"
#include "engine/project-sync-manager.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/shared/expand-collapse-bar.h"
#include "ui/shared/item-actions-bar.h"
#include "ui/shared/status-line.h"
#include <QHash>
#include <QPointer>
#include <QTimer>
#include "ui/shared/action-group-container.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/features/collections/import-selection-dialog.h"
#include "ui/features/collections/project-selector.h"
#include "ui/features/collections/project-import-options-dialog.h"
#include "ui/shared/top-utility-bar.h"
#include "ui/shared/action-sidebar.h"
#include "ui/features/history/process-list-dialog.h"
#include "ui/features/output/logs-panel.h"
#include "ui/layout/layout-model.h"
#include "utils/theme-manager.h"

class QHotkey;
class QSplitter;
class QShortcut;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace kai::ui {

namespace layout {
class DockHost;
class LayoutController;
class LayoutStore;
class ViewRegistry;
} // namespace layout

class ExternalRunSession;
class AiChatWindow;
class MainWindowAgentHost;
class McpBridge;
class QuickRunPopup;
class RunMirrorPanel;
class DocSessionStore;
class DocumentPanel;
class EnvironmentManagerDialog;
class ParameterFormDialog;

// Janela principal: titlebar nativa, bandeja, atalho global (QHotkey), Terminal Drawer. A lógica de negócio
// (ConfigManager, EnvironmentManager, ExecutionPipeline) entra por composição e sinais/slots.
class MainWindow : public QMainWindow {
    Q_OBJECT

    // O host do agente de IA enxerga as listas vivas e a persistência (ver features/ai/).
    friend class MainWindowAgentHost;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Executa um comando pelo NOME (case-insensitive). Retorna false + msg
    // se não encontrado/ambíguo. Não bloqueia (dispara a execução).
    bool runCommandByName(const QString &name, QString &message);
    // `kai import [arquivo|pasta]`: importa o kai.yml sem diálogo (pasta-pai = raiz). `only` vazio = tudo;
    // ver cliImportSelection. `message` = resumo ou erro.
    bool importProjectFromCli(const QString &path, const QStringList &only, QString &message);
    // `kai import --recursive <pasta>`: importa cada projeto (kai.yml) abaixo da pasta, a própria inclusa.
    // `parentFolderId` vazio = raiz (ou a pasta do Kai que já contém o diretório).
    bool importProjectTreeFromCli(const QString &rootDirectory, QString &message, const QString &parentFolderId = QString());
    // A pasta do Kai (projeto ou subpasta) cujo diretório de trabalho contém `directory`, a mais funda; vazio = nenhuma.
    QString enclosingFolderIdFor(const QString &directory) const;
    // `kai attach <nome|pid>`: espelha a saída de um processo que JÁ está
    // rodando (reenvia o fim do log já impresso, depois segue ao vivo) e
    // repassa o teclado. nullptr + `errorMessage` se não há esse processo.
    ExternalRunSession *startAttachSession(const QString &target, QString &errorMessage);
    QJsonArray runningProcessItems();
    // `kai -gdn`: aviso de fim de uma execução disparada pelo CLI em modo destacado (o CLI já foi embora).
    void notifyExternalRunFinished(const QString &commandId, int exitCode, qint64 elapsedMs);
    bool raiseNotificationFromCli(const QString &level, const QString &title, const QString &body,
                                  QString &message);
    // Roda `commandId` pelo app a pedido de um cliente EXTERNO (`kai -g` via IPC) e devolve uma sessão que
    // espelha saída/fim e aceita teclado. nullptr + `errorMessage` se o comando não existe ou já está rodando.
    // `openOutputWindow` (-w): abre a saída numa janela desacoplada. `invocationDir`: diretório do terminal do
    // cliente; vale no lugar do working dir do comando quando ele pede (cliWorkingDir).
    ExternalRunSession *startExternalRun(const QString &commandId,
                                         const QMap<QString, QString> &paramValues,
                                         QString &errorMessage, bool openOutputWindow = false,
                                         const QString &invocationDir = QString());
    // Saída guardada em memória de um comando (id real ou virtual de uma ação de
    // pasta), limitada pelo tamanho máximo de log das configurações.
    QString storedOutputFor(const QString &commandId) const { return m_commandLogs.value(commandId); }
    QStringList commandNames() const;
    // Ativa um environment pelo NOME. Retorna false + msg se não achado.
    bool activateEnvironmentByName(const QString &name, QString &message);
    QStringList environmentNames(QString &activeName);
    // `reason` só alimenta o log: toda vez que o Kai se põe à frente do que o usuário está fazendo fica registrado.
    void showAndRaise(const char *reason = "unspecified");
    // Chamado por main.cpp depois da decisão de visibilidade do boot. Daqui em diante a janela só aparece por ação
    // explícita do usuário (atalho global, bandeja, `kai show`/CLI); qualquer outro show é descartado.
    void armVisibilityGuard();
    // Ver o motivo em setVisible (main-window.cpp): show sem ação do usuário é descartado.
    void setVisible(bool visible) override;

    // O quadro de painéis (zonas, abas, flutuantes), para os testes.
    layout::DockHost *dockHost() const { return m_dock; }
    layout::ViewRegistry *viewRegistry() const { return m_viewRegistry; }
    layout::LayoutController *layoutController() const { return m_layoutController; }
    DocumentPanel *documentPanel() const { return m_documentPanel; }

    // false sem ícone de bandeja (afeta notificações e "ocultar ao rodar").
    bool trayAvailable() const { return m_trayIcon != nullptr; }

    // true se o atalho global foi registrado (setupGlobalHotkey roda síncrono no construtor).
    bool globalHotkeyRegistered() const { return static_cast<bool>(m_globalHotkey); }

    // Decisão final de visibilidade no boot: binária, vem só de settings.startVisible e nunca consulta o atalho
    // global (ele pode falhar por motivo alheio ao usuário: WSL/WSLg, colisão com outro app). Reabrir sem ele:
    // bandeja ou `kai show`.
    bool shouldStartVisible();
    // Nomes de todas as variáveis dinâmicas capturadas, em qualquer escopo (autocomplete {{var}}).
    QStringList availableDynamicVarNames() const;

    // Lista os processos em execução: linhas "nome | pid | status".
    QStringList runningProcessLines();
    bool attachProcessByName(const QString &name, QString &message);
    bool killProcessByName(const QString &name, QString &message);

private:
    // Alvo por PID (numérico) OU nome do comando, entre os processos em execução.
    bool resolveProcessTarget(const QString &arg, QString &commandId,
                              QString &commandName, QString &error);
public:

protected:
    void changeEvent(QEvent *event) override;
    // Reaplica a máscara de cantos a cada redimensionamento (ver updateWindowShape).
    void resizeEvent(QResizeEvent *event) override;

    void closeEvent(QCloseEvent *event) override;
    // Foca a árvore de comandos ao exibir, para as setas operarem de imediato.
    void showEvent(QShowEvent *event) override;
    // Resize por borda na janela frameless: delega ao window manager (QWindow::startSystemResize), que funciona em
    // X11/WSLg sem calcular geometria.
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *e) override;
#if defined(Q_OS_WIN)
    // Snap, redimensionar pelas bordas e arrastar pela barra de título com o comportamento nativo do Windows.
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif
    // Instalado no container central, que cobre a janela e receberia o mouse antes da MainWindow.
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    // Pára tudo o que está rodando (pelo mesmo caminho do botão Parar de cada comando).
    void stopAllRunning();
    // Põe a saída de um comando num painel próprio: um app KIP vira mini app (`kip:`), o resto um espelho (`run:`).
    void openRunPanel(const QString &commandId);
    void pinKipPanel(const QString &commandId);
    void startProjectSync(const QString &folderId, bool kaiToFile);
    void handleCommandActivated(const QString &commandId);
    void handlePipelineLog(const QString &commandId, const QString &text, bool isError);
    void handlePipelineFinished(const engine::PipelineResult &result);
    void handleKipSessionStarted(const QString &commandId, engine::KipSession *session);
    void handleKipNotify(const QString &commandId, const QString &title, const QString &text, core::KipLevel level);
    void handleKipAnswersRemembered(const QString &commandId, const QJsonObject &remembered);
    void handleImportRequested();
    void handleExportRequested();
    // Abre o diálogo de exportar; com `targetId`, já apontando pra essa pasta/comando.
    void showExportDialog(const QString &targetId, bool targetIsFolder);
    void handleTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void trackScreenChanges();
    void handleThemeReloaded(const utils::ResolvedTheme &theme);
    void handleNewFolderRequested();
    void handleNewCommandRequested();
    void handleNewCollectionRequested();
    void handleEditRequested(const QString &commandId, bool isFolder);
    void handleDeleteRequested(const QString &commandId, bool isFolder);
    void handleDuplicateRequested(const QString &itemId, bool isFolder);
    void handleNewNoteRequested();
    void handleNoteEditRequested(const QString &noteId);
    void handleNoteDuplicateRequested(const QString &noteId);
    void handleNoteDeleteRequested(const QString &noteId);
    // Levar a nota para fora do Kai (salvar como, abrir cópia no editor do sistema, copiar) e trazer um arquivo de
    // texto como nota nova. O conteúdo vem da nota guardada (menu da árvore) ou da tela (leitor, com edições não salvas).
    void handleNoteFileActionRequested(const QString &noteId, core::NoteFileAction action);
    void exportNote(const QString &noteId, core::NoteFileAction action, const QString &content);
    // A saída guardada de um comando vira uma nota (na pasta dele), sem abrir nada além dela.
    void saveOutputAsNote(const QString &commandId);
    QString plainLogFor(const QString &commandId) const;
    void handleNoteActivated(const QString &noteId);
    void handleNoteSelected(const QString &noteId);
    void persistNotes();
    void showNoteView(const QString &noteId, bool startEditing, bool reveal = true);
    QVector<DocNote> docNotesFor(const QString &folderId) const;
    QString folderDocumentDirectory(const QString &folderId) const;
    // O botão das pastas do painel Documento: menu com as pastas que têm documentação, mais usadas primeiro.
    void showDocsMenu(const QPoint &globalPos);
    void openFolderDocs(const QString &folderId);
    void recordDocUse(const QString &folderId);
    void refreshDocNotes();
    void resetDrawerForNonCommand();
    bool isNoteId(const QString &id) const;
    void handleCollectionEditRequested(const QString &collectionId);
    void handleCollectionDuplicateRequested(const QString &collectionId);
    void handleCollectionDeleteRequested(const QString &collectionId);
    void persistCollections();
    // Remove uma pasta e, recursivamente, suas subpastas e comandos.
    // Não confirma nem persiste (o chamador decide).
    void deleteFolderRecursively(const QString &folderId);
    // Abre o FolderDeleteDialog (flags: apagar filhos por tipo) e executa a
    // exclusão seletiva da pasta `folderId`; o que não for apagado é
    // reparenteado para o avô. NÃO persiste (o chamador chama persistCommands).
    void deleteFolderWithDialog(const QString &folderId);
    void handleQuickEditBodyRequested(const QString &commandId);
    void handleQuickEditFolderRequested(const QString &folderId);
    void handleSettingsRequested();
    // IA (roadmap A10): abre o chat; sem provedor configurado o painel do chat não existe.
    void showAiChat();
    void refreshAiAvailability();
    void handleConfigRecovered(const QString &filePath, const QString &backupPath);
    void handleEnvironmentSelected(const QString &environmentId);
    void handleManageEnvironmentsRequested();
    void saveEnvironmentsFromPanel();
    void handleRunHistoryRequested();
    void handleNotificationHistoryRequested();
    void handleLogsRequested();
    void handleHelpRequested();
    void handleWelcomeScreenClosed();
    void handleShowWelcomeRequested();
    void handleTerminalInputEntered(const QString &text);
    // Ctrl+C / Ctrl+D do terminal interativo: enviam \x03/\x04 ao runner ativo.
    void handleTerminalInterrupt();
    void handleTerminalEof();
    void handleTerminalCloseRequested();
    // Terminal interativo (Command::interactiveTerminal, ver PtyTerminalWidget):
    // bytes de teclado prontos para o PTY, e mudança de tamanho em células.
    void handleTerminalRawInput(const QByteArray &data);
    void handleTerminalSizeChanged(int rows, int cols);
    void handleFirstErrorInFormattedOutput();

    void handleBackgroundProcessStarted(const QString &commandId, engine::ProcessRunner *runner);
    void handleBackgroundProcessOutput(const QString &commandId, const QString &text, bool isError);
    void handleBackgroundProcessStatusChanged(const QString &commandId, engine::ProcessStatus status);
    void handleShowProcessListRequested();
    // `force`: false = encerramento gracioso (SIGTERM -> timeout configurável -> SIGKILL, ProcessRunner::stop);
    // true = SIGKILL imediato (ProcessRunner::forceStop).
    void handleKillCommandRequested(const QString &commandId, bool force = false);
    void handleResetCommandRequested(const QString &commandId);
    void handleTreeStructureChanged(const QVector<TreeNodePlacement> &placements);
    void toggleVisibility();
    void handleMaximizeRestore();
    void handleWindowMoveRequested();

private:
    void setupUi();
    void setupToolbar();
    void setupTrayIcon();
    enum class NotificationEvent { CommandFailure, BackgroundProcessCrash, BackgroundProcessSuccess, ConfigRecovered, FirstErrorInFormattedOutput, KipNotify };
    // Toast nativo da bandeja se habilitado nas Configurações E o toggle do evento estiver ligado (ver
    // notification-gate.h); sem bandeja é no-op. `commandId` (opcional) fica no histórico para "ir para o comando".
    void maybeShowNotification(NotificationEvent event, const QString &title, const QString &body,
                                QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Warning,
                                const QString &commandId = QString(),
                                const QString &eventKeyOverride = QString());
    // A view KIP deste comando está na frente do usuário (janela visível, em foco, mostrando o comando; ou a
    // janela destacada em foco)? O `notify` do programa só vira toast quando não está.
    bool isKipViewVisible(const QString &commandId) const;
    QIcon loadAppIcon() const;
    // Runner do comando conectado ao Terminal Drawer (execução única do pipeline ou processo background do
    // ProcessManager); nullptr se não houver.
    engine::ProcessRunner *connectedProcessRunner() const;
    void setupGlobalHotkey();
    void setupTheme();
    void setupActionShortcuts();
    void applyEditShortcutOnSelection();
    void applyDeleteShortcutOnSelection();
    // Reconstrói os containers de ação conforme o posicionamento de cada grupo (Item/Exibição/Execução) em
    // SettingsData; chamado no setup e ao salvar Configurações.
    void applyActionGroupPlacement();
    // Layout flexível: registra os painéis (Comandos, Saída, ...), lê o layout.json (migrando o antigo outputPosition) e
    // monta o DockHost. Os widgets dos painéis são construídos uma vez em setupUi(); o host só os reparenta.
    void setupLayout();
    void registerPanelViews();
    void registerRunViews();
    void restoreLayoutFromDisk();
    // Painéis de UM comando: o espelho vivo de uma execução (`run:<id>`) e o comando KIP fixado (`kip:<id>`). Recebem log,
    // resposta HTTP, estado e sessão KIP do comando de origem, sem seguir a seleção da lista.
    QWidget *createMirrorPanel(const QString &commandId, bool kip);
    void forEachMirror(const QString &commandId, const std::function<void(RunMirrorPanel *)> &action);
    // Um comando aberto num painel próprio (`run:`/`kip:`) vive só ali: a Saída padrão não ganha guia nem se conecta
    // a ele. Fechar o painel devolve a guia à Saída. Exceção: o terminal interativo (PTY) só existe na Saída.
    bool livesInPanel(const QString &commandId) const;
    static bool panelOwnsOutput(const core::Command &command);
    // Pai dos diálogos que abrem ao rodar: uma janela flutuante fixada (sempre no topo) cobriria um diálogo filho da
    // janela principal; filho dela, o diálogo fica acima.
    QWidget *dialogParent();
    // O id do painel de um comando: um app KIP vira mini app (`kip:`, volta ao reiniciar); o resto, espelho (`run:`).
    QString panelIdForCommand(const QString &commandId) const;
    bool kipPanelAutoStarts(const QString &commandId);
    void setKipPanelAutoStart(const QString &commandId, bool on);
    // Ao iniciar: roda os apps KIP com painel e "Abrir com o Kai" ligado (inertes: esperam o usuário na tela inicial).
    void autoStartKipPanels();
    void revealCommandPanel(const QString &commandId);
    void moveOutOfOutput(const QString &commandId);
    // Faixa com os botões de execução (executar, parar, forçar, reiniciar, body) no cabeçalho da Saída de um comando que vive
    // fora da árvore — painel ou janela destacada, onde não há a árvore pra agir.
    ActionSidebar *makeCommandActionStrip(const QString &commandId);
    void refreshCommandActionStrips();
    void setMirrorStatus(const QString &commandId, ExecutionStatus status);
    ExecutionStatus executionStatusFor(const QString &commandId) const;
    QWidget *createDocumentPanel();
    bool isViewPlaced(const QString &id) const;
    void revealViewQuietly(const QString &id);
    void syncDocumentPlacement();
    // A Saída descobre pela zona onde está o eixo do colapso (altura embaixo, largura nas laterais, nenhum no miolo).
    void applyDrawerPosition();
    void showOutputPanel();
    void showDocumentPanel();
    void restoreDefaultLayout();
    void syncPanelBarHeights();
    // Com uma coluna de ações entre a lista e a Saída, o handle interno do splitter é desabilitado (só
    // redimensionaria a coluna); a borda da lista, do lado da Saída, passa a arrastar o divisor externo (dragTreeBorder).
    void updateTreeBorderResizeHandles();
    // Cantos da própria janela (sem borda nativa): raio do token radiusMd aplicado por máscara; reto, maximizado ou
    // tela cheia = sem máscara.
    void updateWindowShape();
    bool m_visibilityGuardArmed = false;
    bool m_hiddenExplicitly = false;
    int m_explicitShowDepth = 0;
    bool m_initialFocusDone = false; // o foco inicial na árvore vale só para a primeira exibição (ver showEvent)
    QElapsedTimer m_lastScreenChange;
    bool m_translucentWindow = false; // ver shouldUseTranslucentWindow (main-window.cpp)
    // Windows 11: cantos nativos do DWM aceitos (sem máscara nem raio no QSS) e o
    // último estilo de cantos aplicado (-1 = reaplicar).
    bool m_nativeCorners = false;
    bool m_nativeSnap = false; // Windows: as bordas e o arraste da barra são do sistema (ver native-window-snap.h)
    int m_nativeCornerStyle = -1;
    void setWelcomeMode(bool welcome);
    bool m_welcomeMode = false;
    QVBoxLayout *m_treeContainerLayout = nullptr;
    void applyFrameInsets();
    void dragTreeBorder(bool press, int globalX);
    int m_treeDragStartX = 0;
    int m_treeDragStartWidth = 0;

    // Reusados pelos atalhos de teclado configuráveis (setupActionShortcuts): a ação funciona mesmo com o grupo em
    // "não exibir".
    void triggerPlaySelected();
    void triggerStopSelected();
    void triggerForceStopSelected();
    void triggerResetSelected();
    void triggerEditSelected();
    void triggerEditBodySelected();
    void triggerDeleteSelected();
    void triggerEditCurrentFolder();
    void triggerExpandSelected();
    void triggerCollapseSelected();
    void triggerExpandAll();
    void triggerCollapseAll();
    void triggerToggleHiddenSelected();
    void triggerToggleShowHidden();
    // Filtro "exibir apenas em execução": só da sessão, não persiste.
    void triggerToggleRunningOnly();
    void showEyeEasterEgg();

    void quitApplication();
    void toggleOutputFocus();
    void ensureDefaultThemeInstalled();
    void loadConfig();
    void centerOnActiveScreen();
    void reloadCommandTree();
    // WelcomeScreen quando não há comandos nem pastas; chamado ao fim de reloadCommandTree().
    void updateWelcomeScreenVisibility();
    void applyActiveEnvironment();
    void promptParamsAndRun(core::Command command);
    // Roda o script de uma lista dinâmica (Parameter::optionsScript) e entrega o resultado ao `dialog`.
    void runOptionsScript(const core::Command &command, const QMap<QString, QString> &currentValues,
                          const QString &script, QPointer<ParameterFormDialog> dialog, const QString &paramName);
    void runCommandWithParams(const core::Command &command, const QMap<QString, QString> &paramValues);
    // Dispara os comandos marcados com autoRun ao iniciar o Kai (cada um após
    // seu delay em segundos), usando os valores da última execução.
    void scheduleAutoRunCommands();
    void persistCommands();
    void writeExportFile(const QString &path, const QString &content);
    void copyExportToClipboard(const QString &content);
    void importProjectFromDirectory(const QString &directory);
    void importProjectTreeFromDirectory(const QString &directory);
    struct TreeImportOutcome {
        int imported = 0;
        QStringList failures; // "pasta: motivo"
    };
    // Importa cada pasta de `directories` (pais antes dos filhos) como projeto. `rootProjectPath` é o PROJECT_PATH da
    // raiz; o das demais é ele + o caminho relativo. Um projeto dentro de outro (desta importação) entra dentro dele; os
    // demais em `parentFolderId` (vazio = raiz, ou a pasta do Kai que já contém o diretório, como na CLI).
    TreeImportOutcome importProjectTree(const QString &rootDirectory, const QStringList &directories,
                                        const QString &rootProjectPath, bool detectGeneric,
                                        const QString &parentFolderId);
    // Anexa um projeto já lido (ids únicos, grava, coleções, árvore) — o
    // mesmo fim do import pelo diálogo e pelo CLI. false se a gravação falhou. `newRootId` recebe o id final da pasta do projeto.
    bool mergeImportedProject(ProjectImportResult importResult, const QString &parentFolderId, QString *newRootId = nullptr,
                              bool reloadTree = true);

    // Ligações do pipeline que não dependem de qual comando roda (HTTP, "pulado",
    // variáveis dinâmicas persistidas): o principal e os das ações de pasta.
    void connectPipelineAuxSignals(engine::ExecutionPipeline *pipeline);
    void configureEnvForRun(core::EnvironmentManager &env, const core::Command &command,
                            const QMap<QString, QString> &paramValues);
    void prepareDrawerForRun(const core::Command &command);
    // AÇÕES DE PASTA (core/folder-actions.h): cada par (comando, pasta) é um comando VIRTUAL em m_commandsById, com id
    // próprio, então tudo indexado por id (logs, painel de saída, parar/forçar/resetar) funciona sem mudança. Cada
    // execução tem pipeline e ambiente próprios: o principal só aguenta uma cadeia por vez e as ações rodam em paralelo.
    struct ActionRun;
    void rebuildActionCommands();
    void startFolderAction(const core::Command &command, const QMap<QString, QString> &paramValues);
    void handleActionFinished(const QString &actionId, const engine::PipelineResult &result);
    void handleFolderActionActivated(const QString &commandId, const QString &folderId);
    // Documentação da pasta (README do diretório, ou o primeiro documento que houver) no painel Documento. `Reveal` (a pasta
    // foi selecionada) também traz o painel à frente; `Quiet` só atualiza o que ele mostra.
    enum class DocReveal { Reveal, Quiet };
    bool showFolderDocumentation(const QString &folderId, DocReveal reveal = DocReveal::Reveal);
    void followDocumentToCommand(const QString &commandId);
    void setCommandsCollapsed(bool collapsed);
    // Reflete o painel maximizado no DockHost (a Saída maximizada é o modo foco) no flag, no chevron e nas configurações.
    void syncCommandsCollapsed();
    void showQuickRun();
    void noteRecentCommand(const QString &commandId);
    QStringList m_recentCommandIds;
    bool m_recentSeeded = false;
    // Da bandeja, com o Kai oculto: a busca abre no meio da tela sob o cursor, sem trazer a janela.
    void showQuickRunFromTray();
    QuickRunPopup *preparedQuickRun();
    QuickRunPopup *m_quickRun = nullptr;
    // Ctrl+K: a mesma janelinha do "+", mas com comandos, pastas, notas, ambientes e ações.
    void showCommandPalette();
    void runPaletteAction(const QString &actionId);
    void revealInTree(const QString &itemId, const std::function<void()> &fallback);
    void showCommandInTree(const QString &commandId);
    QString folderPathOf(const QString &folderId) const;
    QuickRunPopup *m_palette = nullptr;
    void showFindPalette();
    void revealItemActions(const QString &itemId);
    QuickRunPopup *m_findPalette = nullptr; // Buscar item (Ctrl+F): inerte, leva ao item e às ações dele
    bool m_restoringUiState = false; // restaurando o recolhido da sessão anterior: não regrava
    void handleDocKaiLink(const QString &action, const QString &target);
    // Tela "caminho não encontrado" da documentação: acerta (automatic) ou abre o editor da pasta que define o diretório.
    void fixDocDirectory(bool automatic, const QString &directory, const QString &suggestion);
    QString m_docFolderId;
    QMap<QString, int> m_docUsage; // id da pasta -> vezes que a documentação dela foi aberta (doc-usage.json)
    QTimer *m_docUsageSaveTimer = nullptr;
    bool m_docUsageSaving = false;
    DocumentPanel *m_documentPanel = nullptr;
    // O painel Variáveis (a tela de ambientes embutida) e o aviso de que a gravação veio dele (não recarregar a lista).
    QPointer<EnvironmentManagerDialog> m_environmentsPanel;
    bool m_savingEnvironmentsFromPanel = false;
    bool m_documentWasPlaced = false;
    // Pasta de um comando selecionado que não tem documentação (não se confere o disco de novo a cada clique) e a "senha" do
    // pedido em andamento (uma seleção nova o invalida).
    QString m_docFollowMiss;
    quint64 m_docFollowToken = 0;
    struct DocSource {
        QString folderId;
        QString directory; // só quando a pasta tem diretório PRÓPRIO a verificar
        bool hasNotes = false;
    };
    void findDocSources(bool includeHidden, std::function<void(const QVector<DocSource> &)> done);
    void refreshDocSources();
    QString documentTargetFor(const QString &itemId) const;
    void showDocumentFor(const QString &itemId);
    bool canShowOutput(const QString &commandId) const;
    void updateFocusActions();
    QSet<QString> m_docSourceFolders; // fontes de documentação (refreshDocSources)
    bool m_smartFocus = false; // Configurações → Foco inteligente (desligado: a Saída e o Documento só vêm quando pedidos)
    bool m_inlineActions = false; // Configurações → Ações inline (botões do item na linha da árvore)
    // O item que já estava selecionado enquanto a árvore é reconstruída (um comando editado, pelo agente ou por um diálogo):
    // a árvore o seleciona de novo, e isso não é uma navegação, então nada passa à frente nem muda de painel.
    QString m_restoredSelectionId;
    quint64 m_docSourcesToken = 0;
    // A memória do leitor de documentos (posições, rascunhos, recentes) e as marcas de "não salvo" na árvore de comandos.
    DocSessionStore *m_docStore = nullptr;
    void refreshDraftMarks();
    void showFolderActionMenu(const QString &commandId, const QString &folderId, const QPoint &globalPos);
    engine::ProcessRunner *runnerFor(const QString &commandId) const;
    QStringList allPipelineRunningCommandIds() const;
    void handleProjectSyncPlan(const engine::SyncPlan &plan);
    void finishProjectSync(const QString &folderId);
    QString folderDisplayName(const QString &folderId) const;
    void importOpenApiFromFile(const QString &path);
    // Coleção Postman / exportação do Insomnia / HAR: vira uma pasta com as requisições (e os padrões HTTP).
    void importHttpCollectionFromFile(const QString &path, int kind);
    void importConfigFromFile(const QString &path);
    bool readConfigPackage(const QString &path, core::ConfigManager::ImportResult &result, QString &error,
                           bool *isPackage = nullptr) const;
    void applyConfigImport(const core::ConfigManager::ImportResult &result,
                           const ImportSelectionDialog::Selection &selection, QString &doneMessage);
    static bool cliImportSelection(const QStringList &only, ImportSelectionDialog::Selection &selection, QString &error);
    void reconnectTerminalToCommand(const QString &commandId);

    // --- Barra de guias das saídas (OutputTabsBar) ---
    // Uma guia por comando executado na sessão (a ação de pasta tem a sua, por pasta). Fechar só tira a guia: o
    // processo segue e o log fica guardado; clicar no comando de novo ou iniciar uma execução a traz de volta.
    void ensureOutputTab(const QString &commandId);
    void refreshOutputTabs();
    void activateOutputTab(const QString &commandId);
    void showCommandOutput(const QString &commandId);
    void closeOutputTab(const QString &commandId);
    void showOutputTabMenu(const QString &commandId, const QPoint &globalPos);
    void cycleOutputTab(int direction);
    bool hasOutputToShow(const QString &commandId) const;
    void clearOutputPanel();
    engine::ProcessRunner *runnerForCommandId(const QString &commandId) const;
    void handleCommandSelectionChanged(const QString &itemId, bool isFolder);
    void updateRunningCommandStatus();
    void appendToCommandLog(const QString &commandId, const QString &text);
    QStringList availableThemeNames() const;
    QStringList bundledThemeNames() const;
    QJsonObject themeDocument(const QString &name) const;
    bool saveThemeFromAgent(const QString &name, const QJsonObject &doc, QString &error);
    // Grava e aplica as configurações como o diálogo faz, sem diálogo (ferramentas de configuração da IA).
    bool applySettingsFromAgent(const core::SettingsData &settings, QString &error);
    QString resolveTargetFolderId() const;
    // Id único para pasta nova: nomes iguais geram o mesmo slug e ids repetidos colidem nos lookups (árvore, herança
    // de env_vars). Anexa sufixo numérico até achar um livre.
    QString uniqueFolderId(const QString &candidate) const;
    QString commitNewFolder(core::Folder folder, bool nested);
    // Ids da pasta raiz + todas as descendentes.
    QVector<QString> folderSubtreeIds(const QString &rootFolderId) const;
    // Coleções vinculadas a uma subárvore de pastas ou a um comando: guardadas ali (Collection::folderId) OU
    // referenciadas por um parâmetro Select do escopo (Parameter::collectionId), mesmo que vivam em outro lugar.
    QVector<core::Collection> linkedCollectionsForFolder(const QString &folderId) const;
    QVector<core::Collection> linkedCollectionsForCommand(const QString &commandId) const;
    QVector<core::Command> commandsInFolder(const QString &folderId) const;
    // Bloqueia nomes duplicados entre irmãos da mesma pasta (pastas diferentes podem repetir). `excludeId` ignora o
    // próprio item ao editar.
    bool hasDuplicateCommandName(const QString &folderId, const QString &name, const QString &excludeId) const;
    bool hasDuplicateCollectionName(const QString &folderId, const QString &name, const QString &excludeId) const;

    core::ConfigManager m_configManager;
    MainWindowAgentHost *m_agentHost = nullptr;
    McpBridge *m_mcpBridge = nullptr;
    QPointer<AiChatWindow> m_aiChat;
    bool m_aiConfigured = false;
    QVector<core::GlobalAction> m_globalActions;
    std::map<QString, std::shared_ptr<ActionRun>> m_actionRuns; // execuções de ação em andamento
    QSet<QString> m_actionsRan; // ações que já rodaram nesta sessão (têm saída guardada pra carregar)
    // Ações KIP: a sessão é filha do pipeline que a criou e a view mostra o estado final dela
    // depois do fim do processo, então o pipeline fica vivo até a próxima execução da ação.
    std::map<QString, QPointer<engine::ExecutionPipeline>> m_keptKipPipelines;
    engine::KipSession *kipSessionFor(const QString &commandId) const;
    engine::ProjectSyncManager *m_projectSync = nullptr;
    QSet<QString> m_projectSyncBusy;
    // Destaque de pasta dessincronizada: sem watcher, só confere (em segundo plano) ao abrir,
    // ao focar a janela, depois de salvar edições e ao fim de um sync.
    QTimer *m_syncDriftTimer = nullptr;
    void scheduleSyncDriftCheck();
    core::EnvironmentManager m_envManager;
    core::RunHistory m_runHistory;
    core::NotificationHistory m_notificationHistory;
    engine::CronScheduler m_cronScheduler;
    QDateTime m_runStartedAt; // início da execução atual (para o histórico)
    core::CommandsData m_commandsData;
    QMap<QString, core::Command> m_commandsById;
    QVector<core::Collection> m_collections;
    QVector<core::Note> m_notes;
    QSet<QString> m_failedCommandIds;
    QMap<QString, QString> m_backgroundProcessLogs;
    QMap<QString, QString> m_commandLogs;
    // "Abrir último link" em terminal interativo: o processo nunca termina com sucesso, então a URL é checada a cada
    // chunk de saída (handlePipelineLog) e a primeira abre uma vez por execução.
    QSet<QString> m_openedLastLinkForRun;
    // Comandos KIP cuja view deve abrir na janela própria quando a sessão nascer
    // (`kai -gw`, mesmo mecanismo do `kip_window`).
    QSet<QString> m_kipOpenWindowFor;
    // Apps KIP abertos sozinhos no boot ("Abrir com o Kai") cuja sessão ainda é essa: não vêm à frente nem pegam o foco.
    QSet<QString> m_kipAutoStarted;
    bool m_autoStartingKip = false;
    // Geração da sessão KIP por comando: o fechamento automático da janela (kip_auto_close)
    // só vale para a sessão que o agendou, não para uma re-execução que veio depois.
    QHash<QString, int> m_kipAutoCloseGeneration;
    // SettingsData::outputMaxLogSizeKb já em caracteres: appendToCommandLog roda a cada chunk de qualquer comando e
    // não deve ler a config do disco. Recalculado ao salvar Configurações.
    int m_outputMaxLogSizeChars = 1024 * 1024;
    // Último resultado HTTP por comando, reaplicado no reconnect (as abas Headers/JSON sumiam ao trocar de comando).
    QMap<QString, engine::HttpResult> m_lastHttpResult;
    // Requisições HTTP enviadas que ainda esperam resposta: quando começou e o limite (0 = sem limite).
    struct HttpWait {
        QDateTime startedAt;
        int timeoutSec = 0;
    };
    QHash<QString, HttpWait> m_httpWaits;
    // Comandos HTTP cuja última execução foi pulada por Execution Condition (conditionSkipBehavior == "success").
    // Um resultado HTTP real (httpResultReady) sempre remove a entrada.
    QMap<QString, QString> m_skippedReason;
    QString m_connectedTerminalCommandId;
    QStringList m_outputTabOrder;   // ids das guias, na ordem da barra
    // Fonte única de "este comando está rodando": background no ProcessManager, set de execuções do pipeline
    // (sobrevive ao gap entre hooks) e o runner real no registry.
    bool isCommandRunning(const QString &commandId) const;

    void applyAppearanceSettings();

    void applyWindowGeometryPreference();

    // Esconde a janela quando o foco vai para FORA do Kai, se a opção estiver ligada; não esconde se foi para um
    // diálogo ou janela do próprio app.
    void maybeAutoHideOnFocusLoss();
    bool m_autoHideOnFocusLoss = false;
    bool m_showHiddenCommands = false;
    bool m_showRunningOnly = false;

    // Comando de execução única ativo no pipeline (vazio se nenhum).
    QString m_activePipelineCommandId;
    // Se esse comando é HTTP (abre o JSON viewer com a resposta ao finalizar).
    bool m_activePipelineIsHttp = false;
    // Comandos com pipeline em andamento: marcados no disparo e limpos só no pipelineFinished. Sobrevive ao gap
    // entre hooks (sem runner ativo) e a execuções concorrentes.
    QSet<QString> m_pipelineRunningIds;

    engine::ExecutionPipeline *m_pipeline = nullptr;    engine::ProcessManager *m_processManager = nullptr;
    utils::ThemeManager *m_themeManager = nullptr;

    TopUtilityBar *m_topBar = nullptr;
    // Os 3 grupos de ações (Item/Exibição/Execução) são widgets soltos até applyActionGroupPlacement()
    // reparentá-los num dos containers abaixo (ou em nenhum, se "não exibir").
    ItemActionsBar *m_itemActionsBar{nullptr};
    StatusLine *m_statusLine{nullptr};
    ExpandCollapseBar *m_expandCollapseBar{nullptr};
    ActionSidebar *m_actionSidebar = nullptr;
    // Containers de posicionamento: upper (acima da árvore), bottom (abaixo), left (1º widget do m_horizontalSplitter)
    // e side (à direita; nome histórico). Hospedam 0-3 grupos (applyActionGroupPlacement()).
    ActionGroupContainer *m_upperActionsContainer{nullptr};
    ActionGroupContainer *m_bottomActionsContainer{nullptr};
    ActionGroupContainer *m_leftActionsContainer{nullptr};
    ActionGroupContainer *m_sideActionsContainer{nullptr};
    CommandTreeWidget *m_commandTree = nullptr;
    // Mostrada em vez da árvore e dos containers de ação quando não há comando nem pasta; vive no mesmo
    // treeContainer (updateWelcomeScreenVisibility()).
    class WelcomeScreen *m_welcomeScreen = nullptr;
    // Fechada manualmente enquanto o app segue vazio: suprime o auto-show até Ajuda -> "Tela de Boas-Vindas".
    bool m_welcomeScreenDismissed = false;
    // Overlay de loading genérico (import de projeto e outras ações demoradas).
    class LoadingOverlay *m_loadingOverlay = nullptr;
    TerminalDrawer *m_terminalDrawer = nullptr;
    QSplitter *m_horizontalSplitter = nullptr;
    // Widget "fixo" da área principal (busca + m_horizontalSplitter), construído uma única vez em setupUi() e entregue ao
    // DockHost como o painel "commands".
    QWidget *m_mainAreaWrapper = nullptr;
    // Widget de conteúdo (abaixo da title bar) que hospeda o DockHost e a tela de boas-vindas.
    QWidget *m_content = nullptr;
    layout::ViewRegistry *m_viewRegistry = nullptr;
    layout::DockHost *m_dock = nullptr;
    layout::LayoutStore *m_layoutStore = nullptr;
    layout::LayoutController *m_layoutController = nullptr;
    QVector<QPointer<ActionSidebar>> m_commandStrips; // as faixas de ação dos painéis/janelas de comando (propriedade kaiCommandId)
    QHash<QString, QPointer<RunMirrorPanel>> m_mirrors; // por id do painel (`run:<id>` / `kip:<id>`)
    // Modo foco: a lista de comandos some e a Saída (com as guias dos programas ativos) ocupa tudo — é o painel Saída
    // maximizado no DockHost. O chevron fica no começo da barra de guias.
    bool m_commandsCollapsed = false;
    // Execução em segundo plano de um comando OCULTO (cron, auto-run, `kai run`): não puxa a Saída, não a expande e não
    // cria guia. `m_quietRun` vale só durante a chamada que dispara; `m_quietRunIds` guarda os que estão rodando assim.
    bool m_quietRun = false;
    QSet<QString> m_quietRunIds;
    // Disparados pelo auto-run (boot) ou pelo agendamento (cron): rodam em silêncio, sem Saída e sem aparecer na barra de
    // status (contagem de rodando e último resultado). Uma execução manual do mesmo comando tira o id daqui.
    bool m_scheduledRun = false;
    QSet<QString> m_silentRunIds;
    void runInBackground(const QString &commandId, bool scheduled = false);
    // Um QVector cobre todas as ações (utils::actionShortcutSpecs); cada spec gera 0..N QShortcut (multi-binding),
    // todos recriados em setupActionShortcuts().
    QVector<QShortcut *> m_actionShortcuts;
    ProjectSelector *m_projectSelector = nullptr;

    QSystemTrayIcon *m_trayIcon = nullptr;
    std::unique_ptr<QHotkey> m_globalHotkey;
    ProcessListDialog *m_processListDialog = nullptr;

    // Borda da janela frameless sob o cursor (0 = interior); `kResizeMargin` é a largura da faixa sensível.
    Qt::Edges edgesAt(const QPoint &pos) const;
    static constexpr int kResizeMargin = 8;
    // Cursor de resize via QApplication::setOverrideCursor (global): guarda o estado para restaurar uma única vez.
    bool m_resizeCursorActive = false;
};

} // namespace kai::ui
