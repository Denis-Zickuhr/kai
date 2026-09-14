#include "ui/main-window.h"
#include "ui/shared/notification-gate.h"
#include "ui/shared/expand-collapse-bar.h"
#include "ui/app-stylesheet.h"
#include "ui/shared/dialog-utils.h"
#include "utils/action-shortcuts.h"
#include "ui/features/collections/export-dialog.h"
#include "ui/features/collections/import-dialog.h"
#include "ui/features/collections/import-selection-dialog.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QShortcut>
#include <QWidget>
#include <QCloseEvent>
#include <QShowEvent>
#include <QTimer>
#include <QSet>
#include <QScreen>
#include <QGuiApplication>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QFileDialog>
#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QPixmap>
#include <QInputDialog>
#include <QUuid>
#include <QCoreApplication>
#include <QWindow>
#include <QMouseEvent>
#include "qhotkey.h"

#include "ui/features/command-editor/parameter-form-dialog.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "ui/features/collections/folder-delete-dialog.h"
#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/collections/collection-editor-dialog.h"
#include "ui/shared/name-uniqueness.h"
#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/loading-overlay.h"
#include "ui/features/command-editor/command-json-editor-dialog.h"
#include "ui/features/settings/settings-dialog.h"
#include "ui/features/environments/environment-manager-dialog.h"
#include "ui/features/history/run-history-dialog.h"
#include "ui/features/history/notification-history-dialog.h"
#include "ui/shared/help-dialog.h"
#include "core/openapi-parser.h"
#include "core/yaml-bridge.h"
#include "ui/shared/quick-body-editor-dialog.h"
#include "ui/shared/welcome-screen.h"
#include "utils/asset-paths.h"
#include "utils/logger.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "utils/autostart-manager.h"

#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <functional>

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";

#if defined(Q_OS_WIN)
// Restaurar/focar a janela a partir de um HOTKEY GLOBAL (processo em
// segundo plano) é um caso clássico onde SetForegroundWindow do Win32
// FALHA SILENCIOSAMENTE — o Windows só deixa o processo que já tem o
// foreground roubar o foco de outro; um processo em background (que é
// exatamente o caso de um hotkey global dele mesmo) não tem essa
// permissão por padrão. show()/showNormal()/raise()/activateWindow() do
// Qt fazem a chamada Win32 "certa" por baixo dos panos, mas ela é
// silenciosamente ignorada pelo SO — a janela DESMINIMIZA (o estado
// muda) mas não vem pra frente/não recebe foco de teclado (bug
// reportado: "não funciona, tem certeza?" — sim, existe mesmo, é uma
// limitação conhecida do Win32, não um bug de lógica no toggleVisibility
// em si). O truque padrão (usado por launchers tipo PowerToys Run/
// Wox): anexar temporariamente a thread de INPUT à do processo que
// atualmente tem o foreground (AttachThreadInput) — enquanto anexado, o
// Windows trata as duas threads como uma só pra fins de permissão de
// foreground, então SetForegroundWindow funciona de verdade.
void forceForegroundOnWindows(QWidget *window)
{
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }
    if (::IsIconic(hwnd)) {
        ::ShowWindow(hwnd, SW_RESTORE);
    }
    const HWND foregroundHwnd = ::GetForegroundWindow();
    const DWORD foregroundThreadId = foregroundHwnd
        ? ::GetWindowThreadProcessId(foregroundHwnd, nullptr) : 0;
    const DWORD currentThreadId = ::GetCurrentThreadId();
    const bool attached = foregroundThreadId != 0 && foregroundThreadId != currentThreadId
        && ::AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);
    ::SetForegroundWindow(hwnd);
    ::BringWindowToTop(hwnd);
    if (attached) {
        ::AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
    }
}
#endif
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_pipeline(new engine::ExecutionPipeline(this))
    , m_processManager(new engine::ProcessManager(this))
    , m_themeManager(new utils::ThemeManager(this))
{
    // Carrega o idioma da interface (language pack i18n) ANTES de
    // construir a UI, para que todos os textos já venham traduzidos na
    // primeira renderização. Fallback en -> própria chave é garantido
    // pelo TranslationManager, então um settings.json sem idioma ou com
    // idioma inválido não quebra nada.
    const core::SettingsData initialSettings = m_configManager.loadSettings();
    utils::TranslationManager::instance().loadLanguage(initialSettings.language);
    m_outputMaxLogSizeChars = qMax(1, initialSettings.outputMaxLogSizeKb) * 1024;

    setupUi();
    applyOutputPosition();
    setupToolbar();
    setupTrayIcon();
    setupGlobalHotkey();
    setupTheme();
    applyAppearanceSettings();
    applyActionGroupPlacement();

    // AUTOSTART: reconcilia o estado do SO com a preferência a CADA boot.
    // Antes o registro só era escrito quando a opção MUDAVA no diálogo, então:
    // (a) se settings.json já vinha com autostart=true, nada era gravado e o
    // Kai nunca subia com o sistema; (b) reinstalar em outra pasta deixava a
    // entrada apontando para o executável antigo. O sync corrige os dois casos
    // (inclui conferência do CAMINHO), e é idempotente quando já está certo.
    {
        const core::SettingsData bootSettings = m_configManager.loadSettings();
        if (!utils::AutostartManager::sync(bootSettings.autostart,
                                           QCoreApplication::applicationFilePath())) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Não foi possível reconciliar o autostart neste sistema."));
        }
    }
    setupActionShortcuts();
    loadConfig();

    connect(m_pipeline, &engine::ExecutionPipeline::logMessage, this, &MainWindow::handlePipelineLog);
    connect(m_pipeline, &engine::ExecutionPipeline::pipelineFinished, this, &MainWindow::handlePipelineFinished);
    connect(m_pipeline, &engine::ExecutionPipeline::backgroundProcessStarted, this, &MainWindow::handleBackgroundProcessStarted);
    // Atualiza o indicador de "rodando" quando um estágio começa (bug
    // reportado no Windows: o ícone de execução não aparecia — o status
    // era calculado só ANTES do processo existir; ao reagir ao início do
    // estágio, o activeProcessRunner já existe e o ícone é pintado).
    connect(m_pipeline, &engine::ExecutionPipeline::stageStarted, this,
            [this](const QString &, engine::PipelineStage) { updateRunningCommandStatus(); });
    connect(m_processManager, &engine::ProcessManager::outputReady, this, &MainWindow::handleBackgroundProcessOutput);
    connect(m_processManager, &engine::ProcessManager::statusChanged, this, &MainWindow::handleBackgroundProcessStatusChanged);
    // Config corrompida restaurada de backup: sinal já existia, mas não
    // tinha NENHUM listener (achado silencioso) — vira notificação opcional.
    connect(&m_configManager, &core::ConfigManager::configRecovered, this, &MainWindow::handleConfigRecovered);

    // Garante encerramento seguro de processos em background ao fechar o
    // app ("processos zumbis").
    // Fechamento do app: ativa o modo de encerramento RÁPIDO antes do
    // stopAll — o dtor dos ProcessRunner deixa de fazer waits bloqueantes.
    // Sem isto, fechar o Kai com N processos em background congelava a GUI
    // por N x (1-3s) somados (achado de auditoria: ~9s com 3 processos).
    connect(qApp, &QCoreApplication::aboutToQuit, this, []() {
        engine::ProcessRunner::setFastShutdown(true);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, m_processManager, &engine::ProcessManager::stopAll);

    // NÃO centralizamos a janela no construtor (bug real reportado: "a
    // janela não aparece ao abrir" no WSL/Wayland). Chamar move() antes
    // do primeiro show(), com a geometria da janela ainda não finalizada
    // e num setup multi-monitor do WSLg (coordenadas globais deslocadas/
    // com DPI inconsistente — mesmo bug de escala já documentado na spec
    // 07), podia posicionar a janela fora de qualquer área visível.
    // Deixamos o window manager posicionar naturalmente no primeiro
    // show(); a centralização defensiva só ocorre ao reexibir via
    // toggleVisibility (quando a janela já foi mostrada uma vez).
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    // Janela sem borda, estilo CopyQ (feedback do usuário: janela
    // sem borda nativa, levemente arredondada, com botões modernos de
    // minimizar/maximizar/fechar na custom title bar = TopUtilityBar).
    // Qt::FramelessWindowHint remove a decoração nativa do SO; o
    // arredondamento é feito por um container central com border-radius
    // aplicado via QSS do tema (objectName "rootContainer"). O drag e o
    // maximizar/restaurar são tratados manualmente (handlers de janela).
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    // TAMANHO DA JANELA CONFIGURÁVEL (pedido do usuário): antes era 1280x760
    // cravado. O modo vem das configurações — tamanho fixo, maximizada, tela
    // cheia ou lembrar o último tamanho usado.
    setMinimumSize(720, 480);
    applyWindowGeometryPreference();
    setWindowTitle(QStringLiteral("Kai"));
    setWindowIcon(loadAppIcon());

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("rootContainer"));
    // Mouse tracking para o resize por borda funcionar (o cursor muda ao
    // se aproximar das bordas mesmo sem botão pressionado). O container
    // deixa uma pequena margem nas bordas (kResizeMargin) onde os eventos
    // de mouse chegam à MainWindow para iniciar o resize nativo.
    setMouseTracking(true);
    central->setMouseTracking(true);
    // Filtro de eventos GLOBAL (no qApp) para o resize por borda: os widgets
    // filhos (árvore, saída, etc.) cobrem as bordas da janela e consumiam os
    // eventos de mouse antes de chegarem à MainWindow — por isso o resize
    // "parecia travado" (relatado). Um filtro global vê o evento primeiro e
    // só age quando o cursor está na FAIXA DE BORDA (kResizeMargin) da
    // janela; no interior, ignora e deixa o clique seguir normal.
    qApp->installEventFilter(this);
    auto *layout = new QVBoxLayout(central);
    // A top bar é a barra de título (frameless) e deve encostar no topo,
    // sem margem nem espaçamento ao redor — senão sobra um vão vertical
    // abaixo dela (bug reportado). O padding do app fica no CONTEÚDO abaixo.
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_topBar = new TopUtilityBar(central);
    connect(m_topBar, &TopUtilityBar::importRequested, this, &MainWindow::handleImportRequested);
    connect(m_topBar, &TopUtilityBar::exportRequested, this, &MainWindow::handleExportRequested);
    connect(m_topBar, &TopUtilityBar::logsRequested, this, &MainWindow::handleLogsRequested);
    connect(m_topBar, &TopUtilityBar::newFolderRequested, this, &MainWindow::handleNewFolderRequested);
    connect(m_topBar, &TopUtilityBar::newCommandRequested, this, &MainWindow::handleNewCommandRequested);
    connect(m_topBar, &TopUtilityBar::newCollectionRequested, this, &MainWindow::handleNewCollectionRequested);
    connect(m_topBar, &TopUtilityBar::settingsRequested, this, &MainWindow::handleSettingsRequested);
    connect(m_topBar, &TopUtilityBar::environmentSelected, this, &MainWindow::handleEnvironmentSelected);
    connect(m_topBar, &TopUtilityBar::manageEnvironmentsRequested, this, &MainWindow::handleManageEnvironmentsRequested);
    connect(m_topBar, &TopUtilityBar::showProcessListRequested, this, &MainWindow::handleShowProcessListRequested);
    connect(m_topBar, &TopUtilityBar::runHistoryRequested, this, &MainWindow::handleRunHistoryRequested);
    connect(m_topBar, &TopUtilityBar::notificationHistoryRequested, this, &MainWindow::handleNotificationHistoryRequested);
    connect(m_topBar, &TopUtilityBar::helpRequested, this, &MainWindow::handleHelpRequested);
    connect(m_topBar, &TopUtilityBar::showWelcomeRequested, this, &MainWindow::handleShowWelcomeRequested);
    connect(m_topBar, &TopUtilityBar::hideRequested, this, &MainWindow::toggleVisibility);
    connect(m_topBar, &TopUtilityBar::quitAppRequested, this, &MainWindow::quitApplication);
    connect(m_topBar, &TopUtilityBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_topBar, &TopUtilityBar::maximizeRestoreRequested, this, &MainWindow::handleMaximizeRestore);
    connect(m_topBar, &TopUtilityBar::closeRequested, this, &MainWindow::close);
    connect(m_topBar, &TopUtilityBar::moveRequested, this, &MainWindow::handleWindowMoveRequested);

    m_searchBar = new FuzzySearchBar(central);
    connect(m_searchBar, &FuzzySearchBar::queryChanged, this, &MainWindow::handleFilterQueryChanged);
    // Busca oculta por padrão (feedback do usuário): aparece e
    // autofoca ao pressionar o atalho de toggle (Ctrl+F por padrão,
    // remapeável), controlado por setupActionShortcuts/toggleSearchBar.
    m_searchBar->setVisible(false);

    m_commandTree = new CommandTreeWidget(central);
    {
        // Plano de fundo da aba de comandos (imagem + opacidade), se salvo.
        const core::SettingsData bg = m_configManager.loadSettings();
        m_commandTree->setBackground(bg.commandsBackgroundImage, bg.commandsBackgroundOpacity);
    }
    connect(m_commandTree, &CommandTreeWidget::commandActivated, this, &MainWindow::handleCommandActivated);
    connect(m_commandTree, &CommandTreeWidget::editRequested, this, &MainWindow::handleEditRequested);
    connect(m_commandTree, &CommandTreeWidget::deleteRequested, this, &MainWindow::handleDeleteRequested);
    // "Ocultar/Mostrar pasta" no menu de contexto da ABA (pasta raiz) —
    // pedido do usuário: "adicione a possibilidade de ocultar pastas de
    // raiz". Direto pelo id (não passa por currentSelectionId(), que só
    // enxerga o item selecionado DENTRO da árvore, nunca a aba em si).
    connect(m_commandTree, &CommandTreeWidget::toggleFolderHiddenRequested, this,
        [this](const QString &folderId) {
            for (core::Folder &f : m_commandsData.folders) {
                if (f.id == folderId) {
                    f.hidden = !f.hidden;
                    break;
                }
            }
            m_configManager.saveCommands(m_commandsData);
            reloadCommandTree();
        });
    connect(m_commandTree, &CommandTreeWidget::duplicateRequested, this, &MainWindow::handleDuplicateRequested);
    connect(m_commandTree, &CommandTreeWidget::quickEditBodyRequested, this, &MainWindow::handleQuickEditBodyRequested);
    connect(m_commandTree, &CommandTreeWidget::collectionEditRequested, this, &MainWindow::handleCollectionEditRequested);
    connect(m_commandTree, &CommandTreeWidget::collectionDuplicateRequested, this, &MainWindow::handleCollectionDuplicateRequested);
    connect(m_commandTree, &CommandTreeWidget::collectionDeleteRequested, this, &MainWindow::handleCollectionDeleteRequested);
    connect(m_commandTree, &CommandTreeWidget::newFolderRequested, this, &MainWindow::handleNewFolderRequested);
    connect(m_commandTree, &CommandTreeWidget::newCommandRequested, this, &MainWindow::handleNewCommandRequested);
    connect(m_commandTree, &CommandTreeWidget::newCollectionRequested, this, &MainWindow::handleNewCollectionRequested);
    connect(m_commandTree, &CommandTreeWidget::playRequested, this, &MainWindow::handleCommandActivated);
    connect(m_commandTree, &CommandTreeWidget::killRequested, this, &MainWindow::handleKillCommandRequested);
    connect(m_commandTree, &CommandTreeWidget::forceStopRequested, this, &MainWindow::handleKillCommandRequested);
    connect(m_commandTree, &CommandTreeWidget::resetRequested, this, &MainWindow::handleResetCommandRequested);
    connect(m_commandTree, &CommandTreeWidget::structureChanged, this, &MainWindow::handleTreeStructureChanged);

    // Os 3 grupos de ações (instanciados aqui; reparentados mais abaixo,
    // via applyActionGroupPlacement(), nos containers onde ficam
    // visualmente ancorados — upper bar acima da árvore ou side bar ao
    // lado, conforme a preferência de posicionamento de cada grupo).
    m_itemActionsBar = new ItemActionsBar(central);
    connect(m_itemActionsBar, &ItemActionsBar::editCurrentFolderRequested, this, &MainWindow::triggerEditCurrentFolder);
    connect(m_itemActionsBar, &ItemActionsBar::newFolderRequested, this, &MainWindow::handleNewFolderRequested);
    connect(m_itemActionsBar, &ItemActionsBar::newCommandRequested, this, &MainWindow::handleNewCommandRequested);
    connect(m_itemActionsBar, &ItemActionsBar::newCollectionRequested, this, &MainWindow::handleNewCollectionRequested);
    connect(m_itemActionsBar, &ItemActionsBar::editSelectedRequested, this, &MainWindow::triggerEditSelected);
    connect(m_itemActionsBar, &ItemActionsBar::deleteSelectedRequested, this, &MainWindow::triggerDeleteSelected);

    m_expandCollapseBar = new ExpandCollapseBar(central);
    connect(m_expandCollapseBar, &ExpandCollapseBar::expandSelectedRequested, this, &MainWindow::triggerExpandSelected);
    connect(m_expandCollapseBar, &ExpandCollapseBar::collapseSelectedRequested, this, &MainWindow::triggerCollapseSelected);
    connect(m_expandCollapseBar, &ExpandCollapseBar::expandAllRequested, this, &MainWindow::triggerExpandAll);
    connect(m_expandCollapseBar, &ExpandCollapseBar::collapseAllRequested, this, &MainWindow::triggerCollapseAll);
    connect(m_expandCollapseBar, &ExpandCollapseBar::toggleHiddenSelectedRequested, this, &MainWindow::triggerToggleHiddenSelected);
    connect(m_expandCollapseBar, &ExpandCollapseBar::toggleShowHiddenRequested, this, &MainWindow::triggerToggleShowHidden);

    connect(m_commandTree, &CommandTreeWidget::tabsReordered, this, [this](const QVector<TreeNodePlacement> &placements) {
        // Persiste a nova ordem das abas SEM reconstruir a árvore (evita
        // recriar as abas no meio do reorder — corrige o travamento no
        // 2º arraste). Atualiza só o campo order das pastas em memória.
        bool changed = false;
        for (const TreeNodePlacement &p : placements) {
            for (core::Folder &folder : m_commandsData.folders) {
                if (folder.id == p.id && folder.order != p.order) {
                    folder.order = p.order;
                    changed = true;
                }
            }
        }
        if (changed) {
            m_configManager.saveCommands(m_commandsData); // sem reloadCommandTree
        }
    });
    connect(m_searchBar, &FuzzySearchBar::navigateToListRequested, m_commandTree,
        static_cast<void (CommandTreeWidget::*)()>(&CommandTreeWidget::focusFirstVisibleItem));
    connect(m_commandTree, &CommandTreeWidget::selectionChanged, this,
        [this](const QString &itemId, bool isFolder) {
            handleCommandSelectionChanged(itemId, isFolder);
        });

    m_actionSidebar = new ActionSidebar(central);
    connect(m_actionSidebar, &ActionSidebar::playSelectedRequested, this, &MainWindow::triggerPlaySelected);
    connect(m_actionSidebar, &ActionSidebar::stopSelectedRequested, this, &MainWindow::triggerStopSelected);
    connect(m_actionSidebar, &ActionSidebar::forceStopSelectedRequested, this, &MainWindow::triggerForceStopSelected);
    connect(m_actionSidebar, &ActionSidebar::resetSelectedRequested, this, &MainWindow::triggerResetSelected);
    connect(m_actionSidebar, &ActionSidebar::editBodySelectedRequested, this, &MainWindow::triggerEditBodySelected);

    m_terminalDrawer = new TerminalDrawer(central);
    connect(m_terminalDrawer, &TerminalDrawer::commandEntered, this, &MainWindow::handleTerminalInputEntered);
    connect(m_terminalDrawer, &TerminalDrawer::interruptRequested, this, &MainWindow::handleTerminalInterrupt);
    connect(m_terminalDrawer, &TerminalDrawer::eofRequested, this, &MainWindow::handleTerminalEof);
    connect(m_terminalDrawer, &TerminalDrawer::rawTerminalInput, this, &MainWindow::handleTerminalRawInput);
    connect(m_terminalDrawer, &TerminalDrawer::terminalSizeChanged, this, &MainWindow::handleTerminalSizeChanged);
    connect(m_terminalDrawer, &TerminalDrawer::firstErrorInFormattedOutput, this, &MainWindow::handleFirstErrorInFormattedOutput);
    // Opções de exibição da Saída: persistência GLOBAL em settings.json
    // (feedback do usuário: as opções do menu da Saída devem valer entre
    // sessões, para todos os comandos — não mais por comando). Ao alternar
    // qualquer opção no menu, grava todas em settings.json.
    connect(m_terminalDrawer, &TerminalDrawer::viewOptionsChanged, this,
            [this](const OutputPanel::ViewOptions &o) {
        core::SettingsData settings = m_configManager.loadSettings();
        settings.outputLineNumbers = o.lineNumbers;
        settings.outputWrap = o.wrapLines;
        settings.outputTimestamps = o.timestamps;
        settings.outputAutoScroll = o.autoScroll;
        settings.outputCompact = o.compact;
        settings.outputFontSize = o.fontPointSize;
        m_configManager.saveSettings(settings);
    });
    // Aplica na init as opções de exibição salvas globalmente (feedback do
    // usuário: as opções do menu da Saída persistem entre sessões).
    {
        const core::SettingsData s = m_configManager.loadSettings();
        OutputPanel::ViewOptions o;
        o.lineNumbers = s.outputLineNumbers;
        o.wrapLines = s.outputWrap;
        o.timestamps = s.outputTimestamps;
        o.autoScroll = s.outputAutoScroll;
        o.compact = s.outputCompact;
        o.fontPointSize = s.outputFontSize;
        m_terminalDrawer->setViewOptions(o);
    }
    // SAÍDA V2: resultado HTTP estruturado alimenta as abas Headers/JSON/Raw.
    // A janela destacada recebe o mesmo quando é do comando de origem.
    connect(m_pipeline, &engine::ExecutionPipeline::httpResultReady, this,
            [this](const QString &commandId, const engine::HttpResult &result) {
        // Guarda o resultado estruturado para reaplicar ao reselecionar o
        // comando (Headers/JSON não somem ao trocar de aba/comando).
        m_lastHttpResult.insert(commandId, result);
        // Um resultado de verdade chegou: a marca de "pulado" de uma
        // rodada anterior não vale mais (ver m_skippedReason).
        m_skippedReason.remove(commandId);
        if (m_connectedTerminalCommandId == commandId) {
            m_terminalDrawer->setSkipped(false);
            m_terminalDrawer->setHttpResult(result);
        }
        if (m_terminalDrawer->hasDetachedWindow()
            && m_terminalDrawer->detachedCommandId() == commandId) {
            m_terminalDrawer->setDetachedHttpResult(result);
        }
    });
    // Comando HTTP pulado por Execution Condition (ver
    // ExecutionPipeline::commandSkippedByCondition) — badge âmbar "Pulado"
    // + painel "Requisição não executada" em vez do resultado em cache de
    // uma execução anterior (bug relatado: parecia sucesso de verdade).
    connect(m_pipeline, &engine::ExecutionPipeline::commandSkippedByCondition, this,
            [this](const QString &commandId, const QString &reasonLabel) {
        m_skippedReason.insert(commandId, reasonLabel);
        if (m_connectedTerminalCommandId == commandId) {
            m_terminalDrawer->setSkipped(true, reasonLabel);
            m_terminalDrawer->setExecutionStatus(ExecutionStatus::Skipped);
        }
    });
    // EnvExtractor::persist == true (feedback do usuário: refresh token/API
    // key não deveria exigir reautenticar a cada boot) — grava write-through
    // em dynamic-vars.json. Arquivo pequeno e evento raro (só extractors
    // marcados persist), então regravar tudo a cada chamada é seguro/simples.
    connect(m_pipeline, &engine::ExecutionPipeline::dynamicVarPersistRequested, this,
            [this](const QString &scopeKey, const QString &name, const QString &value) {
        QMap<QString, QMap<QString, QString>> all = m_configManager.loadPersistedDynamicVars();
        all[scopeKey][name] = value;
        if (!m_configManager.savePersistedDynamicVars(all)) {
            utils::Logger::warning(kLogTag, QStringLiteral("Falha ao persistir variável dinâmica '%1'.").arg(name));
        }
    });
    connect(m_terminalDrawer, &TerminalDrawer::closeRequested, this, &MainWindow::handleTerminalCloseRequested);
    // Persiste o estado colapsado do terminal entre sessões
    // (feedback do usuário: lembrar se o terminal estava colapsado). Ao
    // alternar, grava terminal_collapsed em settings.json.
    connect(m_terminalDrawer, &TerminalDrawer::expandedChanged, this,
        [this](bool expanded) {
            core::SettingsData settings = m_configManager.loadSettings();
            settings.terminalCollapsed = !expanded;
            m_configManager.saveSettings(settings);
        });
    // Painel de Saída sempre visível (feedback do usuário: a
    // caixa da Saída deve sempre estar visível, nunca escondida do
    // layout). Escondê-la dinamicamente via setVisible(false) dentro do
    // QSplitter causava um bug real de sobreposição: o QSplitter não lida
    // bem com filhos escondidos/mostrados repetidamente, e ao tornar o
    // painel visível de novo o header por vezes desenhava sobre o
    // conteúdo antes do layout recalcular. Manter sempre visível elimina
    // a causa raiz. O botão "Fechar" do cabeçalho agora só limpa/reseta o
    // estado para Idle (handleTerminalCloseRequested), sem esconder o
    // widget.

    // Splitters (iteração focada em responsividade): o usuário
    // pode arrastar a divisória entre a árvore de comandos e a
    // ActionSidebar, e entre a área de navegação e o painel de Saída,
    // redimensionando cada componente conforme sua preferência — em vez
    // de proporções fixas via stretch factor de QHBoxLayout/QVBoxLayout.
    // "Redimensionar componentes, não rows de tabela": a altura de linha
    // das tabelas editáveis continua fixa via
    // QHeaderView::setDefaultSectionSize, deliberadamente fora do escopo
    // desta mudança.
    // m_upperActionsContainer fica empilhado acima da árvore de comandos,
    // dentro de um pequeno container próprio — assim ele viaja junto com o
    // painel esquerdo do splitter horizontal, em vez de ocupar sua própria
    // linha no topo da janela. Qual(is) dos 3 grupos entra(m) nele (e no
    // m_sideActionsContainer, ao lado) é decidido em
    // applyActionGroupPlacement() — aqui só os dois containers são
    // instanciados/ancorados; ambos começam vazios/escondidos.
    // 4 containers genéricos de posicionamento (pedido do usuário: cada
    // grupo de ações posicionável em Topo/Embaixo/Esquerda/Direita/Oculto).
    // upper (horizontal, acima da árvore), bottom (horizontal, abaixo da
    // árvore — ambos dentro da coluna da árvore/treeContainer), left
    // (vertical, 1º widget do m_horizontalSplitter) e side (vertical, ao
    // lado direito — nome histórico "side" preservado por compat de
    // settings.json já persistidos, ver applyActionGroupPlacement()).
    m_upperActionsContainer = new ActionGroupContainer(Qt::Horizontal, central);
    m_bottomActionsContainer = new ActionGroupContainer(Qt::Horizontal, central);
    m_leftActionsContainer = new ActionGroupContainer(Qt::Vertical, central);
    m_sideActionsContainer = new ActionGroupContainer(Qt::Vertical, central);

    auto *treeContainer = new QWidget(central);
    auto *treeContainerLayout = new QVBoxLayout(treeContainer);
    treeContainerLayout->setContentsMargins(0, 0, 0, 0);
    treeContainerLayout->setSpacing(4);
    treeContainerLayout->addWidget(m_upperActionsContainer);
    treeContainerLayout->addWidget(m_commandTree, 1);
    treeContainerLayout->addWidget(m_bottomActionsContainer);

    // WelcomeScreen (estilo VSCode Welcome tab — pedido do usuário: "Ao
    // bootar o kai sem nenhum comando, ele dar instruções básicas..."):
    // mesmo container da árvore, ESCONDIDA por padrão — só aparece quando
    // updateWelcomeScreenVisibility() detecta 0 comandos E 0 pastas (ver
    // reloadCommandTree()). Troca de visibilidade, sem recriar nada, no
    // mesmo espírito de applyOutputPosition() para widgets já construídos.
    m_welcomeScreen = new WelcomeScreen(treeContainer);
    m_welcomeScreen->setVisible(false);
    connect(m_welcomeScreen, &WelcomeScreen::newFolderRequested, this, &MainWindow::handleNewFolderRequested);
    connect(m_welcomeScreen, &WelcomeScreen::newCommandRequested, this, &MainWindow::handleNewCommandRequested);
    connect(m_welcomeScreen, &WelcomeScreen::closeRequested, this, &MainWindow::handleWelcomeScreenClosed);
    treeContainerLayout->addWidget(m_welcomeScreen, 1);

    m_horizontalSplitter = new QSplitter(Qt::Horizontal, central);
    m_horizontalSplitter->setChildrenCollapsible(false);
    m_horizontalSplitter->addWidget(m_leftActionsContainer);
    m_horizontalSplitter->addWidget(treeContainer);
    m_horizontalSplitter->addWidget(m_sideActionsContainer);
    m_horizontalSplitter->setStretchFactor(0, 0);
    m_horizontalSplitter->setStretchFactor(1, 1);
    m_horizontalSplitter->setStretchFactor(2, 0);

    // Wrapper que agrupa a busca + a área de navegação (árvore/actions),
    // construído UMA vez aqui — applyOutputPosition() decide depois em
    // qual splitter (vertical, para "bottom"; horizontal, para "left"/
    // "right") ele entra junto com m_terminalDrawer, sem recriar nada.
    m_mainAreaWrapper = new QWidget(central);
    auto *mainAreaLayout = new QVBoxLayout(m_mainAreaWrapper);
    mainAreaLayout->setContentsMargins(0, 0, 0, 0);
    mainAreaLayout->setSpacing(12);
    mainAreaLayout->addWidget(m_searchBar);
    mainAreaLayout->addWidget(m_horizontalSplitter, 1);

    layout->addWidget(m_topBar);

    // Conteúdo abaixo da title bar, com o padding do app (o outer layout
    // ficou com margem 0 para a top bar encostar no topo).
    m_content = new QWidget(central);
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(16, 8, 16, 16);
    contentLayout->setSpacing(12);
    // O splitter externo (m_outerSplitter: Vertical para "bottom" ou
    // Horizontal para "left"/"right") é criado/montado em
    // applyOutputPosition(), chamado logo após setupUi() — aqui só
    // reservamos o layout que vai recebê-lo.

    layout->addWidget(m_content, 1);

    setCentralWidget(central);

    // Overlay de loading genérico sobre a janela toda (feedback do usuário).
    m_loadingOverlay = new LoadingOverlay(this);

    m_projectSelector = new ProjectSelector(this);

    // AUTO-RUN (feedback do usuário): agenda os comandos marcados para rodar
    // ao iniciar o Kai, cada um após o seu delay (em segundos). Um atraso base
    // pequeno garante que a janela, a árvore e as conexões de sinais já estejam
    // prontas antes do primeiro disparo (senão o comando rodava "invisível",
    // sem log — bug reportado).
    QTimer::singleShot(300, this, &MainWindow::scheduleAutoRunCommands);

    // Nenhum QSS hardcoded aqui: o visual é 100% controlado pelo
    // ThemeManager (setStyleSheet aplicado em handleThemeReloaded), evitando
    // que estilos fixos sobrescrevam o tema ativo/live reload.
}

void MainWindow::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Bandeja do sistema não disponível neste ambiente. "
                            "Kai continuará funcionando sem ícone de bandeja."));
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(loadAppIcon());
    m_trayIcon->setToolTip(utils::tr(QStringLiteral("mainwindow.tray.tooltip")));

    auto *menu = new QMenu();

    auto *toggleAction = menu->addAction(utils::tr(QStringLiteral("tray.toggle")));
    connect(toggleAction, &QAction::triggered, this, &MainWindow::toggleVisibility);

    auto *importAction = menu->addAction(utils::tr(QStringLiteral("tray.import")));
    connect(importAction, &QAction::triggered, this, &MainWindow::handleImportRequested);

    menu->addSeparator();

    auto *quitAction = menu->addAction(utils::tr(QStringLiteral("tray.quit")));
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_trayIcon->setContextMenu(menu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::handleTrayIconActivated);

    m_trayIcon->show();
}

void MainWindow::maybeShowNotification(NotificationEvent event, const QString &title, const QString &body,
                                        QSystemTrayIcon::MessageIcon icon)
{
    // Carrega as Configurações na hora (mesmo padrão já usado em dezenas
    // de outros pontos deste arquivo — não existe um SettingsData
    // cacheado hoje, e este caminho só roda em eventos raros/pontuais,
    // não em loop quente).
    const core::SettingsData settings = m_configManager.loadSettings();
    bool eventToggleOn = false;
    QString eventKey;
    switch (event) {
    case NotificationEvent::CommandFailure:
        eventToggleOn = settings.notifyOnCommandFailure;
        eventKey = QStringLiteral("command_failure");
        break;
    case NotificationEvent::BackgroundProcessCrash:
        eventToggleOn = settings.notifyOnBackgroundProcessCrash;
        eventKey = QStringLiteral("background_crash");
        break;
    case NotificationEvent::BackgroundProcessSuccess:
        eventToggleOn = settings.notifyOnBackgroundProcessSuccess;
        eventKey = QStringLiteral("background_success");
        break;
    case NotificationEvent::ConfigRecovered:
        eventToggleOn = settings.notifyOnConfigRecovered;
        eventKey = QStringLiteral("config_recovered");
        break;
    case NotificationEvent::FirstErrorInFormattedOutput:
        eventToggleOn = settings.notifyOnFirstErrorInFormattedOutput;
        eventKey = QStringLiteral("first_error_in_formatted_output");
        break;
    }

    // Registra no histórico persistido SEMPRE que o evento ocorre — mesmo
    // que o toast não vá aparecer (notificações desligadas ou janela em
    // foco) — pedido do usuário: "queria melhorar o processamento de
    // notificações, talvez salvar elas". O toggle de notificações só
    // decide o TOAST, não o log.
    core::NotificationRecord record;
    record.eventKey = eventKey;
    record.title = title;
    record.body = body;
    m_notificationHistory.append(record);

    if (!shouldShowNotification(trayAvailable(), settings.notificationsEnabled,
            eventToggleOn, settings.notifyEvenWhenFocused, isActiveWindow())) {
        return;
    }
    m_trayIcon->showMessage(title, body, icon, 6000);
}

void MainWindow::handleConfigRecovered(const QString &filePath, const QString &backupPath)
{
    Q_UNUSED(backupPath);
    maybeShowNotification(NotificationEvent::ConfigRecovered,
        utils::tr(QStringLiteral("notification.config_recovered.title")),
        utils::tr(QStringLiteral("notification.config_recovered.body")).arg(QFileInfo(filePath).fileName()));
}

void MainWindow::setupToolbar()
{
    // Bug real corrigido: esta QToolBar nativa do
    // QMainWindow (via addToolBar) duplicava as ações já presentes no menu
    // "☰ Menu" da TopUtilityBar, criando duas barras superiores visíveis
    // simultaneamente (relatado pelo usuário com captura de tela real).
    // Removida por completo; Importar Projeto e Processos foram
    // incorporados ao menu único da TopUtilityBar.
    m_processListDialog = new ProcessListDialog(m_processManager, this);
    // Alimenta a lista com os processos de FOREGROUND (registry do pipeline),
    // além dos de background do ProcessManager. Sem isso, quem roda comandos
    // normais via um diálogo sempre vazio.
    // Nomes de exibição de TODOS os comandos: o provedor de foreground entrega
    // apenas ids, e sem o nome a lista mostraria o id cru.
    for (const core::Command &c : m_commandsData.commands) {
        m_processListDialog->setCommandName(c.id, c.name);
    }
    m_processListDialog->setForegroundProvider([this]() {
        QList<QPair<QString, qint64>> running;
        for (const QString &id : m_pipeline->runningCommandIds()) {
            qint64 pid = 0;
            if (auto *r = m_pipeline->runnerFor(id)) {
                pid = r->processId();
            }
            running.append({id, pid});
        }
        return running;
    });
    m_processListDialog->applyThemeVariables(m_themeManager->currentTheme().variables);
}

void MainWindow::setupGlobalHotkey()
{
    // QHotkey (v1.5.0) só tem backend nativo para X11, Win32 e macOS/Carbon.
    // Sob Wayland (incluindo Xwayland/WSLg, onde QGuiApplication::platformName()
    // retorna "wayland"), a biblioteca não tem suporte e sua construção pode
    // segfaultar ao tentar acessar estruturas nativas inexistentes. Também
    // pulamos em "offscreen"/"minimal" (ambientes headless/CI).
    const QString platformName = QGuiApplication::platformName();
    static const QSet<QString> unsupportedPlatforms = {
        QStringLiteral("offscreen"), QStringLiteral("minimal"), QStringLiteral("wayland"),
    };

    if (unsupportedPlatforms.contains(platformName)) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Atalho global desabilitado na plataforma '%1' (sem suporte no QHotkey). "
                            "Kai continuará funcionando normalmente; use o ícone de bandeja ou a "
                            "janela diretamente.")
                .arg(platformName));
        return;
    }

    const core::SettingsData settings = m_configManager.loadSettings();

    if (settings.globalHotkey.trimmed().isEmpty()) {
        utils::Logger::info(kLogTag, QStringLiteral("Atalho global desabilitado (settings.json vazio)."));
        return;
    }

    const QKeySequence sequence(settings.globalHotkey);
    if (sequence.isEmpty()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Atalho global '%1' inválido; nenhum atalho será registrado.")
                .arg(settings.globalHotkey));
        return;
    }

    // autoRegister=false: construímos o objeto sem registrar automaticamente
    // no construtor, para que qualquer falha do backend nativo ocorra numa
    // chamada explícita que podemos, no mínimo, isolar e logar — em vez de
    // potencialmente abortar dentro da construção do objeto.
    m_globalHotkey = std::make_unique<QHotkey>(sequence, false);
    connect(m_globalHotkey.get(), &QHotkey::activated, this, &MainWindow::toggleVisibility);

    const bool registered = m_globalHotkey->setRegistered(true);

    if (!registered) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Não foi possível registrar o atalho global '%1' neste ambiente "
                            "(backend nativo indisponível ou combinação já em uso). "
                            "Kai continuará funcionando normalmente sem o atalho global.")
                .arg(settings.globalHotkey));
        m_globalHotkey.reset();
    } else {
        utils::Logger::info(kLogTag, QStringLiteral("Atalho global '%1' registrado.").arg(settings.globalHotkey));
    }
}

bool MainWindow::shouldStartVisible()
{
    // Binário (feedback do usuário): NÃO cai de volta pro fallback
    // condicionado ao atalho quando desligado — isso reintroduziria o
    // mesmo bug ("abre sozinho" quando o atalho falha ao registrar por um
    // motivo alheio ao usuário). Ligado = SEMPRE visível; desligado =
    // SEMPRE oculto (bandeja), independente do atalho registrar ou não.
    // Reabrir com o atalho falho ainda é possível via ícone de bandeja ou
    // `kai show` (CLI/IPC), que não dependem do atalho global.
    return m_configManager.loadSettings().startVisible;
}

QIcon MainWindow::loadAppIcon() const
{
    // Logo oficial do Kai (assets/logo/kai.png), resolvido com a mesma
    // estratégia de path relativo usada para o tema Dracula padrão: busca
    // relativo ao diretório do executável instalado e, como fallback,
    // relativo ao diretório de trabalho atual (builds locais).
    const QString logoDir = utils::assetDir(QStringLiteral("logo"));
    const QStringList candidatePaths = logoDir.isEmpty()
        ? QStringList{QStringLiteral("assets/logo/kai.png")}
        : QStringList{QDir(logoDir).filePath(QStringLiteral("kai.png"))};

    for (const QString &candidate : candidatePaths) {
        if (QFile::exists(candidate)) {
            // O logo de origem é grande (ex: 2302x1856). Usá-lo cru como
            // ícone de janela estoura o limite do protocolo X11/XCB
            // ("Size ... exceeds maximum xcb request length"), e o ícone é
            // ignorado. Montamos um QIcon multi-resolução com tamanhos de
            // ícone padrão (16..256), escalados com suavização e mantendo
            // proporção — leve, nítido em qualquer DPI e sempre dentro do
            // limite do request.
            const QPixmap source(candidate);
            if (source.isNull()) {
                continue;
            }
            QIcon icon;
            for (int size : {16, 24, 32, 48, 64, 128, 256}) {
                icon.addPixmap(source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
            return icon;
        }
    }

    utils::Logger::warning(kLogTag, QStringLiteral("Logo 'assets/logo/kai.png' não encontrado; usando ícone padrão."));
    return QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
}

void MainWindow::ensureDefaultThemeInstalled()
{
    const QString themesDir = m_themeManager->themesDirPath();
    QDir().mkpath(themesDir);

    // Resolve o diretório de assets/themes relativo ao executável (ou ao
    // diretório de trabalho em builds locais).
    QString assetsThemesDir;
    const QString resolved = utils::assetDir(QStringLiteral("themes"));
    const QStringList candidateDirs = resolved.isEmpty()
        ? QStringList{QStringLiteral("assets/themes")}
        : QStringList{resolved};
    for (const QString &dir : candidateDirs) {
        if (QDir(dir).exists()) {
            assetsThemesDir = dir;
            break;
        }
    }
    if (assetsThemesDir.isEmpty()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Diretório de temas padrão não encontrado; nenhum tema pré-instalado."));
        return;
    }

    // Instala/atualiza TODOS os temas de assets/themes (dracula, light e
    // quaisquer outros — spec 05: "criar mais temas"). Para cada asset:
    // se ainda não instalado, copia; se já instalado com schema_version
    // menor, reinstala com os valores atualizados. Nunca sobrescreve um
    // tema com nome que não exista nos assets (customizado pelo usuário).
    const QFileInfoList assetThemes = QDir(assetsThemesDir).entryInfoList(
        {QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo &asset : assetThemes) {
        const QString installedPath = QDir(themesDir).filePath(asset.fileName());

        auto schemaVersionOf = [](const QString &path) -> int {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                return -1;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            f.close();
            return doc.object().value(QStringLiteral("schema_version")).toInt(0);
        };

        const int assetVersion = schemaVersionOf(asset.absoluteFilePath());

        if (!QFile::exists(installedPath)) {
            if (QFile::copy(asset.absoluteFilePath(), installedPath)) {
                utils::Logger::info(kLogTag,
                    QStringLiteral("Tema '%1' instalado em '%2'.").arg(asset.baseName(), installedPath));
            }
            continue;
        }

        const int installedVersion = schemaVersionOf(installedPath);
        if (assetVersion > installedVersion) {
            QFile::remove(installedPath);
            if (QFile::copy(asset.absoluteFilePath(), installedPath)) {
                utils::Logger::info(kLogTag,
                    QStringLiteral("Tema '%1' atualizado (schema v%2 -> v%3) em '%4'.")
                        .arg(asset.baseName()).arg(installedVersion).arg(assetVersion).arg(installedPath));
            }
        }
    }
}

void MainWindow::setupTheme()
{
    connect(m_themeManager, &utils::ThemeManager::themeReloaded, this, &MainWindow::handleThemeReloaded);
    connect(m_themeManager, &utils::ThemeManager::themeLoadFailed, this,
        [](const QString &filePath, const QString &errorMessage) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Falha ao carregar tema '%1': %2 (tema anterior mantido).").arg(filePath, errorMessage));
        });

    ensureDefaultThemeInstalled();

    const core::SettingsData settings = m_configManager.loadSettings();
    const QString themeName = settings.activeTheme.isEmpty() ? QStringLiteral("dracula") : settings.activeTheme;

    if (!m_themeManager->loadTheme(themeName)) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Tema '%1' não pôde ser carregado; mantendo estilo padrão embutido.").arg(themeName));
    }
}

void MainWindow::setupActionShortcuts()
{
    // Shortcuts Manager v2: UMA tabela (utils::actionShortcutSpecs) + UM
    // mapa id->handler cobrem TODAS as ações agora — antes eram ~12
    // QShortcut nomeados construídos um a um AQUI (cada um precisando de
    // um membro próprio) mais um loop if/else separado pras ações
    // data-driven. Toda ação nova só precisa de UMA linha na tabela + UMA
    // entrada neste mapa — não mais um novo campo em SettingsData/
    // settings-dialog/aqui a cada atalho.
    const core::SettingsData settings = m_configManager.loadSettings();

    qDeleteAll(m_actionShortcuts);
    m_actionShortcuts.clear();

    // id -> handler. Os que exigiam Qt::WidgetWithChildrenShortcut na
    // árvore (navegação de abas, editar/excluir item, menu de contexto)
    // continuam assim via spec.scope — não é mais uma escolha ad hoc por
    // atalho, é um campo da spec (ver utils::actionShortcutSpecs).
    const QMap<QString, std::function<void()>> handlers = {
        {QStringLiteral("action.next_tab"), [this]() { m_commandTree->selectNextTab(); }},
        {QStringLiteral("action.previous_tab"), [this]() { m_commandTree->selectPreviousTab(); }},
        {QStringLiteral("action.edit_item"), [this]() { applyEditShortcutOnSelection(); }},
        {QStringLiteral("action.delete_item"), [this]() { applyDeleteShortcutOnSelection(); }},
        {QStringLiteral("action.new_folder"), [this]() { handleNewFolderRequested(); }},
        {QStringLiteral("action.new_command"), [this]() { handleNewCommandRequested(); }},
        // Fechar Aplicativo: encerra o app de fato (diferente do "X" da
        // janela, que apenas oculta — spec 09/closeEvent).
        {QStringLiteral("action.quit_app"), [this]() { quitApplication(); }},
        // Pedido do usuário: se a Saída (Resposta/JSON ou texto simples) já
        // está em foco, o mesmo atalho ativa/foca a busca DELA em vez de
        // abrir a busca da árvore de comandos — só cai no comportamento
        // antigo se a Saída não é a coisa em foco ou não tem busca própria
        // pertinente ali (terminal interativo, painel "não executado"...).
        {QStringLiteral("action.toggle_search"), [this]() {
            if (m_terminalDrawer && m_terminalDrawer->isVisible()
                && m_terminalDrawer->isAncestorOf(QApplication::focusWidget())
                && m_terminalDrawer->focusSearch()) {
                return;
            }
            toggleSearchBar();
        }},
        {QStringLiteral("action.context_menu"), [this]() { m_commandTree->openContextMenuForCurrent(); }},
        {QStringLiteral("action.focus_output"), [this]() { toggleOutputFocus(); }},
        // NOTA: "action.toggle_edit_mode" propositalmente NÃO tem handler
        // aqui — os próprios diálogos que usam esse atalho
        // (dialog-utils.h/json-editor-dialog.cpp) constroem seu QShortcut
        // sozinhos lendo a sequência via utils::firstShortcutFor; nenhum
        // handler GLOBAL faz sentido pra essa ação, já que ela só existe
        // DENTRO de um diálogo aberto. A spec continua na tabela só pra
        // aparecer na Shortcuts Manager v2 (o loop abaixo pula specs sem
        // handler registrado, então nenhum QShortcut duplicado é criado
        // aqui).
        {QStringLiteral("action.new_collection"), [this]() { handleNewCollectionRequested(); }},
        {QStringLiteral("action.edit_folder"), [this]() { triggerEditCurrentFolder(); }},
        {QStringLiteral("action.edit_body"), [this]() { triggerEditBodySelected(); }},
        {QStringLiteral("action.play"), [this]() { triggerPlaySelected(); }},
        {QStringLiteral("action.stop"), [this]() { triggerStopSelected(); }},
        {QStringLiteral("action.force_stop"), [this]() { triggerForceStopSelected(); }},
        {QStringLiteral("action.reset"), [this]() { triggerResetSelected(); }},
        {QStringLiteral("action.expand_selected"), [this]() { triggerExpandSelected(); }},
        {QStringLiteral("action.collapse_selected"), [this]() { triggerCollapseSelected(); }},
        {QStringLiteral("action.expand_all"), [this]() { triggerExpandAll(); }},
        {QStringLiteral("action.collapse_all"), [this]() { triggerCollapseAll(); }},
        {QStringLiteral("action.hide_selected"), [this]() { triggerToggleHiddenSelected(); }},
        {QStringLiteral("action.show_hidden"), [this]() { triggerToggleShowHidden(); }},
        // Esc (padrão) esconde a janela — mas NUNCA quando o foco está
        // dentro de um terminal INTERATIVO (ex: vim rodando via um comando
        // com interactive_terminal): Esc é tecla de uso comum lá dentro e
        // sequestrá-la quebraria o programa rodando. Fora disso, mesmo
        // comportamento de toggleVisibility() quando a janela já está
        // visível — hide() simples.
        {QStringLiteral("action.hide_window"), [this]() {
            if (m_terminalDrawer && m_terminalDrawer->interactiveMode()
                && m_terminalDrawer->isAncestorOf(QApplication::focusWidget())) {
                return;
            }
            hide();
        }},
    };

    for (const auto &spec : utils::actionShortcutSpecs()) {
        const auto handlerIt = handlers.constFind(spec.id);
        if (handlerIt == handlers.constEnd()) {
            continue; // spec sem handler registrado (não deveria acontecer — ver testes)
        }
        const QStringList sequences = settings.shortcuts.value(spec.id, spec.defaultSequences);
        for (const QString &seq : sequences) {
            if (seq.isEmpty()) {
                continue;
            }
            QWidget *parentWidget = spec.scope == utils::ShortcutScope::TreeWidget
                ? static_cast<QWidget *>(m_commandTree) : static_cast<QWidget *>(this);
            auto *sc = new QShortcut(QKeySequence(seq), parentWidget);
            if (spec.scope == utils::ShortcutScope::TreeWidget) {
                sc->setContext(Qt::WidgetWithChildrenShortcut);
            }
            connect(sc, &QShortcut::activated, this, handlerIt.value());
            m_actionShortcuts.append(sc);
        }
    }

    // Hint visual que menciona o atalho (placeholder do campo de
    // resposta) — recarregado aqui também, já que esta função roda de
    // novo ao salvar as Configurações.
    const QString focusOutputHint = QKeySequence(
        utils::firstShortcutFor(settings, QStringLiteral("action.focus_output"))).toString(QKeySequence::NativeText);
    if (m_terminalDrawer) {
        m_terminalDrawer->setInputShortcutHint(focusOutputHint);
    }
}

void MainWindow::toggleSearchBar()
{
    const bool willShow = !m_searchBar->isVisible();
    m_searchBar->setVisible(willShow);
    if (willShow) {
        // Ao exibir, autofoca para o usuário já digitar.
        m_searchBar->setFocus();
    } else {
        // Ao ocultar, limpa o filtro para não deixar a árvore filtrada
        // "invisivelmente" e devolve o foco à árvore.
        m_searchBar->clear();
        m_commandTree->focusFirstVisibleItem();
    }
}

void MainWindow::toggleOutputFocus()
{
    // Só faz sentido quando há saída ATIVA (o drawer está visível). Se não há,
    // devolve o foco à árvore para o usuário não ficar preso.
    if (!m_terminalDrawer || !m_terminalDrawer->isVisible()) {
        m_commandTree->focusFirstVisibleItem();
        return;
    }
    // Garante o painel expandido para o foco fazer efeito visível.
    if (!m_terminalDrawer->isExpanded()) {
        m_terminalDrawer->setExpanded(true);
    }
    // Alterna: se a Saída já tem o foco, devolve à árvore; senão, foca a Saída.
    if (m_terminalDrawer->isAncestorOf(QApplication::focusWidget())) {
        m_commandTree->focusFirstVisibleItem();
        return;
    }
    // Terminal interativo (Command::interactiveTerminal): foca o terminal de
    // verdade em vez do campo de stdin de texto — é o único jeito de digitar
    // nele, já que teclas vão direto pro PTY (ver PtyTerminalWidget).
    const auto it = m_commandsById.constFind(m_connectedTerminalCommandId);
    const bool connectedIsInteractive = it != m_commandsById.constEnd()
        && it->type == core::CommandType::Shell && it->interactiveTerminal;
    if (connectedIsInteractive) {
        m_terminalDrawer->focusInteractiveTerminal();
    } else {
        m_terminalDrawer->focusInput();
    }
}

void MainWindow::applyActionGroupPlacement()
{
    const core::SettingsData settings = m_configManager.loadSettings();

    // "Mostrar ocultos" também é aplicado aqui (chamado no boot e ao
    // salvar Configurações) — reflete o valor persistido no botão e na
    // árvore.
    m_showHiddenCommands = settings.showHiddenCommands;
    if (m_commandTree) {
        m_commandTree->setShowHidden(m_showHiddenCommands);
    }
    if (m_expandCollapseBar) {
        m_expandCollapseBar->setShowingHidden(m_showHiddenCommands);
    }

    QVector<QWidget *> upperGroups;
    QVector<QWidget *> bottomGroups;
    QVector<QWidget *> leftGroups;
    QVector<QWidget *> sideGroups;

    auto place = [&upperGroups, &bottomGroups, &leftGroups, &sideGroups](QWidget *group, const QString &placement) {
        if (!group) {
            return;
        }
        // "side" é o valor PERSISTIDO histórico pra "direita" (não
        // renomeado, por compat com settings.json já salvos — só o rótulo
        // exibido no Settings virou "Direita"/"Right" agora que "left"
        // existe como opção distinta). "upper"/"bottom"/"left" mapeiam
        // 1:1 para os 4 containers.
        if (placement == QStringLiteral("side")) {
            sideGroups.append(group);
        } else if (placement == QStringLiteral("bottom")) {
            bottomGroups.append(group);
        } else if (placement == QStringLiteral("left")) {
            leftGroups.append(group);
        } else if (placement == QStringLiteral("hidden")) {
            // Nem em container nenhum: o grupo continua existindo (os
            // atalhos de teclado configurados para suas ações continuam
            // funcionando via os métodos trigger*), só não aparece.
            // hide() explícito é OBRIGATÓRIO aqui: um grupo que nunca
            // passou por um ActionGroupContainer::setGroups() (ex.: já
            // nasce com placement "hidden" no boot) nunca teve seu
            // hide() chamado por aquele código — e um QWidget filho sem
            // hide() explícito É exibido quando a janela-mãe aparece,
            // mesmo sem estar em nenhum layout, flutuando na posição
            // (0,0) do pai (bug relatado com captura de tela: ícones
            // "ocultos" apareciam soltos perto do logo do Kai).
            group->hide();
        } else {
            // "upper" (padrão) ou valor desconhecido/futuro.
            upperGroups.append(group);
        }
    };
    // Ordem de exibição pedida pelo usuário: Execução, Exibição, Item.
    place(m_actionSidebar, settings.executionActionsPlacement);
    place(m_expandCollapseBar, settings.displayActionsPlacement);
    place(m_itemActionsBar, settings.itemActionsPlacement);

    // A orientação de cada grupo segue o container em que ele caiu:
    // upper/bottom = horizontal, left/side = vertical.
    auto orientationFor = [&](QWidget *group) {
        return (upperGroups.contains(group) || bottomGroups.contains(group)) ? Qt::Horizontal : Qt::Vertical;
    };
    if (m_itemActionsBar) {
        m_itemActionsBar->setOrientation(orientationFor(m_itemActionsBar));
    }
    if (m_expandCollapseBar) {
        m_expandCollapseBar->setOrientation(orientationFor(m_expandCollapseBar));
    }
    if (m_actionSidebar) {
        m_actionSidebar->setOrientation(orientationFor(m_actionSidebar));
    }

    if (m_upperActionsContainer) {
        m_upperActionsContainer->setGroups(upperGroups);
    }
    if (m_bottomActionsContainer) {
        m_bottomActionsContainer->setGroups(bottomGroups);
    }
    if (m_leftActionsContainer) {
        m_leftActionsContainer->setGroups(leftGroups);
    }
    if (m_sideActionsContainer) {
        m_sideActionsContainer->setGroups(sideGroups);
    }
}

void MainWindow::applyOutputPosition()
{
    if (!m_mainAreaWrapper || !m_terminalDrawer || !m_content) {
        return;
    }

    const core::SettingsData settings = m_configManager.loadSettings();
    QString pos = settings.outputPosition;
    if (pos != QStringLiteral("left") && pos != QStringLiteral("right")) {
        pos = QStringLiteral("bottom"); // padrão/valor desconhecido ou ausente
    }

    // Largura mínima do painel de Saída: seu conteúdo (linhas de log,
    // abas Headers/JSON) foi desenhado supondo uma barra larga no fundo;
    // sem um mínimo, um "left"/"right" com pouco espaço poderia espremer
    // o painel a um ponto ilegível. Inofensivo em "bottom" (sempre tem
    // largura total ali).
    m_terminalDrawer->setMinimumWidth(220);

    // Informa a posição pro drawer decidir o EIXO do colapso (altura na
    // "bottom", largura na "left"/"right") — ver DrawerPosition e o bug
    // corrigido em TerminalDrawer::applyCollapsedConstraints. Chamado DEPOIS
    // do setMinimumWidth acima: se o painel já estiver colapsado nesta
    // posição, reaplica o limite de colapso por cima do mínimo genérico.
    m_terminalDrawer->setDrawerPosition(
        pos == QStringLiteral("left") ? DrawerPosition::Left
        : pos == QStringLiteral("right") ? DrawerPosition::Right
                                         : DrawerPosition::Bottom);

    const Qt::Orientation orientation =
        (pos == QStringLiteral("left") || pos == QStringLiteral("right")) ? Qt::Horizontal : Qt::Vertical;

    auto *newSplitter = new QSplitter(orientation, m_content);
    newSplitter->setChildrenCollapsible(false);

    if (pos == QStringLiteral("left")) {
        // Saída à esquerda, área principal (busca+árvore/actions) à direita.
        newSplitter->addWidget(m_terminalDrawer);
        newSplitter->addWidget(m_mainAreaWrapper);
        newSplitter->setStretchFactor(0, 0);
        newSplitter->setStretchFactor(1, 1);
        newSplitter->setSizes({4000, 8000});
    } else if (pos == QStringLiteral("right")) {
        // Área principal à esquerda, Saída à direita.
        newSplitter->addWidget(m_mainAreaWrapper);
        newSplitter->addWidget(m_terminalDrawer);
        newSplitter->setStretchFactor(0, 1);
        newSplitter->setStretchFactor(1, 0);
        newSplitter->setSizes({8000, 4000});
    } else {
        // "bottom" (padrão/histórico) — estrutura e proporção
        // PIXEL-IDÊNTICAS ao comportamento de sempre: 50/50 entre a área
        // principal (topo) e o terminal (base). Feedback do usuário: "o
        // terminal tá vindo muito grande, faça 50/50".
        newSplitter->addWidget(m_mainAreaWrapper);
        newSplitter->addWidget(m_terminalDrawer);
        newSplitter->setStretchFactor(0, 1);
        newSplitter->setStretchFactor(1, 1);
        newSplitter->setSizes({10000, 10000});
    }

    // m_mainAreaWrapper/m_terminalDrawer acabaram de ser reparentados (via
    // addWidget acima) do splitter externo antigo para o novo — Qt cuida
    // disso automaticamente, sem recriar nenhum widget (conexões/estado
    // do TerminalDrawer sobrevivem). O splitter antigo agora está vazio;
    // removemos ele do layout e agendamos sua destruição.
    QSplitter *oldSplitter = m_outerSplitter;
    if (oldSplitter) {
        if (auto *contentLayout = m_content->layout()) {
            contentLayout->removeWidget(oldSplitter);
        }
        oldSplitter->hide();
        oldSplitter->deleteLater();
    }

    // Restaura o tamanho que o usuário deixou salvo (pedido do usuário: "o
    // que eu salvei redimensionando fica") — sobrescreve o default 50/50
    // ou 33/67 calculado acima, mas só se houver algo salvo para ESTA
    // orientação (um tamanho salvo em "bottom" não faz sentido aplicado a
    // "left"/"right" e vice-versa — ambos são só 2 números, então não dá
    // pra distinguir por conteúdo; a troca de orientação já reseta pro
    // default de qualquer forma na próxima chamada de applyOutputPosition,
    // o que é aceitável: o usuário raramente troca de posição e re-resize
    // é rápido).
    if (settings.outputSplitterSizes.size() == 2) {
        newSplitter->setSizes(settings.outputSplitterSizes);
    }

    if (auto *contentLayout = qobject_cast<QVBoxLayout *>(m_content->layout())) {
        contentLayout->addWidget(newSplitter, 1);
    }
    m_outerSplitter = newSplitter;

    // Painel de Saída SEMPRE visível (bug histórico documentado em
    // setupUi(): reparentar via QSplitter nunca deve escondê-lo).
    m_terminalDrawer->setVisible(true);

    // Persiste o tamanho toda vez que o usuário arrasta o divisor (não em
    // resizes programáticos — splitterMoved só dispara em drag manual do
    // mouse). Debounced: o sinal dispara repetidamente durante o arraste;
    // só grava settings.json quando o usuário PARA de arrastar, evitando
    // I/O a cada pixel movido.
    if (!m_outputSplitterSaveTimer) {
        m_outputSplitterSaveTimer = new QTimer(this);
        m_outputSplitterSaveTimer->setSingleShot(true);
        m_outputSplitterSaveTimer->setInterval(400);
        connect(m_outputSplitterSaveTimer, &QTimer::timeout, this, [this]() {
            if (!m_outerSplitter) {
                return;
            }
            core::SettingsData s = m_configManager.loadSettings();
            s.outputSplitterSizes = m_outerSplitter->sizes();
            m_configManager.saveSettings(s);
        });
    }
    connect(newSplitter, &QSplitter::splitterMoved, m_outputSplitterSaveTimer,
            qOverload<>(&QTimer::start));
}

void MainWindow::applyEditShortcutOnSelection()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty()) {
        if (m_commandTree->currentSelectionIsCollection()) {
            handleCollectionEditRequested(id);
        } else {
            handleEditRequested(id, m_commandTree->currentSelectionIsFolder());
        }
    }
}

void MainWindow::applyDeleteShortcutOnSelection()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty()) {
        handleDeleteRequested(id, m_commandTree->currentSelectionIsFolder());
    }
}

// --- Handlers das ações de item/exibição/execução ---------------------
// Extraídos das lambdas de clique dos 3 grupos (ItemActionsBar/
// ExpandCollapseBar/ActionSidebar) para serem reusados também pelos
// atalhos de teclado configuráveis (ver setupActionShortcuts) — a ação
// funciona pelo atalho mesmo se o grupo estiver com posicionamento "não
// exibir" (nenhum botão visível para clicar).

void MainWindow::triggerPlaySelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        handleCommandActivated(id);
    }
}

void MainWindow::triggerStopSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        handleKillCommandRequested(id);
    }
}

void MainWindow::triggerForceStopSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        handleKillCommandRequested(id); // ProcessManager::stop já faz terminate->timeout->kill
    }
}

void MainWindow::triggerResetSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        handleResetCommandRequested(id);
    }
}

void MainWindow::triggerEditSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty()) {
        if (m_commandTree->currentSelectionIsCollection()) {
            handleCollectionEditRequested(id);
        } else {
            handleEditRequested(id, m_commandTree->currentSelectionIsFolder());
        }
    }
}

void MainWindow::triggerEditBodySelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        handleQuickEditBodyRequested(id);
    }
}

void MainWindow::triggerDeleteSelected()
{
    const QString id = m_commandTree->currentSelectionId();
    if (!id.isEmpty()) {
        if (m_commandTree->currentSelectionIsCollection()) {
            handleCollectionDeleteRequested(id);
        } else {
            handleDeleteRequested(id, m_commandTree->currentSelectionIsFolder());
        }
    }
}

void MainWindow::triggerEditCurrentFolder()
{
    // A pasta raiz da aba ativa não é mais um item navegável na árvore
    // (reformulação de abas); esta ação é o único ponto de entrada para
    // editá-la.
    const QString rootFolderId = m_commandTree->currentRootFolderId();
    if (!rootFolderId.isEmpty()) {
        handleEditRequested(rootFolderId, true);
    }
}

void MainWindow::triggerExpandSelected()
{
    if (m_commandTree) {
        m_commandTree->expandCurrentItem();
    }
}

void MainWindow::triggerCollapseSelected()
{
    if (m_commandTree) {
        m_commandTree->collapseCurrentItem();
    }
}

void MainWindow::triggerExpandAll()
{
    if (m_commandTree) {
        m_commandTree->expandAll();
    }
}

void MainWindow::triggerCollapseAll()
{
    if (m_commandTree) {
        m_commandTree->collapseAll();
    }
}

void MainWindow::triggerToggleHiddenSelected()
{
    // Comandos, PASTAS e COLEÇÕES têm o campo `hidden` (pedido do
    // usuário). Uma pasta RAIZ oculta esconde a aba inteira.
    const QString id = m_commandTree->currentSelectionId();
    if (id.isEmpty()) {
        return;
    }
    if (m_commandTree->currentSelectionIsCollection()) {
        for (core::Collection &col : m_collections) {
            if (col.id == id) {
                col.hidden = !col.hidden;
                break;
            }
        }
        persistCollections(); // já chama reloadCommandTree()
    } else if (m_commandTree->currentSelectionIsFolder()) {
        for (core::Folder &f : m_commandsData.folders) {
            if (f.id == id) {
                f.hidden = !f.hidden;
                break;
            }
        }
        m_configManager.saveCommands(m_commandsData);
        reloadCommandTree();
    } else {
        for (core::Command &c : m_commandsData.commands) {
            if (c.id == id) {
                c.hidden = !c.hidden;
                break;
            }
        }
        m_configManager.saveCommands(m_commandsData);
        reloadCommandTree();
    }
    // Se acabou de ficar oculto e "mostrar ocultos" está desligado, o item
    // some da árvore — a seleção se perde (esperado). Senão, reflete o
    // novo estado no botão via handleCommandSelectionChanged (disparado
    // pelo próprio reloadCommandTree/setData ao reselecionar, se aplicável).
}

void MainWindow::triggerToggleShowHidden()
{
    m_showHiddenCommands = !m_showHiddenCommands;
    if (m_commandTree) {
        m_commandTree->setShowHidden(m_showHiddenCommands);
    }
    if (m_expandCollapseBar) {
        m_expandCollapseBar->setShowingHidden(m_showHiddenCommands);
    }
    core::SettingsData settings = m_configManager.loadSettings();
    settings.showHiddenCommands = m_showHiddenCommands;
    m_configManager.saveSettings(settings);
}

void MainWindow::quitApplication()
{
    // Fechamento real do app (nova ação "Fechar Aplicativo",
    // Ctrl+Q por padrão): encerra processos em background com segurança
    // via aboutToQuit -> ProcessManager::stopAll (já conectado no
    // construtor) e termina o event loop. Diferente de toggleVisibility/
    // closeEvent, que apenas ocultam a janela.
    QCoreApplication::quit();
}

void MainWindow::handleThemeReloaded(const utils::ResolvedTheme &theme)
{
    // Publica os tokens no design system ANTES de aplicar o QSS: todo widget
    // que lê cor/métrica passa a ver os valores do tema recém-carregado
    // (inclui live-reload).
    utils::tokens::publishTheme(theme.variables);

    utils::Logger::info(kLogTag, QStringLiteral("Aplicando tema '%1' (live reload).").arg(theme.name));
    // Tema do core + CAMADA MODERNA (mesma especificidade, aplicada depois,
    // então sobrescreve o que precisa): raio, elevação, escala tipográfica,
    // scrollbars slim, seleção com accent. Tudo derivado dos design tokens.
    setStyleSheet(theme.qss + buildModernStylesheet());
    // Backdrop nativo (Mica/Acrylic no Win11) quando o modo "material
    // líquido" está ligado nos efeitos.
    applyWindowBackdrop(this);
    m_terminalDrawer->applyThemeVariables(theme.variables);
    if (m_processListDialog) {
        m_processListDialog->applyThemeVariables(theme.variables);
    }
    // Recolori os ícones neutros dos 3 grupos de ações com a cor de
    // destaque do tema (feedback do usuário: ícones seguem a cor do tema,
    // não um roxo fixo).
    const QString accentHex = theme.variables.value(QStringLiteral("accent_color"),
                                                     theme.variables.value(QStringLiteral("accent")));
    if (m_actionSidebar && !accentHex.isEmpty()) {
        m_actionSidebar->applyAccentColor(QColor(accentHex));
    }
    if (m_expandCollapseBar && !accentHex.isEmpty()) {
        m_expandCollapseBar->applyAccentColor(QColor(accentHex));
    }
    if (m_itemActionsBar && !accentHex.isEmpty()) {
        m_itemActionsBar->applyAccentColor(QColor(accentHex));
    }
    // Reconstrói a árvore/abas para os ícones (abas de root, itens) que são
    // recoloridos com o accent do tema (IconPickerWidget::iconForName) pegarem
    // a nova cor no live-reload — senão só atualizariam ao reabrir o app.
    if (m_commandTree) {
        m_commandTree->setData(m_commandsData.folders, m_commandsData.commands, m_collections);
        m_commandTree->refreshTreeConnectorColor(QColor(utils::tokens::accent()));
    }
}

void MainWindow::loadConfig()
{
    m_commandsData = m_configManager.loadCommands();
    m_collections = m_configManager.loadCollections();

    const core::SettingsData settings = m_configManager.loadSettings();
    // As variáveis globais agora vêm do environment (pacote) ATIVO — o
    // globalEnvVars legado já foi migrado para um pacote no loadSettings.
    applyActiveEnvironment();
    // Restaura dinâmicas marcadas EnvExtractor::persist == true na sessão
    // anterior (dynamic-vars.json) — ANTES de qualquer comando poder rodar.
    m_envManager.seedPersistedDynamicVars(m_configManager.loadPersistedDynamicVars());

    // Restaura o estado colapsado do terminal salvo na sessão anterior
    //. Como setExpanded emite expandedChanged (que persiste),
    // usamos o mesmo valor já salvo — idempotente, não regrava nada
    // diferente.
    m_terminalDrawer->setExpanded(!settings.terminalCollapsed);

    reloadCommandTree();
}

void MainWindow::appendToCommandLog(const QString &commandId, const QString &text)
{
    QString &log = m_commandLogs[commandId];
    log += text;

    // Limita o buffer por comando para evitar crescimento ilimitado de
    // memória em processos de longa duração (mesmo espírito do
    // setMaximumBlockCount do TerminalDrawer) — configurável em
    // Configurações → Saída (bug reportado: um valor fixo de 200KB
    // descartava o INÍCIO do log de scripts verbosos, "rodei um script
    // grandinho e perdi logs"; default agora 1MB, ajustável).
    if (log.size() > m_outputMaxLogSizeChars) {
        log = log.right(m_outputMaxLogSizeChars);
    }
}

void MainWindow::reconnectTerminalToCommand(const QString &commandId)
{
    m_connectedTerminalCommandId = commandId;
    m_terminalDrawer->setCurrentCommandId(commandId);
    // LIMPA PRIMEIRO: reseta as abas (JSON/Headers) e a saída ANTES de
    // repopular. Agora a ordem é: limpar -> popular.
    m_terminalDrawer->clear();
    // Prompt "user@<caminho>": mostra o diretório de execução do comando
    // conectado (pedido do usuário). Cai para o diretório do projeto ativo
    // e, por fim, para o CWD, quando o comando não define um.
    {
        QString dir;
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == commandId) {
                dir = m_envManager.interpolate(c.workingDir);
                break;
            }
        }
        if (dir.trimmed().isEmpty()) {
            dir = m_envManager.interpolate(QStringLiteral("{{PROJECT_DIR}}"));
            if (dir.contains(QStringLiteral("{{"))) {
                dir.clear();
            }
        }
        // SEM fallback para o CWD: quando o comando não define diretório, o
        // prompt mostrava o caminho de instalação do PRÓPRIO Kai, o que é
        // enganoso (relatado). Vazio é mais honesto — o painel exibe só o
        // usuário nesses casos.
        m_terminalDrawer->setWorkingDirectory(dir.trimmed());
    }

    const auto it = m_commandsById.constFind(commandId);
    const QString displayName = (it != m_commandsById.constEnd()) ? it.value().name : commandId;

    // Terminal interativo (Command::interactiveTerminal): troca o CORPO do
    // painel pro terminal de verdade (PtyTerminalWidget) e reconstrói a
    // tela a partir do log bruto já acumulado — um único vterm é
    // reaproveitado entre comandos (ver OutputPanel::setInteractiveMode),
    // então trocar de seleção e voltar é seguro/determinístico.
    const bool isInteractive = (it != m_commandsById.constEnd())
        && it->type == core::CommandType::Shell && it->interactiveTerminal;
    m_terminalDrawer->setInteractiveMode(isInteractive);
    // "Saída" não faz sentido pra HTTP (feedback do usuário) — some pra
    // qualquer comando HTTP, aparece de novo pra shell/desconhecido.
    m_terminalDrawer->setStdoutTabVisible(
        it == m_commandsById.constEnd() || it->type != core::CommandType::Http);
    // Saída formatada (Command::formattedOutput) — POR COMANDO (pedido do
    // usuário), não uma preferência de exibição global.
    m_terminalDrawer->setFormattedOutputEnabled(
        it != m_commandsById.constEnd() && it->formattedOutput);
    // Render Markdown (Command::renderMarkdown) — mesmo padrão POR COMANDO
    // do formattedOutput acima, não uma preferência de exibição global.
    m_terminalDrawer->setMarkdownOutputEnabled(
        it != m_commandsById.constEnd() && it->renderMarkdown);

    m_terminalDrawer->setCommandName(displayName);
    // (removido) linha "[Saída conectada ao comando X]": boilerplate — o nome do
    // comando já aparece no cabeçalho da saída.
    //
    // ANTI-DUPLICAÇÃO: o clear() (feito no INÍCIO da função) é essencial. Sem
    // ele, reconectar ao mesmo comando reinjetaria o log inteiro sobre o que
    // já estava na tela, dobrando a saída. A ordem é sempre limpar (topo) ->
    // reinjetar o histórico -> receber os chunks novos ao vivo.
    m_terminalDrawer->appendRawText(m_commandLogs.value(commandId), false);
    // FLUSH SÍNCRONO: appendRawText só ENFILEIRA (coalescing de 16ms
    // pensado para chunks de streaming ao vivo, não para o replay do
    // histórico ao reconectar). Bug relatado: comandos em modo Markdown
    // às vezes carregavam vazios (no boot ou ao trocar de aba) — a
    // reconexão seguinte (ex.: showEvent() reselecionando o primeiro item
    // visível, 0ms) podia rodar clear() ANTES do timer de 16ms disparar,
    // apagando o histórico enfileirado sem nada disparar de novo
    // (reselecionar o MESMO item não reemite currentItemChanged). Forçar o
    // flush aqui garante que o conteúdo já esteja no widget de saída antes
    // de qualquer reconexão subsequente ter chance de limpar de novo.
    m_terminalDrawer->flushPendingOutput();
    if (isInteractive) {
        m_terminalDrawer->resetInteractiveAndReplay(m_commandLogs.value(commandId));
    }

    // Botão "Ver JSON" reflete o log DESTE comando — SÓ para HTTP (pedido do
    // usuário, revertendo uma versão anterior que detectava JSON em
    // QUALQUER log de shell: "quero apenas para cmds http" — a Saída
    // Formatada já cobre o caso de logs de shell estruturados, então a
    // detecção automática por conteúdo virou ruído/falso positivo ali).
    if (it != m_commandsById.constEnd() && it->type == core::CommandType::Http) {
        m_terminalDrawer->setJsonAvailable(m_commandLogs.value(commandId));
    } else {
        m_terminalDrawer->setJsonAvailable(QString());
    }

    // Reaplica o resultado HTTP estruturado (se houver) para RESTAURAR as
    // abas Headers/JSON ao reselecionar um comando HTTP já executado — antes
    // elas sumiam ao trocar de aba/comando e voltar (relatado), pois só eram
    // populadas ao vivo. Vem DEPOIS do clear/appendRawText para reconstruir
    // as abas em cima do estado limpo.
    // Última execução foi pulada por condição? Painel "não executada" em
    // vez do resultado em cache (mesmo se existir um de uma rodada MAIS
    // ANTIGA — a condição decidiu que esta rodada não representa mais o
    // estado atual do comando).
    if (m_skippedReason.contains(commandId)) {
        m_terminalDrawer->setSkipped(true, m_skippedReason.value(commandId));
    } else if (m_lastHttpResult.contains(commandId)) {
        m_terminalDrawer->setHttpResult(m_lastHttpResult.value(commandId));
    }

    // NÃO força expandir ao trocar/reselecionar comando: o estado de colapso
    // da Saída é uma preferência da INSTÂNCIA do app e deve persistir entre
    // trocas de comando (pedido do usuário: "quando eu troco de comando está
    // voltando"). Antes um setExpanded(true) aqui reabria a Saída toda vez.

    // Input habilitado se este processo específico ainda estiver rodando e
    // aceitar stdin. Cobre DOIS casos: (a) processo background rastreado
    // pelo ProcessManager; (b) comando de execução única ativo no pipeline
    // (bug reportado: ao trocar de seleção e voltar, perdia-se a conexão
    // de stdin — o pipeline-ativo não era considerado aqui).
    const bool runningInManager = m_processManager->isTracked(commandId)
        && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running;
    // O runner do pipeline pertence a ESTE comando? (casa por id — antes
    // usava m_activePipelineCommandId, que ficava desatualizado ao rodar um
    // 2º TTY, fazendo o 1º parecer "sem input" ao reconectar).
    // Runner DESTE comando (registry por commandId) — não mais o "ativo".
    const bool runningInPipeline = m_pipeline->runnerFor(commandId) != nullptr;
    const bool stillRunning = runningInManager || runningInPipeline;
    m_terminalDrawer->setInputEnabled(stillRunning);
    if (isInteractive) {
        m_terminalDrawer->setInteractiveAcceptingInput(stillRunning);
    }

    // Indicador visual de execução: processos em background
    // continuam sinalizados como tal, deixando claro que continuam vivos
    // mesmo após reconectar o terminal (diferente de uma execução única
    // que já concluiu).
    if (runningInPipeline) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Running);
    } else if (stillRunning) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Background);
    } else if (m_skippedReason.contains(commandId)) {
        // Última execução deste comando foi pulada por Execution Condition
        // — badge âmbar, não o verde de sucesso (ver commandSkippedByCondition).
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Skipped);
    } else {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Success);
    }
}

engine::ProcessRunner *MainWindow::runnerForCommandId(const QString &commandId) const
{
    if (commandId.isEmpty()) {
        return nullptr;
    }
    engine::ProcessRunner *r = m_pipeline->runnerFor(commandId);
    if (!r) {
        r = m_processManager->runnerFor(commandId);
    }
    return r;
}

void MainWindow::handleTerminalCloseRequested()
{
    // Fecha o terminal manualmente (indicadores visuais de
    // execução: comandos de execução única mostram o resultado e
    // "paravam", cabendo ao usuário decidir quando esconder). Não afeta o
    // processo em si: se estiver em background, continua rodando e
    // rastreável via ProcessListDialog ou clicando no comando de novo.
    // A caixa da Saída permanece sempre visível no layout; o
    // botão "Fechar" apenas limpa o conteúdo e volta ao estado Idle, sem
    // remover o widget do splitter.
    m_terminalDrawer->clear();
    m_terminalDrawer->setCommandName(QString());
    m_terminalDrawer->setExecutionStatus(ExecutionStatus::Idle);
    m_terminalDrawer->setInputEnabled(false);
    m_terminalDrawer->setInteractiveMode(false);
    m_terminalDrawer->setStdoutTabVisible(true);
    m_terminalDrawer->setInteractiveAcceptingInput(false);
    m_connectedTerminalCommandId.clear();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        maybeAutoHideOnFocusLoss();
    }
}

void MainWindow::maybeAutoHideOnFocusLoss()
{
    if (!m_autoHideOnFocusLoss || !isVisible()) {
        return;
    }
    // Sem bandeja NÃO esconde: o usuário não teria como trazer a janela de volta
    // e o app pareceria ter desaparecido.
    if (!trayAvailable()) {
        return;
    }

    // DEFERIDO em um ciclo do event loop: no instante da desativação o Qt ainda
    // não sabe QUAL janela recebeu o foco, então decidir agora esconderia a
    // janela mesmo quando o foco foi para um diálogo do próprio Kai.
    QTimer::singleShot(0, this, [this]() {
        if (!m_autoHideOnFocusLoss || !isVisible() || isActiveWindow()) {
            return;
        }
        // Se QUALQUER janela do Kai está ativa (editor de comando, configurações,
        // saída destacada, seletor de arquivo), o foco continua no app.
        if (QApplication::activeWindow() != nullptr
            || QApplication::activeModalWidget() != nullptr
            || QApplication::activePopupWidget() != nullptr) {
            return;
        }
        hide();
    });
}

void MainWindow::applyWindowGeometryPreference()
{
    const core::SettingsData st = m_configManager.loadSettings();
    const QString mode = st.windowMode.trimmed().toLower();

    // Tamanho pedido, com limites sãos: nunca menor que o mínimo da janela nem
    // maior que a área útil da tela (evita janela fora do monitor).
    int w = st.windowWidth > 0 ? st.windowWidth : 1280;
    int h = st.windowHeight > 0 ? st.windowHeight : 760;
    // Limita pela área útil da tela para a janela não nascer fora do monitor.
    // Plataformas HEADLESS (offscreen/minimal, usadas nos testes e em CI)
    // reportam uma tela sintética de 800x800, e o clamp encolhia a janela
    // indevidamente — nesses backends não há monitor de verdade para respeitar.
    const QString platform = QGuiApplication::platformName();
    const bool headless = platform.contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)
        || platform.contains(QStringLiteral("minimal"), Qt::CaseInsensitive);
    if (!headless) {
        if (const QScreen *screen = QGuiApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            w = qBound(720, w, avail.width());
            h = qBound(480, h, avail.height());
        }
    }

    // GUARDA (bug relatado: "Iniciar visível" desmarcado e o Kai abria
    // sozinho mesmo assim): esta função roda tanto no CONSTRUTOR da janela
    // (antes de main.cpp decidir se mostra ou não) quanto em runtime,
    // quando o usuário troca o modo em Configurações com a janela já
    // visível. showFullScreen()/showMaximized() tornam a janela visível na
    // hora — sem esta guarda, um windowMode "maximized"/"fullscreen" salvo
    // ignorava "Iniciar visível" por completo, pois a janela já aparecia
    // aqui, bem antes do main.cpp sequer chegar a checar a config. Só
    // aplica o show* de verdade se a janela já está visível (mudança em
    // runtime) ou se o boot vai mesmo mostrar a janela.
    const bool allowShow = isVisible() || shouldStartVisible();
    if (mode == QStringLiteral("fullscreen")) {
        resize(w, h); // tamanho de retorno ao sair da tela cheia
        if (allowShow) {
            showFullScreen();
        }
        return;
    }
    if (mode == QStringLiteral("maximized")) {
        resize(w, h);
        if (allowShow) {
            showMaximized();
        }
        return;
    }
    if (mode == QStringLiteral("remember")) {
        // "Lembrar" reusa os mesmos campos: eles são atualizados no closeEvent.
        resize(w, h);
        return;
    }
    resize(w, h);
}

void MainWindow::applyAppearanceSettings()
{
    const core::SettingsData st = m_configManager.loadSettings();

    m_autoHideOnFocusLoss = st.autoHideOnFocusLoss;

    utils::tokens::setDensity(st.uiDensity == QStringLiteral("compact")
        ? utils::tokens::Density::Compact
        : utils::tokens::Density::Comfortable);

    utils::tokens::Effects fx;
    fx.shadows = st.fxShadows;
    fx.translucency = st.fxTranslucency;
    // Blur implica translucidez: sem fundo translúcido não há o que desfocar.
    fx.blur = st.fxBlur;
    if (fx.blur) {
        fx.translucency = true;
    }
    fx.animations = st.fxAnimations;
    fx.cornerStyle = st.uiCornerStyle;
    utils::tokens::setEffects(fx);

    // Re-aplica a folha (os tokens mudaram) e o backdrop da janela.
    if (m_themeManager && m_themeManager->hasTheme()) {
        setStyleSheet(m_themeManager->currentTheme().qss + buildModernStylesheet());
    } else {
        setStyleSheet(buildModernStylesheet());
    }
    applyWindowBackdrop(this);
    applyElevation(m_terminalDrawer, 1);

    // Os containers de posicionamento (upper/bottom/left/side) têm seu
    // próprio QSS (fundo/borda discretos), fora do stylesheet global acima
    // — sem isso, mudar "Estilo de cantos" no Settings não refletia neles
    // (bug relatado originalmente na antiga ExpandCollapseBar: "não segue
    // a preferência do usuário").
    if (m_upperActionsContainer) {
        m_upperActionsContainer->refreshStyle();
    }
    if (m_bottomActionsContainer) {
        m_bottomActionsContainer->refreshStyle();
    }
    if (m_leftActionsContainer) {
        m_leftActionsContainer->refreshStyle();
    }
    if (m_sideActionsContainer) {
        m_sideActionsContainer->refreshStyle();
    }

    if (m_commandTree) {
        m_commandTree->setTreeConnectorStyle(st.treeConnectorStyle, QColor(utils::tokens::accent()));
    }
}

bool MainWindow::isCommandRunning(const QString &commandId) const
{
    if (commandId.isEmpty()) {
        return false;
    }
    if (m_processManager->isTracked(commandId)
        && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running) {
        return true;
    }
    if (m_pipelineRunningIds.contains(commandId)) {
        return true;
    }
    return m_pipeline->runnerFor(commandId) != nullptr;
}

void MainWindow::handleCommandSelectionChanged(const QString &itemId, bool isFolder)
{
    // Atualiza as row actions da sidebar conforme a linha selecionada
    // (habilitar play/stop/force-stop/reset/edit/delete só quando a
    // linha permite). Estado de execução/falha vem dos conjuntos
    // rastreados pelo ProcessManager e por m_failedCommandIds.
    const bool hasSelection = !itemId.isEmpty();
    const bool isCommand = hasSelection && !isFolder;
    // BUG REPORTADO ("as ações no painel lateral bugam e somem"): aqui ainda
    // se usava o slot único (itemId == m_activePipelineCommandId), que o
    // SEGUNDO comando sobrescrevia — ao voltar ao dev server, isRunning virava
    // false e Stop/Force-stop desapareciam. Agora usa a fonte única.
    const bool isRunning = isCommand && isCommandRunning(itemId);
    const bool hasFailed = isCommand && m_failedCommandIds.contains(itemId);
    bool isHttp = false;
    bool isHidden = false;
    if (isCommand) {
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == itemId) { isHttp = (c.type == core::CommandType::Http); isHidden = c.hidden; break; }
        }
    } else if (hasSelection && m_commandTree->currentSelectionIsCollection()) {
        for (const core::Collection &col : m_collections) {
            if (col.id == itemId) { isHidden = col.hidden; break; }
        }
    } else if (hasSelection && isFolder) {
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.id == itemId) { isHidden = f.hidden; break; }
        }
    }
    m_actionSidebar->setRowContext(hasSelection, isCommand, isRunning, hasFailed, isHttp);
    if (m_itemActionsBar) {
        m_itemActionsBar->setRowContext(hasSelection);
    }
    if (m_expandCollapseBar) {
        m_expandCollapseBar->setSelectedHidden(hasSelection && isHidden);
    }

    // Terminal por-comando: a caixa da Saída permanece sempre visível no
    // layout (feedback do usuário), mas o conteúdo ainda é
    // contextual. Selecionar um comando com histórico de log ou em
    // execução (background) reconecta e exibe o log automaticamente;
    // selecionar uma pasta ou nada SEMPRE limpa o conteúdo e volta ao
    // estado Idle, sem esconder o widget — mesmo que o comando antes
    // conectado ainda esteja rodando em background (bug reportado: "ao
    // selecionar uma PASTA, e se um cmd está rodando dentro dela, o
    // sistema exibe o cmd rodando, não quero isso" — antes só limpava
    // quando o comando conectado NÃO estava mais rodando, então uma
    // pasta selecionada continuava mostrando a saída de um comando ativo
    // por baixo dela).
    if (itemId.isEmpty() || isFolder) {
        m_terminalDrawer->clear();
        m_terminalDrawer->setCommandName(QString());
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Idle);
        m_terminalDrawer->setInputEnabled(false);
        m_terminalDrawer->setInteractiveMode(false);
        m_terminalDrawer->setStdoutTabVisible(true);
        m_connectedTerminalCommandId.clear();
        return;
    }

    const bool hasLog = m_commandLogs.contains(itemId) && !m_commandLogs.value(itemId).isEmpty();
    const bool isTracked = m_processManager->isTracked(itemId);
    // Fonte ÚNICA de verdade: o Set de execuções em andamento (mesma usada
    // pela bolinha) + o runner real do comando. Antes usava
    // m_activePipelineCommandId (slot único), que o 2º comando sobrescrevia
    // — daí o Stop desaparecer ao voltar ao 1º TTY.
    const bool isPipelineActive = isCommandRunning(itemId);

    if (hasLog || isTracked || isPipelineActive) {
        reconnectTerminalToCommand(itemId);
    } else if (m_connectedTerminalCommandId != itemId) {
        // Comando sem histórico ainda: limpa o conteúdo em vez de forçar
        // exibição de um log vazio, evitando ruído visual para comandos
        // nunca executados. A caixa continua visível, apenas ociosa.
        //
        // BUG REAL CORRIGIDO ("saída formatada... traz a saída do comando
        // pro cara errado"): este ramo limpava a tela mas NUNCA atualizava
        // m_connectedTerminalCommandId — ficava apontando pro comando
        // selecionado ANTES. Selecionar um comando B sem histórico ainda,
        // vindo de um comando A conectado, deixava o backend pensando que
        // A continuava conectado; um chunk de log NOVO de A (ainda rodando
        // em background) então aparecia na tela sob o cabeçalho/seleção de
        // B — e um chunk de B era descartado (o guard "!=
        // m_connectedTerminalCommandId" no handler de log via commandId
        // ainda achava que A era o conectado). Alinhar o id conectado (e
        // o toggle de Saída Formatada, por-comando) aqui, mesmo sem
        // histórico ainda, fecha a lacuna.
        m_connectedTerminalCommandId = itemId;
        m_terminalDrawer->setCurrentCommandId(itemId);
        m_terminalDrawer->clear();
        m_terminalDrawer->setCommandName(QString());
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Idle);
        m_terminalDrawer->setInputEnabled(false);
        m_terminalDrawer->setInteractiveMode(false);
        m_terminalDrawer->setStdoutTabVisible(true);
        {
            bool formattedOutput = false;
            for (const core::Command &c : m_commandsData.commands) {
                if (c.id == itemId) { formattedOutput = c.formattedOutput; break; }
            }
            m_terminalDrawer->setFormattedOutputEnabled(formattedOutput);
        }
    }
}

void MainWindow::updateRunningCommandStatus()
{
    // Mantém a lista de Processos viva enquanto estiver aberta (ela também
    // exibe os processos de foreground agora).
    if (m_processListDialog && m_processListDialog->isVisible()) {
        m_processListDialog->refreshProcessList();
    }

    QSet<QString> runningIds;
    for (const QString &commandId : m_processManager->trackedCommandIds()) {
        if (m_processManager->statusOf(commandId) == engine::ProcessStatus::Running) {
            runningIds.insert(commandId);
        }
    }
    // Comandos de execução única (foreground) em andamento: fonte de verdade
    // ESTÁVEL, marcada no disparo e limpa só no pipelineFinished. Antes o
    // indicador dependia de m_activePipelineCommandId + activeProcessRunner()
    // ->isRunning(), que (a) só guardava UM comando (2º comando apagava o
    // ícone do 1º) e (b) ficava null no gap entre hooks (bolinha sumia e
    // bugava o parar). Agora unimos o set estável.
    runningIds.unite(m_pipelineRunningIds);
    // Runners de processo VIVOS no pipeline (fonte real): garante que a
    // bolinha reflita a realidade mesmo se o Set divergir.
    for (const QString &id : m_pipeline->runningCommandIds()) {
        runningIds.insert(id);
    }
    m_commandTree->setRunningCommandIds(runningIds);
    m_commandTree->setFailedCommandIds(m_failedCommandIds);

    // PID visual na saída (feedback do usuário): mostra o PID do SO do
    // processo do comando atualmente CONECTADO ao terminal, quando há um
    // ProcessRunner ativo (pipeline ou background). 0 esconde o rótulo.
    if (m_terminalDrawer) {
        qint64 pid = 0;
        const QString cid = m_connectedTerminalCommandId;
        if (!cid.isEmpty()) {
            if (auto *pr = m_pipeline->runnerFor(cid)) {
                pid = pr->processId();
            } else if (auto *r = m_processManager->runnerFor(cid)) {
                if (m_processManager->statusOf(cid) == engine::ProcessStatus::Running) {
                    pid = r->processId();
                }
            }
        }
        m_terminalDrawer->setProcessPid(pid);
    }

    // Reaplica o contexto das row actions da sidebar para a seleção atual
    // (o botão Stop/Force/Reset habilita/desabilita conforme o comando
    // selecionado passa a rodar/parar).
    if (m_actionSidebar && m_commandTree) {
        const QString selId = m_commandTree->currentSelectionId();
        const bool isFolder = m_commandTree->currentSelectionIsFolder();
        const bool hasSelection = !selId.isEmpty();
        const bool isCommand = hasSelection && !isFolder;
        const bool isRunning = isCommand && runningIds.contains(selId);
        const bool hasFailed = isCommand && m_failedCommandIds.contains(selId);
        bool isHttp = false;
        if (isCommand) {
            for (const core::Command &c : m_commandsData.commands) {
                if (c.id == selId) { isHttp = (c.type == core::CommandType::Http); break; }
            }
        }
        m_actionSidebar->setRowContext(hasSelection, isCommand, isRunning, hasFailed, isHttp);
        if (m_itemActionsBar) {
            m_itemActionsBar->setRowContext(hasSelection);
        }
    }
    // Título da janela destacada reflete o estado do comando de ORIGEM, para
    // servir de monitor ("está rodando ou não?") mesmo com outro selecionado.
    if (m_terminalDrawer->hasDetachedWindow()) {
        const QString did = m_terminalDrawer->detachedCommandId();
        if (!did.isEmpty()) {
            QString st = QStringLiteral("parado");
            if (isCommandRunning(did)) {
                qint64 pid = 0;
                if (auto *r = m_pipeline->runnerFor(did)) {
                    pid = r->processId();
                } else if (auto *br = m_processManager->runnerFor(did)) {
                    pid = br->processId();
                }
                st = pid > 0 ? QStringLiteral("rodando (pid %1)").arg(pid)
                             : QStringLiteral("rodando");
            } else if (m_failedCommandIds.contains(did)) {
                st = QStringLiteral("falhou");
            }
            m_terminalDrawer->setDetachedStatusText(st);
        }
    }
}

void MainWindow::reloadCommandTree()
{
    m_commandsById.clear();
    for (const core::Command &command : m_commandsData.commands) {
        m_commandsById[command.id] = command;
    }
    m_commandTree->setData(m_commandsData.folders, m_commandsData.commands, m_collections);
    updateRunningCommandStatus();
    updateWelcomeScreenVisibility();
}

void MainWindow::updateWelcomeScreenVisibility()
{
    // Condição de "app recém-bootado, nada configurado ainda": 0 comandos
    // E 0 pastas — não apenas "a pasta/aba selecionada está vazia" (uma
    // pasta vazia dentro de um app já usado NÃO deve mostrar a tela de
    // boas-vindas, só a instalação limpa/config apagada).
    const bool isFreshInstall = m_commandsData.commands.isEmpty() && m_commandsData.folders.isEmpty();
    // "x" no canto da tela de boas-vindas (pedido do usuário): fechar
    // manualmente NÃO deve reabri-la sozinha de novo enquanto o app
    // continuar vazio — "quando fechar exibe vazio como antes" (a árvore
    // normal, mesmo sem nada dentro). m_welcomeScreenDismissed só é
    // resetado por handleShowWelcomeRequested() (Ajuda -> Tela de
    // Boas-Vindas) ou implicitamente quando o app deixa de estar vazio
    // (o próximo boot com dados já nem entra nesta condição).
    const bool showWelcome = isFreshInstall && !m_welcomeScreenDismissed;
    if (m_welcomeScreen) {
        m_welcomeScreen->setVisible(showWelcome);
    }
    if (m_commandTree) {
        m_commandTree->setVisible(!showWelcome);
    }
    // Saída começa COLAPSADA na tela de boas-vindas (pedido do usuário) —
    // não tem nenhum comando pra mostrar ainda, então o painel vazio só
    // ocupa espaço à toa. Não força expandir de volta ao sair do estado
    // fresh-install: assim que o usuário rodar o primeiro comando, o
    // caminho normal (handleCommandActivated -> setExpanded(true)) já
    // cuida disso sozinho.
    if (showWelcome && m_terminalDrawer) {
        m_terminalDrawer->setExpanded(false);
    }
    // Os containers de ações (m_upperActionsContainer/m_bottomActionsContainer)
    // NÃO são tocados aqui de propósito: sua visibilidade já é gerenciada
    // por ActionGroupContainer::setGroups (some sozinho quando vazio) +
    // applyActionGroupPlacement, conforme a preferência do usuário — forçar
    // aqui reapareceria uma barra vazia mesmo com placement "hidden".
}

void MainWindow::handleWelcomeScreenClosed()
{
    // "x" da tela de boas-vindas: fecha e devolve a árvore normal (mesmo
    // vazia) — "quando fechar exibe vazio como antes". m_commandsData
    // não muda em nada aqui, só a flag que suprime o auto-show.
    m_welcomeScreenDismissed = true;
    updateWelcomeScreenVisibility();
}

void MainWindow::handleShowWelcomeRequested()
{
    // Ajuda -> "Tela de Boas-Vindas" (pedido do usuário: "traga uma tela
    // que reabre essa tela de boas vindas"): reabre o tutorial a
    // qualquer momento, MESMO com comandos já cadastrados — diferente de
    // updateWelcomeScreenVisibility(), que só mostra em instalação
    // vazia. Some sozinha de novo ao fechar (volta pro estado normal:
    // árvore se houver dados, ou vazia se ainda não houver).
    m_welcomeScreenDismissed = false;
    if (m_welcomeScreen) {
        m_welcomeScreen->setVisible(true);
    }
    if (m_commandTree) {
        m_commandTree->setVisible(false);
    }
}

void MainWindow::centerOnActiveScreen()
{
    const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect screenGeometry = screen->availableGeometry();
    QPoint target = screenGeometry.center() - rect().center();

    // Defensivo (bug real WSL/Wayland): garante que a janela nunca seja
    // posicionada fora da área visível da tela — clampa o canto superior
    // esquerdo dentro dos limites de availableGeometry, para nunca "sumir"
    // em coordenadas negativas/deslocadas num setup multi-monitor do WSLg.
    const int maxX = screenGeometry.right() - qMin(width(), screenGeometry.width());
    const int maxY = screenGeometry.bottom() - qMin(height(), screenGeometry.height());
    target.setX(qBound(screenGeometry.left(), target.x(), qMax(screenGeometry.left(), maxX)));
    target.setY(qBound(screenGeometry.top(), target.y(), qMax(screenGeometry.top(), maxY)));

    move(target);
}

void MainWindow::handleFilterQueryChanged(const QString &query)
{
    m_commandTree->setFilterQuery(query);
}

void MainWindow::handleCommandActivated(const QString &commandId)
{
    const auto it = m_commandsById.constFind(commandId);
    if (it == m_commandsById.constEnd()) {
        utils::Logger::warning(kLogTag, QStringLiteral("Comando '%1' não encontrado.").arg(commandId));
        return;
    }

    const core::Command &command = it.value();

    // Terminal por-comando (feedback do usuário): se este
    // comando já está em execução em background, clicar nele de novo
    // reconecta o Terminal Drawer ao log daquele processo específico, em
    // vez de disparar uma nova execução por cima. Bug real corrigido:
    // "não consigo mais rodar/rebootar um comando" após pará-lo —
    // isTracked() continuava true mesmo depois do processo finalizar
    // (Success/Error), pois o ProcessManager mantém o histórico do último
    // processo daquele comando até uma nova execução sobrescrever. Checar
    // também o status real (Running) evita bloquear permanentemente novas
    // execuções depois que o processo já terminou/foi parado.
    if (m_processManager->isTracked(commandId)
        && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running) {
        reconnectTerminalToCommand(commandId);
        return;
    }

    promptParamsAndRun(command);
}

// Coleta os parâmetros do comando (via ParameterFormDialog, se houver) e
// dispara a execução.
void MainWindow::promptParamsAndRun(const core::Command &command)
{
    // DIAGNÓSTICO (bug reportado: comando com parâmetro não abre o diálogo):
    // registra quantos parâmetros o comando REALMENTE tem em runtime, para
    // distinguir "params vazio em memória" de "dialog abre e fecha".
    utils::Logger::info(kLogTag, QStringLiteral(
        "promptParamsAndRun: comando '%1' (id=%2) tem %3 parâmetro(s).")
        .arg(command.name, command.id).arg(command.params.size()));
    if (!command.params.isEmpty()) {
        // Comando parametrizado: coleta os valores via
        // formulário antes de disparar o pipeline. Pré-preenche com os
        // últimos valores informados (feedback do usuário).
        ParameterFormDialog dialog(command.params, this, command.lastParamValues, command.paramUsageHistory, m_collections, command.description);
        if (dialog.exec() != QDialog::Accepted) {
            utils::Logger::info(kLogTag, QStringLiteral("Execução de '%1' cancelada pelo usuário.").arg(command.name));
            return;
        }
        const QMap<QString, QString> values = dialog.values();
        const QMap<QString, QStringList> updatedHistory = dialog.updatedUsageHistory();

        // Toggles de favorito feitos na tela de seleção dedicada: persiste
        // as coleções atualizadas no collections.json.
        if (dialog.collectionsChanged()) {
            m_collections = dialog.updatedCollections();
            persistCollections();
        }

        // Salva os últimos parâmetros informados E o histórico de uso
        // (feedback do usuário: ordenar opções pelo histórico) no comando
        // e persiste, para pré-preencher/reordenar na próxima execução.
        for (core::Command &c : m_commandsData.commands) {
            if (c.id == command.id) {
                if (c.lastParamValues != values || c.paramUsageHistory != updatedHistory) {
                    c.lastParamValues = values;
                    c.paramUsageHistory = updatedHistory;
                    m_configManager.saveCommands(m_commandsData);
                    m_commandsById[c.id] = c;
                }
                break;
            }
        }

        runCommandWithParams(command, values);
        return;
    }

    runCommandWithParams(command, {});
}

void MainWindow::scheduleAutoRunCommands()
{
    for (const core::Command &command : m_commandsData.commands) {
        if (!command.autoRun) {
            continue;
        }
        const int delayMs = qMax(0, command.autoRunDelaySec) * 1000;
        const QString commandId = command.id;
        // AUTO-RUN = AUTOCLICK (feedback do usuário: "é como se fosse apenas um
        // autoclick, apenas!"). Chama exatamente o mesmo caminho do clique
        // manual (handleCommandActivated): conecta o Terminal Drawer, mostra o
        // andamento, aceita resposta via stdin (ex: comando de leitura) e, se o
        // comando tiver parâmetros, abre o formulário — idêntico a clicar nele.
        QTimer::singleShot(delayMs, this, [this, commandId]() {
            if (!m_commandsById.contains(commandId)) {
                return; // comando removido no intervalo
            }
            utils::Logger::info(kLogTag,
                QStringLiteral("Auto-run (autoclick): '%1'.").arg(commandId));
            handleCommandActivated(commandId);
        });
    }
}

void MainWindow::runCommandWithParams(const core::Command &command, const QMap<QString, QString> &paramValues)
{
    // OCULTAR AO EXECUTAR (preferência POR COMANDO): esconde a janela do Kai ao
    // disparar. Só esconde se houver bandeja — sem ela o usuário não teria como
    // trazer a janela de volta e o app pareceria ter desaparecido.
    if (command.hideOnRun) {
        if (trayAvailable()) {
            hide();
        } else {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Comando '%1' pede para ocultar o Kai, mas não há bandeja "
                               "neste ambiente; mantendo a janela visível.").arg(command.id));
        }
    }

    // Escopo de pasta COM HERANÇA pela hierarquia (bug reportado: comando
    // em SUBPASTA não pegava o env_vars da pasta PAI/projeto — ex:
    // {{API_BASE}} ficava literal e o HTTP dava "Protocol '' is unknown").
    // Coletamos os env_vars subindo a cadeia de pais e aplicamos do PAI
    // (raiz) para o FILHO (mais específico sobrescreve), garantindo que as
    // vars do projeto cheguem aos comandos em qualquer subpasta.
    {
        QMap<QString, core::Folder> foldersById;
        for (const core::Folder &f : m_commandsData.folders) {
            foldersById.insert(f.id, f);
        }
        // Monta a cadeia raiz -> ... -> pasta do comando.
        QVector<core::Folder> chain;
        QString cur = command.folderId;
        QSet<QString> seen;
        while (!cur.isEmpty() && foldersById.contains(cur) && !seen.contains(cur)) {
            seen.insert(cur);
            const core::Folder &f = foldersById.value(cur);
            chain.prepend(f); // prepend => raiz primeiro
            cur = f.parentId.value_or(QString());
        }
        QMap<QString, QString> mergedVars;
        for (const core::Folder &f : chain) {
            for (auto it = f.envVars.constBegin(); it != f.envVars.constEnd(); ++it) {
                mergedVars[it.key()] = it.value(); // filho sobrescreve pai
            }
        }
        m_envManager.setFolderVars(mergedVars);

        // Escopo das variáveis DINÂMICAS (ver EnvironmentManager::
        // setDynamicVarScope): a pasta-projeto (Folder::isProject) MAIS
        // PRÓXIMA na MESMA cadeia já montada acima — folha primeiro, então
        // percorre de trás pra frente. Nenhum ancestral marcado -> Global.
        QString dynamicScope;
        for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
            if (it->isProject) {
                dynamicScope = it->id;
                break;
            }
        }
        m_envManager.setDynamicVarScope(dynamicScope);
    }

    // Parâmetros do formulário têm a precedência mais alta.
    // Expansão de COLEÇÃO: para cada parâmetro ligado a uma
    // coleção, o valor escolhido é o ID de uma entrada; injetamos TODOS os
    // campos dessa entrada como chaves "param.campo" (ex: usuarios.email),
    // permitindo o replace {{usuarios.email}} no comando. Mantém também o
    // valor do próprio parâmetro (id da entrada) para retrocompat.
    QMap<QString, QString> expandedParams = paramValues;
    for (const core::Parameter &param : command.params) {
        if (param.collectionId.isEmpty()) {
            continue;
        }
        const QString chosenRaw = paramValues.value(param.name);
        if (chosenRaw.isEmpty()) {
            continue;
        }
        const auto colIt = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
            [&param](const core::Collection &c) { return c.id == param.collectionId; });
        if (colIt == m_collections.constEnd()) {
            continue;
        }
        // Suporte a MÚLTIPLA seleção: os ids vêm separados por vírgula.
        // Expande a PRIMEIRA entrada como {{param.campo}} (uso comum de um
        // valor por vez) e também disponibiliza, para cada campo, a lista
        // de valores de TODAS as entradas escolhidas em {{param.campo__all}}
        // (separados por vírgula), útil para multi-seleção.
        const QStringList chosenIds = chosenRaw.split(QLatin1Char(','), Qt::SkipEmptyParts);
        // CÓPIAS por valor, não ponteiros pra dentro de colIt->entries
        // (endurecimento defensivo: um ponteiro cru sobrevivendo além deste
        // laço é uma UB à espreita se m_collections for tocado por
        // qualquer motivo entre a busca e o uso — ex.: um favorito marcado
        // há pouco tempo disparando algum caminho de persistência/replace
        // no meio do caminho. Elimina a classe de bug inteira ao custo de
        // uma cópia pequena por entrada escolhida).
        QVector<core::CollectionEntry> chosenEntries;
        for (const QString &id : chosenIds) {
            const auto entryIt = std::find_if(colIt->entries.constBegin(), colIt->entries.constEnd(),
                [&id](const core::CollectionEntry &e) { return e.id == id; });
            if (entryIt != colIt->entries.constEnd()) {
                chosenEntries << *entryIt;
            }
        }
        if (chosenEntries.isEmpty()) {
            continue;
        }
        // Primeira entrada -> {{param.campo}}.
        const core::CollectionEntry &first = chosenEntries.first();
        for (auto vit = first.values.constBegin(); vit != first.values.constEnd(); ++vit) {
            expandedParams.insert(QStringLiteral("%1.%2").arg(param.name, vit.key()), vit.value());
        }
        // Todas -> {{param.campo__all}} (CSV) — SEMPRE, mesmo com só 1
        // entrada escolhida (bug real reportado: só era gerado com >1,
        // então um comando usando "{{param.campo__all}}" ficava com o
        // placeholder literal, sem substituir NADA, sempre que o usuário
        // marcava uma única entrada — inconsistente com o multi-select de
        // opções fixas, cujo "__labels" já era sempre gerado independente
        // da contagem).
        // Rótulo da entrada (pedido do usuário: "vou precisar ainda que
        // injete o nome os rótulos tbm.. ambientes.value__label e
        // ambientes.value__label__all") — o texto de EXIBIÇÃO da entrada
        // (collectionDisplayField, com o mesmo fallback pro 1º campo do
        // schema já usado no picker), à parte do valor cru do campo.
        // Replicado por campo (não só uma chave "param__label" solta) pra
        // bater exatamente com o padrão de nomenclatura já usado pelo
        // usuário no dia a dia ("param.campo__sufixo").
        const QString displayField = !param.collectionDisplayField.isEmpty()
            ? param.collectionDisplayField
            : (!colIt->schema.isEmpty() ? colIt->schema.first().name : QString());
        const QString firstLabel = displayField.isEmpty() ? QString() : first.values.value(displayField);
        QStringList allLabels;
        if (!displayField.isEmpty()) {
            for (const core::CollectionEntry &e : chosenEntries) {
                allLabels << e.values.value(displayField);
            }
        }
        for (const core::CollectionField &f : colIt->schema) {
            QStringList allVals;
            for (const core::CollectionEntry &e : chosenEntries) {
                allVals << e.values.value(f.name);
            }
            expandedParams.insert(QStringLiteral("%1.%2__all").arg(param.name, f.name),
                                  allVals.join(QLatin1Char(',')));
            expandedParams.insert(QStringLiteral("%1.%2__label").arg(param.name, f.name), firstLabel);
            expandedParams.insert(QStringLiteral("%1.%2__label__all").arg(param.name, f.name),
                                  allLabels.join(QLatin1Char(',')));
        }
    }
    m_envManager.setParamVars(expandedParams);

    // Execução começa diretamente na pasta/aba do comando; nenhum
    // histórico separado é mantido na navegação principal.
    // Terminal por-comando (indicadores visuais de execução):
    // ao disparar a execução, conecta e exibe o terminal já sinalizando
    // "Rodando", com o nome do comando no título.
    m_connectedTerminalCommandId = command.id;
    m_commandLogs[command.id].clear();
    // "Abrir último link" para TERMINAL INTERATIVO (ver handlePipelineLog):
    // reseta a marca de "já abriu nesta execução" a cada novo run.
    m_openedLastLinkForRun.remove(command.id);
    // Rastreia o comando de execução única ativo (shell rodando via
    // pipeline, não background) para marcá-lo como "rodando" na árvore,
    // permitir matá-lo e reconectar o stdin (bugs reportados). Só shell
    // tem ProcessRunner; HTTP não.
    m_activePipelineCommandId = (command.type == core::CommandType::Shell) ? command.id : QString();
    // Marca a execução como em andamento (indicador estável — sobrevive a
    // hooks e a outros comandos rodando). Limpo só no pipelineFinished.
    if (command.type == core::CommandType::Shell) {
        m_pipelineRunningIds.insert(command.id);
        // Bug real reportado: "ao re-rodar um comando via double click ou
        // enter, esse tempo não reseta" — CommandTreeWidget::
        // setRunningCommandIds só reinicia o cronômetro visual de um id
        // quando o poll periódico o vê AUSENTE antes de reaparecer; um
        // re-disparo rápido o bastante nunca passa por esse "ausente"
        // intermediário. Reset explícito e síncrono aqui, no instante real
        // em que a execução começa (mesma condição de tipo que alimenta
        // m_pipelineRunningIds — é o que popula o cronômetro visual).
        m_commandTree->resetRunTimer(command.id);
    }
    // Marca o início desta execução para registrar a duração no histórico.
    m_runStartedAt = QDateTime::currentDateTime();
    // Marca se o comando ativo é HTTP, para abrir o JSON viewer com a
    // resposta ao finalizar (feature JSON viewer).
    m_activePipelineIsHttp = (command.type == core::CommandType::Http);
    // Nova execução limpa qualquer marcação anterior de "falhou"
    // 05 — botão inline de reset): o estado de erro é só até a próxima
    // tentativa, nunca permanente.
    m_failedCommandIds.remove(command.id);
    updateRunningCommandStatus();
    m_terminalDrawer->clear();
    // Terminal interativo (Command::interactiveTerminal): o corpo do painel
    // vira um terminal de verdade em vez das abas Resposta/Saída/Headers/
    // Envs — só faz sentido para shell (HTTP não tem PTY/processo).
    m_terminalDrawer->setInteractiveMode(
        command.type == core::CommandType::Shell && command.interactiveTerminal);
    // "Saída" não faz sentido pra HTTP (feedback do usuário: "ela não é
    // útil" — HTTP já tem Resposta/Requisição/Headers).
    m_terminalDrawer->setStdoutTabVisible(command.type != core::CommandType::Http);
    m_terminalDrawer->setFormattedOutputEnabled(command.formattedOutput);
    m_terminalDrawer->setInteractiveAcceptingInput(command.type == core::CommandType::Shell);
    m_terminalDrawer->setCommandName(command.name);
    m_terminalDrawer->setExecutionStatus(ExecutionStatus::Running);
    m_terminalDrawer->setExpanded(true);
    // Bug real corrigido (feedback do usuário: comandos como
    // `read -p "..." var` não aceitavam resposta digitada): o campo de
    // input só era habilitado dentro de handlePipelineLog, ao chegar o
    // primeiro output do processo. Mas `read -p` no bash escreve o
    // prompt em condições que nem sempre chegam como output capturável
    // antes de bloquear esperando stdin (confirmado via teste real,
    // test_read_prompt_diag: ProcessRunner::writeToStdin funciona
    // corretamente mesmo sem nenhum outputReady prévio — o bug era só a
    // UI nunca habilitar o campo a tempo). Habilita já ao disparar a
    // execução (comandos Shell sempre têm um ProcessRunner associado via
    // ExecutionPipeline); handlePipelineFinished desabilita ao terminar.
    m_terminalDrawer->setInputEnabled(command.type == core::CommandType::Shell);
    // SEM autofoco (feedback do usuário: rodar um comando não deve roubar o
    // foco de onde o usuário estava — árvore, busca, etc.). O campo de
    // resposta fica habilitado/visível com um placeholder convidativo (ver
    // OutputPanel::setInputEnabled) para o usuário saber que pode clicar
    // nele quando quiser responder, em vez de o foco pular sozinho.

    utils::Logger::info(kLogTag, QStringLiteral("Executando comando '%1'.").arg(command.name));
    // Alvos de terminal configuráveis (feedback do usuário): passa
    // a lista atual das configurações para o pipeline aplicar o template
    // do alvo escolhido pelo comando (ex: WSL bridge).
    m_pipeline->setTerminalProfiles(m_configManager.loadSettings().terminalProfiles);
    // Hierarquia de pastas para a RESOLUÇÃO do perfil herdado (@parent):
    // um comando/subpasta que herda sobe pela cadeia até achar um perfil
    // concreto (ver ExecutionPipeline::effectiveTerminalProfileName).
    m_pipeline->setFolders(m_commandsData.folders);
    m_pipeline->run(command, m_commandsById, m_envManager);
    // Refresca o indicador de "rodando" no próximo ciclo do event loop,
    // quando o ProcessRunner já foi criado/iniciado (corrige o ícone que
    // não aparecia, especialmente no Windows, por ser calculado antes do
    // runner existir).
    QTimer::singleShot(0, this, [this]() { updateRunningCommandStatus(); });
    QTimer::singleShot(150, this, [this]() { updateRunningCommandStatus(); });
}

void MainWindow::handlePipelineLog(const QString &commandId, const QString &text, bool isError)
{
    appendToCommandLog(commandId, text);

    // "Abrir último link" para TERMINAL INTERATIVO (ver comentário do
    // membro m_openedLastLinkForRun no header) — checa a cada chunk em vez
    // de esperar o pipeline terminar, já que um servidor de dev interativo
    // tipicamente nunca "termina com sucesso" (o usuário para manualmente).
    // Abre a PRIMEIRA URL vista nesta execução, não a última — pro caso de
    // uso real (URL impressa uma vez no início), "última" não faz sentido
    // pra um processo que nunca acaba de imprimir coisas.
    if (!m_openedLastLinkForRun.contains(commandId)) {
        const auto cmdIt = m_commandsById.constFind(commandId);
        if (cmdIt != m_commandsById.constEnd() && cmdIt->interactiveTerminal && cmdIt->openLastLink) {
            // Varre o log ACUMULADO (não só este chunk) - uma URL longa
            // pode ter sido cortada bem na fronteira entre dois chunks de
            // saída.
            static const QRegularExpression interactiveUrlRe(QStringLiteral("https?://[^\\s\"'<>\\])}]+"));
            const QRegularExpressionMatch m = interactiveUrlRe.match(m_commandLogs.value(commandId));
            if (m.hasMatch()) {
                const QString url = m.captured(0);
                m_openedLastLinkForRun.insert(commandId);
                utils::Logger::info(kLogTag,
                    QStringLiteral("Abrindo link impresso por '%1' (terminal interativo): %2").arg(commandId, url));
                QDesktopServices::openUrl(QUrl(url));
            }
        }
    }

    // JANELA DESTACADA: é um monitor FIXO do comando pelo qual foi
    // destacada. Recebe a saída DESSE comando mesmo que outro esteja
    // selecionado (bug reportado: ela espelhava o comando selecionado).
    if (m_terminalDrawer->hasDetachedWindow()
        && m_terminalDrawer->detachedCommandId() == commandId) {
        m_terminalDrawer->appendToDetached(text, isError);
    }

    // Terminal por-comando: só ecoa no Terminal Drawer visível se este for
    // o comando atualmente conectado (ou se nenhum estiver conectado
    // ainda E a seleção atual não é uma pasta, conecta automaticamente ao
    // primeiro log recebido). Sem o segundo checagem, uma PASTA
    // selecionada (que limpa m_connectedTerminalCommandId ao ser
    // selecionada) reconectava sozinha assim que qualquer comando em
    // background produzisse output novo — bug reportado: "ao selecionar
    // uma PASTA, e se um cmd está rodando dentro dela, o sistema exibe o
    // cmd rodando, não quero isso".
    if (m_connectedTerminalCommandId.isEmpty() && !m_commandTree->currentSelectionIsFolder()) {
        m_connectedTerminalCommandId = commandId;
    m_terminalDrawer->setCurrentCommandId(commandId);
    // Prompt "user@<caminho>": mostra o diretório de execução do comando
    // conectado (pedido do usuário). Cai para o diretório do projeto ativo
    // e, por fim, para o CWD, quando o comando não define um.
    {
        QString dir;
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == commandId) {
                dir = m_envManager.interpolate(c.workingDir);
                break;
            }
        }
        if (dir.trimmed().isEmpty()) {
            dir = m_envManager.interpolate(QStringLiteral("{{PROJECT_DIR}}"));
            if (dir.contains(QStringLiteral("{{"))) {
                dir.clear();
            }
        }
        // SEM fallback para o CWD: quando o comando não define diretório, o
        // prompt mostrava o caminho de instalação do PRÓPRIO Kai, o que é
        // enganoso (relatado). Vazio é mais honesto — o painel exibe só o
        // usuário nesses casos.
        m_terminalDrawer->setWorkingDirectory(dir.trimmed());
    }
    }
    if (m_connectedTerminalCommandId != commandId) {
        return;
    }

    m_terminalDrawer->appendRawText(text, isError);
    // Terminal interativo: alimenta o vterm com o texto AO VIVO, na mesma
    // hora que o texto puro (m_commandLogs/appendRawText acima) — os dois
    // sempre andam juntos, então trocar de comando e voltar (que realimenta
    // o vterm a partir de m_commandLogs) reconstrói exatamente o mesmo
    // estado. Só alimenta se o comando CONECTADO for mesmo interativo —
    // senão seria trabalho à toa (parse de vterm) num widget escondido.
    const auto connectedIt = m_commandsById.constFind(commandId);
    const bool connectedIsInteractive = connectedIt != m_commandsById.constEnd()
        && connectedIt->type == core::CommandType::Shell && connectedIt->interactiveTerminal;
    if (connectedIsInteractive) {
        m_terminalDrawer->feedInteractive(text);
    }
    if (!m_terminalDrawer->isExpanded()) {
        m_terminalDrawer->setExpanded(true);
    }
    // Habilita o input a cada log recebido: se há saída chegando, é sinal
    // de que existe um processo ativo. Usa o runner do comando CONECTADO
    // especificamente (connectedProcessRunner(), que aqui é o MESMO
    // `commandId` deste log) — NÃO m_pipeline->activeProcessRunner(), que é
    // o runner "ativo" (o mais recentemente disparado) do pipeline como um
    // todo. Bug real: com dois comandos rodando em paralelo (ex: um bash
    // interativo conectado + um script comum em background), o eco do PTY
    // do próprio bash disparava este handler, mas activeProcessRunner()
    // podia apontar pro OUTRO comando (ou nullptr, se ele já tivesse
    // terminado) — desligando a entrada do bash por engano bem no
    // instante em que você digitava nele ("digito qualquer coisa, trava").
    const bool hasActiveRunner = connectedProcessRunner() != nullptr;
    m_terminalDrawer->setInputEnabled(hasActiveRunner);
    if (connectedIsInteractive) {
        m_terminalDrawer->setInteractiveAcceptingInput(hasActiveRunner);
    }
}

void MainWindow::handlePipelineFinished(const engine::PipelineResult &result)
{
    m_envManager.clearParamVars();
    // NOTA: as variáveis DINÂMICAS (token extraído por env_extractors, env
    // capturado por hook) são mantidas DE PROPÓSITO entre execuções — é a
    // feature "extraia o token do login e use nos próximos comandos". Não
    // limpar aqui é intencional (a auditoria sugeriu limpar, o que quebraria
    // esse caso de uso documentado).
    // Só desliga a entrada do comando CONECTADO se ele de fato não tiver
    // mais um runner rodando de verdade. O sinal `finished` do pipeline não
    // diz QUAL comando terminou, e este handler presumia que era sempre o
    // CONECTADO — quebra quando há mais de uma execução em paralelo (bug
    // relatado: com um bash interativo (A) conectado e um comando comum
    // (B) rodando em paralelo, terminar B disparava este handler e
    // desligava a entrada de A por engano, mesmo ele continuando vivo —
    // "volto pro bash, digito qualquer coisa, trava"). Reverifica de
    // verdade em vez de assumir.
    engine::ProcessRunner *stillConnectedRunner = connectedProcessRunner();
    if (!stillConnectedRunner || !stillConnectedRunner->isRunning()) {
        m_terminalDrawer->setInputEnabled(false);
        // Idem para o terminal interativo: o processo acabou, não
        // encaminha mais teclas (evita "digitar no vazio"/reviver um
        // processo morto).
        m_terminalDrawer->setInteractiveAcceptingInput(false);
    }
    // Execução única terminou: não há mais comando ativo no pipeline.
    // Limpa a marcação de "rodando" (fonte de verdade estável do indicador).
    if (!m_connectedTerminalCommandId.isEmpty()) {
        m_pipelineRunningIds.remove(m_connectedTerminalCommandId);
    }
    if (!m_activePipelineCommandId.isEmpty()) {
        m_pipelineRunningIds.remove(m_activePipelineCommandId);
    }
    m_activePipelineCommandId.clear();

    // Indicador visual de execução: comandos de execução única
    // mostram o resultado (sucesso/erro) e permanecem parados/visíveis —
    // não desaparecem automaticamente, cabendo ao usuário fechar o
    // terminal manualmente (botão "Fechar", closeRequested) quando quiser.
    if (result.success) {
        // (removido) linha "[Pipeline concluído com sucesso]": boilerplate.
        // O badge de status no cabeçalho da saída já comunica o resultado, e a
        // mensagem só empurrava a saída real do comando para cima.
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Success);
        m_failedCommandIds.remove(m_connectedTerminalCommandId);

        // Abrir último link impresso (feedback do usuário): a flag
        // openLastLink vale para o comando principal E para seus HOOKS
        // (pre/post) — cada um é um caso de uso válido (ex: um pre-hook que
        // sobe um túnel e imprime a URL). Como cada comando do pipeline tem
        // seu log acumulado em m_commandLogs[<id>], varremos o principal +
        // todos os hooks e abrimos a última URL http(s) de cada um que
        // tenha a flag marcada.
        QStringList idsToCheck;
        idsToCheck << m_connectedTerminalCommandId;
        const auto mainIt = m_commandsById.constFind(m_connectedTerminalCommandId);
        if (mainIt != m_commandsById.constEnd()) {
            idsToCheck << mainIt->hooks.pre << mainIt->hooks.post;
        }
        static const QRegularExpression urlRe(QStringLiteral("https?://[^\\s\"'<>\\])}]+"));
        QSet<QString> alreadyChecked;
        for (const QString &cid : idsToCheck) {
            if (cid.isEmpty() || alreadyChecked.contains(cid)) {
                continue;
            }
            alreadyChecked.insert(cid);
            const auto cmdIt = m_commandsById.constFind(cid);
            if (cmdIt == m_commandsById.constEnd() || !cmdIt->openLastLink) {
                continue;
            }
            const QString log = m_commandLogs.value(cid);
            QString lastUrl;
            auto matches = urlRe.globalMatch(log);
            while (matches.hasNext()) {
                lastUrl = matches.next().captured(0);
            }
            if (!lastUrl.isEmpty()) {
                utils::Logger::info(kLogTag,
                    QStringLiteral("Abrindo último link impresso por '%1': %2").arg(cid, lastUrl));
                m_terminalDrawer->appendRawText(
                    utils::tr(QStringLiteral("mainwindow.opening_link")).arg(lastUrl) + QStringLiteral("\n"), false);
                QDesktopServices::openUrl(QUrl(lastUrl));
            }
        }
    } else {
        m_terminalDrawer->appendRawText(
            // Sem o rótulo "[Pipeline falhou: ...]": o badge já indica erro. Só
            // a mensagem em si é informação útil, e ela vai crua.
            result.errorMessage.trimmed().isEmpty()
                ? QString()
                : QStringLiteral("%1\n").arg(result.errorMessage.trimmed()), true);
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Failed);
        // Botão inline de reset (novo): mantém o comando
        // marcado como "com erro" até uma nova execução ser disparada,
        // permitindo reiniciar com um clique mesmo depois do processo já
        // ter terminado (não só enquanto ainda está rodando).
        if (!m_connectedTerminalCommandId.isEmpty()) {
            m_failedCommandIds.insert(m_connectedTerminalCommandId);
        }
        // Notificação (pedido do usuário): cobre falha manual E de
        // auto-run (mesmo caminho — handleCommandActivated), já que é o
        // cenário mais silencioso (roda sem ninguém olhando).
        {
            const QString failedId = result.failedCommandId.isEmpty()
                ? m_connectedTerminalCommandId : result.failedCommandId;
            const auto failedIt = m_commandsById.constFind(failedId);
            const QString failedName = failedIt != m_commandsById.constEnd() ? failedIt->name : failedId;
            const QString detail = result.errorMessage.trimmed();
            maybeShowNotification(NotificationEvent::CommandFailure,
                utils::tr(QStringLiteral("notification.command_failure.title")),
                detail.isEmpty()
                    ? utils::tr(QStringLiteral("notification.command_failure.body_no_detail")).arg(failedName)
                    : utils::tr(QStringLiteral("notification.command_failure.body")).arg(failedName, detail));
        }
    }
    updateRunningCommandStatus();

    // Botão "Ver JSON" (árvore navegável) SOB DEMANDA no cabeçalho da
    // saída — SÓ para HTTP (pedido do usuário, revertendo uma versão
    // anterior que detectava JSON em QUALQUER log de shell: "quero apenas
    // para cmds http" — a Saída Formatada cobre logs de shell estruturados
    // agora, a detecção por conteúdo virou falso positivo/ruído ali).
    // Passamos o log acumulado do comando; setJsonAvailable detecta se há
    // JSON (e o JsonViewerWidget extrai o bloco balanceado do texto misto),
    // escondendo o botão quando não houver JSON algum. Vale para sucesso e
    // erro (ex: 400 com {"message": "..."}).
    if (m_activePipelineIsHttp && !m_connectedTerminalCommandId.isEmpty()) {
        m_terminalDrawer->setJsonAvailable(m_commandLogs.value(m_connectedTerminalCommandId));
    } else {
        m_terminalDrawer->setJsonAvailable(QString());
    }
    m_activePipelineIsHttp = false;

    // Registra a execução no histórico (feature Runs): comando, tipo,
    // timestamp, duração, sucesso e a saída acumulada (truncada pelo
    // RunHistory). Usa o comando conectado ao terminal (o principal).
    if (!m_connectedTerminalCommandId.isEmpty()) {
        const auto cmdIt = m_commandsById.constFind(m_connectedTerminalCommandId);
        if (cmdIt != m_commandsById.constEnd()) {
            core::RunRecord rec;
            rec.commandId = cmdIt->id;
            rec.commandName = cmdIt->name;
            rec.commandType = (cmdIt->type == core::CommandType::Http)
                ? QStringLiteral("http") : QStringLiteral("shell");
            rec.startedAt = m_runStartedAt.isValid() ? m_runStartedAt : QDateTime::currentDateTime();
            rec.durationMs = rec.startedAt.msecsTo(QDateTime::currentDateTime());
            rec.success = result.success;
            rec.output = m_commandLogs.value(m_connectedTerminalCommandId);
            m_runHistory.append(rec);
        }
    }

    // Mantém a conexão do terminal com este comando após o término
    // 05 — indicadores visuais de execução): o usuário deve ver o
    // resultado (Concluído/Falhou) e decidir quando fechar, em vez do
    // terminal se desconectar sozinho. A próxima execução de qualquer
    // comando substitui a conexão normalmente (runCommandWithParams).
}

engine::ProcessRunner *MainWindow::connectedProcessRunner() const
{
    engine::ProcessRunner *r = nullptr;
    const QString cid = m_connectedTerminalCommandId;
    if (!cid.isEmpty()) {
        r = m_pipeline->runnerFor(cid);
        if (!r) {
            r = m_processManager->runnerFor(cid);
        }
    } else {
        r = m_pipeline->activeProcessRunner();
    }
    return r;
}

void MainWindow::handleTerminalInputEntered(const QString &text)
{
    engine::ProcessRunner *activeRunner = connectedProcessRunner();
    if (!activeRunner || !activeRunner->isRunning()) {
        m_terminalDrawer->appendRawText(
            QStringLiteral("[Nenhum processo ativo para receber entrada neste comando]\n"), true);
        return;
    }
    activeRunner->writeToStdin(text);
}

void MainWindow::handleTerminalInterrupt()
{
    // Ctrl+C: sob PTY/ConPTY, escrever \x03 no mestre faz a disciplina de
    // linha do terminal gerar SIGINT para o processo em foreground — é como
    // um Ctrl+C de verdade. Sem PTY, cai no stop() (terminate->kill do grupo).
    engine::ProcessRunner *r = connectedProcessRunner();
    if (!r || !r->isRunning()) {
        return;
    }
    r->writeRaw(QStringLiteral("\x03"));
}

void MainWindow::handleTerminalEof()
{
    // Ctrl+D: envia \x04 (EOF) ao processo — encerra leituras de stdin (ex:
    // um `cat` sem argumento fecha).
    engine::ProcessRunner *r = connectedProcessRunner();
    if (!r || !r->isRunning()) {
        return;
    }
    r->writeRaw(QStringLiteral("\x04"));
}

void MainWindow::handleTerminalRawInput(const QByteArray &data)
{
    // Bytes gerados pela libvterm a partir de teclado/mouse no terminal
    // interativo (PtyTerminalWidget) — vão DIRETO pro PTY do processo
    // conectado, sem passar pelo campo de stdin de texto (esse é o caminho
    // do terminal "de verdade", não do terminal simples).
    engine::ProcessRunner *r = connectedProcessRunner();
    if (!r || !r->isRunning()) {
        return;
    }
    r->writeRawBytes(data);
}

void MainWindow::handleTerminalSizeChanged(int rows, int cols)
{
    // Widget do terminal interativo mudou de tamanho (em CÉLULAS) — propaga
    // pro PTY do processo conectado (ioctl TIOCSWINSZ/ConPTY), senão apps
    // curses (vim/htop) desenham com o tamanho ERRADO até o próximo resize
    // real da janela.
    engine::ProcessRunner *r = connectedProcessRunner();
    if (!r || !r->isRunning()) {
        return;
    }
    r->resizePty(rows, cols);
}

void MainWindow::handleFirstErrorInFormattedOutput()
{
    // Setting opt-in, default OFF (pedido do usuário: "pode adicionar uma
    // configuração que caso ocorra um erro do tipo ERROR em view formatada
    // ele notifica? só a primeira vez, muitas vezes pode dar spam") — a
    // trava do "só a primeira vez por execução" já foi aplicada em
    // OutputPanel antes de emitir este sinal; maybeShowNotification só
    // decide SE o toggle está ligado e se deve realmente mostrar o toast.
    const auto it = m_commandsById.constFind(m_connectedTerminalCommandId);
    const QString commandName = it != m_commandsById.constEnd() ? it->name : m_connectedTerminalCommandId;
    maybeShowNotification(NotificationEvent::FirstErrorInFormattedOutput,
        utils::tr(QStringLiteral("notification.first_error_in_formatted_output.title")),
        utils::tr(QStringLiteral("notification.first_error_in_formatted_output.body")).arg(commandName));
}

void MainWindow::handleBackgroundProcessStarted(const QString &commandId, engine::ProcessRunner *runner)
{
    Q_UNUSED(runner);

    // Transfere a ownership do ProcessRunner do ExecutionPipeline para o
    // ProcessManager de longa duração: o processo continua
    // vivo mesmo depois que este pipeline específico terminar/for
    // destruído. Chamado sincronamente dentro do slot de
    // backgroundProcessStarted, antes do pipeline seguir adiante.
    std::unique_ptr<engine::ProcessRunner> ownedRunner = m_pipeline->releaseActiveProcessRunner();
    if (!ownedRunner) {
        return;
    }

    const auto it = m_commandsById.constFind(commandId);
    const QString displayName = (it != m_commandsById.constEnd()) ? it.value().name : commandId;
    m_processListDialog->setCommandName(commandId, displayName);

    m_processManager->track(commandId, std::move(ownedRunner));
    m_processListDialog->refreshProcessList();
    updateRunningCommandStatus();

    m_terminalDrawer->appendRawText(
        // Sem anúncio de "processo iniciado": o badge do cabeçalho e a lista de
        // Processos já comunicam isso, e a linha empurrava a saída real.
        QString(), false);

    // Indicador visual de execução: comandos em background
    // continuam sinalizados como tal (não "Concluído"), deixando claro
    // que o processo permanece vivo e pode ser restaurado depois.
    if (m_connectedTerminalCommandId == commandId) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Background);
    }
}

void MainWindow::handleBackgroundProcessOutput(const QString &commandId, const QString &text, bool isError)
{
    m_processListDialog->appendLogFor(commandId, text, isError);
    appendToCommandLog(commandId, text);

    // Janela destacada fixa no comando de origem (ver handlePipelineLog).
    if (m_terminalDrawer->hasDetachedWindow()
        && m_terminalDrawer->detachedCommandId() == commandId) {
        m_terminalDrawer->appendToDetached(text, isError);
    }

    // Terminal por-comando: se o Terminal Drawer principal estiver
    // conectado a este processo em background, ecoa o output ali também
    // (clicar num comando em execução deve permitir acompanhar
    // seu log a qualquer momento, não só via ProcessListDialog).
    if (m_connectedTerminalCommandId == commandId) {
        m_terminalDrawer->appendRawText(text, isError);
        // Terminal interativo: mesmo espírito de handlePipelineLog — só
        // alimenta o vterm se o comando conectado for mesmo interativo.
        const auto it = m_commandsById.constFind(commandId);
        if (it != m_commandsById.constEnd() && it->type == core::CommandType::Shell
            && it->interactiveTerminal) {
            m_terminalDrawer->feedInteractive(text);
        }
    }
}

void MainWindow::handleBackgroundProcessStatusChanged(const QString &commandId, engine::ProcessStatus status)
{
    m_processListDialog->refreshProcessList();

    // CLEANUP HOOKS para comandos de BACKGROUND.
    // Lacuna encontrada: o pipeline retorna cedo para is_background (considera
    // sucesso ao INICIAR e não acompanha o término), então esses comandos nunca
    // chegavam ao abort()/conclusão — e o cleanup NUNCA rodava justamente no
    // caso mais crítico (o CLI que sobe um ambiente Docker roda em background).
    // Aqui o término é real: o processo já morreu quando este status chega.
    if (status != engine::ProcessStatus::Running) {
        const auto it = m_commandsById.constFind(commandId);
        if (it != m_commandsById.constEnd() && !it.value().hooks.cleanup.isEmpty()) {
            m_pipeline->runCleanupHooks(it.value(), m_commandsById);
        }
    }

    // Botão inline de reset (novo): mesmo tratamento de estado
    // de falha aplicado a processos em background.
    if (status == engine::ProcessStatus::Error) {
        m_failedCommandIds.insert(commandId);
    } else if (status == engine::ProcessStatus::Success) {
        m_failedCommandIds.remove(commandId);
    }

    // Notificação (pedido do usuário): processo em background que caiu
    // sozinho (ou, opcionalmente, que terminou bem) sem ninguém olhando.
    if (status == engine::ProcessStatus::Error || status == engine::ProcessStatus::Success) {
        const auto it = m_commandsById.constFind(commandId);
        const QString name = it != m_commandsById.constEnd() ? it->name : commandId;
        if (status == engine::ProcessStatus::Error) {
            const bool crashed = m_processManager->lastRunCrashed(commandId);
            maybeShowNotification(NotificationEvent::BackgroundProcessCrash,
                utils::tr(QStringLiteral("notification.background_crash.title")),
                utils::tr(crashed ? QStringLiteral("notification.background_crash.body_crashed")
                                   : QStringLiteral("notification.background_crash.body_error")).arg(name));
        } else {
            maybeShowNotification(NotificationEvent::BackgroundProcessSuccess,
                utils::tr(QStringLiteral("notification.background_success.title")),
                utils::tr(QStringLiteral("notification.background_success.body")).arg(name),
                QSystemTrayIcon::Information);
        }
    }

    updateRunningCommandStatus();

    // Reflete o status real (Sucesso/Erro) do processo em background no
    // Terminal Drawer, se este for o comando atualmente conectado
    // 05 — indicadores visuais de execução: processos em background devem
    // continuar sinalizando seu estado real mesmo após o término).
    if (m_connectedTerminalCommandId == commandId) {
        switch (status) {
        case engine::ProcessStatus::Running:
            m_terminalDrawer->setExecutionStatus(ExecutionStatus::Background);
            break;
        case engine::ProcessStatus::Success:
            m_terminalDrawer->setExecutionStatus(ExecutionStatus::Success);
            m_terminalDrawer->setInteractiveAcceptingInput(false);
            break;
        case engine::ProcessStatus::Error:
            m_terminalDrawer->setExecutionStatus(ExecutionStatus::Failed);
            m_terminalDrawer->setInteractiveAcceptingInput(false);
            break;
        }
    }
}

void MainWindow::handleShowProcessListRequested()
{
    // ATUALIZA ao reabrir: antes só fazia show/raise, então a janela podia
    // exibir um estado antigo até o próximo sinal chegar.
    m_processListDialog->refreshProcessList();
    m_processListDialog->show();
    m_processListDialog->raise();
    m_processListDialog->activateWindow();
}

void MainWindow::handleKillCommandRequested(const QString &commandId)
{
    // CLEANUP HOOKS: NÃO são disparados aqui de propósito.
    // Antes eram, e havia dois problemas: (a) rodavam ANTES de o processo
    // morrer — o "docker compose down" competia com o ambiente ainda de pé e
    // falhava; (b) o abort() do pipeline dispara os MESMOS hooks quando o
    // processo termina, então rodavam DUAS vezes em paralelo.
    // Agora existe uma fonte única: o término real do processo (abort/finished),
    // que por definição acontece DEPOIS da morte. Ver runCleanupHooks().

    // Controle visual inline de SIGKILL: encerra imediatamente o
    // processo em background associado a este comando. ProcessManager::stop
    // já implementa terminate -> timeout -> kill de forma segura.
    m_processManager->stop(commandId);

    // Encerra o comando de EXECUÇÃO ÚNICA ativo no pipeline (bug reportado:
    // "encerramento parece não funcionar, fica rodando"). Para o runner
    // ativo (que pode ser o do comando principal OU o de um hook em curso)
    // e LIMPA a marcação de rodando (indicador estável) — antes só limpava
    // m_activePipelineCommandId, então a bolinha continuava acesa e o
    // "parar" parecia não ter efeito.
    // Para o runner DESTE comando (registry) — antes parava o "ativo", que
    // podia ser o processo de OUTRO TTY (matava o comando errado).
    if (engine::ProcessRunner *runner = m_pipeline->runnerFor(commandId)) {
        runner->stop();
    }
    m_pipelineRunningIds.remove(commandId);
    if (commandId == m_activePipelineCommandId) {
        m_activePipelineCommandId.clear();
    }
    // Marca como não-falho e atualiza o indicador imediatamente.
    m_failedCommandIds.remove(commandId);
    updateRunningCommandStatus();

    // Stop encerra o processo e desabilita a entrada, mas PRESERVA a Saída
    // já produzida (bug reportado: "para o comando todo e caga a saída, some
    // os logs" — apagar o log de um comando que acabou de ser interrompido
    // destrói justamente a informação que o usuário quer ver: o que rodou
    // até a interrupção). Só mexe se a Saída estiver conectada a este
    // comando específico; o log em si só é limpo no INÍCIO da PRÓXIMA
    // execução (ver runCommandWithParams), nunca aqui.
    if (m_connectedTerminalCommandId == commandId) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Idle);
        m_terminalDrawer->setInputEnabled(false);
        m_terminalDrawer->setInteractiveMode(false);
    }
}

void MainWindow::handleResetCommandRequested(const QString &commandId)
{
    // Botão inline de reset (novo): visível enquanto o comando
    // está em execução ou terminou com erro. Encerra o processo em
    // background se ainda estiver rodando (mesmo caminho seguro de
    // handleKillCommandRequested) e sempre dispara uma nova execução do
    // zero, sem reabrir o formulário de parâmetros — reset é uma ação
    // rápida de "tentar de novo com os mesmos dados", não uma nova
    // configuração manual.
    const auto it = m_commandsById.constFind(commandId);
    if (it == m_commandsById.constEnd()) {
        utils::Logger::warning(kLogTag, QStringLiteral("Comando '%1' não encontrado para reset.").arg(commandId));
        return;
    }

    if (m_processManager->isTracked(commandId)
        && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running) {
        m_processManager->stop(commandId);
    }

    m_failedCommandIds.remove(commandId);
    updateRunningCommandStatus();

    // CLEANUP no reset: NÃO aqui. O reset já aguarda o processo antigo morrer
    // (poll abaixo) e o término dispara o cleanup pela fonte única. Chamar aqui
    // duplicaria e rodaria cedo demais.

    // Reset = "tentar de novo com os MESMOS dados". Relê o comando da FONTE
    // DE VERDADE (m_commandsData) — m_commandsById podia estar defasado, o
    // que fazia o reset rodar SEM os parâmetros (bug reportado: "reset perde
    // referência aos parâmetros"), deixando {{param}} sem resolver.
    core::Command fresh = it.value();
    for (const core::Command &c : m_commandsData.commands) {
        if (c.id == commandId) { fresh = c; break; }
    }
    // Se o comando TEM parâmetros mas não há valores salvos, reabre o
    // formulário em vez de executar com o mapa vazio (que geraria comando
    // inválido com {{param}} literal).
    if (!fresh.params.isEmpty() && fresh.lastParamValues.isEmpty()) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Reset de '%1': sem parâmetros salvos, reabrindo o formulário.").arg(fresh.name));
        handleCommandActivated(commandId);
        return;
    }

    // Se o processo antigo ainda está vivo, ESPERA ele morrer antes de
    // re-executar (antes disparava na hora e as duas instâncias coexistiam,
    // dando "address already in use" em dev servers — achado de auditoria).
    const bool stillAlive = (m_processManager->isTracked(commandId)
            && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running)
        || m_pipeline->runnerFor(commandId) != nullptr;
    if (stillAlive) {
        auto *waitTimer = new QTimer(this);
        waitTimer->setInterval(150);
        int *tries = new int(0);
        connect(waitTimer, &QTimer::timeout, this, [this, commandId, fresh, waitTimer, tries]() {
            const bool alive = (m_processManager->isTracked(commandId)
                    && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running)
                || m_pipeline->runnerFor(commandId) != nullptr;
            if (!alive || ++(*tries) > 40) { // ~6s de teto
                waitTimer->stop();
                waitTimer->deleteLater();
                delete tries;
                runCommandWithParams(fresh, fresh.lastParamValues);
            }
        });
        waitTimer->start();
        return;
    }
    runCommandWithParams(fresh, fresh.lastParamValues);
}

void MainWindow::handleTreeStructureChanged(const QVector<TreeNodePlacement> &placements)
{
    // Persiste ordem + reparenting definidos por drag-and-drop:
    // reordenar e agrupar comando dentro de comando/pasta). Para cada
    // item: comandos atualizam folderId (que pode ser id de pasta OU de
    // outro comando — agrupar comando em comando é permitido); pastas
    // atualizam parentId. Em ambos, o campo order reflete a posição.
    bool changed = false;
    bool collectionsChanged = false;
    for (const TreeNodePlacement &p : placements) {
        if (p.isCommand) {
            for (core::Command &command : m_commandsData.commands) {
                if (command.id == p.id) {
                    if (command.order != p.order || command.folderId != p.parentId) {
                        command.order = p.order;
                        command.folderId = p.parentId;
                        changed = true;
                    }
                    break;
                }
            }
        } else if (p.isCollection) {
            // Coleções guardam order + folderId (como comandos). Sem este
            // ramo, mover uma coleção não persistia (bug reportado: mexi
            // em 'clientes' e "nada a persistir").
            for (core::Collection &collection : m_collections) {
                if (collection.id == p.id) {
                    if (collection.order != p.order || collection.folderId != p.parentId) {
                        collection.order = p.order;
                        collection.folderId = p.parentId;
                        collectionsChanged = true;
                    }
                    break;
                }
            }
        } else {
            for (core::Folder &folder : m_commandsData.folders) {
                if (folder.id == p.id) {
                    const std::optional<QString> newParent =
                        p.parentId.isEmpty() ? std::nullopt : std::make_optional(p.parentId);
                    if (folder.order != p.order || folder.parentId != newParent) {
                        folder.order = p.order;
                        folder.parentId = newParent;
                        changed = true;
                    }
                    break;
                }
            }
        }
    }

    if (changed || collectionsChanged) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Drag&drop persistido: %1 item(ns) processados (comandos/pastas: %2, coleções: %3).")
                .arg(placements.size())
                .arg(changed ? QStringLiteral("sim") : QStringLiteral("nao"))
                .arg(collectionsChanged ? QStringLiteral("sim") : QStringLiteral("nao")));
        // Coleções vivem em collections.json; comandos/pastas em
        // commands.json. Persiste o que mudou. Ambos disparam
        // reloadCommandTree — chama só um se possível para evitar rebuild
        // duplo, mas prioriza persistir os dois arquivos corretamente.
        if (changed && collectionsChanged) {
            if (!m_configManager.saveCommands(m_commandsData)) {
                QMessageBox::warning(this, QStringLiteral("Kai"),
                    utils::tr(QStringLiteral("mainwindow.error.save_commands")));
            }
            persistCollections(); // salva coleções + reloadCommandTree
        } else if (changed) {
            persistCommands();
        } else {
            persistCollections();
        }
    } else {
        utils::Logger::debug(kLogTag,
            QStringLiteral("Drag&drop: nenhuma mudança efetiva de order/hierarquia (nada a persistir)."));
    }
}

// ENTRADA ÚNICA de importação (pedido do usuário: "só dois botões... um
// jeito simplificado e mais fácil, porém completo, de importar, com
// apenas um form") — substitui os 3 itens de menu antigos
// (importProjectRequested/importOpenApiRequested/importConfigRequested).
// ImportDialog só decide a FONTE (pasta de projeto vs arquivo) e, pra
// arquivo, o TIPO exato por conteúdo; a partir daí delega pro fluxo
// específico de sempre (mesma lógica, só a entrada mudou).
void MainWindow::handleImportRequested()
{
    ImportDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    switch (dialog.kind()) {
    case ImportDialog::Kind::Project:
        importProjectFromDirectory(dialog.path());
        break;
    case ImportDialog::Kind::OpenApi:
        importOpenApiFromFile(dialog.path());
        break;
    case ImportDialog::Kind::Config:
        importConfigFromFile(dialog.path());
        break;
    }
}

void MainWindow::importProjectFromDirectory(const QString &directory)
{
    // Confere/edita o path e escolhe o formato salvo como PROJECT_PATH
    // (pedido do usuário: sob WSL/WSLg o seletor nativo às vezes devolve um
    // path Windows mesmo para um projeto que roda dentro do WSL — ver
    // ProjectImportOptionsDialog).
    ProjectImportOptionsDialog optionsDialog(directory, m_commandsData.folders, this);
    if (optionsDialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString confirmedDirectory = optionsDialog.directory();
    if (confirmedDirectory.isEmpty()) {
        return;
    }

    // Feedback visual: mostra o overlay enquanto lê/parseia o kai.json.
    LoadingScope loading(m_loadingOverlay, utils::tr(QStringLiteral("mainwindow.import_project.loading")));
    ProjectImportResult importResult =
        m_projectSelector->importFromDirectory(confirmedDirectory, optionsDialog.projectPath(),
                                                optionsDialog.detectGenericDefinitions());
    if (!importResult.success) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("mainwindow.import_project.title")), importResult.errorMessage);
        return;
    }
    if (!importResult.detectedEcosystems.isEmpty()) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Importação genérica detectou: %1").arg(importResult.detectedEcosystems.join(QStringLiteral(", "))));
    }

    // PASTA DE DESTINO (feedback do usuário): importa a pasta do projeto já
    // como subpasta de uma pasta existente no Kai, em vez de sempre criar
    // na raiz — evita ter que arrastar depois. Vazio (padrão) = raiz,
    // comportamento antigo inalterado.
    const QString parentFolderId = optionsDialog.parentFolderId();
    if (!parentFolderId.isEmpty()) {
        importResult.folder.parentId = parentFolderId;
    }

    // ID ÚNICO da pasta-raiz importada (bug relatado: 2 pastas com o MESMO
    // NOME geravam o MESMO id, colidindo em qualquer lookup por id e
    // fazendo uma "carregar" o conteúdo da outra — ver uniqueFolderId).
    // Reimportar um projeto cujo nome já existe como pasta é exatamente
    // esse caso. Subpastas/comandos referenciam o id ANTIGO da raiz
    // (gerado dentro de ProjectSelector), então o troco é remapeado aqui.
    const QString oldRootId = importResult.folder.id;
    const QString newRootId = uniqueFolderId(oldRootId);
    if (newRootId != oldRootId) {
        importResult.folder.id = newRootId;
        for (core::Folder &sub : importResult.subFolders) {
            if (sub.parentId == oldRootId) {
                sub.parentId = newRootId;
            }
        }
        for (core::Command &command : importResult.commands) {
            if (command.folderId == oldRootId) {
                command.folderId = newRootId;
            }
        }
    }

    m_commandsData.folders << importResult.folder;
    // Subpastas do projeto (feedback do usuário: organizar em várias pastas
    // — suporte base a pastas em projetos). Cada "folder" declarado no
    // kai.json vira uma subpasta sob a pasta raiz do projeto.
    for (const core::Folder &sub : importResult.subFolders) {
        m_commandsData.folders << sub;
    }
    for (const core::Command &command : importResult.commands) {
        m_commandsData.commands << command;
    }

    if (!m_configManager.saveCommands(m_commandsData)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("mainwindow.import_project.title")),
            utils::tr(QStringLiteral("mainwindow.import_project.save_failed")));
    }

    // Coleções versionadas no kai.json do projeto (fonte de dados por
    // projeto): adiciona ao conjunto e persiste no collections.json.
    if (!importResult.collections.isEmpty()) {
        for (const core::Collection &collection : importResult.collections) {
            m_collections << collection;
        }
        persistCollections();
    }

    reloadCommandTree();
}

void MainWindow::importConfigFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("importcfg.title")),
            utils::tr(QStringLiteral("importcfg.failed")).arg(file.errorString()));
        return;
    }
    QString jsonText = QString::fromUtf8(file.readAll());
    file.close();

    // Suporte a YAML no import: se o arquivo escolhido é .yml/.yaml (ou, por
    // segurança, se o conteúdo simplesmente não parece JSON), converte para
    // JSON antes de entregar pro ConfigManager — que continua operando só
    // com JSON internamente.
    const QString lowerPath = path.toLower();
    const bool isYamlExtension = lowerPath.endsWith(QStringLiteral(".yml"))
        || lowerPath.endsWith(QStringLiteral(".yaml"));
    if (isYamlExtension || !core::looksLikeJson(jsonText)) {
        bool yamlOk = false;
        QString yamlError;
        const QString converted = core::yamlTextToJsonText(jsonText, &yamlOk, &yamlError);
        if (!yamlOk) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("importcfg.title")),
                utils::tr(QStringLiteral("importcfg.failed")).arg(yamlError));
            return;
        }
        jsonText = converted;
    }

    const core::ConfigManager::ImportResult result = core::ConfigManager::importFromJson(jsonText);
    if (!result.ok) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("importcfg.title")),
            utils::tr(QStringLiteral("importcfg.failed")).arg(result.errorMessage));
        return;
    }

    // IMPORTAÇÃO SELETIVA: pergunta o que trazer, mostrando só as categorias
    // que o arquivo realmente contém. Pedido do usuário: "quero pra
    // importação o mesmo cenário [do export v2] (vou importar um projeto, e
    // o kai detecta me pede se quero importar coleções, perfis, comandos,
    // pastas e etc.)".
    ImportSelectionDialog selectionDialog(result, this);
    if (selectionDialog.exec() != QDialog::Accepted) {
        return;
    }
    const ImportSelectionDialog::Selection sel = selectionDialog.selection();

    // Merge por id: substitui itens existentes de mesmo id, adiciona os
    // novos (evita duplicatas ao reimportar). Import global também mescla
    // settings (env vars e terminal targets), sem sobrescrever tudo cegamente.
    int foldersImported = 0;
    int commandsImported = 0;
    if (sel.commands) {
        for (const core::Folder &f : result.folders) {
            bool replaced = false;
            for (core::Folder &existing : m_commandsData.folders) {
                if (existing.id == f.id) { existing = f; replaced = true; break; }
            }
            if (!replaced) {
                m_commandsData.folders.append(f);
            }
        }
        foldersImported = result.folders.size();
        for (const core::Command &c : result.commands) {
            bool replaced = false;
            for (core::Command &existing : m_commandsData.commands) {
                if (existing.id == c.id) { existing = c; replaced = true; break; }
            }
            if (!replaced) {
                m_commandsData.commands.append(c);
            }
        }
        commandsImported = result.commands.size();
    }

    // COLEÇÕES: mesmo merge por id. A importação antiga ignorava coleções, então
    // um pacote exportado "completo" não voltava completo.
    if (result.hasCollections && sel.collections) {
        for (const core::Collection &col : result.collections) {
            bool replaced = false;
            for (core::Collection &existing : m_collections) {
                if (existing.id == col.id) {
                    // Se o pacote veio SEM registros (export só da estrutura),
                    // preserva os dados locais e atualiza apenas a estrutura —
                    // importar a estrutura não deve apagar os dados do usuário.
                    if (col.entries.isEmpty() && !existing.entries.isEmpty()) {
                        core::Collection merged = col;
                        merged.entries = existing.entries;
                        existing = merged;
                    } else {
                        existing = col;
                    }
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                m_collections.append(col);
            }
        }
        persistCollections();
    }

    // ALVOS DE TERMINAL: TerminalProfile não tem id, só `name` — então "mesmo
    // item" é decidido por CONTEÚDO (template/shell/pty/ícone), não pelo
    // nome. Pedido do usuário: "exportando alvos de terminais, que terão que
    // vir com nomes autogerados + nome original para permitir conflitos, se
    // a config for igual a um dos atuais, não precisa trazer outro."
    //   - conteúdo idêntico a um alvo já existente -> não duplica, apenas
    //     remapeia referências (terminalTarget) pro nome já existente.
    //   - nome em conflito mas conteúdo DIFERENTE -> importa com um nome
    //     novo gerado automaticamente, preservando o alvo local intacto.
    //   - sem conflito -> importa como veio.
    // O remapeamento é necessário para comandos/pastas importados NO MESMO
    // LOTE que apontavam pro nome antigo (terminalTarget é uma string solta,
    // não uma referência por id).
    QMap<QString, QString> terminalTargetRename;
    if (result.hasTerminalProfiles && sel.terminalProfiles) {
        core::SettingsData settings = m_configManager.loadSettings();
        auto sameContent = [](const core::TerminalProfile &a, const core::TerminalProfile &b) {
            return a.commandTemplate == b.commandTemplate && a.shell == b.shell
                && a.usePty == b.usePty && a.icon == b.icon;
        };
        QSet<QString> usedNames;
        for (const core::TerminalProfile &existing : settings.terminalProfiles) {
            usedNames.insert(existing.name);
        }
        for (const core::TerminalProfile &incoming : result.settings.terminalProfiles) {
            const core::TerminalProfile *identical = nullptr;
            for (const core::TerminalProfile &existing : settings.terminalProfiles) {
                if (sameContent(existing, incoming)) { identical = &existing; break; }
            }
            if (identical) {
                if (identical->name != incoming.name) {
                    terminalTargetRename[incoming.name] = identical->name;
                }
                continue;
            }
            const bool nameConflict = usedNames.contains(incoming.name);
            core::TerminalProfile toAdd = incoming;
            if (nameConflict) {
                QString candidate = utils::tr(QStringLiteral("importcfg.terminal_target.renamed"))
                    .arg(incoming.name);
                int suffix = 2;
                while (usedNames.contains(candidate)) {
                    candidate = utils::tr(QStringLiteral("importcfg.terminal_target.renamed_n"))
                        .arg(incoming.name).arg(suffix++);
                }
                toAdd.name = candidate;
                toAdd.isDefault = false; // não rouba o alvo padrão local ao renomear
                terminalTargetRename[incoming.name] = candidate;
            }
            usedNames.insert(toAdd.name);
            settings.terminalProfiles.append(toAdd);
        }
        m_configManager.saveSettings(settings);
    }

    if (!terminalTargetRename.isEmpty() && sel.commands) {
        // Reescreve terminalTarget SÓ nos itens que vieram DESTE import (por
        // id), nunca nos que já existiam localmente — bug de revisão: antes
        // isto rodava sobre m_commandsData.folders/.commands inteiro (local +
        // importado), então um comando local que já usava "MyTerm" era
        // silenciosamente desviado pro perfil importado "MyTerm (importado)"
        // assim que houvesse um conflito de nome.
        QSet<QString> importedFolderIds;
        QSet<QString> importedCommandIds;
        for (const core::Folder &f : result.folders) importedFolderIds.insert(f.id);
        for (const core::Command &c : result.commands) importedCommandIds.insert(c.id);
        for (core::Folder &f : m_commandsData.folders) {
            if (!importedFolderIds.contains(f.id)) continue;
            if (terminalTargetRename.contains(f.terminalTarget)) {
                f.terminalTarget = terminalTargetRename.value(f.terminalTarget);
            }
        }
        for (core::Command &c : m_commandsData.commands) {
            if (!importedCommandIds.contains(c.id)) continue;
            if (terminalTargetRename.contains(c.terminalTarget)) {
                c.terminalTarget = terminalTargetRename.value(c.terminalTarget);
            }
        }
    }

    if ((result.hasSettings && sel.settings) || (result.hasEnvironments && sel.environments)) {
        core::SettingsData settings = m_configManager.loadSettings();
        if (sel.settings) {
            for (auto it = result.settings.globalEnvVars.constBegin();
                 it != result.settings.globalEnvVars.constEnd(); ++it) {
                settings.globalEnvVars[it.key()] = it.value();
            }
        }
        // ENVIRONMENTS: bug de revisão — o checkbox existia no diálogo mas
        // nada consumia `sel.environments`; os pacotes de ambiente do
        // arquivo nunca eram de fato mesclados. Merge por id, mesma
        // convenção usada pra pastas/comandos/coleções.
        if (sel.environments) {
            for (const core::Environment &e : result.settings.environments) {
                bool replaced = false;
                for (core::Environment &existing : settings.environments) {
                    if (existing.id == e.id) { existing = e; replaced = true; break; }
                }
                if (!replaced) {
                    settings.environments.append(e);
                }
            }
            if (!result.settings.activeEnvironmentId.isEmpty()) {
                settings.activeEnvironmentId = result.settings.activeEnvironmentId;
            }
        }
        m_configManager.saveSettings(settings);
        m_envManager.setGlobalVars(settings.globalEnvVars);
    }

    persistCommands();

    QMessageBox::information(this, utils::tr(QStringLiteral("importcfg.title")),
        utils::tr(QStringLiteral("importcfg.done"))
            .arg(foldersImported).arg(commandsImported));
}

void MainWindow::importOpenApiFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("openapi.title")),
            utils::tr(QStringLiteral("openapi.read_failed")));
        return;
    }
    const QString jsonText = QString::fromUtf8(file.readAll());
    file.close();

    const core::OpenApiParseResult parsed = core::parseOpenApi(jsonText);
    if (!parsed.ok) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("openapi.title")),
            utils::tr(QStringLiteral("openapi.failed")).arg(parsed.errorMessage));
        return;
    }

    // Cria uma pasta com o título da API e um comando HTTP por endpoint.
    core::Folder folder;
    folder.name = parsed.apiTitle;
    folder.id = uniqueFolderId(FolderEditorDialog::generateFolderId(parsed.apiTitle));
    m_commandsData.folders.append(folder);

    QSet<QString> usedIds;
    for (const core::Command &c : m_commandsData.commands) {
        usedIds.insert(c.id);
    }

    int created = 0;
    for (const core::OpenApiEndpoint &ep : parsed.endpoints) {
        core::Command cmd;
        cmd.name = ep.name;
        cmd.folderId = folder.id;
        cmd.type = core::CommandType::Http;
        cmd.httpConfig = ep.config;
        QString id = CommandEditorDialog::generateCommandId(folder.id, ep.name);
        QString baseId = id;
        int suffix = 2;
        while (usedIds.contains(id)) {
            id = baseId + QStringLiteral("_%1").arg(suffix++);
        }
        cmd.id = id;
        usedIds.insert(id);
        m_commandsData.commands.append(cmd);
        ++created;
    }

    persistCommands();
    reloadCommandTree();

    QMessageBox::information(this, utils::tr(QStringLiteral("openapi.title")),
        utils::tr(QStringLiteral("openapi.done")).arg(created).arg(parsed.apiTitle));
}

void MainWindow::handleExportRequested()
{
    // MENU ÚNICO (pedido do usuário: "queria um menu unificado para
    // exportação, não 3 (ele deixa eu escolher o modo)") — Global sempre
    // disponível; Pasta/Comando usam um SELETOR GLOBAL sobre TODAS as
    // pastas/comandos do app (pedido do usuário, com foto: "esse setor de
    // pastas é meio ruim, deveria ser o seletor global disponível no
    // sistema com um todo" — antes só oferecia a pasta/comando que já
    // estivesse selecionado na árvore no momento de abrir a tela).
    QVector<ExportDialog::TargetChoice> folderChoices;
    for (const core::Folder &f : foldersInTreeOrder(m_commandsData.folders)) {
        folderChoices << ExportDialog::TargetChoice{f.id, folderComboLabel(m_commandsData.folders, f.id)};
    }

    // Comandos: mesma ideia de folderComboLabel, mas pra Command — nome +
    // caminho da pasta como hint entre parênteses (vazio pra comando na
    // raiz, sem ancestral nenhum).
    QVector<ExportDialog::TargetChoice> commandChoices;
    for (const core::Command &c : m_commandsData.commands) {
        const QStringList pathSegments = folderPathSegments(m_commandsData.folders, c.folderId);
        const QString label = pathSegments.isEmpty()
            ? c.name
            : QStringLiteral("%1   (%2)").arg(c.name, pathSegments.join(QStringLiteral(" / ")));
        commandChoices << ExportDialog::TargetChoice{c.id, label};
    }

    const QVector<core::TerminalProfile> availableProfiles = m_configManager.loadSettings().terminalProfiles;
    ExportDialog dlg(folderChoices, commandChoices, availableProfiles,
        [this](const QString &folderId) { return linkedCollectionsForFolder(folderId); },
        [this](const QString &commandId) { return linkedCollectionsForCommand(commandId); },
        this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QString format = dlg.selectedFormat();
    const QString filter = format == QStringLiteral("yml")
        ? utils::tr(QStringLiteral("importcfg.filter.yaml_only"))
        : utils::tr(QStringLiteral("importcfg.filter.json_only"));

    switch (dlg.selectedScope()) {
    case ExportDialog::Scope::Global: {
        const QString suggestedName = QStringLiteral("kai-config.%1").arg(format);
        const QString path = QFileDialog::getSaveFileName(this,
            utils::tr(QStringLiteral("export.dialog_title")), suggestedName, filter);
        if (path.isEmpty()) {
            return;
        }
        const QString json = core::ConfigManager::exportSelective(
            dlg.globalSelection(), m_configManager.loadSettings(), m_commandsData, m_collections,
            dlg.leanExport());
        writeExportFile(path, json);
        break;
    }
    case ExportDialog::Scope::Folder: {
        const QString suggestedName = QStringLiteral("kai-folder.%1").arg(format);
        const QString path = QFileDialog::getSaveFileName(this,
            utils::tr(QStringLiteral("export.dialog_title")), suggestedName, filter);
        if (path.isEmpty()) {
            return;
        }
        writeExportFile(path, core::ConfigManager::exportFolder(
            dlg.selectedTargetId(), m_commandsData, dlg.selectedCollections(), dlg.selectedTerminalProfiles(),
            dlg.leanExport()));
        break;
    }
    case ExportDialog::Scope::Command: {
        const QString suggestedName = QStringLiteral("kai-command.%1").arg(format);
        const QString path = QFileDialog::getSaveFileName(this,
            utils::tr(QStringLiteral("export.dialog_title")), suggestedName, filter);
        if (path.isEmpty()) {
            return;
        }
        writeExportFile(path, core::ConfigManager::exportCommand(
            dlg.selectedTargetId(), m_commandsData, dlg.selectedCollections(), dlg.selectedTerminalProfiles(),
            dlg.leanExport()));
        break;
    }
    }
}

void MainWindow::writeExportFile(const QString &path, const QString &content)
{
    // Suporte a YAML no export: se o usuário escolheu salvar como .yml/.yaml,
    // converte o JSON já gerado pelo ConfigManager para YAML antes de
    // escrever — o ConfigManager continua produzindo só JSON internamente.
    QString finalContent = content;
    const QString lowerPath = path.toLower();
    if (lowerPath.endsWith(QStringLiteral(".yml")) || lowerPath.endsWith(QStringLiteral(".yaml"))) {
        const QString yaml = core::jsonTextToYamlText(content);
        if (!yaml.isEmpty()) {
            finalContent = yaml;
        }
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("export.title")),
            utils::tr(QStringLiteral("export.save_failed")));
        return;
    }
    file.write(finalContent.toUtf8());
    if (!file.commit()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("export.title")),
            utils::tr(QStringLiteral("export.save_failed")));
        return;
    }
    QMessageBox::information(this, utils::tr(QStringLiteral("export.title")),
        utils::tr(QStringLiteral("export.saved")).arg(path));
}

void MainWindow::persistCommands()
{
    if (!m_configManager.saveCommands(m_commandsData)) {
        QMessageBox::warning(this, QStringLiteral("Kai"),
            utils::tr(QStringLiteral("mainwindow.error.persist_commands")));
    }
    reloadCommandTree();
}

QVector<core::Command> MainWindow::commandsInFolder(const QString &folderId) const
{
    // Comandos elegíveis como hook para um comando desta pasta: os da
    // PRÓPRIA pasta MAIS os das pastas ANCESTRAIS (pai, avô, ...) — feedback
    // do usuário: um hook definido numa pasta pai deve estar disponível nas
    // filhas (comandos utilitários reutilizáveis descem na hierarquia).
    // Monta o conjunto de ids da cadeia de ancestrais subindo por parentId.
    QSet<QString> chainFolderIds;
    chainFolderIds.insert(folderId);
    QMap<QString, core::Folder> foldersById;
    for (const core::Folder &f : m_commandsData.folders) {
        foldersById.insert(f.id, f);
    }
    QString current = folderId;
    QSet<QString> guard;
    while (foldersById.contains(current) && !guard.contains(current)) {
        guard.insert(current);
        const core::Folder &f = foldersById.value(current);
        if (!f.parentId.has_value() || f.parentId->isEmpty()) {
            break;
        }
        current = f.parentId.value();
        chainFolderIds.insert(current);
    }

    QVector<core::Command> result;
    for (const core::Command &command : m_commandsData.commands) {
        if (chainFolderIds.contains(command.folderId)) {
            result << command;
        }
    }
    return result;
}

bool MainWindow::hasDuplicateCommandName(const QString &folderId, const QString &name, const QString &excludeId) const
{
    // Lógica pura em name-uniqueness.h (testável sem instanciar MainWindow).
    return commandNameCollides(m_commandsData.commands, folderId, name, excludeId);
}

bool MainWindow::hasDuplicateCollectionName(const QString &folderId, const QString &name, const QString &excludeId) const
{
    return collectionNameCollides(m_collections, folderId, name, excludeId);
}

QString MainWindow::resolveTargetFolderId() const
{
    // Prioriza a seleção explícita na árvore (feedback do
    // usuário: pré-preencher a pasta com base na pasta/aba selecionada).
    // Se o item selecionado for uma pasta, usa-a diretamente; se for um
    // comando, usa a pasta a que ele pertence. Sem seleção na árvore,
    // cai para a pasta raiz da aba atualmente ativa. Só na ausência de
    // qualquer aba (nenhuma pasta cadastrada ainda) retorna vazio.
    const QString selectedId = m_commandTree->currentSelectionId();
    if (!selectedId.isEmpty()) {
        if (m_commandTree->currentSelectionIsFolder()) {
            return selectedId;
        }
        const auto it = m_commandsById.constFind(selectedId);
        if (it != m_commandsById.constEnd()) {
            return it.value().folderId;
        }
    }

    const QString activeRootId = m_commandTree->currentRootFolderId();
    if (!activeRootId.isEmpty()) {
        return activeRootId;
    }

    if (!m_commandsData.folders.isEmpty()) {
        return m_commandsData.folders.constFirst().id;
    }
    return QString();
}

QString MainWindow::uniqueFolderId(const QString &candidate) const
{
    auto idExists = [this](const QString &id) {
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.id == id) return true;
        }
        return false;
    };
    if (!idExists(candidate)) {
        return candidate;
    }
    int n = 2;
    QString unique = QStringLiteral("%1_%2").arg(candidate).arg(n);
    while (idExists(unique)) {
        ++n;
        unique = QStringLiteral("%1_%2").arg(candidate).arg(n);
    }
    utils::Logger::warning(kLogTag,
        QStringLiteral("ID de pasta '%1' já existe; usando '%2' para evitar colisão.")
            .arg(candidate, unique));
    return unique;
}

QVector<QString> MainWindow::folderSubtreeIds(const QString &rootFolderId) const
{
    QVector<QString> ids = {rootFolderId};
    QVector<QString> queue = {rootFolderId};
    while (!queue.isEmpty()) {
        const QString currentId = queue.takeFirst();
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.parentId.has_value() && f.parentId.value() == currentId && !ids.contains(f.id)) {
                ids << f.id;
                queue << f.id;
            }
        }
    }
    return ids;
}

QVector<core::Collection> MainWindow::linkedCollectionsForFolder(const QString &folderId) const
{
    const QVector<QString> subtreeIds = folderSubtreeIds(folderId);
    QSet<QString> collectionIds;
    QVector<core::Collection> result;
    auto addIfNew = [&](const core::Collection &col) {
        if (!collectionIds.contains(col.id)) {
            collectionIds.insert(col.id);
            result.append(col);
        }
    };
    // (1) Fisicamente guardada dentro da subárvore.
    for (const core::Collection &col : m_collections) {
        if (subtreeIds.contains(col.folderId)) {
            addIfNew(col);
        }
    }
    // (2) Referenciada por um parâmetro Select de algum comando da subárvore.
    for (const core::Command &c : m_commandsData.commands) {
        if (!subtreeIds.contains(c.folderId)) {
            continue;
        }
        for (const core::Parameter &p : c.params) {
            if (p.collectionId.isEmpty()) {
                continue;
            }
            const auto it = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
                [&p](const core::Collection &col) { return col.id == p.collectionId; });
            if (it != m_collections.constEnd()) {
                addIfNew(*it);
            }
        }
    }
    return result;
}

QVector<core::Collection> MainWindow::linkedCollectionsForCommand(const QString &commandId) const
{
    QSet<QString> collectionIds;
    QVector<core::Collection> result;
    auto addIfNew = [&](const core::Collection &col) {
        if (!collectionIds.contains(col.id)) {
            collectionIds.insert(col.id);
            result.append(col);
        }
    };
    const auto cmdIt = m_commandsById.constFind(commandId);
    if (cmdIt == m_commandsById.constEnd()) {
        return result;
    }
    // (1) Fisicamente guardada na MESMA pasta direta do comando (um
    // comando não tem "descendentes", então não há subárvore aqui).
    for (const core::Collection &col : m_collections) {
        if (col.folderId == cmdIt->folderId) {
            addIfNew(col);
        }
    }
    // (2) Referenciada por um parâmetro Select do próprio comando.
    for (const core::Parameter &p : cmdIt->params) {
        if (p.collectionId.isEmpty()) {
            continue;
        }
        const auto it = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
            [&p](const core::Collection &col) { return col.id == p.collectionId; });
        if (it != m_collections.constEnd()) {
            addIfNew(*it);
        }
    }
    return result;
}

void MainWindow::handleNewFolderRequested()
{
    // Pré-preenche a pasta pai sugerida com base na seleção/aba atual
    // (feedback do usuário), sem forçar: o usuário pode trocar
    // livremente antes de salvar.
    const QString suggestedParentId = resolveTargetFolderId();
    FolderEditorDialog dialog(m_commandsData.folders, this, nullptr, suggestedParentId,
                              m_configManager.loadSettings().terminalProfiles);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    core::Folder folder = dialog.buildFolder();
    if (folder.name.isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("folder.title.new")), utils::tr(QStringLiteral("mainwindow.folder.name_required")));
        return;
    }
    folder.id = uniqueFolderId(folder.id);

    m_commandsData.folders << folder;
    persistCommands();
}

void MainWindow::handleNewCommandRequested()
{
    QString targetFolderId = resolveTargetFolderId();

    if (targetFolderId.isEmpty()) {
        // Fluxo integrado: em vez de apenas informar e obrigar o usuário a
        // cancelar e clicar em "Nova Pasta" manualmente, oferece criar a
        // pasta necessária no mesmo fluxo (lacuna de UX).
        if (!confirmYesNo(this, utils::tr(QStringLiteral("mainwindow.no_folder.title")),
                          utils::tr(QStringLiteral("mainwindow.no_folder.body")), /*defaultToYes=*/true)) {
            return;
        }

        FolderEditorDialog folderDialog(m_commandsData.folders, this, nullptr, QString(),
                                        m_configManager.loadSettings().terminalProfiles);
        if (folderDialog.exec() != QDialog::Accepted) {
            return;
        }

        core::Folder newFolder = folderDialog.buildFolder();
        if (newFolder.name.isEmpty()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("folder.title.new")), utils::tr(QStringLiteral("mainwindow.folder.name_required")));
            return;
        }
        newFolder.id = uniqueFolderId(newFolder.id);

        m_commandsData.folders << newFolder;
        persistCommands();
        targetFolderId = newFolder.id;
    }

    // Criação de comando com SWAP DINÂMICO entre modo simples (formulário)
    // e avançado (JSON cru): o botão "Modo avançado"/"Modo simples" apenas
    // ALTERNA o editor preservando o conteúdo — NÃO valida nem salva (bug
    // reportado: ao trocar de componente na criação, o sistema dava erro de
    // validação e tentava salvar). O laço abaixo reabre o editor no modo
    // pedido, carregando o comando parcial construído no modo anterior.
    const core::SettingsData settings = m_configManager.loadSettings();
    core::Command command;
    bool advanced = (settings.commandCreationMode == QStringLiteral("advanced"));
    bool hasDraft = false;      // já temos um rascunho para transportar entre modos?
    core::Command draft;        // rascunho preservado no swap
    bool committed = false;     // usuário confirmou (OK), não apenas trocou de modo

    while (true) {
        if (advanced) {
            // No modo avançado: se já há rascunho, edita-o; senão template.
            CommandJsonEditorDialog dialog(hasDraft ? &draft : nullptr, targetFolderId, this);
            if (dialog.exec() != QDialog::Accepted) {
                return; // cancelou
            }
            draft = dialog.buildCommand();
            hasDraft = true;
            // Nome duplicado na mesma pasta: barra o salvamento e reabre o
            // editor com o rascunho, no mesmo modo, para o usuário renomear
            // (mesmo tratamento do "nome obrigatório", spec do usuário).
            if (hasDuplicateCommandName(draft.folderId, draft.name, QString())) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.duplicate_name.title")),
                    utils::tr(QStringLiteral("command.error.duplicate_name.body")));
                continue;
            }
            if (dialog.switchToSimpleRequested()) {
                advanced = false;
                continue; // reabre no formulário com o rascunho
            }
            command = draft;
            committed = true;
            break;
        } else {
            CommandEditorDialog dialog(targetFolderId, commandsInFolder(targetFolderId),
                                       m_commandsData.folders, this,
                                       hasDraft ? &draft : nullptr, settings.terminalProfiles,
                                       m_commandsData.commands);
            dialog.setAvailableCollections(m_collections);
            dialog.setAvailableDynamicVarNames(availableDynamicVarNames());
            if (dialog.exec() != QDialog::Accepted) {
                return; // cancelou
            }
            draft = dialog.buildCommand();
            hasDraft = true;
            if (hasDuplicateCommandName(draft.folderId, draft.name, QString())) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.duplicate_name.title")),
                    utils::tr(QStringLiteral("command.error.duplicate_name.body")));
                continue;
            }
            if (dialog.switchToAdvancedRequested()) {
                advanced = true;
                continue; // reabre no JSON com o rascunho (sem validar/salvar)
            }
            command = draft;
            committed = true;
            break;
        }
    }

    if (!committed) {
        return;
    }

    if (command.name.isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("command.title.new")), utils::tr(QStringLiteral("mainwindow.command.name_required")));
        return;
    }

    // Garante ID ÚNICO (bug real corrigido: dois comandos com o mesmo nome
    // na mesma pasta geravam o MESMO id via generateCommandId, colidindo em
    // m_commandsById — a última versão sobrescrevia a outra, fazendo hooks
    // "sumirem" no pipeline). Se o id já existe, anexa um sufixo numérico.
    {
        auto idExists = [this](const QString &candidate) {
            for (const core::Command &c : m_commandsData.commands) {
                if (c.id == candidate) return true;
            }
            return false;
        };
        if (idExists(command.id)) {
            const QString base = command.id;
            int n = 2;
            QString unique = QStringLiteral("%1_%2").arg(base).arg(n);
            while (idExists(unique)) {
                ++n;
                unique = QStringLiteral("%1_%2").arg(base).arg(n);
            }
            utils::Logger::warning(kLogTag,
                QStringLiteral("ID de comando '%1' já existe; usando '%2' para evitar colisão.")
                    .arg(base, unique));
            command.id = unique;
        }
    }

    m_commandsData.commands << command;
    persistCommands();
}

void MainWindow::handleEditRequested(const QString &itemId, bool isFolder)
{
    if (isFolder) {
        // Pasta PADRÃO "Geral" (sintética): não há o que editar (nome/pai/
        // env). Editá-la = oferecer a REMOÇÃO dos órfãos que ela contém
        // (feedback do usuário: "tem que adicionar a opção de remover a
        // pasta"). Roteia direto para o fluxo de exclusão, que trata o caso.
        if (itemId == CommandTreeWidget::defaultFolderId()) {
            handleDeleteRequested(itemId, true);
            return;
        }
        int folderIndex = -1;
        for (int i = 0; i < m_commandsData.folders.size(); ++i) {
            if (m_commandsData.folders.at(i).id == itemId) {
                folderIndex = i;
                break;
            }
        }
        if (folderIndex < 0) {
            return;
        }

        const core::Folder existing = m_commandsData.folders.at(folderIndex);
        FolderEditorDialog dialog(m_commandsData.folders, this, &existing, QString(),
                                  m_configManager.loadSettings().terminalProfiles);
        const int rc = dialog.exec();

        // Exclusão pedida via botão "Excluir Pasta" do próprio diálogo
        // (feedback do usuário): abre o diálogo de flags (apagar filhos por
        // tipo) — mesmo fluxo do menu contextual, para consistência.
        if (dialog.deleteWasRequested()) {
            deleteFolderWithDialog(itemId);
            persistCommands();
            return;
        }

        if (rc != QDialog::Accepted) {
            return;
        }

        m_commandsData.folders[folderIndex] = dialog.buildFolder();
        persistCommands();
        return;
    }

    int commandIndex = -1;
    for (int i = 0; i < m_commandsData.commands.size(); ++i) {
        if (m_commandsData.commands.at(i).id == itemId) {
            commandIndex = i;
            break;
        }
    }
    if (commandIndex < 0) {
        return;
    }

    const core::Command existing = m_commandsData.commands.at(commandIndex);

    // Modo de edição: "advanced" abre o editor de JSON cru;
    // "standard" (padrão) usa o formulário. Item 8 (feedback do usuário):
    // um botão no topo de cada editor permite ALTERNAR entre os modos em
    // tempo real, preservando o conteúdo. Fazemos isso num laço: cada
    // editor, ao pedir troca, fecha com Accepted transportando o Command
    // atual; reabrimos no outro modo com esse Command como base, até o
    // usuário confirmar (OK sem troca) ou cancelar.
    const core::SettingsData settings = m_configManager.loadSettings();
    core::Command working = existing;
    bool advanced = (settings.commandEditMode == QStringLiteral("advanced"));
    core::Command edited;

    while (true) {
        if (advanced) {
            CommandJsonEditorDialog dialog(&working, working.folderId, this);
            if (dialog.exec() != QDialog::Accepted) {
                return; // cancelou
            }
            working = dialog.buildCommand();
            if (hasDuplicateCommandName(working.folderId, working.name, existing.id)) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.duplicate_name.title")),
                    utils::tr(QStringLiteral("command.error.duplicate_name.body")));
                continue;
            }
            if (dialog.switchToSimpleRequested()) {
                advanced = false;
                continue; // reabre no modo simples com o conteúdo atual
            }
            edited = working;
            break;
        } else {
            CommandEditorDialog dialog(working.folderId, commandsInFolder(working.folderId),
                                       m_commandsData.folders, this, &working, settings.terminalProfiles,
                                       m_commandsData.commands);
            dialog.setAvailableCollections(m_collections);
            dialog.setAvailableDynamicVarNames(availableDynamicVarNames());
            if (dialog.exec() != QDialog::Accepted) {
                return; // cancelou
            }
            working = dialog.buildCommand();
            if (hasDuplicateCommandName(working.folderId, working.name, existing.id)) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.duplicate_name.title")),
                    utils::tr(QStringLiteral("command.error.duplicate_name.body")));
                continue;
            }
            if (dialog.switchToAdvancedRequested()) {
                advanced = true;
                continue; // reabre no modo avançado com o conteúdo atual
            }
            edited = working;
            break;
        }
    }

    m_commandsData.commands[commandIndex] = edited;
    persistCommands();
}

void MainWindow::handleDeleteRequested(const QString &itemId, bool isFolder)
{
    // Caso especial: a pasta PADRÃO "Geral" é sintética (não existe em
    // m_commandsData.folders). "Excluir" essa aba significa remover os
    // ÓRFÃOS que ela hospeda (comandos/coleções sem pasta válida) — feedback
    // do usuário: dar como limpar os itens soltos. Confirmação própria.
    if (isFolder && itemId == CommandTreeWidget::defaultFolderId()) {
        QSet<QString> folderIds;
        for (const core::Folder &f : m_commandsData.folders) {
            folderIds.insert(f.id);
        }
        auto resolves = [this, &folderIds](const QString &parentId) -> bool {
            QString cur = parentId;
            QSet<QString> visited;
            for (int guard = 0; guard < 128; ++guard) {
                if (cur.isEmpty() || visited.contains(cur)) return false;
                visited.insert(cur);
                if (folderIds.contains(cur)) return true;
                const auto it = std::find_if(m_commandsData.commands.constBegin(),
                    m_commandsData.commands.constEnd(),
                    [&cur](const core::Command &c) { return c.id == cur; });
                if (it == m_commandsData.commands.constEnd()) return false;
                cur = it->folderId;
            }
            return false;
        };
        int orphanCmds = 0, orphanCols = 0;
        for (const core::Command &c : m_commandsData.commands) {
            if (!resolves(c.folderId)) ++orphanCmds;
        }
        for (const core::Collection &col : m_collections) {
            if (!folderIds.contains(col.folderId)) ++orphanCols;
        }
        if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                          utils::tr(QStringLiteral("mainwindow.delete_general_folder.confirm"))
                              .arg(orphanCmds).arg(orphanCols))) {
            return;
        }
        m_commandsData.commands.removeIf([&resolves](const core::Command &c) {
            return !resolves(c.folderId);
        });
        bool colsChanged = false;
        m_collections.removeIf([&folderIds, &colsChanged](const core::Collection &col) {
            const bool orphan = !folderIds.contains(col.folderId);
            if (orphan) colsChanged = true;
            return orphan;
        });
        if (colsChanged) {
            persistCollections();
        }
        persistCommands();
        return;
    }

    if (!isFolder) {
        if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                          utils::tr(QStringLiteral("delete.confirm.command")))) {
            return;
        }
        m_commandsData.commands.removeIf([&itemId](const core::Command &c) { return c.id == itemId; });
        persistCommands();
        return;
    }

    // Exclusão de PASTA: diálogo com flags (pedido do usuário). O usuário
    // decide se apaga os filhos e, em caso afirmativo, quais TIPOS
    // (comandos/subpastas/coleções). O que não for marcado é REPARENTADO
    // para o avô (parentId da pasta removida) em vez de excluído.
    deleteFolderWithDialog(itemId);
    persistCommands();
}

void MainWindow::deleteFolderWithDialog(const QString &folderId)
{
    // Coleta a subárvore de subpastas descendentes (sem a própria).
    QVector<QString> descendantFolders;
    {
        QVector<QString> queue = {folderId};
        while (!queue.isEmpty()) {
            const QString currentId = queue.takeFirst();
            for (const core::Folder &folder : m_commandsData.folders) {
                if (folder.parentId.value_or(QString()) == currentId) {
                    descendantFolders << folder.id;
                    queue << folder.id;
                }
            }
        }
    }
    // Conjunto = a própria pasta + descendentes (para contar comandos/coleções
    // em QUALQUER nível da subárvore).
    QSet<QString> subtree;
    subtree.insert(folderId);
    for (const QString &id : descendantFolders) {
        subtree.insert(id);
    }

    FolderDeleteDialog::Counts counts;
    counts.folders = descendantFolders.size();
    for (const core::Command &c : m_commandsData.commands) {
        if (subtree.contains(c.folderId)) ++counts.commands;
    }
    for (const core::Collection &col : m_collections) {
        if (subtree.contains(col.folderId)) ++counts.collections;
    }

    // Nome e novo pai (avô) para o reparent.
    QString folderName = folderId;
    QString grandparentId;
    for (const core::Folder &f : m_commandsData.folders) {
        if (f.id == folderId) {
            folderName = f.name;
            grandparentId = f.parentId.value_or(QString());
            break;
        }
    }

    FolderDeleteDialog dialog(folderName, counts, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const FolderDeleteDialog::Result choice = dialog.result();

    // Filhos DIRETOS da pasta removida (comandos, subpastas, coleções cujo
    // pai é exatamente `folderId`). O que não for apagado é reparentado para
    // o avô, para nada ser perdido.
    auto reparentDirectChildrenToGrandparent = [&]() {
        for (core::Folder &f : m_commandsData.folders) {
            if (f.parentId.value_or(QString()) == folderId) {
                f.parentId = grandparentId.isEmpty() ? std::nullopt
                                                      : std::make_optional(grandparentId);
            }
        }
        for (core::Command &c : m_commandsData.commands) {
            if (c.folderId == folderId) {
                c.folderId = grandparentId;
            }
        }
        bool colsChanged = false;
        for (core::Collection &col : m_collections) {
            if (col.folderId == folderId) {
                col.folderId = grandparentId;
                colsChanged = true;
            }
        }
        if (colsChanged) persistCollections();
    };

    if (!choice.deleteChildren) {
        // Só a pasta: reparenta os filhos diretos para o avô e remove a pasta.
        reparentDirectChildrenToGrandparent();
        m_commandsData.folders.removeIf([&folderId](const core::Folder &f) { return f.id == folderId; });
        return;
    }

    // Apagar filhos, seletivamente por tipo. O que NÃO for marcado para
    // apagar é reparentado para o avô (itens diretos) — subpastas não
    // apagadas levam sua própria subárvore junto.
    bool colsChanged = false;

    // COLEÇÕES da subárvore.
    if (choice.deleteCollections) {
        m_collections.removeIf([&](const core::Collection &col) {
            const bool hit = subtree.contains(col.folderId);
            if (hit) colsChanged = true;
            return hit;
        });
    } else {
        // Não apagar: reparenta as coleções DIRETAS da pasta para o avô.
        for (core::Collection &col : m_collections) {
            if (col.folderId == folderId) { col.folderId = grandparentId; colsChanged = true; }
        }
    }

    // COMANDOS da subárvore.
    if (choice.deleteCommands) {
        m_commandsData.commands.removeIf([&](const core::Command &c) {
            return subtree.contains(c.folderId);
        });
    } else {
        for (core::Command &c : m_commandsData.commands) {
            if (c.folderId == folderId) c.folderId = grandparentId;
        }
    }

    // SUBPASTAS descendentes.
    if (choice.deleteFolders) {
        // Apaga todas as subpastas descendentes. Comandos/coleções que
        // sobraram (tipo não marcado para apagar) e ainda apontam para uma
        // subpasta apagada precisam de destino: reparenta para o avô.
        const QSet<QString> removedSubs(descendantFolders.begin(), descendantFolders.end());
        for (core::Command &c : m_commandsData.commands) {
            if (removedSubs.contains(c.folderId)) c.folderId = grandparentId;
        }
        for (core::Collection &col : m_collections) {
            if (removedSubs.contains(col.folderId)) { col.folderId = grandparentId; colsChanged = true; }
        }
        m_commandsData.folders.removeIf([&removedSubs](const core::Folder &f) {
            return removedSubs.contains(f.id);
        });
    } else {
        // Não apagar subpastas: reparenta as subpastas DIRETAS para o avô
        // (as mais profundas seguem suas diretas).
        for (core::Folder &f : m_commandsData.folders) {
            if (f.parentId.value_or(QString()) == folderId) {
                f.parentId = grandparentId.isEmpty() ? std::nullopt
                                                      : std::make_optional(grandparentId);
            }
        }
    }

    // Por fim, remove a própria pasta.
    m_commandsData.folders.removeIf([&folderId](const core::Folder &f) { return f.id == folderId; });

    if (colsChanged) persistCollections();
}

void MainWindow::deleteFolderRecursively(const QString &folderId)
{
    // Coleta a subárvore de pastas (a raiz + descendentes) e remove todas
    // elas e todos os comandos associados. NÃO persiste nem confirma — o
    // chamador decide (handleDeleteRequested confirma; a edição de pasta
    // já confirmou no próprio diálogo).
    QVector<QString> foldersToRemove = {folderId};
    QVector<QString> queue = {folderId};
    while (!queue.isEmpty()) {
        const QString currentId = queue.takeFirst();
        for (const core::Folder &folder : m_commandsData.folders) {
            if (folder.parentId.has_value() && folder.parentId.value() == currentId) {
                foldersToRemove << folder.id;
                queue << folder.id;
            }
        }
    }

    m_commandsData.folders.removeIf([&foldersToRemove](const core::Folder &f) {
        return foldersToRemove.contains(f.id);
    });
    m_commandsData.commands.removeIf([&foldersToRemove](const core::Command &c) {
        return foldersToRemove.contains(c.folderId);
    });
}

void MainWindow::handleDuplicateRequested(const QString &itemId, bool isFolder)
{
    // Gera um id único a partir de uma base, sufixando _copy/_copyN caso
    // já exista (comandos e pastas compartilham o espaço de nomes de id,
    // pois um comando pode ser pai de outro).
    auto idExists = [this](const QString &id) {
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == id) return true;
        }
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.id == id) return true;
        }
        return false;
    };
    auto uniqueId = [&idExists](const QString &base) {
        QString candidate = base + QStringLiteral("_copy");
        int n = 2;
        while (idExists(candidate)) {
            candidate = base + QStringLiteral("_copy%1").arg(n++);
        }
        return candidate;
    };

    if (!isFolder) {
        const auto it = std::find_if(m_commandsData.commands.constBegin(), m_commandsData.commands.constEnd(),
            [&itemId](const core::Command &c) { return c.id == itemId; });
        if (it == m_commandsData.commands.constEnd()) {
            return;
        }
        core::Command copy = *it;                 // deep copy (todos os campos)
        copy.id = uniqueId(it->id);
        copy.name = it->name + utils::tr(QStringLiteral("duplicate.name_suffix"));
        copy.order = -1;                          // sem ordem manual: vai para o fim
        m_commandsData.commands << copy;
        persistCommands();
        return;
    }

    // Duplicação de PASTA: copia a pasta, todas as subpastas descendentes
    // e todos os comandos associados, gerando novos ids e remapeando
    // parentId/folderId para manter a hierarquia intacta na cópia.
    const auto folderIt = std::find_if(m_commandsData.folders.constBegin(), m_commandsData.folders.constEnd(),
        [&itemId](const core::Folder &f) { return f.id == itemId; });
    if (folderIt == m_commandsData.folders.constEnd()) {
        return;
    }

    // Coleta a subárvore de pastas (a raiz duplicada + descendentes).
    QVector<QString> subtreeFolderIds = {itemId};
    QVector<QString> queue = {itemId};
    while (!queue.isEmpty()) {
        const QString currentId = queue.takeFirst();
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.parentId.has_value() && f.parentId.value() == currentId) {
                subtreeFolderIds << f.id;
                queue << f.id;
            }
        }
    }

    // Mapa id antigo -> id novo para remapear pais/folderIds.
    QMap<QString, QString> idMap;
    for (const QString &oldId : subtreeFolderIds) {
        idMap.insert(oldId, uniqueId(oldId));
    }

    QVector<core::Folder> newFolders;
    for (const core::Folder &f : m_commandsData.folders) {
        if (!subtreeFolderIds.contains(f.id)) {
            continue;
        }
        core::Folder copy = f;
        copy.id = idMap.value(f.id);
        // A raiz duplicada mantém o mesmo pai; as subpastas remapeiam para
        // o novo pai. A raiz ganha " (cópia)" no nome.
        if (f.id == itemId) {
            copy.name = f.name + utils::tr(QStringLiteral("duplicate.name_suffix"));
            copy.order = -1;
        } else if (f.parentId.has_value()) {
            copy.parentId = idMap.value(f.parentId.value(), f.parentId.value());
        }
        newFolders << copy;
    }

    // Comandos cujo folderId cai na subárvore são duplicados e remapeados.
    QVector<core::Command> newCommands;
    for (const core::Command &c : m_commandsData.commands) {
        if (!subtreeFolderIds.contains(c.folderId)) {
            continue;
        }
        core::Command copy = c;
        copy.id = uniqueId(c.id);
        copy.folderId = idMap.value(c.folderId, c.folderId);
        newCommands << copy;
    }

    m_commandsData.folders += newFolders;
    m_commandsData.commands += newCommands;
    persistCommands();
}

void MainWindow::handleNewCollectionRequested()
{
    // Cria uma coleção nova (schema default key/value) na pasta/aba atual e
    // abre o grid para o usuário preencher. Só persiste se ele confirmar.
    const QString folderId = resolveTargetFolderId();
    if (folderId.isEmpty()) {
        QMessageBox::information(this, utils::tr(QStringLiteral("sidebar.new_collection")),
            utils::tr(QStringLiteral("mainwindow.collection.need_folder")));
        return;
    }

    core::Collection col;
    col.id = QStringLiteral("col_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    col.folderId = folderId;
    col.name = utils::tr(QStringLiteral("sidebar.new_collection"));
    col.icon = QStringLiteral("database");
    col.schema = core::Collection::defaultSchema();

    // O nome e a pasta são definidos no próprio grid (campos no topo do
    // CollectionEditorDialog), então não abrimos mais um prompt separado.
    CollectionEditorDialog dialog(col, m_commandsData.folders, this);
    core::Collection built;
    while (true) {
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        built = dialog.buildCollection();
        // Nome duplicado na mesma pasta: barra o salvamento e reabre o
        // mesmo diálogo (já preenchido) para o usuário renomear.
        if (hasDuplicateCollectionName(built.folderId, built.name, QString())) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("collection.error.duplicate_name.title")),
                utils::tr(QStringLiteral("collection.error.duplicate_name.body")));
            continue;
        }
        break;
    }
    m_collections << built;
    persistCollections();
}

void MainWindow::persistCollections()
{
    if (!m_configManager.saveCollections(m_collections)) {
        QMessageBox::warning(this, QStringLiteral("Kai"),
            utils::tr(QStringLiteral("mainwindow.error.persist_collections")));
    }
    reloadCommandTree();
}

void MainWindow::handleCollectionEditRequested(const QString &collectionId)
{
    int index = -1;
    for (int i = 0; i < m_collections.size(); ++i) {
        if (m_collections.at(i).id == collectionId) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return;
    }
    const QString existingId = m_collections.at(index).id;
    CollectionEditorDialog dialog(m_collections.at(index), m_commandsData.folders, this);
    core::Collection built;
    while (true) {
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        built = dialog.buildCollection();
        if (hasDuplicateCollectionName(built.folderId, built.name, existingId)) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("collection.error.duplicate_name.title")),
                utils::tr(QStringLiteral("collection.error.duplicate_name.body")));
            continue;
        }
        break;
    }
    m_collections[index] = built;
    persistCollections();
}

void MainWindow::handleCollectionDuplicateRequested(const QString &collectionId)
{
    const auto it = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
        [&collectionId](const core::Collection &c) { return c.id == collectionId; });
    if (it == m_collections.constEnd()) {
        return;
    }
    // Gera id único para a cópia.
    auto idExists = [this](const QString &id) {
        for (const core::Collection &c : m_collections) {
            if (c.id == id) return true;
        }
        return false;
    };
    QString newId = it->id + QStringLiteral("_copy");
    int n = 2;
    while (idExists(newId)) {
        newId = it->id + QStringLiteral("_copy%1").arg(n++);
    }
    core::Collection copy = *it;      // deep copy (schema + entries + tags)
    copy.id = newId;
    copy.name = it->name + utils::tr(QStringLiteral("duplicate.name_suffix"));
    copy.order = -1;
    m_collections << copy;
    persistCollections();
}

void MainWindow::handleCollectionDeleteRequested(const QString &collectionId)
{
    if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                      utils::tr(QStringLiteral("delete.confirm.collection")))) {
        return;
    }
    m_collections.removeIf([&collectionId](const core::Collection &c) { return c.id == collectionId; });
    persistCollections();
}

void MainWindow::handleQuickEditBodyRequested(const QString &commandId)
{
    int commandIndex = -1;
    for (int i = 0; i < m_commandsData.commands.size(); ++i) {
        if (m_commandsData.commands.at(i).id == commandId) {
            commandIndex = i;
            break;
        }
    }
    if (commandIndex < 0) {
        return;
    }

    core::Command &command = m_commandsData.commands[commandIndex];
    if (command.type != core::CommandType::Http || !command.httpConfig.has_value()) {
        return;
    }

    QuickBodyEditorDialog dialog(command.name, command.httpConfig->body, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    command.httpConfig->body = dialog.body();
    persistCommands();
}

void MainWindow::handleTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        toggleVisibility();
    }
}

QStringList MainWindow::availableThemeNames() const
{
    QStringList names;
    const QDir themesDir(m_themeManager->themesDirPath());
    for (const QFileInfo &info : themesDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        names << info.baseName();
    }
    if (names.isEmpty()) {
        names << QStringLiteral("dracula");
    }
    return names;
}

void MainWindow::handleSettingsRequested()
{
    const core::SettingsData currentSettings = m_configManager.loadSettings();
    SettingsDialog dialog(currentSettings, availableThemeNames(), m_themeManager->themesDirPath(),
        m_commandsData, m_collections,
        [this]() { persistCommands(); }, [this]() { persistCollections(); }, this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const core::SettingsData newSettings = dialog.buildSettings();
    if (!m_configManager.saveSettings(newSettings)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("mainwindow.settings.title")),
            utils::tr(QStringLiteral("mainwindow.settings.save_failed")));
        return;
    }
    m_outputMaxLogSizeChars = qMax(1, newSettings.outputMaxLogSizeKb) * 1024;

    // As variáveis globais NÃO são mais editadas aqui — vêm do environment
    // (pacote) ativo, gerido pela tela de Environments. Não reaplicamos
    // globalEnvVars a partir das Configurações.

    if (newSettings.activeTheme != currentSettings.activeTheme) {
        if (!m_themeManager->loadTheme(newSettings.activeTheme)) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Tema '%1' não pôde ser carregado após alteração em Configurações.")
                    .arg(newSettings.activeTheme));
        }
    }

    // Aparência (densidade/cantos/efeitos): aplica NA HORA, sem reiniciar.
    if (newSettings.uiDensity != currentSettings.uiDensity
        || newSettings.uiCornerStyle != currentSettings.uiCornerStyle
        || newSettings.treeConnectorStyle != currentSettings.treeConnectorStyle
        || newSettings.fxShadows != currentSettings.fxShadows
        || newSettings.fxTranslucency != currentSettings.fxTranslucency
        || newSettings.fxBlur != currentSettings.fxBlur
        || newSettings.fxAnimations != currentSettings.fxAnimations
        || newSettings.autoHideOnFocusLoss != currentSettings.autoHideOnFocusLoss) {
        applyAppearanceSettings();
    }

    // Plano de fundo da aba de comandos (imagem/opacidade): aplica na hora.
    if (newSettings.commandsBackgroundImage != currentSettings.commandsBackgroundImage
        || newSettings.commandsBackgroundOpacity != currentSettings.commandsBackgroundOpacity) {
        if (m_commandTree) {
            m_commandTree->setBackground(newSettings.commandsBackgroundImage,
                                         newSettings.commandsBackgroundOpacity);
        }
    }

    // Geometria da janela: aplica NA HORA quando o modo ou o tamanho mudam.
    if (newSettings.windowMode != currentSettings.windowMode
        || newSettings.windowWidth != currentSettings.windowWidth
        || newSettings.windowHeight != currentSettings.windowHeight) {
        // Sai de maximizada/tela cheia antes, senão um resize seria ignorado.
        if (isFullScreen() || isMaximized()) {
            showNormal();
        }
        applyWindowGeometryPreference();
    }

    // Troca de idioma (language pack i18n): aplica o novo idioma no
    // TranslationManager imediatamente (diálogos abertos a partir de agora
    // já vêm traduzidos). A janela principal e widgets já construídos só
    // refletem 100% após reiniciar o Kai — informamos o usuário para não
    // parecer que "não funcionou".
    if (newSettings.language != currentSettings.language) {
        utils::TranslationManager::instance().loadLanguage(newSettings.language);
        QMessageBox::information(this, utils::tr(QStringLiteral("settings.title")),
            utils::tr(QStringLiteral("settings.language_restart")));
    }

    // Autostart / autoboot: aplica a preferência no mecanismo nativo do
    // SO apenas quando o valor muda (evita reescrever o .desktop/registro
    // à toa). Usa o caminho absoluto do binário em execução.
    if (newSettings.autostart != currentSettings.autostart) {
        const bool ok = utils::AutostartManager::setEnabled(
            newSettings.autostart, QCoreApplication::applicationFilePath());
        if (!ok) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Não foi possível %1 o autostart do Kai neste sistema.")
                    .arg(newSettings.autostart ? QStringLiteral("habilitar") : QStringLiteral("desabilitar")));
        }
    }

    // Recarrega TODOS os atalhos de ação (revolução dos
    // atalhos): setupActionShortcuts já destrói os QShortcut anteriores e
    // recria com as sequências recém-salvas, sem reiniciar o Kai.
    setupActionShortcuts();
    // Reposiciona os 4 grupos de ações (upper/bottom/left/side/oculto)
    // conforme a preferência recém-salva — SÓ se algo relevante mudou. Bug
    // relatado: "ao trocar/salvar alguma configuração há um recálculo do
    // tamanho das abas, causando redimensionamento indevido" — antes isto
    // rodava incondicionalmente em TODA gravação de Configurações (mesmo
    // mudando um atalho ou o tema), e applyActionGroupPlacement()
    // desmonta/remonta os 4 containers de ícones do zero, causando um
    // "pulo" visível de layout mesmo quando nada relacionado a grupos
    // mudou.
    if (newSettings.executionActionsPlacement != currentSettings.executionActionsPlacement
        || newSettings.displayActionsPlacement != currentSettings.displayActionsPlacement
        || newSettings.itemActionsPlacement != currentSettings.itemActionsPlacement
        || newSettings.showHiddenCommands != currentSettings.showHiddenCommands) {
        applyActionGroupPlacement();
    }
    // Reposiciona o painel de Saída (bottom/left/right) conforme a
    // preferência recém-salva — idem: applyOutputPosition() RECONSTRÓI o
    // QSplitter inteiro do zero, então só faz sentido chamar quando a
    // posição de fato mudou.
    if (newSettings.outputPosition != currentSettings.outputPosition) {
        applyOutputPosition();
    }

    utils::Logger::info(kLogTag, QStringLiteral("Configurações globais atualizadas."));
}

void MainWindow::applyActiveEnvironment()
{
    const core::SettingsData settings = m_configManager.loadSettings();
    QMap<QString, QString> vars;
    for (const core::Environment &e : settings.environments) {
        if (e.id == settings.activeEnvironmentId) {
            vars = e.vars;
            break;
        }
    }
    m_envManager.setGlobalVars(vars);
    refreshEnvironmentSelector();
}

void MainWindow::refreshEnvironmentSelector()
{
    if (!m_topBar) {
        return;
    }
    const core::SettingsData settings = m_configManager.loadSettings();
    QStringList ids;
    QStringList names;
    for (const core::Environment &e : settings.environments) {
        ids << e.id;
        names << e.name;
    }
    m_topBar->setEnvironments(ids, names, settings.activeEnvironmentId);
}

void MainWindow::handleEnvironmentSelected(const QString &environmentId)
{
    core::SettingsData settings = m_configManager.loadSettings();
    if (settings.activeEnvironmentId == environmentId) {
        return;
    }
    settings.activeEnvironmentId = environmentId;
    if (!m_configManager.saveSettings(settings)) {
        utils::Logger::warning(kLogTag, QStringLiteral("Falha ao persistir o environment ativo."));
        return;
    }
    applyActiveEnvironment();
    utils::Logger::info(kLogTag,
        QStringLiteral("Environment ativo alterado para '%1'.").arg(environmentId));
}

QStringList MainWindow::availableDynamicVarNames() const
{
    QStringList names;
    const QMap<QString, QMap<QString, QString>> all = m_envManager.allDynamicVars();
    for (auto scopeIt = all.constBegin(); scopeIt != all.constEnd(); ++scopeIt) {
        names += scopeIt.value().keys();
    }
    names.removeDuplicates();
    return names;
}

void MainWindow::handleManageEnvironmentsRequested()
{
    core::SettingsData settings = m_configManager.loadSettings();
    EnvironmentManagerDialog dialog(settings.environments, settings.activeEnvironmentId, this,
        &m_envManager, m_commandsData.folders,
        [this]() { return m_configManager.loadPersistedDynamicVars(); },
        [this](const QMap<QString, QMap<QString, QString>> &data) { return m_configManager.savePersistedDynamicVars(data); });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    settings.environments = dialog.environments();
    settings.activeEnvironmentId = dialog.activeEnvironmentId();
    if (!m_configManager.saveSettings(settings)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("env.manage.title")),
            utils::tr(QStringLiteral("mainwindow.error.save_environments")));
        return;
    }
    applyActiveEnvironment();
    utils::Logger::info(kLogTag, QStringLiteral("Environments atualizados via tela de gestão."));
}

// --- API para o IPC/CLI ---

bool MainWindow::runCommandByName(const QString &name, QString &message)
{
    const QString target = name.trimmed().toLower();
    QVector<const core::Command *> matches;
    for (const core::Command &c : m_commandsData.commands) {
        if (c.name.trimmed().toLower() == target) {
            matches.append(&c);
        }
    }
    if (matches.isEmpty()) {
        message = utils::tr(QStringLiteral("cli.error.command_not_found")).arg(name);
        return false;
    }
    if (matches.size() > 1) {
        message = utils::tr(QStringLiteral("cli.error.ambiguous_command")).arg(name).arg(matches.size());
        return false;
    }
    const QString id = matches.first()->id;
    // Reusa o fluxo padrão (trata params via formulário na GUI, hooks etc).
    // Traz a janela à frente para o usuário acompanhar/preencher params.
    showAndRaise();
    handleCommandActivated(id);
    message = utils::tr(QStringLiteral("cli.command.triggered")).arg(matches.first()->name);
    return true;
}

QStringList MainWindow::commandNames() const
{
    QStringList names;
    for (const core::Command &c : m_commandsData.commands) {
        names << c.name;
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool MainWindow::activateEnvironmentByName(const QString &name, QString &message)
{
    const core::SettingsData settings = m_configManager.loadSettings();
    const QString target = name.trimmed().toLower();
    for (const core::Environment &e : settings.environments) {
        if (e.name.trimmed().toLower() == target) {
            handleEnvironmentSelected(e.id);
            message = utils::tr(QStringLiteral("cli.environment.activated")).arg(e.name);
            return true;
        }
    }
    message = utils::tr(QStringLiteral("cli.error.environment_not_found")).arg(name);
    return false;
}

QStringList MainWindow::environmentNames(QString &activeName)
{
    const core::SettingsData settings = m_configManager.loadSettings();
    QStringList names;
    for (const core::Environment &e : settings.environments) {
        names << e.name;
        if (e.id == settings.activeEnvironmentId) {
            activeName = e.name;
        }
    }
    return names;
}

void MainWindow::showAndRaise()
{
    show();
    raise();
    activateWindow();
#if defined(Q_OS_WIN)
    forceForegroundOnWindows(this);
#endif
}

QStringList MainWindow::runningProcessLines()
{
    // Lista comandos com processo rastreado (background) e o comando de
    // execução única ativo no pipeline. Formato: "nome | pid N | status".
    QStringList lines;
    auto nameOf = [this](const QString &id) -> QString {
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == id) return c.name;
        }
        return id;
    };
    for (const QString &id : m_processManager->trackedCommandIds()) {
        const engine::ProcessStatus st = m_processManager->statusOf(id);
        QString status = (st == engine::ProcessStatus::Running) ? utils::tr(QStringLiteral("cli.status.running"))
                        : (st == engine::ProcessStatus::Success) ? utils::tr(QStringLiteral("cli.status.success"))
                        : utils::tr(QStringLiteral("cli.status.error"));
        qint64 pid = 0;
        if (auto *r = m_processManager->runnerFor(id)) {
            pid = r->processId();
        }
        lines << utils::tr(QStringLiteral("cli.ps.line")).arg(nameOf(id)).arg(pid).arg(status);
    }
    // Foreground: percorre o REGISTRY (todos os runners vivos). Antes só
    // mostrava o "ativo" (slot único), então com 2+ comandos rodando o `ps`
    // listava no máximo um — daí "o windows não lista os processos".
    const QSet<QString> already(m_processManager->trackedCommandIds().cbegin(),
                                m_processManager->trackedCommandIds().cend());
    for (const QString &id : m_pipeline->runningCommandIds()) {
        if (already.contains(id)) {
            continue;
        }
        qint64 pid = 0;
        if (auto *r = m_pipeline->runnerFor(id)) {
            pid = r->processId();
        }
        lines << utils::tr(QStringLiteral("cli.ps.line")).arg(nameOf(id)).arg(pid).arg(utils::tr(QStringLiteral("cli.status.running")));
    }
    return lines;
}

bool MainWindow::attachProcessByName(const QString &arg, QString &message)
{
    // Aceita PID (numérico) OU nome do comando. Resolve para o commandId
    // interno — o attach reusa a conexão do terminal por comando.
    QString cid, cname, why;
    if (!resolveProcessTarget(arg, cid, cname, why)) {
        message = why;
        return false;
    }
    showAndRaise();
    reconnectTerminalToCommand(cid);
    message = utils::tr(QStringLiteral("cli.attach.done")).arg(cname);
    return true;
}

bool MainWindow::killProcessByName(const QString &arg, QString &message)
{
    // Aceita PID (numérico) OU nome. Sempre encerra pelo commandId, para
    // manter o kill robusto do grupo de processos (terminate->timeout->
    // kill em -PGID), evitando processos filhos órfãos que um kill por PID
    // cru do SO deixaria.
    QString cid, cname, why;
    if (!resolveProcessTarget(arg, cid, cname, why)) {
        message = why;
        return false;
    }
    handleKillCommandRequested(cid);
    message = utils::tr(QStringLiteral("cli.kill.done")).arg(cname);
    return true;
}

bool MainWindow::resolveProcessTarget(const QString &arg, QString &commandId,
                                      QString &commandName, QString &error)
{
    // Coleta os processos EM EXECUÇÃO (rastreados + pipeline ativo) com
    // seus PIDs e nomes, para casar por PID ou por nome.
    struct Proc { QString id; QString name; qint64 pid; };
    QVector<Proc> procs;
    auto nameOf = [this](const QString &id) -> QString {
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == id) return c.name;
        }
        return id;
    };
    for (const QString &id : m_processManager->trackedCommandIds()) {
        if (m_processManager->statusOf(id) != engine::ProcessStatus::Running) {
            continue;
        }
        qint64 pid = 0;
        if (auto *r = m_processManager->runnerFor(id)) {
            pid = r->processId();
        }
        procs.append({id, nameOf(id), pid});
    }
    // Foreground: todos os runners vivos do registry (antes só o "ativo",
    // então attach/kill por nome falhavam com execuções concorrentes).
    for (const QString &id : m_pipeline->runningCommandIds()) {
        bool dup = false;
        for (const Proc &p : procs) {
            if (p.id == id) { dup = true; break; }
        }
        if (dup) {
            continue;
        }
        qint64 pid = 0;
        if (auto *r = m_pipeline->runnerFor(id)) {
            pid = r->processId();
        }
        procs.append({id, nameOf(id), pid});
    }

    if (procs.isEmpty()) {
        error = utils::tr(QStringLiteral("cli.error.no_process"));
        return false;
    }

    // Argumento numérico -> casa por PID.
    bool isPid = false;
    const qint64 wantedPid = arg.trimmed().toLongLong(&isPid);
    if (isPid) {
        for (const Proc &p : procs) {
            if (p.pid == wantedPid) {
                commandId = p.id; commandName = p.name; return true;
            }
        }
        error = utils::tr(QStringLiteral("cli.error.no_process_pid")).arg(wantedPid);
        return false;
    }

    // Caso contrário -> casa por nome (case-insensitive).
    const QString target = arg.trimmed().toLower();
    for (const Proc &p : procs) {
        if (p.name.trimmed().toLower() == target) {
            commandId = p.id; commandName = p.name; return true;
        }
    }
    error = utils::tr(QStringLiteral("cli.error.no_process_name")).arg(arg);
    return false;
}

void MainWindow::handleRunHistoryRequested()
{
    RunHistoryDialog dialog(&m_runHistory, this);
    connect(&dialog, &RunHistoryDialog::rerunRequested, this, [this](const QString &commandId) {
        // Reexecuta pelo id (o dialog já fechou com accept()).
        if (m_commandsById.contains(commandId)) {
            handleCommandActivated(commandId);
        } else {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Reexecução: comando '%1' não existe mais.").arg(commandId));
        }
    });
    dialog.exec();
}

void MainWindow::handleNotificationHistoryRequested()
{
    NotificationHistoryDialog dialog(&m_notificationHistory, this);
    dialog.exec();
}

void MainWindow::handleLogsRequested()
{
    if (!m_logViewerDialog) {
        m_logViewerDialog = new LogViewerDialog(this);
    }
    m_logViewerDialog->show();
    m_logViewerDialog->raise();
    m_logViewerDialog->activateWindow();
}

void MainWindow::handleHelpRequested()
{
    HelpDialog dialog(this);
    dialog.exec();
}

void MainWindow::toggleVisibility()
{
    // Ocultar (menu/atalho global) tem o MESMO comportamento do "X" da
    // barra: esconde a janela (hide) e mantém o processo rodando.
    // Reexibir é feito pelo atalho global (funcional sob xcb/X11) ou pela
    // bandeja quando disponível.
    if (isVisible() && !isMinimized()) {
        hide();
    } else {
        // Reexibir precisa respeitar o "modo de abertura" das Configurações
        // (Tamanho/Maximizada/Tela cheia/Lembrar) — showNormal() fixo aqui
        // ignorava a preferência e sempre devolvia a janela em modo normal,
        // mesmo configurada como maximizada/tela cheia (bug relatado).
        const QString mode = m_configManager.loadSettings().windowMode.trimmed().toLower();
        if (mode == QStringLiteral("fullscreen")) {
            showFullScreen();
        } else if (mode == QStringLiteral("maximized")) {
            showMaximized();
        } else {
            showNormal();
        }
        raise();
        activateWindow();
#if defined(Q_OS_WIN)
        // Ver forceForegroundOnWindows: sem isto, um atalho GLOBAL
        // (processo em segundo plano) frequentemente desminimiza a janela
        // (o estado muda) mas o Windows recusa silenciosamente trazê-la
        // pra frente/dar foco de teclado — bug real reportado.
        forceForegroundOnWindows(this);
#endif
    }
}

void MainWindow::handleMaximizeRestore()
{
    if (isMaximized()) {
        showNormal();
        m_topBar->setMaximized(false);
    } else {
        showMaximized();
        m_topBar->setMaximized(true);
    }
}

void MainWindow::handleWindowMoveRequested()
{
    // Arraste nativo da janela frameless: delega ao window manager via
    // QWindow::startSystemMove (Qt 6). É o método robusto que funciona em
    // X11/XWayland/WSLg, sem cálculo manual de delta (que falhava porque o
    // QMenuBar da title bar consumia os eventos de mouse antes de chegarem
    // ao handler manual). Não arrasta se maximizada.
    if (isMaximized()) {
        return;
    }
    if (QWindow *handle = windowHandle()) {
        handle->startSystemMove();
    }
}

Qt::Edges MainWindow::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    // Só marca borda quando o ponto está DENTRO da janela e dentro da faixa
    // de kResizeMargin da respectiva borda. Sem a checagem de limites, um
    // ponto fora (coordenada negativa ou além de width/height) — que ocorre
    // com o filtro global observando widgets em outras posições — acusava
    // borda e o resize ativava em lugar indevido (relatado).
    const bool inX = pos.x() >= 0 && pos.x() < width();
    const bool inY = pos.y() >= 0 && pos.y() < height();
    if (!inX || !inY) {
        return edges;
    }
    if (pos.x() <= kResizeMargin) {
        edges |= Qt::LeftEdge;
    }
    if (pos.x() >= width() - kResizeMargin) {
        edges |= Qt::RightEdge;
    }
    if (pos.y() <= kResizeMargin) {
        edges |= Qt::TopEdge;
    }
    if (pos.y() >= height() - kResizeMargin) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    // O cursor de resize é gerido pelo eventFilter GLOBAL (override cursor),
    // que funciona sobre qualquer widget da janela. Aqui não mexemos no
    // cursor para não conflitar com o override (o setCursor(this) daqui
    // grudava e vazava o cursor de resize pelo app — relatado).
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    // Inicia o resize nativo se o clique começou numa borda (frameless).
    if (event->button() == Qt::LeftButton && !isMaximized()) {
        const Qt::Edges edges = edgesAt(event->pos());
        if (edges != Qt::Edges()) {
            if (QWindow *handle = windowHandle()) {
                handle->startSystemResize(edges);
                return;
            }
        }
    }
    QMainWindow::mousePressEvent(event);
}

bool MainWindow::event(QEvent *e)
{
    // Mantém o glifo do botão maximizar/restaurar em sincronia com o
    // estado real da janela (ex: maximizar via atalho do WM).
    if (e->type() == QEvent::WindowStateChange && m_topBar) {
        m_topBar->setMaximized(isMaximized());
    }
    return QMainWindow::event(e);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *e)
{
    // RESIZE POR BORDA (janela frameless). Filtro GLOBAL: os widgets filhos
    // cobrem as bordas, então observamos todos os eventos e só agimos na
    // FAIXA de kResizeMargin da borda da janela.
    //
    // O cursor de resize usa QApplication::setOverrideCursor (GLOBAL) em vez
    // de setCursor(this): setCursor num top-level HERDA para os filhos e o
    // unsetCursor só valia quando o evento vinha do próprio 'this' (raro,
    // pois o mouse quase sempre está sobre um filho) — por isso o cursor de
    // resize "grudava" e aparecia em todo lugar (relatado). O override é uma
    // pilha global: empilhamos ao ENTRAR na borda e restauramos ao SAIR,
    // exatamente uma vez (m_resizeCursorActive).
    auto clearResizeCursor = [this]() {
        if (m_resizeCursorActive) {
            QApplication::restoreOverrideCursor();
            m_resizeCursorActive = false;
        }
    };

    if (!isMaximized() && isActiveWindow()
        && (e->type() == QEvent::MouseMove || e->type() == QEvent::MouseButtonPress)) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (w && w->window() == this) {
            auto *me = static_cast<QMouseEvent *>(e);
            const QPoint posInWindow = mapFromGlobal(me->globalPosition().toPoint());
            const Qt::Edges edges = edgesAt(posInWindow);

            if (edges != Qt::Edges()) {
                if (e->type() == QEvent::MouseMove && me->buttons() == Qt::NoButton) {
                    Qt::CursorShape shape = Qt::ArrowCursor;
                    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) {
                        shape = Qt::SizeFDiagCursor;
                    } else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)) {
                        shape = Qt::SizeBDiagCursor;
                    } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
                        shape = Qt::SizeHorCursor;
                    } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
                        shape = Qt::SizeVerCursor;
                    }
                    // Troca o override: restaura o anterior e empilha o novo
                    // (evita empilhar vários ao mover ao longo da borda).
                    clearResizeCursor();
                    QApplication::setOverrideCursor(shape);
                    m_resizeCursorActive = true;
                } else if (e->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
                    if (QWindow *handle = windowHandle()) {
                        clearResizeCursor(); // o WM assume o cursor durante o resize
                        handle->startSystemResize(edges);
                        return true; // consumido: inicia o resize
                    }
                }
            } else if (e->type() == QEvent::MouseMove) {
                // Fora da faixa de borda: restaura o cursor normal (o filho
                // sob o mouse volta a mandar no próprio cursor).
                clearResizeCursor();
            }
        }
    }
    return QMainWindow::eventFilter(watched, e);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Setas recuperam o foco para a árvore de comandos (pedido de UX). Este
    // handler só recebe as teclas que NÃO foram consumidas pelo widget focado
    // (propagação normal do Qt) — então quando o foco está num campo de
    // texto/combo/lista, as setas nem chegam aqui. Isso substitui o antigo
    // filtro global no qApp, que vazava o tratamento de resize para toda a UI
    // e bloqueava cliques (bug reportado).
    const int k = event->key();
    const bool isArrow = (k == Qt::Key_Up || k == Qt::Key_Down
                          || k == Qt::Key_Left || k == Qt::Key_Right);
    if (isArrow && m_commandTree) {
        QWidget *fw = QApplication::focusWidget();
        const bool alreadyInTree = fw && (m_commandTree->isAncestorOf(fw) || fw == m_commandTree);
        if (!alreadyInTree) {
            m_commandTree->focusFirstVisibleItem();
            return; // consumido
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // Prioriza a navegação por seta: ao exibir a janela, foca a árvore de
    // comandos e seleciona o primeiro item visível, para que ↑/↓ naveguem
    // os comandos imediatamente (feedback do usuário). Adiado para o
    // próximo ciclo do event loop, garantindo que o layout já esteja
    // assentado e o foco não seja roubado por outro widget no setup.
    QTimer::singleShot(0, this, [this]() {
        if (m_commandTree) {
            m_commandTree->focusFirstVisibleItem();
        }
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Modo "lembrar": guarda o tamanho atual para o próximo boot. Só grava se
    // a janela NÃO está maximizada/tela cheia (senão salvaríamos o tamanho da
    // tela como se fosse a preferência do usuário).
    {
        core::SettingsData st = m_configManager.loadSettings();
        bool dirty = false;
        if (st.windowMode.compare(QStringLiteral("remember"), Qt::CaseInsensitive) == 0
            && !isMaximized() && !isFullScreen()) {
            const QSize s = size();
            if (s.width() != st.windowWidth || s.height() != st.windowHeight) {
                st.windowWidth = s.width();
                st.windowHeight = s.height();
                dirty = true;
            }
        }
        if (dirty) {
            m_configManager.saveSettings(st);
        }
    }

    // O "X" da barra de título fecha a JANELA mas NÃO mata o processo
    // (semântica pedida pelo usuário): oculta a janela e o Kai
    // segue rodando em background, exatamente como a ação "Ocultar".
    // Encerrar o processo de fato é responsabilidade do "Fechar
    // Aplicativo" (Ctrl+Q → quitApplication) ou do "Sair" na bandeja.
    // Reexibir é feito pelo atalho global (funcional sob xcb/X11) ou pela
    // bandeja quando disponível.
    event->ignore();
    hide();
}

} // namespace kai::ui
