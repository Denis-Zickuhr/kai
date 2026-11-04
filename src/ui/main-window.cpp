

#include "ui/features/docs/document-panel.h"

#include "ui/main-window.h"

#include "ui/layout/dock-host.h"

#include "ui/features/ai/main-window-agent-host.h"
#include "ui/features/ai/mcp-bridge.h"

#include "engine/http-cookie-store.h"
#include "utils/path-format.h"

#include "ui/shared/panel-metrics.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QSplitter>

#include <QToolTip>

#include <QMessageBox>

#include <QApplication>

#include "qhotkey.h"

#include "ui/features/docs/doc-session-store.h"

#include "ui/shared/loading-overlay.h"

#include "engine/module-files.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/output/run-mirror-panel.h"

#include "engine/script-spill.h"

#include "ui/shared/manifesto-dialog.h"

#include "ui/shared/welcome-screen.h"

#include "utils/translation-manager.h"
#include "utils/autostart-manager.h"

#include "ui/shared/window-translucency.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_pipeline(new engine::ExecutionPipeline(this))
    , m_processManager(new engine::ProcessManager(this))
    , m_themeManager(new utils::ThemeManager(this))
{
    // A translucidez precisa ser definida antes de a janela nativa existir (ver shouldUseTranslucentWindow e
    // QMainWindow[kaiTranslucent] no QSS).
    m_translucentWindow = shouldUseTranslucentWindow();
    if (m_translucentWindow) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setProperty("kaiRoundedWindow", true); // lido por applyWindowBackdrop
        setProperty("kaiTranslucent", true);   // seletor do QSS
    }

    const core::SettingsData initialSettings = m_configManager.loadSettings();
    core::setKipSettings(initialSettings.kip);
    // O idioma tem de estar carregado antes de construir a UI.
    utils::TranslationManager::instance().loadLanguage(initialSettings.language);
    m_outputMaxLogSizeChars = qMax(1, initialSettings.outputMaxLogSizeKb) * 1024;
    m_globalActions = initialSettings.globalActions;
    m_smartFocus = initialSettings.smartFocus;
    m_inlineActions = initialSettings.inlineActions;

    setupUi();
    setupToolbar();
    // O host da IA nasce antes do quadro de painéis: o chat é um painel que pode já estar no layout salvo.
    m_agentHost = new MainWindowAgentHost(this);
    setupLayout();
    setupTrayIcon();
    trackScreenChanges();
    setupGlobalHotkey();
    setupTheme();
    applyAppearanceSettings();
    applyActionGroupPlacement();

    // Reconcilia o autostart do SO com a preferência a cada boot (inclusive o caminho do executável, que muda ao
    // reinstalar em outra pasta); idempotente.
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
    // O host da IA guarda os últimos logs desde o início.
    m_mcpBridge = new McpBridge(m_agentHost, this);
    refreshAiAvailability();
    restoreLayoutFromDisk();
    // Depois do layout (o painel do app tem de estar no quadro) e das conexões abaixo (a sessão precisa achar o painel).
    QTimer::singleShot(0, this, &MainWindow::autoStartKipPanels);

    connect(m_pipeline, &engine::ExecutionPipeline::logMessage, this, &MainWindow::handlePipelineLog);
    connect(m_pipeline, &engine::ExecutionPipeline::pipelineFinished, this, &MainWindow::handlePipelineFinished);
    connect(m_pipeline, &engine::ExecutionPipeline::backgroundProcessStarted, this, &MainWindow::handleBackgroundProcessStarted);
    connect(m_pipeline, &engine::ExecutionPipeline::kipSessionStarted, this, &MainWindow::handleKipSessionStarted);
    connect(m_pipeline, &engine::ExecutionPipeline::kipNotifyRequested, this, &MainWindow::handleKipNotify);
    connect(m_pipeline, &engine::ExecutionPipeline::kipAnswersRemembered, this, &MainWindow::handleKipAnswersRemembered);
    connect(m_terminalDrawer, &TerminalDrawer::kipRunAgainRequested, this, &MainWindow::handleCommandActivated);
    // No início do estágio o runner já existe; só calcular antes dele deixava o ícone de "rodando" sem aparecer (Windows).
    connect(m_pipeline, &engine::ExecutionPipeline::stageStarted, this,
            [this](const QString &, engine::PipelineStage) { updateRunningCommandStatus(); });
    connect(m_processManager, &engine::ProcessManager::outputReady, this, &MainWindow::handleBackgroundProcessOutput);
    connect(m_processManager, &engine::ProcessManager::statusChanged, this, &MainWindow::handleBackgroundProcessStatusChanged);
    connect(&m_configManager, &core::ConfigManager::configRecovered, this, &MainWindow::handleConfigRecovered);

    connect(&m_cronScheduler, &engine::CronScheduler::commandDue, this, [this](const QString &commandId) {
        if (!m_commandsById.contains(commandId)) {
            return; // comando removido
        }
        utils::Logger::info(kLogTag,
            QStringLiteral("CRON: disparando '%1'.").arg(commandId));
        runInBackground(commandId, true);
    });

    // O encerramento rápido (sem waits bloqueantes no destrutor dos ProcessRunner) vem antes do stopAll; senão fechar
    // com N processos em background congela a GUI por N x 1-3s.
    connect(qApp, &QCoreApplication::aboutToQuit, this, []() {
        engine::ProcessRunner::setFastShutdown(true);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, m_processManager, &engine::ProcessManager::stopAll);

    // A janela não é centralizada no construtor: move() antes do primeiro show(), com a geometria ainda não
    // finalizada num setup multi-monitor do WSLg, pode deixá-la fora de qualquer área visível.
}

MainWindow::~MainWindow()
{
    // Os painéis de execução morrem com os filhos da janela, DEPOIS dos membros: o aviso de `destroyed` deles mexe em
    // m_mirrors e na Saída, que a essa altura já não existem.
    for (const QPointer<RunMirrorPanel> &panel : std::as_const(m_mirrors)) {
        if (panel) {
            disconnect(panel, &QObject::destroyed, nullptr, nullptr);
        }
    }
}

void MainWindow::connectPipelineAuxSignals(engine::ExecutionPipeline *pipeline)
{
    connect(pipeline, &engine::ExecutionPipeline::httpRequestStarted, this,
            [this](const QString &commandId, int timeoutSec) {
        const HttpWait wait{QDateTime::currentDateTime(), timeoutSec};
        m_httpWaits.insert(commandId, wait);
        if (m_connectedTerminalCommandId == commandId) {
            m_terminalDrawer->setHttpWaiting(wait.startedAt, wait.timeoutSec);
        }
    });
    connect(pipeline, &engine::ExecutionPipeline::httpResultReady, this,
            [this](const QString &commandId, const engine::HttpResult &result) {
        // Guardado para reaplicar ao reselecionar o comando.
        m_lastHttpResult.insert(commandId, result);
        m_httpWaits.remove(commandId);
        if (m_connectedTerminalCommandId == commandId) {
            m_terminalDrawer->clearHttpWaiting();
        }
        // A marca de "pulado" de uma rodada anterior não vale mais (ver m_skippedReason).
        m_skippedReason.remove(commandId);
        if (m_connectedTerminalCommandId == commandId) {
            m_terminalDrawer->setSkipped(false);
            m_terminalDrawer->setHttpResult(result);
        }
        if (m_terminalDrawer->hasDetachedWindow()
            && m_terminalDrawer->detachedCommandId() == commandId) {
            m_terminalDrawer->setDetachedHttpResult(result);
        }
        forEachMirror(commandId, [&result](RunMirrorPanel *panel) { panel->setHttpResult(result); });
    });
    // O painel "não executada" evita mostrar o resultado em cache de uma execução anterior como se fosse atual.
    connect(pipeline, &engine::ExecutionPipeline::commandSkippedByCondition, this,
            [this](const QString &commandId, const QString &reasonLabel) {
        m_skippedReason.insert(commandId, reasonLabel);
        if (m_connectedTerminalCommandId == commandId) {
            m_terminalDrawer->setSkipped(true, reasonLabel);
            m_terminalDrawer->setExecutionStatus(ExecutionStatus::Skipped);
        }
    });
    // EnvExtractor::persist: write-through em dynamic-vars.json (arquivo pequeno, evento raro).
    connect(pipeline, &engine::ExecutionPipeline::dynamicVarPersistRequested, this,
            [this](const QString &scopeKey, const QString &name, const QString &value) {
        QMap<QString, QMap<QString, QString>> all = m_configManager.loadPersistedDynamicVars();
        all[scopeKey][name] = value;
        if (!m_configManager.savePersistedDynamicVars(all)) {
            utils::Logger::warning(kLogTag, QStringLiteral("Falha ao persistir variável dinâmica '%1'.").arg(name));
        }
    });
}

void MainWindow::setupUi()
{
    // Sem borda nativa; a TopUtilityBar é a barra de título e o arredondamento vem do QSS do "rootContainer". Os hints
    // de minimizar/menu/maximizar/fechar são explícitos de propósito: com FramelessWindowHint o Qt não aplica os
    // padrões, e no Windows a janela nascia sem WS_MINIMIZEBOX/WS_SYSMENU (o clique na barra de tarefas era ignorado).
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                   | Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    setMinimumSize(720, 480);
    applyWindowGeometryPreference();
    setWindowTitle(QStringLiteral("Kai"));
    setWindowIcon(loadAppIcon());

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("rootContainer"));
    // Um QWidget puro só honra qlineargradient() do QSS (fundo "primary" do tema, ver app-stylesheet.cpp) com este
    // atributo; cor sólida funciona sem ele.
    central->setAttribute(Qt::WA_StyledBackground, true);
    // Mouse tracking para o cursor de resize mudar perto das bordas sem botão pressionado. O filtro global (qApp) é
    // necessário porque os filhos cobrem as bordas e consumiriam os eventos; ele só age na faixa kResizeMargin.
    setMouseTracking(true);
    central->setMouseTracking(true);
    qApp->installEventFilter(this);
    auto *layout = new QVBoxLayout(central);
    // A top bar encosta no topo sem margem; o padding do app fica no conteúdo abaixo.
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_topBar = new TopUtilityBar(central);
    connect(m_topBar, &TopUtilityBar::importRequested, this, &MainWindow::handleImportRequested);
    connect(m_topBar, &TopUtilityBar::exportRequested, this, &MainWindow::handleExportRequested);
    connect(m_topBar, &TopUtilityBar::logsRequested, this, &MainWindow::handleLogsRequested);
    connect(m_topBar, &TopUtilityBar::clearCookiesRequested, this, []() {
        engine::HttpCookieStore::clear();
        QToolTip::showText(QCursor::pos(), utils::tr(QStringLiteral("http.cookies.cleared")));
    });
    connect(m_topBar, &TopUtilityBar::settingsRequested, this, &MainWindow::handleSettingsRequested);
    connect(m_topBar, &TopUtilityBar::runHistoryRequested, this, &MainWindow::handleRunHistoryRequested);
    connect(m_topBar, &TopUtilityBar::helpRequested, this, &MainWindow::handleHelpRequested);
    connect(m_topBar, &TopUtilityBar::manifestoRequested, this, [this]() {
        ManifestoDialog dialog(this);
        dialog.exec();
    });    connect(m_topBar, &TopUtilityBar::showWelcomeRequested, this, &MainWindow::handleShowWelcomeRequested);
    connect(m_topBar, &TopUtilityBar::hideRequested, this, &MainWindow::toggleVisibility);
    connect(m_topBar, &TopUtilityBar::quitAppRequested, this, &MainWindow::quitApplication);
    connect(m_topBar, &TopUtilityBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_topBar, &TopUtilityBar::maximizeRestoreRequested, this, &MainWindow::handleMaximizeRestore);
    connect(m_topBar, &TopUtilityBar::closeRequested, this, &MainWindow::close);
    connect(m_topBar, &TopUtilityBar::moveRequested, this, &MainWindow::handleWindowMoveRequested);

    m_commandTree = new CommandTreeWidget(central);
    {
        const core::SettingsData bg = m_configManager.loadSettings();
        m_commandTree->setBackground(bg.commandsBackgroundImage, bg.commandsBackgroundOpacity);
    }
    connect(m_commandTree, &CommandTreeWidget::commandActivated, this, &MainWindow::handleCommandActivated);
    connect(m_commandTree, &CommandTreeWidget::editRequested, this, &MainWindow::handleEditRequested);
    connect(m_commandTree, &CommandTreeWidget::deleteRequested, this, &MainWindow::handleDeleteRequested);
    // Scripts longos demais para a linha de comando vão para <temp>/kai-run: aqui só se limpam os antigos.
    engine::ScriptSpill::cleanupOldAsync();
    engine::ModuleFiles::ensureAsync();
    m_projectSync = new engine::ProjectSyncManager(QString(), this);
    connect(m_projectSync, &engine::ProjectSyncManager::planReady, this, &MainWindow::handleProjectSyncPlan);
    connect(m_projectSync, &engine::ProjectSyncManager::kaiToFileFinished, this,
        [this](const QString &folderId, bool ok, const QString &errorDetail) {
            const QString title = utils::tr(QStringLiteral("project_sync.title"));
            if (ok) {
                QMessageBox::information(this, title, utils::tr(QStringLiteral("project_sync.done.kai_to_file"))
                    .arg(folderDisplayName(folderId)));
            } else {
                QMessageBox::warning(this, title, utils::tr(QStringLiteral("project_sync.write_failed")).arg(errorDetail));
            }
            finishProjectSync(folderId);
        });
    connect(m_commandTree, &CommandTreeWidget::projectSyncRequested, this, &MainWindow::startProjectSync);
    connect(m_projectSync, &engine::ProjectSyncManager::driftChecked, m_commandTree, &CommandTreeWidget::setSyncDrift);
    m_syncDriftTimer = new QTimer(this);
    m_syncDriftTimer->setSingleShot(true);
    m_syncDriftTimer->setInterval(400);
    connect(m_syncDriftTimer, &QTimer::timeout, this, [this]() {
        m_commandsData.notes = m_notes;
        m_projectSync->requestDriftCheck(m_commandsData, m_collections);
    });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            scheduleSyncDriftCheck();
        }
    });
    scheduleSyncDriftCheck();
    connect(m_commandTree, &CommandTreeWidget::pinAsPanelRequested, this, &MainWindow::pinKipPanel);
    connect(m_commandTree, &CommandTreeWidget::exportRequested, this, [this](const QString &id, bool isFolder) {
        showExportDialog(id, isFolder);
    });

    // Direto pelo id: currentSelectionId() só enxerga itens dentro da árvore, nunca a aba.
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
    connect(m_commandTree, &CommandTreeWidget::quickEditFolderRequested, this, &MainWindow::handleQuickEditFolderRequested);
    connect(m_commandTree, &CommandTreeWidget::collectionEditRequested, this, &MainWindow::handleCollectionEditRequested);
    connect(m_commandTree, &CommandTreeWidget::collectionDuplicateRequested, this, &MainWindow::handleCollectionDuplicateRequested);
    connect(m_commandTree, &CommandTreeWidget::collectionDeleteRequested, this, &MainWindow::handleCollectionDeleteRequested);
    connect(m_commandTree, &CommandTreeWidget::newFolderRequested, this, &MainWindow::handleNewFolderRequested);
    connect(m_commandTree, &CommandTreeWidget::newCommandRequested, this, &MainWindow::handleNewCommandRequested);
    connect(m_commandTree, &CommandTreeWidget::newCollectionRequested, this, &MainWindow::handleNewCollectionRequested);
    connect(m_commandTree, &CommandTreeWidget::newNoteRequested, this, &MainWindow::handleNewNoteRequested);
    connect(m_commandTree, &CommandTreeWidget::noteActivated, this, &MainWindow::handleNoteActivated);
    connect(m_commandTree, &CommandTreeWidget::noteEditRequested, this, &MainWindow::handleNoteEditRequested);
    connect(m_commandTree, &CommandTreeWidget::noteDuplicateRequested, this, &MainWindow::handleNoteDuplicateRequested);
    connect(m_commandTree, &CommandTreeWidget::noteDeleteRequested, this, &MainWindow::handleNoteDeleteRequested);
    connect(m_commandTree, &CommandTreeWidget::noteFileActionRequested, this, &MainWindow::handleNoteFileActionRequested);
    connect(m_commandTree, &CommandTreeWidget::playRequested, this, &MainWindow::handleCommandActivated);
    connect(m_commandTree, &CommandTreeWidget::killRequested, this,
            [this](const QString &commandId) { handleKillCommandRequested(commandId, false); });
    connect(m_commandTree, &CommandTreeWidget::forceStopRequested, this,
            [this](const QString &commandId) { handleKillCommandRequested(commandId, true); });
    connect(m_commandTree, &CommandTreeWidget::resetRequested, this, &MainWindow::handleResetCommandRequested);
    connect(m_commandTree, &CommandTreeWidget::structureChanged, this, &MainWindow::handleTreeStructureChanged);

    // Os grupos de ações são reparentados em applyActionGroupPlacement().
    m_itemActionsBar = new ItemActionsBar(central);
    connect(m_itemActionsBar, &ItemActionsBar::editCurrentFolderRequested, this, &MainWindow::triggerEditCurrentFolder);
    connect(m_itemActionsBar, &ItemActionsBar::newFolderRequested, this, &MainWindow::handleNewFolderRequested);
    connect(m_itemActionsBar, &ItemActionsBar::newCommandRequested, this, &MainWindow::handleNewCommandRequested);
    connect(m_itemActionsBar, &ItemActionsBar::newCollectionRequested, this, &MainWindow::handleNewCollectionRequested);
    connect(m_itemActionsBar, &ItemActionsBar::newNoteRequested, this, &MainWindow::handleNewNoteRequested);
    connect(m_itemActionsBar, &ItemActionsBar::editSelectedRequested, this, &MainWindow::triggerEditSelected);
    connect(m_itemActionsBar, &ItemActionsBar::deleteSelectedRequested, this, &MainWindow::triggerDeleteSelected);

    m_expandCollapseBar = new ExpandCollapseBar(central);
    connect(m_expandCollapseBar, &ExpandCollapseBar::expandSelectedRequested, this, &MainWindow::triggerExpandSelected);
    connect(m_expandCollapseBar, &ExpandCollapseBar::collapseSelectedRequested, this, &MainWindow::triggerCollapseSelected);
    connect(m_expandCollapseBar, &ExpandCollapseBar::expandAllRequested, this, &MainWindow::triggerExpandAll);
    connect(m_expandCollapseBar, &ExpandCollapseBar::collapseAllRequested, this, &MainWindow::triggerCollapseAll);
    connect(m_expandCollapseBar, &ExpandCollapseBar::toggleHiddenSelectedRequested, this, &MainWindow::triggerToggleHiddenSelected);
    connect(m_expandCollapseBar, &ExpandCollapseBar::toggleShowHiddenRequested, this, &MainWindow::triggerToggleShowHidden);
    connect(m_expandCollapseBar, &ExpandCollapseBar::toggleRunningOnlyRequested, this, &MainWindow::triggerToggleRunningOnly);
    connect(m_expandCollapseBar, &ExpandCollapseBar::eyeEasterEggTriggered, this, &MainWindow::showEyeEasterEgg);

    connect(m_commandTree, &CommandTreeWidget::tabsReordered, this, [this](const QVector<TreeNodePlacement> &placements) {
        // Sem reconstruir a árvore: recriar as abas no meio do reorder travava o segundo arraste.
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
    connect(m_commandTree, &CommandTreeWidget::selectionChanged, this,
        [this](const QString &itemId, bool isFolder) {
            if (isNoteId(itemId)) {
                if (itemId == m_restoredSelectionId) {
                    handleCommandSelectionChanged(itemId, true); // só as barras: a nota não vem para a frente
                } else {
                    handleNoteSelected(itemId);
                }
                return;
            }
            handleCommandSelectionChanged(itemId, isFolder);
        });
    // Clicar no item já selecionado não emite selectionChanged: se a saída mostra outro comando (um KIP iniciado fora
    // da árvore), o clique precisa trazer a saída deste.
    connect(m_commandTree, &CommandTreeWidget::commandClicked, this, [this](const QString &commandId) {
        if (!m_smartFocus) {
            return;
        }
        if (commandId != m_connectedTerminalCommandId) {
            handleCommandSelectionChanged(commandId, false);
        }
        revealViewQuietly(layout::view_id::kOutput); // já selecionado e com a aba do Documento à frente: volta a Saída
    });
    // Idem para a pasta (ou nota) já selecionada: o clique traz o Documento à frente.
    connect(m_commandTree, &CommandTreeWidget::folderClicked, this, [this](const QString &itemId) {
        if (isNoteId(itemId)) {
            showDocumentPanel();
        } else if (!m_smartFocus) {
            return;
        } else if (itemId == m_docFolderId) {
            if (m_documentPanel && m_documentPanel->documentMode()) { // pasta sem nada a mostrar não leva à aba vazia
                revealViewQuietly(layout::view_id::kDocument);
            }
        } else {
            handleCommandSelectionChanged(itemId, true);
        }
    });
    connect(m_commandTree, &CommandTreeWidget::folderActionActivated, this, &MainWindow::handleFolderActionActivated);
    connect(m_commandTree, &CommandTreeWidget::folderActionContextRequested, this, &MainWindow::showFolderActionMenu);
    // Sem o foco inteligente, saída e documento vêm pelo menu do item e pelos botões do grupo Execução.
    m_commandTree->setFocusPredicates([this](const QString &commandId) { return !m_smartFocus && canShowOutput(commandId); },
                                      [this](const QString &itemId) { return !m_smartFocus && !documentTargetFor(itemId).isEmpty(); });
    connect(m_commandTree, &CommandTreeWidget::commandOutputRequested, this, &MainWindow::showCommandOutput);
    connect(m_commandTree, &CommandTreeWidget::documentRequested, this, &MainWindow::showDocumentFor);
    m_commandTree->setOutputMarksEnabled(!m_smartFocus);
    m_commandTree->setInlineActionsEnabled(m_inlineActions);

    m_actionSidebar = new ActionSidebar(central);
    connect(m_actionSidebar, &ActionSidebar::playSelectedRequested, this, &MainWindow::triggerPlaySelected);
    connect(m_actionSidebar, &ActionSidebar::stopSelectedRequested, this, &MainWindow::triggerStopSelected);
    connect(m_actionSidebar, &ActionSidebar::forceStopSelectedRequested, this, &MainWindow::triggerForceStopSelected);
    connect(m_actionSidebar, &ActionSidebar::resetSelectedRequested, this, &MainWindow::triggerResetSelected);
    connect(m_actionSidebar, &ActionSidebar::editBodySelectedRequested, this, &MainWindow::triggerEditBodySelected);
    connect(m_actionSidebar, &ActionSidebar::showOutputSelectedRequested, this, [this]() {
        const QString id = m_commandTree->currentSelectionId();
        if (canShowOutput(id)) {
            showCommandOutput(id);
        }
    });
    connect(m_actionSidebar, &ActionSidebar::showDocumentSelectedRequested, this,
            [this]() { showDocumentFor(m_commandTree->currentSelectionId()); });

    m_terminalDrawer = new TerminalDrawer(central);
    connect(m_terminalDrawer, &TerminalDrawer::detachedPanelCreated, this, [this](OutputPanel *panel, const QString &commandId) {
        panel->addHeaderWidget(makeCommandActionStrip(commandId));
    });
    connect(m_terminalDrawer, &TerminalDrawer::commandEntered, this, &MainWindow::handleTerminalInputEntered);
    connect(m_terminalDrawer->embeddedPanel(), &OutputPanel::saveAsNoteRequested, this,
            [this]() { saveOutputAsNote(m_connectedTerminalCommandId); });
    // Guarda onde o usuário parou em cada arquivo, o texto não salvo e os recentes. Lida em segundo plano; ao sair, o
    // estado da tela é capturado e o arquivo gravado.
    m_docStore = new DocSessionStore(m_configManager.docStateFilePath(), this);
    m_docStore->load();
    connect(m_docStore, &DocSessionStore::draftsChanged, this, &MainWindow::refreshDraftMarks);
    connect(m_docStore, &DocSessionStore::loaded, this, &MainWindow::refreshDraftMarks);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_documentPanel) {
            m_documentPanel->captureState();
        }
        m_docStore->flush();
    });
    connect(m_terminalDrawer->outputTabs(), &OutputTabsBar::tabActivated, this, &MainWindow::activateOutputTab);
    m_terminalDrawer->outputTabs()->setQuickRunVisible(true);
    connect(m_terminalDrawer->outputTabs(), &OutputTabsBar::quickRunRequested, this, &MainWindow::showQuickRun);
    connect(m_terminalDrawer->outputTabs(), &OutputTabsBar::tabCloseRequested, this, &MainWindow::closeOutputTab);
    connect(m_terminalDrawer->outputTabs(), &OutputTabsBar::tabMoved, this, [this](const QString &id, int newIndex) {
        const int from = m_outputTabOrder.indexOf(id);
        if (from < 0) {
            return;
        }
        m_outputTabOrder.move(from, std::clamp(newIndex, 0, int(m_outputTabOrder.size()) - 1));
        refreshOutputTabs();
    });
    connect(m_terminalDrawer->outputTabs(), &OutputTabsBar::tabContextRequested, this, &MainWindow::showOutputTabMenu);
    // Puxar uma guia para fora da barra a transforma num painel (`run:<id>`), ver DockHost::beginViewDrag.
    m_terminalDrawer->outputTabs()->setExternalDragAllowed([this]() { return m_dock && m_dock->editMode(); });
    connect(m_terminalDrawer->outputTabs(), &OutputTabsBar::tabDragStarted, this, [this](const QString &) {
        if (m_dock) {
            m_dock->beginViewDrag();
        }
    });
    connect(m_terminalDrawer->outputTabs(), &OutputTabsBar::tabDragFinished, this,
            [this](const QString &id, Qt::DropAction action, const QPoint &globalPos) {
        if (m_dock) {
            const QString panelId = panelIdForCommand(id);
            m_dock->endViewDrag(panelId, action, globalPos);
            // Um app KIP num painel vira mini app e abre com o Kai (desligável no menu do painel).
            if (panelId.startsWith(layout::view_id::kKipPrefix) && m_dock->model().isPlaced(panelId)) {
                setKipPanelAutoStart(id, true);
            }
        }
    });
    connect(m_terminalDrawer->embeddedPanel(), &OutputPanel::kipFocusWindowRequested, this, [this]() {
        const QString id = layout::view_id::kKipPrefix + m_connectedTerminalCommandId;
        if (m_dock && m_dock->model().isPlaced(id)) {
            m_dock->revealView(id);
        }
    });
    connect(m_terminalDrawer, &TerminalDrawer::interruptRequested, this, &MainWindow::handleTerminalInterrupt);
    connect(m_terminalDrawer, &TerminalDrawer::eofRequested, this, &MainWindow::handleTerminalEof);
    connect(m_terminalDrawer, &TerminalDrawer::rawTerminalInput, this, &MainWindow::handleTerminalRawInput);
    connect(m_terminalDrawer, &TerminalDrawer::terminalSizeChanged, this, &MainWindow::handleTerminalSizeChanged);
    connect(m_terminalDrawer, &TerminalDrawer::firstErrorInFormattedOutput, this, &MainWindow::handleFirstErrorInFormattedOutput);
    // As opções de exibição da Saída são globais (valem entre sessões e comandos): qualquer alteração grava todas.
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
    connectPipelineAuxSignals(m_pipeline);
    connect(m_terminalDrawer, &TerminalDrawer::closeRequested, this, &MainWindow::handleTerminalCloseRequested);
    // O colapso da Saída (só a tela de boas-vindas e o modo foco o usam) não é lembrado entre sessões.
    connect(m_terminalDrawer, &TerminalDrawer::expandedChanged, this,
        [this](bool expanded) {
            if (expanded) {
                if (m_dock) {
                    m_dock->refreshSizes();
                }
            } else if (m_commandsCollapsed) {
                setCommandsCollapsed(false); // a tela nunca fica com os dois recolhidos
            }
        });
    // Quatro containers de posicionamento, começando vazios (quem entra em cada um é decidido em
    // applyActionGroupPlacement()): upper e bottom (horizontais, dentro do treeContainer), left (vertical, primeiro
    // widget do m_horizontalSplitter) e side (vertical, à direita; nome histórico mantido por compatibilidade).
    m_upperActionsContainer = new ActionGroupContainer(Qt::Horizontal, central);
    m_bottomActionsContainer = new ActionGroupContainer(Qt::Horizontal, central);
    m_upperActionsContainer->setEdgeSeparator(Qt::BottomEdge);
    m_bottomActionsContainer->setEdgeSeparator(Qt::TopEdge);
    m_leftActionsContainer = new ActionGroupContainer(Qt::Vertical, central);
    m_sideActionsContainer = new ActionGroupContainer(Qt::Vertical, central);

    // Mesma moldura da Saída (regra "panelCard" em app-stylesheet.cpp).
    auto *treeContainer = new QWidget(central);
    treeContainer->setObjectName(QStringLiteral("panelCard"));
    treeContainer->setAttribute(Qt::WA_StyledBackground, true);
    auto *treeContainerLayout = new QVBoxLayout(treeContainer);
    m_treeContainerLayout = treeContainerLayout;
    const int frameInset = panelFrameInset();
    treeContainerLayout->setContentsMargins(frameInset, frameInset, frameInset, frameInset);
    treeContainerLayout->setSpacing(0);
    treeContainerLayout->addWidget(m_upperActionsContainer);
    treeContainerLayout->addWidget(m_commandTree, 1);
    treeContainerLayout->addWidget(m_bottomActionsContainer);

    // Escondida por padrão (ver updateWelcomeScreenVisibility). Irmã do quadro de painéis (entra em contentLayout) e o
    // substitui: nada dentro dele é tocado, só fica oculto.
    m_welcomeScreen = new WelcomeScreen(central);
    m_welcomeScreen->setVisible(false);
    connect(m_welcomeScreen, &WelcomeScreen::newFolderRequested, this, &MainWindow::handleNewFolderRequested);
    connect(m_welcomeScreen, &WelcomeScreen::newCommandRequested, this, &MainWindow::handleNewCommandRequested);
    connect(m_welcomeScreen, &WelcomeScreen::closeRequested, this, &MainWindow::handleWelcomeScreenClosed);

    m_horizontalSplitter = new QSplitter(Qt::Horizontal, central);
    m_horizontalSplitter->setObjectName(QStringLiteral("actionsSplitter"));
    m_horizontalSplitter->setChildrenCollapsible(false);
    m_horizontalSplitter->addWidget(m_leftActionsContainer);
    m_horizontalSplitter->addWidget(treeContainer);
    m_horizontalSplitter->addWidget(m_sideActionsContainer);
    m_horizontalSplitter->setStretchFactor(0, 0);
    m_horizontalSplitter->setStretchFactor(1, 1);
    m_horizontalSplitter->setStretchFactor(2, 0);
    // As colunas de ícones sempre seguem o próprio sizeHint: os handles 1 e 2 (os adjacentes a elas) ficam travados.
    if (QSplitterHandle *leftHandle = m_horizontalSplitter->handle(1)) {
        leftHandle->setEnabled(false);
    }
    if (QSplitterHandle *sideHandle = m_horizontalSplitter->handle(2)) {
        sideHandle->setEnabled(false);
    }

    // Construído uma vez; o DockHost decide depois onde ele entra, sem recriar.
    m_mainAreaWrapper = new QWidget(central);
    auto *mainAreaLayout = new QVBoxLayout(m_mainAreaWrapper);
    mainAreaLayout->setContentsMargins(0, 0, 0, 0);
    mainAreaLayout->setSpacing(12);
    mainAreaLayout->addWidget(m_horizontalSplitter, 1);

    layout->addWidget(m_topBar);

    m_content = new QWidget(central);
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(16, 8, 16, 16);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(m_welcomeScreen, 1); // começa oculta (ver setWelcomeMode)
    // O DockHost é adicionado a este layout em setupLayout().

    layout->addWidget(m_content, 1);

    m_statusLine = new StatusLine(central);
    connect(m_statusLine, &StatusLine::runningClicked, this, &MainWindow::handleShowProcessListRequested);
    connect(m_statusLine, &StatusLine::notificationsRequested, this, &MainWindow::handleNotificationHistoryRequested);
    connect(&m_notificationHistory, &core::NotificationHistory::unreadCountChanged,
            m_statusLine, &StatusLine::setUnreadNotifications);
    m_statusLine->setUnreadNotifications(m_notificationHistory.unreadCount());
    layout->addWidget(m_statusLine);

    setCentralWidget(central);

    m_loadingOverlay = new LoadingOverlay(this);

    m_projectSelector = new ProjectSelector(this);

    // O atraso base garante que janela, árvore e conexões de sinais estejam prontas antes do primeiro auto-run (senão
    // o comando rodava sem log).
    QTimer::singleShot(300, this, &MainWindow::scheduleAutoRunCommands);
    // Com a interface ociosa, monta o leitor de documentos: a primeira pasta com documentação abre sem esse custo.
    QTimer::singleShot(1500, this, [this]() {
        if (m_documentPanel) {
            m_documentPanel->prepareViewer();
        }
    });
}

} // namespace kai::ui
