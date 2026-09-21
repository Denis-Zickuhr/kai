#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMap>
#include <QSet>
#include <QIcon>
#include <QDateTime>
#include <memory>

#include "core/config-manager.h"
#include "core/environment-manager.h"
#include "core/run-history.h"
#include "core/notification-history.h"
#include "core/models.h"
#include "engine/execution-pipeline.h"
#include "engine/process-manager.h"
#include "engine/cron-scheduler.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/shared/expand-collapse-bar.h"
#include "ui/shared/item-actions-bar.h"
#include "ui/shared/action-group-container.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/features/collections/project-selector.h"
#include "ui/features/collections/project-import-options-dialog.h"
#include "ui/shared/top-utility-bar.h"
#include "ui/shared/action-sidebar.h"
#include "ui/features/history/process-list-dialog.h"
#include "ui/features/output/log-viewer-dialog.h"
#include "utils/theme-manager.h"

class QHotkey;
class QSplitter;
class QShortcut;
class QStackedWidget;
class QTimer;

namespace kai::ui {

// Janela principal do Kai: usa a titlebar NATIVA do sistema operacional
// (decisão de escopo: sem frameless customizado), com bandeja
// do sistema, atalho global (QHotkey), Top Utility Bar (Settings/Debug/
// Help/Scope badge), busca fuzzy, navegação por abas e Terminal Drawer
// integrado ao ExecutionPipeline.
//
// Toda lógica de negócio (ConfigManager, EnvironmentManager,
// ExecutionPipeline) é acessada via composição e sinais/slots, nunca
// herança — mantendo o desacoplamento exigido pelo AGENTS.md.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // --- API para o IPC/CLI (kai run/list/env/show) ---
    // Executa um comando pelo NOME (case-insensitive). Retorna false + msg
    // se não encontrado/ambíguo. Não bloqueia (dispara a execução).
    bool runCommandByName(const QString &name, QString &message);
    // Nomes de todos os comandos (para `kai list`).
    QStringList commandNames() const;
    // Ativa um environment pelo NOME. Retorna false + msg se não achado.
    bool activateEnvironmentByName(const QString &name, QString &message);
    // Nomes dos environments + o nome do ativo (para `kai env list`).
    QStringList environmentNames(QString &activeName);
    // Exibe/traz a janela do Kai à frente (para `kai show`).
    void showAndRaise();

    // true se o ícone de bandeja foi registrado com sucesso. Usado por
    // outras decisões (notificações, "ocultar ao rodar" só faz sentido
    // com bandeja pra voltar) — NÃO decide mais o boot inicial (ver
    // globalHotkeyRegistered() abaixo).
    bool trayAvailable() const { return m_trayIcon != nullptr; }

    // true se o atalho global (QHotkey) foi registrado com sucesso —
    // setupGlobalHotkey() roda SÍNCRONO no construtor, então isto já
    // reflete o resultado real assim que a MainWindow termina de
    // construir. main.cpp usa isto (não trayAvailable()) para decidir o
    // boot: o Kai (app que inicia com o sistema) só sobe OCULTO se
    // conseguir registrar o atalho — é o único jeito de trazê-lo de volta
    // sem bandeja/clique. Bug relatado: "hoje quando liguei meu PC o Kai
    // aparece aberto" — a decisão antes olhava trayAvailable(), que pode
    // legitimamente estar disponível mas não ser o mecanismo real de
    // retorno (ex: ambiente sem bandeja de fato clicável) nem cobre o
    // caso em que o atalho falhou (aí sim faria sentido ficar visível).
    bool globalHotkeyRegistered() const { return static_cast<bool>(m_globalHotkey); }

    // Decisão FINAL de visibilidade no boot (usada por main.cpp em vez de
    // globalHotkeyRegistered() direto). "Iniciar visível" (settings.
    // startVisible, feedback do usuário) é BINÁRIA e nunca consulta o
    // atalho global: ligada = sempre visível; desligada = sempre oculto
    // (bandeja). Antes, "desligado" caía de volta num fallback condicionado
    // ao atalho (visível se o registro falhasse) — reintroduzia o mesmo bug
    // que a opção resolve ("abre sozinho" quando o atalho falha ao
    // registrar por motivo alheio ao usuário: WSL/WSLg, colisão com outro
    // app). Reabrir com a opção desligada continua possível via ícone de
    // bandeja ou `kai show` (CLI/IPC), que não dependem do atalho.
    bool shouldStartVisible();
    // Nomes de TODAS as variáveis dinâmicas capturadas, em qualquer escopo —
    // alimenta o autocomplete {{var}} do CommandEditorDialog (feedback do
    // usuário: "adicione ENVS temporárias na interpolação do autocomplete").
    QStringList availableDynamicVarNames() const;

    // --- API de processos para o IPC/CLI (kai ps/attach/kill) ---
    // Lista os processos em execução: linhas "nome | pid | status".
    QStringList runningProcessLines();
    // Atacha: traz a janela e conecta o terminal ao comando (por nome).
    bool attachProcessByName(const QString &name, QString &message);
    // Mata o processo do comando (por nome).
    bool killProcessByName(const QString &name, QString &message);

private:
    // Resolve um alvo de processo por PID (numérico) OU nome do comando,
    // dentre os processos EM EXECUÇÃO. Preenche commandId/commandName ou
    // error. Usado por attach/kill via CLI.
    bool resolveProcessTarget(const QString &arg, QString &commandId,
                              QString &commandName, QString &error);
public:

protected:
    // Auto-ocultar ao perder o foco (comportamento de launcher, configurável).
    void changeEvent(QEvent *event) override;


    void closeEvent(QCloseEvent *event) override;
    // Ao exibir a janela, move o foco para a árvore de comandos, para que
    // a navegação por seta (↑/↓) opere nos comandos imediatamente
    // (feedback do usuário: navegação por seta deve ser prioridade).
    void showEvent(QShowEvent *event) override;
    // Resize por borda na janela frameless: detecta o cursor próximo às
    // bordas/cantos e delega o redimensionamento nativo ao window manager
    // (QWindow::startSystemResize), o jeito robusto que funciona em X11/
    // WSLg sem cálculo manual de geometria.
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *e) override;
    // Filtro instalado no container central: intercepta movimento/clique
    // do mouse nas bordas para iniciar o resize nativo, já que o container
    // cobre a área da janela e receberia os eventos antes da MainWindow.
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void handleFilterQueryChanged(const QString &query);
    void handleCommandActivated(const QString &commandId);
    void handlePipelineLog(const QString &commandId, const QString &text, bool isError);
    void handlePipelineFinished(const engine::PipelineResult &result);
    // Menu único de importação/exportação (pedido do usuário: "só dois
    // botões... um jeito simplificado e mais fácil, porém completo, de
    // importar, com apenas um form" / "queria um menu unificado para
    // exportação, não 3") — ImportDialog/ExportDialog resolvem fonte/
    // tipo/escopo por dentro.
    void handleImportRequested();
    void handleExportRequested();
    void handleTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void handleThemeReloaded(const utils::ResolvedTheme &theme);
    void handleNewFolderRequested();
    void handleNewCommandRequested();
    void handleNewCollectionRequested();
    void handleEditRequested(const QString &commandId, bool isFolder);
    void handleDeleteRequested(const QString &commandId, bool isFolder);
    void handleDuplicateRequested(const QString &itemId, bool isFolder);
    // Handlers de coleção (item não-executável).
    void handleCollectionEditRequested(const QString &collectionId);
    void handleCollectionDuplicateRequested(const QString &collectionId);
    void handleCollectionDeleteRequested(const QString &collectionId);
    // Carrega e persiste collections.json.
    void persistCollections();
    // Remove uma pasta e, recursivamente, suas subpastas e comandos.
    // Não confirma nem persiste (o chamador decide).
    void deleteFolderRecursively(const QString &folderId);
    // Abre o FolderDeleteDialog (flags: apagar filhos por tipo) e executa a
    // exclusão seletiva da pasta `folderId`; o que não for apagado é
    // reparenteado para o avô. NÃO persiste (o chamador chama persistCommands).
    void deleteFolderWithDialog(const QString &folderId);
    void handleQuickEditBodyRequested(const QString &commandId);
    void handleSettingsRequested();
    // Config corrompida foi restaurada de backup (ConfigManager::
    // configRecovered) — hoje só logava; vira notificação opcional.
    void handleConfigRecovered(const QString &filePath, const QString &backupPath);
    // Environments (pacotes selecionáveis): trocar o ativo pelo seletor do
    // topo e abrir a tela de gestão (criar/editar/excluir).
    void handleEnvironmentSelected(const QString &environmentId);
    void handleManageEnvironmentsRequested();
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
    // Setting opt-in "notificar no primeiro ERROR da saída formatada" —
    // ver OutputPanel::firstErrorInFormattedOutput.
    void handleFirstErrorInFormattedOutput();

    void handleBackgroundProcessStarted(const QString &commandId, engine::ProcessRunner *runner);
    void handleBackgroundProcessOutput(const QString &commandId, const QString &text, bool isError);
    void handleBackgroundProcessStatusChanged(const QString &commandId, engine::ProcessStatus status);
    void handleShowProcessListRequested();
    // `force`: false = encerramento GRACIOSO (SIGTERM -> timeout configurável
    // -> SIGKILL, ver ProcessRunner::stop); true = encerramento IMEDIATO
    // (SIGKILL direto, sem esperar nada, ver ProcessRunner::forceStop) —
    // pedido do usuário: "Force" precisa ser um SIGKILL de verdade, distinto
    // do "Parar" gracioso (antes os dois chamavam exatamente o mesmo caminho).
    void handleKillCommandRequested(const QString &commandId, bool force = false);
    void handleResetCommandRequested(const QString &commandId);
    void handleTreeStructureChanged(const QVector<TreeNodePlacement> &placements);
    void toggleVisibility();
    // Controles da janela frameless (custom title bar).
    void handleMaximizeRestore();
    // Inicia o arraste nativo da janela (delegado ao window manager via
    // QWindow::startSystemMove) — chamado pela title bar ao pressionar.
    void handleWindowMoveRequested();

private:
    void setupUi();
    void setupToolbar();
    void setupTrayIcon();
    // Quais toggles de SettingsData::notifyOn* correspondem a cada evento
    // — mapeado internamente por maybeShowNotification, pra quem chama
    // não precisar carregar Configurações duas vezes (uma pro toggle,
    // outra dentro do método).
    enum class NotificationEvent { CommandFailure, BackgroundProcessCrash, BackgroundProcessSuccess, ConfigRecovered, FirstErrorInFormattedOutput };
    // Notificações (pedido do usuário): mostra um toast nativo da bandeja
    // se habilitado nas Configurações E o toggle daquele evento
    // específico estiver ligado, respeitando foco/bandeja (ver
    // notification-gate.h::shouldShowNotification — sem bandeja ou
    // master switch desligado, é no-op silencioso).
    void maybeShowNotification(NotificationEvent event, const QString &title, const QString &body,
                                QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Warning);
    QIcon loadAppIcon() const;
    // Resolve o ProcessRunner do comando CONECTADO ao Terminal Drawer no
    // momento — mesma lógica repetida em handleTerminalInputEntered/
    // Interrupt/Eof (execução única via pipeline OU processo background
    // rastreado pelo ProcessManager), extraída para os handlers do
    // terminal interativo (handleTerminalRawInput/SizeChanged) não
    // duplicá-la de novo. nullptr se não houver comando conectado ou o
    // runner já não existir.
    engine::ProcessRunner *connectedProcessRunner() const;
    void setupGlobalHotkey();
    void setupTheme();
    void setupActionShortcuts();
    void applyEditShortcutOnSelection();
    void applyDeleteShortcutOnSelection();
    // Reconstrói m_upperActionsContainer/m_bottomActionsContainer/
    // m_leftActionsContainer/m_sideActionsContainer conforme a preferência
    // de posicionamento de cada grupo (Item/Exibição/Execução) em
    // SettingsData — chamado no setup e ao salvar Configurações.
    void applyActionGroupPlacement();
    // Reparenta m_mainAreaWrapper (busca+árvore/actions) e m_terminalDrawer
    // no splitter externo certo, conforme SettingsData::outputPosition
    // ("bottom"/"left"/"right") — chamado no setup e ao salvar
    // Configurações. Reparenta os widgets JÁ CONSTRUÍDOS (não recria nada),
    // então conexões/estado do TerminalDrawer sobrevivem.
    void applyOutputPosition();

    // Handlers das ações de item/exibição/execução, extraídos das lambdas
    // dos sinais de clique para serem reusados também pelos atalhos de
    // teclado configuráveis (ver setupActionShortcuts/actionShortcutSpecs)
    // — a ação funciona pelo atalho mesmo se o grupo estiver com
    // posicionamento "não exibir".
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

    void quitApplication();
    void toggleSearchBar();
    // Foca/desfoca o painel de Saída (só quando há saída ativa): alterna o
    // foco entre o campo de entrada da Saída e a árvore de comandos.
    void toggleOutputFocus();
    void ensureDefaultThemeInstalled();
    void loadConfig();
    void centerOnActiveScreen();
    void reloadCommandTree();
    // Alterna entre a WelcomeScreen (0 comandos E 0 pastas — instalação
    // limpa/config apagada) e a árvore normal. Chamado ao fim de
    // reloadCommandTree() para reagir a qualquer criação/exclusão sem
    // precisar de um ponto de chamada dedicado por handler.
    void updateWelcomeScreenVisibility();
    // Aplica as vars do environment ativo no EnvironmentManager (escopo
    // global) e atualiza o seletor do topo.
    void applyActiveEnvironment();
    void refreshEnvironmentSelector();
    // Abre o formulário de parâmetros (se o comando tiver) e então executa.
    void promptParamsAndRun(const core::Command &command);
    void runCommandWithParams(const core::Command &command, const QMap<QString, QString> &paramValues);
    // Dispara os comandos marcados com autoRun ao iniciar o Kai (cada um após
    // seu delay em segundos), usando os valores da última execução.
    void scheduleAutoRunCommands();
    void persistCommands();
    void writeExportFile(const QString &path, const QString &content);
    // Corpo de cada fluxo de import, sem a etapa de escolher a fonte (já
    // resolvida pelo ImportDialog em handleImportRequested) — mesma lógica
    // de sempre, só sem o picker próprio.
    void importProjectFromDirectory(const QString &directory);
    void importOpenApiFromFile(const QString &path);
    void importConfigFromFile(const QString &path);
    void reconnectTerminalToCommand(const QString &commandId);
    // Resolve o ProcessRunner de UM comando específico por id — mesma
    // lógica repetida em handleTerminal{Interrupt,Eof,RawData,Resize}
    // (pipeline primeiro, depois ProcessManager).
    engine::ProcessRunner *runnerForCommandId(const QString &commandId) const;
    void handleCommandSelectionChanged(const QString &itemId, bool isFolder);
    void updateRunningCommandStatus();
    void appendToCommandLog(const QString &commandId, const QString &text);
    QStringList availableThemeNames() const;
    QString resolveTargetFolderId() const;
    // Garante ID ÚNICO pra uma pasta nova (bug relatado: 2 pastas com o
    // MESMO NOME geravam o MESMO id via FolderEditorDialog::
    // generateFolderId — puro slug do nome, sem desambiguação — colidindo
    // em qualquer lookup por id (árvore, chain de herança de env_vars,
    // etc.) e fazendo uma pasta "carregar" o conteúdo da outra). Mesmo
    // padrão já usado pra comandos (ver handleSaveOrUpdateCommand): se o
    // id já existe, anexa um sufixo numérico até achar um livre.
    QString uniqueFolderId(const QString &candidate) const;
    // Ids da pasta raiz + todas as descendentes (mesma varredura usada em
    // handleDuplicateFolderRequested) — reaproveitado pra achar coleções
    // VINCULADAS ao exportar uma pasta (ver handleExportRequested/
    // linkedCollectionsForFolder).
    QVector<QString> folderSubtreeIds(const QString &rootFolderId) const;
    // Coleções VINCULADAS a uma subárvore de pastas ou a um comando único —
    // usado pela tela de exportação (pedido do usuário: "to selecionando
    // uma pasta que tem coleções vinculadas, não estão dentro da PASTA,
    // mas não aparecem o botão"). Duas formas de vínculo, unidas: (1) a
    // coleção está fisicamente GUARDADA ali (Collection::folderId dentro da
    // subárvore/pasta do comando) - o caso original; (2) a coleção é
    // REFERENCIADA por algum parâmetro Select de um comando dentro do
    // escopo (Parameter::collectionId), mesmo que a coleção em si viva
    // fisicamente em outro lugar (ou em lugar nenhum) - o caso que estava
    // faltando, e é o mais comum na prática ("uso a coleção X num comando
    // desta pasta, quero versionar os dois juntos").
    QVector<core::Collection> linkedCollectionsForFolder(const QString &folderId) const;
    QVector<core::Collection> linkedCollectionsForCommand(const QString &commandId) const;
    QVector<core::Command> commandsInFolder(const QString &folderId) const;
    // Bloqueia nomes duplicados de Command/Collection DENTRO DA MESMA pasta
    // (feedback do usuário: pastas diferentes podem repetir nome livremente,
    // só irmãos na mesma pasta não). `excludeId` ignora o próprio item ao
    // editar (senão ele colidiria consigo mesmo).
    bool hasDuplicateCommandName(const QString &folderId, const QString &name, const QString &excludeId) const;
    bool hasDuplicateCollectionName(const QString &folderId, const QString &name, const QString &excludeId) const;

    core::ConfigManager m_configManager;
    core::EnvironmentManager m_envManager;
    core::RunHistory m_runHistory;
    core::NotificationHistory m_notificationHistory;
    engine::CronScheduler m_cronScheduler; // Etapa 4: agendador CRON
    QDateTime m_runStartedAt; // início da execução atual (para o histórico)
    core::CommandsData m_commandsData;
    QMap<QString, core::Command> m_commandsById;
    // Coleções (item não-executável na árvore), persistidas em
    // collections.json separado.
    QVector<core::Collection> m_collections;
    QSet<QString> m_failedCommandIds;
    QMap<QString, QString> m_backgroundProcessLogs;
    QMap<QString, QString> m_commandLogs;
    // "Abrir último link" (Command::openLastLink) para comandos de TERMINAL
    // INTERATIVO (achado real: "a regra de auto clicar link impresso por
    // ultimo não funciona nele") — um servidor de dev interativo (`npm
    // start`, etc.) imprime a URL uma vez no início e continua rodando
    // indefinidamente; a checagem normal (ver handlePipelineFinished) só
    // dispara quando o pipeline TERMINA com sucesso, o que nunca acontece
    // pra um processo que o usuário para manualmente. Pra terminal
    // interativo, checa a cada chunk de saída (ver handlePipelineLog) e
    // abre a PRIMEIRA URL vista, uma vez por execução (marcado aqui).
    QSet<QString> m_openedLastLinkForRun;
    // Cache de SettingsData::outputMaxLogSizeKb (em CARACTERES, já
    // convertido) — appendToCommandLog roda a cada chunk de saída de
    // QUALQUER comando, então lê daqui em vez de m_configManager.loadSettings()
    // (I/O de disco) toda vez. Recalculado no construtor e sempre que
    // Configurações é salvo com um valor novo (ver handleSettingsRequested).
    int m_outputMaxLogSizeChars = 1024 * 1024;
    // Último resultado HTTP estruturado POR comando: as abas Headers/JSON
    // vinham só do resultado ao vivo (httpResultReady) e sumiam ao trocar de
    // aba/comando e voltar (relatado). Guardamos aqui para REAPLICAR no
    // reconnect, mantendo Headers/JSON ao reselecionar um comando HTTP.
    QMap<QString, engine::HttpResult> m_lastHttpResult;
    // Comandos HTTP cuja ÚLTIMA execução foi pulada por Execution Condition
    // (conditionSkipBehavior == "success") — ver ExecutionPipeline::
    // commandSkippedByCondition. ausência = última execução real (ou nunca
    // rodou). Um resultado HTTP de verdade (httpResultReady) sempre remove
    // a entrada — um resultado novo nunca pode conviver com o marcador de
    // pulo de uma rodada anterior.
    QMap<QString, QString> m_skippedReason;
    QString m_connectedTerminalCommandId;
    // Id do comando de EXECUÇÃO ÚNICA atualmente rodando no ExecutionPipeline
    // (não background, portanto NÃO rastreado pelo m_processManager). Usado
    // para marcá-lo como "rodando" na árvore, permitir matá-lo e reconectar
    // o stdin ao trocar de seleção e voltar (bugs reportados). Vazio quando
    // não há execução única ativa.
    // FONTE ÚNICA DE VERDADE de "este comando está rodando".
    // Considera: (1) background no ProcessManager, (2) o Set de execuções do
    // pipeline (sobrevive ao gap entre hooks) e (3) o runner REAL do comando
    // no registry. Antes cada ponto da UI calculava isso do seu jeito, e o
    // painel lateral usava o slot único m_activePipelineCommandId — que o 2º
    // comando sobrescrevia, fazendo as ações "sumirem" ao voltar ao 1º.
    bool isCommandRunning(const QString &commandId) const;

    // Traduz as preferências de aparência (densidade, cantos, efeitos) para
    // os design tokens e re-aplica a folha de estilo. Chamado no boot e ao
    // salvar o Settings, para a troca valer sem reiniciar o app.
    void applyAppearanceSettings();

    // Aplica a preferência de geometria da janela (tamanho fixo, maximizada,
    // tela cheia ou lembrar o último tamanho).
    void applyWindowGeometryPreference();

    // Esconde a janela quando o foco vai para FORA do Kai, se a opção estiver
    // ligada. Tem guardas para não esconder quando o foco foi para um diálogo
    // ou janela do PRÓPRIO app.
    void maybeAutoHideOnFocusLoss();
    // Auto-ocultar ao perder o foco (comportamento de launcher) — ver
    // maybeAutoHideOnFocusLoss().
    bool m_autoHideOnFocusLoss = false;
    // "Mostrar ocultos" (toggle da barra de Exibição) — espelha
    // SettingsData::showHiddenCommands, aplicado em m_commandTree.
    bool m_showHiddenCommands = false;

    QString m_activePipelineCommandId;
    // Se o comando de execução única ativo é HTTP (para abrir o JSON viewer
    // com a resposta ao finalizar).
    bool m_activePipelineIsHttp = false;
    // Comandos cuja EXECUÇÃO (pipeline) está em andamento — marcados no
    // disparo e limpos SÓ no pipelineFinished. Fonte de verdade estável do
    // indicador "rodando": sobrevive à troca de estágios/hooks (o
    // activeProcessRunner fica null no gap entre um hook e o próximo,
    // apagando a bolinha — bug reportado) e a execuções concorrentes (o 2º
    // comando não some o ícone do 1º — bug reportado).
    QSet<QString> m_pipelineRunningIds;

    engine::ExecutionPipeline *m_pipeline = nullptr;    engine::ProcessManager *m_processManager = nullptr;
    utils::ThemeManager *m_themeManager = nullptr;

    TopUtilityBar *m_topBar = nullptr;
    FuzzySearchBar *m_searchBar = nullptr;
    // Os 3 grupos de ações (Item/Exibição/Execução — pedido do usuário: cada
    // um posicionável independentemente via Settings). São widgets "soltos"
    // até applyActionGroupPlacement() reparentá-los num dos dois
    // containers abaixo (ou em nenhum, se "não exibir").
    ItemActionsBar *m_itemActionsBar{nullptr};
    ExpandCollapseBar *m_expandCollapseBar{nullptr};
    ActionSidebar *m_actionSidebar = nullptr;
    // Containers genéricos de posicionamento: upper (horizontal, acima da
    // árvore), bottom (horizontal, abaixo da árvore), left (vertical, 1º
    // widget do m_horizontalSplitter) e side (vertical, ao lado direito —
    // nome histórico, ver applyActionGroupPlacement()). Hospedam 0-3 dos
    // grupos acima, decidido em applyActionGroupPlacement().
    ActionGroupContainer *m_upperActionsContainer{nullptr};
    ActionGroupContainer *m_bottomActionsContainer{nullptr};
    ActionGroupContainer *m_leftActionsContainer{nullptr};
    ActionGroupContainer *m_sideActionsContainer{nullptr};
    CommandTreeWidget *m_commandTree = nullptr;
    // Tela de boas-vindas (estilo VSCode Welcome tab), mostrada em vez de
    // m_commandTree/m_upperActionsContainer/m_bottomActionsContainer
    // quando o app não tem NENHUM comando nem pasta — ver
    // updateWelcomeScreenVisibility(). Vive dentro do mesmo treeContainer
    // (ver setupUi), então continua respeitando o splitter/redimensionamento
    // existente sem widgets extras.
    class WelcomeScreen *m_welcomeScreen = nullptr;
    // "x" da tela de boas-vindas fechada manualmente enquanto o app
    // continua vazio — suprime o auto-show até Ajuda -> "Tela de
    // Boas-Vindas" (handleShowWelcomeRequested) resetar. Ver
    // updateWelcomeScreenVisibility().
    bool m_welcomeScreenDismissed = false;
    // Overlay de loading genérico da janela principal (import de projeto e
    // outras ações demoradas — feedback do usuário: "tudo que carrega").
    class LoadingOverlay *m_loadingOverlay = nullptr;
    TerminalDrawer *m_terminalDrawer = nullptr;
    QSplitter *m_horizontalSplitter = nullptr;
    // Widget "fixo" da área principal (busca + m_horizontalSplitter),
    // construído uma única vez em setupUi(); applyOutputPosition() decide
    // em qual splitter externo ele entra (m_outerSplitter, junto com
    // m_terminalDrawer), sem recriar nada — ver applyOutputPosition().
    QWidget *m_mainAreaWrapper = nullptr;
    // Widget de conteúdo (abaixo da title bar) cujo QVBoxLayout hospeda o
    // m_outerSplitter — applyOutputPosition() troca o splitter externo
    // dentro deste layout ao mudar SettingsData::outputPosition.
    QWidget *m_content = nullptr;
    // Splitter externo ATUAL (recriado por applyOutputPosition() a cada
    // mudança de posição): Vertical [m_mainAreaWrapper, m_terminalDrawer]
    // para "bottom" (comportamento padrão/histórico), ou Horizontal
    // [m_terminalDrawer, m_mainAreaWrapper] / [m_mainAreaWrapper,
    // m_terminalDrawer] para "left"/"right".
    QSplitter *m_outerSplitter = nullptr;
    // Debounce do salvamento do tamanho do m_outerSplitter (pedido do
    // usuário: "o que eu salvei redimensionando fica") — reinicia a cada
    // QSplitter::splitterMoved (drag do mouse), só persiste settings.json
    // quando o usuário PARA de arrastar. Ver applyOutputPosition().
    QTimer *m_outputSplitterSaveTimer = nullptr;
    // Shortcuts Manager v2 (ver utils::actionShortcutSpecs): UM QVector
    // cobre TODAS as ações agora (antes eram ~12 ponteiros nomeados +
    // este vetor separado pras data-driven) — cada spec pode gerar 0..N
    // QShortcut (multi-binding), todos recriados junto em
    // setupActionShortcuts().
    QVector<QShortcut *> m_actionShortcuts;
    ProjectSelector *m_projectSelector = nullptr;

    QSystemTrayIcon *m_trayIcon = nullptr;
    std::unique_ptr<QHotkey> m_globalHotkey;
    ProcessListDialog *m_processListDialog = nullptr;
    LogViewerDialog *m_logViewerDialog = nullptr;

    // Estado do arraste da janela frameless (custom title bar).
    // Detecção de borda para resize da janela frameless: retorna as
    // bordas (Qt::Edges) sob a posição do cursor, ou 0 se estiver no
    // interior. `kResizeMargin` é a largura da faixa sensível nas bordas.
    Qt::Edges edgesAt(const QPoint &pos) const;
    static constexpr int kResizeMargin = 8;
    // Cursor de resize ativo via QApplication::setOverrideCursor (global, não
    // herda por widget). Guardamos o estado para restaurar exatamente uma vez
    // ao sair da faixa de borda.
    bool m_resizeCursorActive = false;
};

} // namespace kai::ui
