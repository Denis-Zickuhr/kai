
#include "core/folder-actions.h"

#include "ui/features/docs/document-panel.h"

#include "ui/main-window.h"

#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/panel-card.h"
#include "ui/layout/layout-store.h"

#include "ui/features/ai/ai-chat-window.h"
#include "ui/features/ai/main-window-agent-host.h"

#include "engine/http-cookie-store.h"

#include "ui/shared/panel-metrics.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QSplitter>

#include "ui/shared/lucide-icons.h"

#include <QApplication>

#include "ui/features/environments/environment-manager-dialog.h"

#include "ui/features/history/run-history-dialog.h"
#include "ui/features/history/notification-history-dialog.h"

#include "ui/features/output/run-mirror-panel.h"

#include "utils/translation-manager.h"

#include "ui/shared/tray-menu.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";

// Repassa o arraste do mouse num QSplitterHandle como (press/move, X global); o handle em si não move nada.
// Serve para a borda da lista de comandos redimensionar o divisor externo.
class HandleDragForwarder : public QObject {
public:
    HandleDragForwarder(QWidget *handle, std::function<void(bool, int)> onEvent)
        : QObject(handle), m_onEvent(std::move(onEvent))
    {
        handle->installEventFilter(this);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched);
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragging = true;
                m_onEvent(true, me->globalPosition().toPoint().x());
                return true;
            }
            break;
        }
        case QEvent::MouseMove:
            if (m_dragging) {
                m_onEvent(false, static_cast<QMouseEvent *>(event)->globalPosition().toPoint().x());
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (m_dragging) {
                m_dragging = false;
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    std::function<void(bool, int)> m_onEvent;
    bool m_dragging = false;
};
}

void MainWindow::toggleOutputFocus()
{
    if (!m_terminalDrawer) {
        return;
    }
    // Fechada ou em outra aba, a Saída volta à tela: pedir o foco dela é uma ação explícita.
    if (!m_terminalDrawer->isVisible()) {
        showOutputPanel();
    }
    if (!m_terminalDrawer->isVisible()) {
        m_commandTree->focusFirstVisibleItem();
        return;
    }
    if (!m_terminalDrawer->isExpanded()) {
        m_terminalDrawer->setExpanded(true);
    }
    if (m_terminalDrawer->isAncestorOf(QApplication::focusWidget())) {
        m_commandTree->focusFirstVisibleItem();
        return;
    }
    // Terminal interativo: as teclas vão direto ao PTY, então o foco tem de ir ao terminal e não ao campo de stdin.
    const auto it = m_commandsById.constFind(m_connectedTerminalCommandId);
    const bool connectedIsInteractive = it != m_commandsById.constEnd()
        && it->type == core::CommandType::Command && it->interactiveTerminal;
    if (connectedIsInteractive) {
        m_terminalDrawer->focusInteractiveTerminal();
    } else {
        m_terminalDrawer->focusInput();
    }
}

void MainWindow::applyActionGroupPlacement()
{
    const core::SettingsData settings = m_configManager.loadSettings();

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
        // "side" é o valor persistido para "direita" (mantido por compatibilidade com settings.json já salvos).
        if (placement == QStringLiteral("side")) {
            sideGroups.append(group);
        } else if (placement == QStringLiteral("bottom")) {
            bottomGroups.append(group);
        } else if (placement == QStringLiteral("left")) {
            leftGroups.append(group);
        } else if (placement == QStringLiteral("hidden")) {
            // Os atalhos continuam valendo (métodos trigger*), o grupo só não aparece. O hide() explícito é
            // obrigatório: um grupo que nunca passou por setGroups() seria exibido solto em (0,0) do pai quando
            // a janela aparecer.
            group->hide();
        } else {
            // "upper" (padrão) ou valor desconhecido.
            upperGroups.append(group);
        }
    };
    // Com as ações inline os botões de execução ficam na linha do item: o grupo seria redundante.
    place(m_actionSidebar, settings.inlineActions ? QStringLiteral("hidden") : settings.executionActionsPlacement);
    place(m_expandCollapseBar, settings.displayActionsPlacement);
    place(m_itemActionsBar, settings.itemActionsPlacement);

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

    // O handle de resize das colunas de ícones está desabilitado, e a distribuição inicial do QSplitter roda com os
    // containers vazios: a largura natural de cada coluna (0 se oculta) tem de ser reaplicada sempre que os grupos mudam.
    if (m_horizontalSplitter && m_leftActionsContainer && m_sideActionsContainer) {
        const int leftW = m_leftActionsContainer->isHidden() ? 0 : m_leftActionsContainer->contentWidth();
        const int sideW = m_sideActionsContainer->isHidden() ? 0 : m_sideActionsContainer->contentWidth();
        m_horizontalSplitter->setSizes({leftW, 10000, sideW});
    }

    updateTreeBorderResizeHandles();
    syncPanelBarHeights();
}

void MainWindow::applyFrameInsets()
{
    // Sem o recuo, o canto quadrado dos filhos cobre o arco da borda (ver panelFrameInset).
    const int inset = panelFrameInset();
    if (m_treeContainerLayout) {
        m_treeContainerLayout->setContentsMargins(inset, inset, inset, inset);
    }
    if (m_terminalDrawer) {
        m_terminalDrawer->refreshFrameInset();
    }
    if (m_viewRegistry) {
        for (const QString &id : m_viewRegistry->registeredIds()) {
            layout::refreshPanelCardInsets(m_viewRegistry->existingWidget(id));
        }
        for (const QPointer<RunMirrorPanel> &mirror : std::as_const(m_mirrors)) {
            if (mirror) {
                mirror->refreshStyle();
            }
        }
    }
}

void MainWindow::syncPanelBarHeights()
{
    if (!m_terminalDrawer) {
        return;
    }
    // As barras de ação podem ser mais altas que a base do design system (os botões não acompanham a densidade):
    // vale a maior altura real entre elas, com a base como piso.
    const int baseHeight = utils::tokens::controlHeight() + utils::tokens::space(2);
    int height = baseHeight;
    for (const ActionGroupContainer *bar : {m_upperActionsContainer, m_bottomActionsContainer}) {
        if (bar && !bar->isHidden()) {
            height = qMax(height, bar->naturalBarHeight());
        }
    }
    // Teto: um sizeHint transitório (grupos que acabaram de trocar de orientação) inflaria todas as barras de uma vez
    // e empurraria a lista para fora da tela.
    height = qMin(height, baseHeight + utils::tokens::space(3));
    for (ActionGroupContainer *bar : {m_upperActionsContainer, m_bottomActionsContainer}) {
        if (bar) {
            bar->setBarHeight(height);
        }
    }
    m_terminalDrawer->setBarHeight(height);
    if (m_dock) {
        m_dock->setBarHeight(height);
    }
    if (m_commandTree) {
        m_commandTree->setTabBarHeight(height);
    }
}

// A célula da Saída quando ela está lado a lado com a da lista de comandos, e de que lado da lista fica. É o único
// arranjo com uma coluna de ações entre a lista e a Saída (ver updateTreeBorderResizeHandles).
struct OutputBesideCommands {
    layout::Zone zone;
    bool onLeft = false;
};

static std::optional<OutputBesideCommands> outputSideBesideCommands(const layout::DockHost *dock)
{
    if (!dock) {
        return std::nullopt;
    }
    const auto output = dock->zoneOf(layout::view_id::kOutput);
    const auto commands = dock->zoneOf(layout::view_id::kCommands);
    if (!output || !commands || *output == *commands || !dock->zoneWidget(*output) || !dock->zoneWidget(*commands)) {
        return std::nullopt;
    }
    const layout::GridPlan &plan = dock->plan();
    const auto locate = [&plan](layout::Zone zone) -> std::pair<int, int> {
        for (int s = 0; s < plan.stacks.size(); ++s) {
            const int c = int(plan.stacks.at(s).cells.indexOf(zone));
            if (c >= 0) {
                return {s, c};
            }
        }
        return {-1, -1};
    };
    const auto [os, oc] = locate(*output);
    const auto [cs, cc] = locate(*commands);
    if (os < 0 || cs < 0) {
        return std::nullopt;
    }
    // Modo vertical: as pilhas são colunas, vizinhas se são consecutivas; horizontal: as células de uma linha.
    const bool columns = plan.expansion == layout::Expansion::Vertical;
    if (columns ? std::abs(os - cs) == 1 : (os == cs && std::abs(oc - cc) == 1)) {
        return OutputBesideCommands{*output, columns ? os < cs : oc < cc};
    }
    return std::nullopt;
}

void MainWindow::updateTreeBorderResizeHandles()
{
    if (!m_horizontalSplitter || !m_leftActionsContainer || !m_sideActionsContainer) {
        return;
    }
    // Só há coluna ENTRE a lista e a Saída quando a Saída está numa lateral: à direita -> coluna "side" (handle 2); à
    // esquerda -> coluna "left" (handle 1). Embaixo (ou em qualquer outro arranjo) não há nada a repassar.
    const auto side = outputSideBesideCommands(m_dock);
    struct Border { int handleIndex; bool active; };
    const Border borders[] = {
        {1, side && side->onLeft && !m_leftActionsContainer->isHidden()},
        {2, side && !side->onLeft && !m_sideActionsContainer->isHidden()},
    };
    for (const Border &border : borders) {
        QSplitterHandle *handle = m_horizontalSplitter->handle(border.handleIndex);
        if (!handle) {
            continue;
        }
        // Uma vez por handle: o repassador vive como filho dele e lê o estado atual a cada evento.
        if (!handle->property("kaiTreeBorderForwarder").toBool()) {
            handle->setProperty("kaiTreeBorderForwarder", true);
            new HandleDragForwarder(handle, [this](bool press, int globalX) {
                dragTreeBorder(press, globalX);
            });
        }
        handle->setEnabled(border.active);
        handle->setCursor(border.active ? Qt::SplitHCursor : Qt::ArrowCursor);
    }
}

void MainWindow::dragTreeBorder(bool press, int globalX)
{
    const auto side = outputSideBesideCommands(m_dock);
    if (!side || m_commandsCollapsed) {
        return;
    }
    if (press) {
        m_treeDragStartX = globalX;
        m_treeDragStartWidth = m_dock->zoneWidth(side->zone);
        return;
    }
    // Arrastar a borda para a direita alarga a lateral ESQUERDA (a Saída) ou encolhe a DIREITA (a Saída à direita).
    const int delta = globalX - m_treeDragStartX;
    m_dock->setZoneWidth(side->zone, side->onLeft ? m_treeDragStartWidth + delta : m_treeDragStartWidth - delta);
}

void MainWindow::setupLayout()
{
    using namespace layout;
    m_viewRegistry = new ViewRegistry(this);

    ViewSpec commands;
    commands.id = view_id::kCommands;
    commands.titleKey = QStringLiteral("layout.view.commands");
    commands.icon = QStringLiteral("folder-tree");
    commands.home = Home::Primary;
    m_viewRegistry->registerView(commands, m_mainAreaWrapper);

    ViewSpec output;
    output.id = view_id::kOutput;
    output.titleKey = QStringLiteral("layout.view.output");
    output.icon = QStringLiteral("square-terminal");
    output.home = Home::Secondary;
    output.minWidth = 220;
    m_viewRegistry->registerView(output, m_terminalDrawer);
    // Sem um mínimo, numa lateral estreita a Saída viraria uma tira ilegível.
    m_terminalDrawer->setMinimumWidth(220);

    registerPanelViews();

    registerRunViews();

    m_layoutStore = new LayoutStore(m_configManager.layoutFilePath(), this);
    m_dock = new DockHost(m_viewRegistry, this, m_content);
    if (auto *contentLayout = qobject_cast<QVBoxLayout *>(m_content->layout())) {
        contentLayout->addWidget(m_dock, 1);
    }
    m_layoutController = new LayoutController(m_dock, m_layoutStore, {}, this, this);
    connect(m_dock, &DockHost::maximizedChanged, this, &MainWindow::syncCommandsCollapsed);
    connect(m_dock, &DockHost::restoreRequested, this, &MainWindow::restoreDefaultLayout);
    connect(m_dock, &DockHost::arrangementChanged, this, [this]() {
        applyDrawerPosition();
        syncDocumentPlacement();
        updateTreeBorderResizeHandles();
    });
    connect(m_topBar->viewMenu(), &QMenu::aboutToShow, this,
            [this]() { m_layoutController->populateViewMenu(m_topBar->viewMenu()); });
    // Até o layout.json ser lido (restoreLayoutFromDisk, no fim do construtor) vale o padrão: Comandos e Saída já existem
    // enquanto a configuração carrega.
    m_dock->setModel(LayoutModel::defaultLayout());
    m_dock->setVisible(!m_welcomeMode);
}

// Lê o layout.json (ou migra o antigo outputPosition) e o aplica. Roda depois de os comandos e as configurações estarem
// carregados: os painéis dinâmicos (`kip:<id>`) só são válidos se o comando deles ainda existe.
void MainWindow::restoreLayoutFromDisk()
{
    using namespace layout;
    const core::SettingsData settings = m_configManager.loadSettings();
    const LayoutStore::Loaded loaded = m_layoutStore->load(m_viewRegistry->validator(), [&settings]() {
        // Primeira vez com o layout flexível: o que as configurações antigas diziam (posição e proporção da Saída).
        return LayoutModel::fromLegacy(settings.outputPosition, settings.outputSplitterSizes);
    });
    m_layoutController->setPerspectives(loaded.perspectives);
    LayoutModel model = loaded.layout;
    // O espelho de uma execução vale só para a sessão em que nasceu (o log dela já não existe): não volta ao reiniciar.
    for (const QString &id : model.placedViews() + model.closedViews()) {
        if (id.startsWith(view_id::kRunPrefix)) {
            model.forgetView(id);
        }
    }
    // O modo foco (a Saída maximizada) tem a própria persistência e foi restaurado antes: reaplica por cima do layout.
    if (m_commandsCollapsed) {
        model.setMaximized(view_id::kOutput);
    }
    m_dock->setModel(model);
    syncCommandsCollapsed();
    // O menu Exibir é repovoado a cada abertura; começa com itens para nunca aparecer vazio.
    m_layoutController->populateViewMenu(m_topBar->viewMenu());
    // Um layout migrado das configurações antigas (ou consertado) é gravado já, para o layout.json passar a mandar.
    if (!loaded.fileExisted && loaded.layout != LayoutModel::defaultLayout()) {
        m_layoutController->scheduleSave();
    }
}

// O leitor em si só é montado no primeiro documento.
QWidget *MainWindow::createDocumentPanel()
{
    m_documentPanel = new DocumentPanel;
    m_documentPanel->setSessionStore(m_docStore);
    connect(m_documentPanel, &DocumentPanel::documentKaiLinkActivated, this, &MainWindow::handleDocKaiLink);
    connect(m_documentPanel, &DocumentPanel::documentDirectoryFixAutomatically, this,
            [this](const QString &dir, const QString &suggestion) { fixDocDirectory(true, dir, suggestion); });
    connect(m_documentPanel, &DocumentPanel::documentDirectoryFixManually, this,
            [this](const QString &dir) { fixDocDirectory(false, dir, QString()); });
    connect(m_documentPanel, &DocumentPanel::documentFoldersRequested, this, &MainWindow::showDocsMenu);
    connect(m_documentPanel, &DocumentPanel::documentNewNoteRequested, this, &MainWindow::handleNewNoteRequested);
    connect(m_documentPanel, &DocumentPanel::documentNoteDeleteRequested, this, &MainWindow::handleNoteDeleteRequested);
    connect(m_documentPanel, &DocumentPanel::documentNoteFileActionRequested, this, &MainWindow::exportNote);
    // Os nomes do autocompletar dos links do Kai no editor de documentos (`kai:run/`, `kai:open/`, `kai:env/`). Os
    // ambientes vêm do disco: ficam em cache por alguns segundos para a digitação não reler o arquivo a cada tecla.
    struct EnvironmentCache {
        QStringList names;
        QElapsedTimer age;
    };
    auto cache = std::make_shared<EnvironmentCache>();
    m_documentPanel->setNameProvider([this, cache](const QString &kind) {
        QStringList names;
        if (kind == QLatin1String("commands")) {
            for (const core::Command &command : std::as_const(m_commandsData.commands)) {
                if (!command.name.isEmpty() && !core::isFolderActionCommandId(command.id)) {
                    names << command.name;
                }
            }
        } else if (kind == QLatin1String("folders")) {
            for (const core::Folder &folder : std::as_const(m_commandsData.folders)) {
                if (!folder.name.isEmpty()) {
                    names << folder.name;
                }
            }
        } else if (kind == QLatin1String("environments")) {
            if (!cache->age.isValid() || cache->age.elapsed() > 4000) {
                QString active;
                cache->names = environmentNames(active);
                cache->age.restart();
            }
            names = cache->names;
        }
        return names;
    });
    // Notas editadas no leitor: quem grava é o app (notes.json).
    m_documentPanel->setNoteSaver([this](const QString &noteId, const QString &content) {
        for (core::Note &note : m_notes) {
            if (note.id == noteId) {
                note.content = content;
                return m_configManager.saveNotes(m_notes);
            }
        }
        return false;
    });
    QWidget *card = layout::wrapInPanelCard(m_documentPanel);
    card->setParent(m_content);
    return card;
}

// Processos, histórico, notificações, chat de IA, variáveis e logs: cada um é construído sob demanda, na primeira vez
// que aparece.
void MainWindow::registerPanelViews()
{
    using namespace layout;
    // Abaixo da largura mínima o quadro (e depois o painel) rola em vez de espremer botões e textos.
    constexpr int kProcessesMinWidth = 300;
    constexpr int kHistoryMinWidth = 400;
    constexpr int kNotificationsMinWidth = 400;
    constexpr int kVariablesMinWidth = 520;
    constexpr int kAiChatMinWidth = 360;
    constexpr int kLogsMinWidth = 360;
    const auto base = [](const QString &id, const char *titleKey, const char *icon, Home home) {
        ViewSpec spec;
        spec.id = id;
        spec.titleKey = QString::fromLatin1(titleKey);
        spec.icon = QString::fromLatin1(icon);
        spec.home = home;
        return spec;
    };

    ViewSpec document = base(view_id::kDocument, "layout.view.document", "file-text", Home::Secondary);
    document.focus = [this]() { m_documentPanel->focusContent(); };
    m_viewRegistry->registerView(document, createDocumentPanel());

    ViewSpec processes = base(view_id::kProcesses, "layout.view.processes", "activity", Home::Secondary);
    processes.minWidth = kProcessesMinWidth;
    processes.factory = [this]() -> QWidget * {
        m_processListDialog->setEmbedded(true);
        QWidget *card = wrapInPanelCard(m_processListDialog, QSize(kProcessesMinWidth, 240));
        m_processListDialog->show(); // escondido na criação
        return card;
    };
    m_viewRegistry->registerView(processes);

    ViewSpec history = base(view_id::kHistory, "layout.view.history", "clock", Home::Secondary);
    history.minWidth = kHistoryMinWidth;
    history.factory = [this]() -> QWidget * {
        auto *panel = new RunHistoryDialog(&m_runHistory, nullptr, true);
        connect(panel, &RunHistoryDialog::rerunRequested, this, [this](const QString &commandId) {
            if (m_commandsById.contains(commandId)) {
                handleCommandActivated(commandId);
            } else {
                utils::Logger::warning(kLogTag,
                    QStringLiteral("Reexecução: comando '%1' não existe mais.").arg(commandId));
            }
        });
        return wrapInPanelCard(panel, QSize(kHistoryMinWidth, 320));
    };
    m_viewRegistry->registerView(history);

    ViewSpec notifications = base(view_id::kNotifications, "layout.view.notifications", "bell", Home::Secondary);
    notifications.minWidth = kNotificationsMinWidth;
    notifications.factory = [this]() -> QWidget * {
        auto *panel = new NotificationHistoryDialog(&m_notificationHistory, nullptr, true);
        // "Ir para o comando" só aparece enquanto o comando ainda existe.
        panel->setCommandResolver([this](const QString &commandId) {
            const auto it = m_commandsById.constFind(commandId);
            return it != m_commandsById.constEnd() ? it->name : QString();
        });
        connect(panel, &NotificationHistoryDialog::commandRequested, this, &MainWindow::showCommandInTree);
        return wrapInPanelCard(panel, QSize(kNotificationsMinWidth, 240));
    };
    m_viewRegistry->registerView(notifications);

    // As edições valem sozinhas, sem OK/Cancelar.
    ViewSpec variables = base(view_id::kVariables, "layout.view.variables", "variable", Home::Secondary);
    variables.minWidth = kVariablesMinWidth;
    variables.factory = [this]() -> QWidget * {
        const core::SettingsData settings = m_configManager.loadSettings();
        m_environmentsPanel = new EnvironmentManagerDialog(
            settings.environments, settings.activeEnvironmentId, nullptr, &m_envManager, m_commandsData.folders,
            [this]() { return m_configManager.loadPersistedDynamicVars(); },
            [this](const QMap<QString, QMap<QString, QString>> &data) {
                return m_configManager.savePersistedDynamicVars(data);
            },
            true);
        m_environmentsPanel->setObjectName(QStringLiteral("variablesPanel"));
        connect(m_environmentsPanel, &EnvironmentManagerDialog::changed, this, &MainWindow::saveEnvironmentsFromPanel);
        return wrapInPanelCard(m_environmentsPanel, QSize(kVariablesMinWidth, 420));
    };
    m_viewRegistry->registerView(variables);

    ViewSpec logs = base(view_id::kLogs, "layout.view.logs", "scroll-text", Home::Secondary);
    logs.minWidth = kLogsMinWidth;
    logs.factory = [this]() -> QWidget * { return wrapInPanelCard(new LogsPanel, QSize(kLogsMinWidth, 240)); };
    m_viewRegistry->registerView(logs);

    // Sem provedor configurado: nenhum botão, nenhum painel, nenhuma rede.
    m_aiConfigured = m_configManager.loadAiSettings().isConfigured();
    ViewSpec aiChat = base(view_id::kAiChat, "layout.view.ai_chat", "sparkles", Home::Secondary);
    aiChat.minWidth = kAiChatMinWidth;
    aiChat.available = [this]() { return m_aiConfigured; };
    aiChat.factory = [this]() -> QWidget * {
        m_aiChat = new AiChatWindow(m_agentHost, nullptr, true);
        m_aiChat->setEmbedded(true);
        return wrapInPanelCard(m_aiChat, QSize(kAiChatMinWidth, 300));
    };
    m_viewRegistry->registerView(aiChat);
}

// `run:<id>` é o espelho de uma execução (a guia da Saída puxada para uma zona); `kip:<id>` é um comando KIP fixado como
// mini-app. Os dois só existem enquanto o comando existe.
void MainWindow::registerRunViews()
{
    using namespace layout;
    const auto spec = [this](const QString &id, const QString &prefix, bool kip) -> std::optional<ViewSpec> {
        const QString commandId = id.mid(prefix.size());
        const auto it = m_commandsById.constFind(commandId);
        if (it == m_commandsById.constEnd() || (kip && !(it->kip && it->type == core::CommandType::Command))) {
            return std::nullopt;
        }
        ViewSpec view;
        view.id = id;
        view.titleProvider = [this, commandId]() {
            const auto found = m_commandsById.constFind(commandId);
            return found != m_commandsById.constEnd() ? found->name : commandId;
        };
        view.icon = LucideIcons::has(it->icon) ? it->icon : kip ? QStringLiteral("sparkles") : QStringLiteral("square-terminal");
        if (kip) {
            view.menuExtras = [this, commandId](QMenu *menu) {
                QAction *autoStart = menu->addAction(utils::tr(QStringLiteral("layout.kip_panel.auto_start")));
                autoStart->setCheckable(true);
                autoStart->setChecked(kipPanelAutoStarts(commandId));
                connect(autoStart, &QAction::toggled, this, [this, commandId](bool on) { setKipPanelAutoStart(commandId, on); });
            };
        }
        view.home = Home::Secondary;
        view.factory = [this, commandId, kip]() -> QWidget * { return createMirrorPanel(commandId, kip); };
        return view;
    };
    m_viewRegistry->registerPrefix(view_id::kRunPrefix, [spec](const QString &id) { return spec(id, view_id::kRunPrefix, false); });
    m_viewRegistry->registerPrefix(view_id::kKipPrefix, [spec](const QString &id) { return spec(id, view_id::kKipPrefix, true); });
}

QWidget *MainWindow::createMirrorPanel(const QString &commandId, bool kip)
{
    const auto it = m_commandsById.constFind(commandId);
    const QString name = it != m_commandsById.constEnd() ? it->name : commandId;
    const auto kind = kip ? RunMirrorPanel::Kind::Kip : RunMirrorPanel::Kind::Run;
    auto *panel = new RunMirrorPanel(commandId, name, kind);
    const bool kipCommand = it != m_commandsById.constEnd() && it->kip && it->type == core::CommandType::Command;
    const bool ownsOutput = it != m_commandsById.constEnd() && panelOwnsOutput(*it);
    panel->setFormattedOutput(it != m_commandsById.constEnd() && it->formattedOutput && !kipCommand);
    panel->setAcceptsInput(ownsOutput && !kipCommand);
    panel->output()->addHeaderWidget(makeCommandActionStrip(commandId));
    OutputPanel *embedded = m_terminalDrawer->embeddedPanel();
    panel->applyBarHeight(embedded->barHeight());
    panel->seed(embedded, m_commandLogs.value(commandId));
    panel->setInitialStatus(executionStatusFor(commandId));
    // Uma view interativa por sessão: enquanto o painel existe, a Saída embutida mostra o cartão "rodando em outro painel".
    if (ownsOutput) {
        if (kipCommand) {
            if (engine::KipSession *session = kipSessionFor(commandId)) {
                panel->bindKipSession(session, !m_kipAutoStarted.contains(commandId));
            }
            m_terminalDrawer->setKipPinned(commandId, true);
        } else {
            // Sem a Saída por perto, o campo de resposta do painel fala com o processo do comando.
            OutputPanel *output = panel->output();
            connect(output, &OutputPanel::commandEntered, this, [this, commandId](const QString &text) {
                if (engine::ProcessRunner *runner = runnerForCommandId(commandId); runner && runner->isRunning()) {
                    runner->writeToStdin(text);
                }
            });
            connect(output, &OutputPanel::interruptRequested, this, [this, commandId]() {
                if (engine::ProcessRunner *runner = runnerForCommandId(commandId); runner && runner->isRunning()) {
                    runner->writeRaw(QStringLiteral("\x03"));
                }
            });
            connect(output, &OutputPanel::eofRequested, this, [this, commandId]() {
                if (engine::ProcessRunner *runner = runnerForCommandId(commandId); runner && runner->isRunning()) {
                    runner->writeRaw(QStringLiteral("\x04"));
                }
            });
        }
        // Sem outro painel do mesmo comando, a Saída embutida volta a poder mostrar a view. O contexto é a Saída (não a
        // janela): ao fechar o app o painel morre durante a destruição da janela.
        TerminalDrawer *drawer = m_terminalDrawer;
        const QString otherKey = (kip ? layout::view_id::kRunPrefix : layout::view_id::kKipPrefix) + commandId;
        const QString ownKey = (kip ? layout::view_id::kKipPrefix : layout::view_id::kRunPrefix) + commandId;
        connect(panel, &QObject::destroyed, drawer, [this, drawer, commandId, otherKey, ownKey, kipCommand]() {
            // Durante o `destroyed` o QPointer do mapa ainda aponta para o painel: tira-o antes de perguntar quem sobrou.
            m_mirrors.remove(ownKey);
            const auto other = m_mirrors.constFind(otherKey);
            const bool stillInPanel = other != m_mirrors.constEnd() && other.value();
            if (kipCommand) {
                drawer->setKipPinned(commandId, stillInPanel);
            }
            // A guia reaparece (sem tomar a frente) se há o que mostrar.
            const bool hasSomething = kipCommand ? (kipSessionFor(commandId) || isCommandRunning(commandId))
                                                 : hasOutputToShow(commandId);
            if (!stillInPanel && hasSomething) {
                if (!m_outputTabOrder.contains(commandId)) m_outputTabOrder.append(commandId);
                refreshOutputTabs();
            }
        });
        // Adiado: a barra de guias não pode mudar no meio do arraste que criou o painel.
        QTimer::singleShot(0, this, [this, commandId]() { moveOutOfOutput(commandId); });
    }
    connect(panel, &RunMirrorPanel::runRequested, this, [this](const QString &id) { handleCommandActivated(id); });
    m_mirrors.insert((kip ? layout::view_id::kKipPrefix : layout::view_id::kRunPrefix) + commandId, panel);
    return panel;
}

ActionSidebar *MainWindow::makeCommandActionStrip(const QString &commandId)
{
    auto *strip = new ActionSidebar();
    strip->setProperty("kaiCommandId", commandId);
    strip->setOrientation(Qt::Horizontal);
    strip->setFocusActions(false, false, false);
    strip->applyAccentColor(QColor(utils::tokens::accent()));
    connect(strip, &ActionSidebar::playSelectedRequested, this, [this, commandId]() { handleCommandActivated(commandId); });
    connect(strip, &ActionSidebar::stopSelectedRequested, this, [this, commandId]() { handleKillCommandRequested(commandId); });
    connect(strip, &ActionSidebar::forceStopSelectedRequested, this,
            [this, commandId]() { handleKillCommandRequested(commandId, /*force=*/true); });
    connect(strip, &ActionSidebar::resetSelectedRequested, this, [this, commandId]() { handleResetCommandRequested(commandId); });
    connect(strip, &ActionSidebar::editBodySelectedRequested, this,
            [this, commandId]() { handleQuickEditBodyRequested(commandId); });
    m_commandStrips.append(strip);
    refreshCommandActionStrips();
    return strip;
}

void MainWindow::refreshCommandActionStrips()
{
    m_commandStrips.removeIf([](const QPointer<ActionSidebar> &strip) { return strip.isNull(); });
    for (const QPointer<ActionSidebar> &strip : std::as_const(m_commandStrips)) {
        const QString id = strip->property("kaiCommandId").toString();
        const auto found = m_commandsById.constFind(id);
        const bool known = found != m_commandsById.constEnd();
        strip->setRowContext(known, known, isCommandRunning(id), m_failedCommandIds.contains(id),
                             known && found->type == core::CommandType::Http);
    }
}

bool MainWindow::panelOwnsOutput(const core::Command &command)
{
    return (command.kip && command.type == core::CommandType::Command) || !command.interactiveTerminal;
}

QWidget *MainWindow::dialogParent()
{
    if (m_dock) {
        if (layout::FloatingViewWindow *pinned = m_dock->frontPinnedWindow()) {
            return pinned;
        }
    }
    return this;
}

bool MainWindow::livesInPanel(const QString &commandId) const
{
    const auto it = m_commandsById.constFind(commandId);
    if (it == m_commandsById.constEnd() || !panelOwnsOutput(*it)) {
        return false;
    }
    for (const QString &prefix : {layout::view_id::kRunPrefix, layout::view_id::kKipPrefix}) {
        const auto mirror = m_mirrors.constFind(prefix + commandId);
        // Pode estar no quadro sem ter sido construído ainda (faixa recolhida).
        if ((mirror != m_mirrors.constEnd() && mirror.value()) || isViewPlaced(prefix + commandId)) {
            return true;
        }
    }
    return false;
}

void MainWindow::revealCommandPanel(const QString &commandId)
{
    for (const QString &prefix : {layout::view_id::kRunPrefix, layout::view_id::kKipPrefix}) {
        if (isViewPlaced(prefix + commandId)) {
            revealViewQuietly(prefix + commandId);
            return;
        }
    }
}

void MainWindow::moveOutOfOutput(const QString &commandId)
{
    if (!livesInPanel(commandId)) {
        return;
    }
    const int index = m_outputTabOrder.indexOf(commandId);
    if (index >= 0) {
        m_outputTabOrder.removeAt(index);
    }
    if (m_connectedTerminalCommandId == commandId) {
        if (m_outputTabOrder.isEmpty()) {
            clearOutputPanel();
        } else {
            reconnectTerminalToCommand(m_outputTabOrder.at(qMin(qMax(index, 0), int(m_outputTabOrder.size()) - 1)));
        }
    }
    refreshOutputTabs();
}

void MainWindow::forEachMirror(const QString &commandId, const std::function<void(RunMirrorPanel *)> &action)
{
    for (const QString &prefix : {layout::view_id::kRunPrefix, layout::view_id::kKipPrefix}) {
        const auto it = m_mirrors.constFind(prefix + commandId);
        if (it != m_mirrors.constEnd() && it.value()) {
            action(it.value().data());
        }
    }
}

void MainWindow::setMirrorStatus(const QString &commandId, ExecutionStatus status)
{
    forEachMirror(commandId, [status](RunMirrorPanel *panel) { panel->setStatus(status); });
    refreshCommandActionStrips();
}

ExecutionStatus MainWindow::executionStatusFor(const QString &commandId) const
{
    if (isCommandRunning(commandId)) {
        return ExecutionStatus::Running;
    }
    if (m_skippedReason.contains(commandId)) {
        return ExecutionStatus::Skipped;
    }
    if (m_failedCommandIds.contains(commandId)) {
        return ExecutionStatus::Failed;
    }
    return m_commandLogs.contains(commandId) ? ExecutionStatus::Success : ExecutionStatus::Idle;
}

void MainWindow::openRunPanel(const QString &commandId)
{
    if (m_layoutController && m_commandsById.contains(commandId)) {
        const QString panelId = panelIdForCommand(commandId);
        m_layoutController->focusView(panelId);
        if (panelId.startsWith(layout::view_id::kKipPrefix)) {
            setKipPanelAutoStart(commandId, true);
        }
    }
}

void MainWindow::pinKipPanel(const QString &commandId)
{
    if (m_layoutController && m_commandsById.contains(commandId)) {
        m_layoutController->focusView(layout::view_id::kKipPrefix + commandId);
        setKipPanelAutoStart(commandId, true);
    }
}

QString MainWindow::panelIdForCommand(const QString &commandId) const
{
    const auto it = m_commandsById.constFind(commandId);
    const bool kip = it != m_commandsById.constEnd() && it->kip && it->type == core::CommandType::Command
        && !core::isFolderActionCommandId(commandId);
    return (kip ? layout::view_id::kKipPrefix : layout::view_id::kRunPrefix) + commandId;
}

bool MainWindow::kipPanelAutoStarts(const QString &commandId)
{
    return m_configManager.loadSettings().kipPanelAutoStart.contains(commandId);
}

void MainWindow::setKipPanelAutoStart(const QString &commandId, bool on)
{
    core::SettingsData settings = m_configManager.loadSettings();
    if (settings.kipPanelAutoStart.contains(commandId) == on) {
        return;
    }
    if (on) {
        settings.kipPanelAutoStart << commandId;
    } else {
        settings.kipPanelAutoStart.removeAll(commandId);
    }
    m_configManager.saveSettings(settings);
}

void MainWindow::autoStartKipPanels()
{
    const QStringList ids = m_configManager.loadSettings().kipPanelAutoStart;
    for (const QString &id : ids) {
        const auto it = m_commandsById.constFind(id);
        if (it == m_commandsById.constEnd() || !it->kip || it->type != core::CommandType::Command
            || !isViewPlaced(layout::view_id::kKipPrefix + id) || kipSessionFor(id) || isCommandRunning(id)) {
            continue;
        }
        // Com parâmetros precisaria de um formulário: fica o cartão "Executar" em vez de abrir sozinho.
        if (!it->params.isEmpty()) {
            continue;
        }
        utils::Logger::info(kLogTag, QStringLiteral("Abrindo o app KIP '%1' no painel dele (Abrir com o Kai).").arg(id));
        m_autoStartingKip = true;
        handleCommandActivated(id);
        m_autoStartingKip = false;
    }
}

bool MainWindow::isViewPlaced(const QString &id) const
{
    return m_dock && m_dock->model().isPlaced(id);
}

// Traz o painel à frente da zona dele em resposta a uma seleção na árvore, só se já está no quadro: clicar na árvore não
// reabre o que o usuário fechou nem desfaz o que ele arrumou.
void MainWindow::revealViewQuietly(const QString &id)
{
    if (!isViewPlaced(id)) {
        return;
    }
    const QString maximized = m_dock->model().maximized();
    if (!maximized.isEmpty() && maximized != id) {
        return;
    }
    // Faixa fechada não abre sozinha a cada clique: a seleção só troca o painel já aberto.
    const auto zone = m_dock->model().zoneOf(id);
    if (zone && m_dock->model().zone(*zone).collapsed && m_dock->revealedView(*zone).isEmpty()) {
        return;
    }
    m_dock->revealView(id);
}

// Ao voltar ao quadro o painel Documento pega a seleção atual, que a árvore não lhe contou enquanto esteve fechado.
void MainWindow::syncDocumentPlacement()
{
    const bool placed = isViewPlaced(layout::view_id::kDocument);
    if (placed == m_documentWasPlaced) {
        return;
    }
    m_documentWasPlaced = placed;
    if (!placed) {
        m_docFolderId.clear();
        m_docFollowMiss.clear();
        return;
    }
    if (!m_commandTree) {
        return;
    }
    const QString id = m_commandTree->currentSelectionId();
    if (id.isEmpty()) {
        return;
    }
    if (isNoteId(id)) {
        showNoteView(id, false, false);
    } else if (m_commandTree->currentSelectionIsFolder()) {
        showFolderDocumentation(id, DocReveal::Quiet);
    } else if (!m_commandTree->currentSelectionIsCollection()) {
        followDocumentToCommand(id);
    }
}

void MainWindow::applyDrawerPosition()
{
    using namespace layout;
    if (!m_dock || !m_terminalDrawer) {
        return;
    }
    const LayoutModel &model = m_dock->model();
    const auto zone = model.zoneOf(view_id::kOutput);
    DrawerPosition position = DrawerPosition::Center;
    // Só recolhe se há para quem devolver o espaço: um vizinho no eixo do colapso (mesmo escondido por um painel
    // maximizado). Numa zona recolhida numa faixa o painel abre por cima do conteúdo: não há para onde recolher.
    if (zone && !model.zone(*zone).collapsed) {
        const GridPlan plan = model.plan();
        const bool columns = plan.expansion == Expansion::Vertical;
        for (int s = 0; s < plan.stacks.size(); ++s) {
            const int at = int(plan.stacks.at(s).cells.indexOf(*zone));
            if (at < 0) {
                continue;
            }
            const int inStack = int(plan.stacks.at(s).cells.size());
            if (inStack > 1) {
                // Vizinho na própria pilha: recolhe em direção à borda mais próxima.
                if (columns) {
                    position = at == 0 ? DrawerPosition::Top : DrawerPosition::Bottom;
                } else {
                    position = at == 0 ? DrawerPosition::Left : DrawerPosition::Right;
                }
            } else if (plan.stacks.size() > 1) {
                if (columns) {
                    position = s == 0 ? DrawerPosition::Left : DrawerPosition::Right;
                } else {
                    position = s == 0 ? DrawerPosition::Top : DrawerPosition::Bottom;
                }
            }
            break;
        }
    }
    m_terminalDrawer->setDrawerPosition(position);
}

void MainWindow::showOutputPanel()
{
    if (m_dock) {
        m_dock->revealView(layout::view_id::kOutput);
    }
    if (m_terminalDrawer && !m_terminalDrawer->isExpanded()) {
        m_terminalDrawer->setExpanded(true);
    }
}

// Pedido explícito do usuário: o painel Documento aparece mesmo fechado ou recolhido.
void MainWindow::showDocumentPanel()
{
    if (m_dock) {
        m_dock->revealView(layout::view_id::kDocument);
    }
}

void MainWindow::restoreDefaultLayout()
{
    if (m_layoutController) {
        m_layoutController->restoreDefault();
    }
}

void MainWindow::setCommandsCollapsed(bool collapsed)
{
    if (!m_dock || !m_terminalDrawer) {
        return;
    }
    if (collapsed) {
        showOutputPanel();
        m_dock->setMaximized(layout::view_id::kOutput);
    } else if (m_dock->model().maximized() == layout::view_id::kOutput) {
        m_dock->setMaximized(QString());
    }
    syncCommandsCollapsed();
}

void MainWindow::syncCommandsCollapsed()
{
    if (!m_dock) {
        return;
    }
    const bool collapsed = m_dock->model().maximized() == layout::view_id::kOutput;
    const bool changed = collapsed != m_commandsCollapsed;
    m_commandsCollapsed = collapsed;
    applyDrawerPosition();
    updateTreeBorderResizeHandles();
    if (changed && !m_restoringUiState) {
        core::SettingsData settings = m_configManager.loadSettings();
        if (settings.commandsCollapsed != m_commandsCollapsed) {
            settings.commandsCollapsed = m_commandsCollapsed;
            m_configManager.saveSettings(settings);
        }
    }
}

} // namespace kai::ui
