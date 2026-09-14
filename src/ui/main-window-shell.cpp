
#include "core/folder-actions.h"

#include "ui/features/docs/document-panel.h"

#include "ui/main-window.h"

#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"

#include "ui/shared/notification-gate.h"

#include "utils/action-shortcuts.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QShortcut>

#include <QDir>

#include <QJsonDocument>
#include <QApplication>

#include "qhotkey.h"

#include "utils/asset-paths.h"

#include "utils/translation-manager.h"

#include "ui/shared/tray-menu.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
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

    auto *menu = new TrayMenu();
    // Estado lido a cada abertura, para nada ficar desatualizado.
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        const core::SettingsData settings = m_configManager.loadSettings();
        TrayMenu::State state;
        for (const QString &id : m_processManager->trackedCommandIds()) {
            if (m_processManager->statusOf(id) == engine::ProcessStatus::Running) {
                ++state.runningCount;
            }
        }
        state.notificationsEnabled = settings.notificationsEnabled;
        for (const core::Environment &e : settings.environments) {
            state.environmentIds << e.id;
            state.environmentNames << (e.name.isEmpty() ? e.id : e.name);
        }
        // A contagem e o "Parar tudo" valem para qualquer execução (segundo plano, pipeline, ações de pasta).
        state.runningCount = 0;
        for (auto it = m_commandsById.cbegin(); it != m_commandsById.cend(); ++it) {
            if (isCommandRunning(it.key())) {
                ++state.runningCount;
            }
        }
        if (!m_recentSeeded) {
            m_recentSeeded = true;
            for (const core::RunRecord &record : m_runHistory.load()) {
                if (!m_recentCommandIds.contains(record.commandId)) {
                    m_recentCommandIds.append(record.commandId);
                }
                if (m_recentCommandIds.size() >= 8) {
                    break;
                }
            }
        }
        for (const QString &id : std::as_const(m_recentCommandIds)) {
            const auto found = m_commandsById.constFind(id);
            if (found == m_commandsById.constEnd() || found->hidden || core::isFolderActionCommandId(id)) {
                continue;
            }
            state.recentIds << id;
            state.recentNames << found->name;
            if (state.recentIds.size() >= 5) {
                break;
            }
        }
        state.activeEnvironmentId = settings.activeEnvironmentId;
        state.themes = availableThemeNames();
        state.activeTheme = settings.activeTheme;
        menu->rebuild(state);
    });
    connect(menu, &TrayMenu::toggleRequested, this, &MainWindow::toggleVisibility);
    connect(menu, &TrayMenu::importRequested, this, &MainWindow::handleImportRequested);
    connect(menu, &TrayMenu::quitRequested, qApp, &QCoreApplication::quit);
    // Configurações, importar e a busca rápida abrem sozinhos: a janela principal só aparece por "Mostrar/Ocultar".
    connect(menu, &TrayMenu::quickRunRequested, this, &MainWindow::showQuickRunFromTray);
    connect(menu, &TrayMenu::stopAllRequested, this, &MainWindow::stopAllRunning);
    connect(menu, &TrayMenu::recentCommandChosen, this, &MainWindow::handleCommandActivated);
    connect(menu, &TrayMenu::notificationsHistoryRequested, this, [this]() {
        showAndRaise("tray: notifications");
        handleNotificationHistoryRequested();
    });
    connect(menu, &TrayMenu::notificationsToggled, this, [this](bool enabled) {
        core::SettingsData settings = m_configManager.loadSettings();
        settings.notificationsEnabled = enabled;
        if (!m_configManager.saveSettings(settings)) {
            utils::Logger::warning(kLogTag, QStringLiteral("Falha ao persistir o estado das notificações."));
        }
    });
    connect(menu, &TrayMenu::environmentChosen, this, &MainWindow::handleEnvironmentSelected);
    connect(menu, &TrayMenu::themeChosen, this, [this](const QString &themeName) {
        core::SettingsData settings = m_configManager.loadSettings();
        if (settings.activeTheme == themeName) {
            return;
        }
        if (!m_themeManager->loadTheme(themeName)) {
            utils::Logger::warning(kLogTag, QStringLiteral("Tema '%1' não pôde ser carregado pela bandeja.").arg(themeName));
            return;
        }
        settings.activeTheme = themeName;
        m_configManager.saveSettings(settings);
    });
    connect(menu, &TrayMenu::settingsRequested, this, &MainWindow::handleSettingsRequested);
    // Logs e histórico de notificações são painéis do quadro: precisam da janela.
    connect(menu, &TrayMenu::logsRequested, this, [this]() {
        showAndRaise("tray: logs");
        handleLogsRequested();
    });

    m_trayIcon->setContextMenu(menu);
    connect(this, &QObject::destroyed, menu, &QObject::deleteLater);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::handleTrayIconActivated);

    m_trayIcon->show();
}

void MainWindow::maybeShowNotification(NotificationEvent event, const QString &title, const QString &body,
                                        QSystemTrayIcon::MessageIcon icon, const QString &commandId,
                                        const QString &eventKeyOverride)
{
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
    case NotificationEvent::KipNotify:
        // Pedido explícito do programa KIP: só o interruptor geral decide.
        eventToggleOn = true;
        eventKey = eventKeyOverride.isEmpty() ? QStringLiteral("kip_notify") : eventKeyOverride;
        break;
    }

    // O histórico registra sempre, mesmo sem toast; os toggles só decidem o toast.
    core::NotificationRecord record;
    record.eventKey = eventKey;
    record.title = title;
    record.body = body;
    record.commandId = commandId;
    m_notificationHistory.append(record);

    // Num `notify` de app KIP, "em foco" significa que o usuário está olhando para essa view.
    const bool viewInFront = event == NotificationEvent::KipNotify ? isKipViewVisible(commandId) : isActiveWindow();
    if (!shouldShowNotification(trayAvailable(), settings.notificationsEnabled,
            eventToggleOn, settings.notifyEvenWhenFocused, viewInFront)) {
        return;
    }
    m_trayIcon->showMessage(title, body, icon, 6000);
}

bool MainWindow::isKipViewVisible(const QString &commandId) const
{
    if (m_terminalDrawer->hasDetachedWindow() && m_terminalDrawer->detachedCommandId() == commandId
        && m_terminalDrawer->detachedWindowActive()) {
        return true;
    }
    return isVisible() && !isMinimized() && isActiveWindow() && m_connectedTerminalCommandId == commandId
        && m_terminalDrawer->isExpanded() && m_terminalDrawer->kipMode()
        && !(m_terminalDrawer->hasDetachedWindow() && m_terminalDrawer->detachedCommandId() == commandId);
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
    m_processListDialog = new ProcessListDialog(m_processManager, this, true);
    m_processListDialog->hide(); // filho embutido: só aparece quando o painel Processos entra numa zona
    // Parar/forçar vale para qualquer processo, pelo mesmo caminho do botão Parar da árvore.
    connect(m_processListDialog, &ProcessListDialog::stopRequested, this,
            [this](const QString &commandId, bool force) { handleKillCommandRequested(commandId, force); });
    connect(m_processListDialog, &ProcessListDialog::commandRequested, this, &MainWindow::showCommandInTree);
    // Os provedores entregam só ids, resolvidos na hora (os comandos carregam depois de esta tela nascer; as ações de
    // pasta têm ids virtuais). O de foreground inclui o registry do pipeline, além do background do ProcessManager.
    m_processListDialog->setLabelProvider([this](const QString &id) {
        ProcessListDialog::Label label;
        QString folderId;
        if (const auto actionIds = core::parseFolderActionCommandId(id)) {
            label.name = m_commandsById.value(actionIds->commandId).name;
            folderId = actionIds->folderId;
        } else if (const auto it = m_commandsById.constFind(id); it != m_commandsById.constEnd()) {
            label.name = it->name;
            folderId = it->folderId;
        }
        if (!label.name.isEmpty() && !folderId.isEmpty()) {
            label.where = folderDisplayName(folderId);
        }
        label.startedAt = m_commandTree ? m_commandTree->runStartedAt(id) : QDateTime();
        return label;
    });
    m_processListDialog->setForegroundProvider([this]() {
        QList<QPair<QString, qint64>> running;
        for (const QString &id : allPipelineRunningCommandIds()) {
            qint64 pid = 0;
            if (auto *r = runnerFor(id)) {
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
    // QHotkey (v1.5.0) só tem backend nativo para X11, Win32 e macOS: sob Wayland (inclusive WSLg) a construção pode
    // segfaultar. Também pula "offscreen"/"minimal" (headless/CI).
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

    // autoRegister=false: uma falha do backend nativo acontece numa chamada explícita, que dá para isolar e logar.
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
    // Binário, sem depender de o atalho global ter registrado: ligado = sempre visível, desligado = sempre na bandeja.
    return m_configManager.loadSettings().startVisible;
}

QIcon MainWindow::loadAppIcon() const
{
    const QString logoDir = utils::assetDir(QStringLiteral("logo"));
    const QStringList candidatePaths = logoDir.isEmpty()
        ? QStringList{QStringLiteral("assets/logo/kai.png")}
        : QStringList{QDir(logoDir).filePath(QStringLiteral("kai.png"))};

    for (const QString &candidate : candidatePaths) {
        if (QFile::exists(candidate)) {
            // O logo de origem é grande (2302x1856): cru, estoura o limite de request do X11/XCB e o ícone é ignorado.
            // Por isso um QIcon multi-resolução.
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

    // Copia o tema se não instalado; reinstala se o schema_version instalado é menor. Um tema customizado (sem
    // correspondente nos assets) nunca é tocado.
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
    // Uma ação nova precisa de uma linha em utils::actionShortcutSpecs e uma entrada no mapa de handlers.
    const core::SettingsData settings = m_configManager.loadSettings();

    qDeleteAll(m_actionShortcuts);
    m_actionShortcuts.clear();

    // O escopo na árvore (Qt::WidgetWithChildrenShortcut) vem de spec.scope.
    const QMap<QString, std::function<void()>> handlers = {
        {QStringLiteral("action.next_tab"), [this]() { m_commandTree->selectNextTab(); }},
        {QStringLiteral("action.previous_tab"), [this]() { m_commandTree->selectPreviousTab(); }},
        {QStringLiteral("action.edit_item"), [this]() { applyEditShortcutOnSelection(); }},
        {QStringLiteral("action.delete_item"), [this]() { applyDeleteShortcutOnSelection(); }},
        {QStringLiteral("action.new_folder"), [this]() { handleNewFolderRequested(); }},
        {QStringLiteral("action.new_command"), [this]() { handleNewCommandRequested(); }},
        // Encerra o app de fato; o "X" da janela só oculta (ver closeEvent).
        {QStringLiteral("action.quit_app"), [this]() { quitApplication(); }},
        // Com o foco na Saída (ou no Documento), o atalho foca a busca dela; sem busca própria (terminal interativo,
        // painel "não executado"...) ou com o foco fora, abre a busca da árvore.
        {QStringLiteral("action.toggle_search"), [this]() {
            if (m_terminalDrawer && m_terminalDrawer->isVisible()
                && m_terminalDrawer->isAncestorOf(QApplication::focusWidget())
                && m_terminalDrawer->focusSearch()) {
                return;
            }
            if (m_documentPanel && m_documentPanel->isVisible()
                && m_documentPanel->isAncestorOf(QApplication::focusWidget()) && m_documentPanel->focusSearch()) {
                return;
            }
            showFindPalette();
        }},
        {QStringLiteral("action.context_menu"), [this]() { m_commandTree->openContextMenuForCurrent(); }},
        {QStringLiteral("action.focus_output"), [this]() { toggleOutputFocus(); }},
        {QStringLiteral("action.command_palette"), [this]() { showCommandPalette(); }},
        {QStringLiteral("action.ai_chat"), [this]() { showAiChat(); }},
        {QStringLiteral("action.next_output"), [this]() { cycleOutputTab(1); }},
        {QStringLiteral("action.focus_panel_1"), [this]() { m_layoutController->focusPanelNumber(1); }},
        {QStringLiteral("action.focus_panel_2"), [this]() { m_layoutController->focusPanelNumber(2); }},
        {QStringLiteral("action.focus_panel_3"), [this]() { m_layoutController->focusPanelNumber(3); }},
        {QStringLiteral("action.focus_panel_4"), [this]() { m_layoutController->focusPanelNumber(4); }},
        {QStringLiteral("action.focus_panel_5"), [this]() { m_layoutController->focusPanelNumber(5); }},
        {QStringLiteral("action.focus_panel_6"), [this]() { m_layoutController->focusPanelNumber(6); }},
        {QStringLiteral("action.focus_panel_7"), [this]() { m_layoutController->focusPanelNumber(7); }},
        {QStringLiteral("action.focus_panel_8"), [this]() { m_layoutController->focusPanelNumber(8); }},
        {QStringLiteral("action.focus_panel_9"), [this]() { m_layoutController->focusPanelNumber(9); }},
        {QStringLiteral("action.next_panel"), [this]() { m_layoutController->cycleFocus(true); }},
        {QStringLiteral("action.previous_panel"), [this]() { m_layoutController->cycleFocus(false); }},
        {QStringLiteral("action.maximize_panel"), [this]() { m_layoutController->toggleMaximizeFocused(); }},
        {QStringLiteral("action.rearrange_panels"), [this]() { m_dock->setEditMode(!m_dock->editMode()); }},
        {QStringLiteral("action.restore_layout"), [this]() { restoreDefaultLayout(); }},
        {QStringLiteral("action.previous_output"), [this]() { cycleOutputTab(-1); }},
        // "action.toggle_edit_mode" não tem handler de propósito: os diálogos que o usam criam o próprio QShortcut (via
        // utils::firstShortcutFor). Ele fica na tabela só para aparecer nas Configurações; o loop abaixo pula specs sem
        // handler.
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
        {QStringLiteral("action.show_running_only"), [this]() { triggerToggleRunningOnly(); }},
        // Nunca esconde com o foco num terminal interativo (ex: vim): o Esc pertence ao programa.
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
            continue; // spec sem handler registrado
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

    const QString focusOutputHint = QKeySequence(
        utils::firstShortcutFor(settings, QStringLiteral("action.focus_output"))).toString(QKeySequence::NativeText);
    if (m_terminalDrawer) {
        m_terminalDrawer->setInputShortcutHint(focusOutputHint);
    }
}

} // namespace kai::ui
