

#include "ui/main-window.h"

#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"

#include "utils/action-shortcuts.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QApplication>

#include "ui/features/output/output-tabs-bar.h"

#include "ui/features/output/quick-run-popup.h"

#include "utils/translation-manager.h"

namespace kai::ui {

QString MainWindow::folderPathOf(const QString &folderId) const
{
    QStringList names;
    QSet<QString> seen;
    QString current = folderId;
    while (!current.isEmpty() && !seen.contains(current)) {
        seen.insert(current);
        QString parent;
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.id == current) {
                names.prepend(f.name);
                parent = f.parentId.value_or(QString());
                break;
            }
        }
        current = parent;
    }
    return names.join(QStringLiteral(" / "));
}

QuickRunPopup *MainWindow::preparedQuickRun()
{
    if (!m_quickRun) {
        m_quickRun = new QuickRunPopup(this);
        connect(m_quickRun, &QuickRunPopup::commandChosen, this, [this](const QString &commandId) {
            handleCommandActivated(commandId);
        });
    }
    auto pathOf = [this](const QString &folderId) { return folderPathOf(folderId); };
    QVector<QuickRunPopup::Entry> entries;
    for (const core::Command &command : m_commandsData.commands) {
        if (!command.hidden) {
            entries.append({command.id, command.name, pathOf(command.folderId)});
        }
    }
    m_quickRun->refreshStyle();
    m_quickRun->setEntries(entries);
    return m_quickRun;
}

void MainWindow::stopAllRunning()
{
    QStringList running;
    for (auto it = m_commandsById.cbegin(); it != m_commandsById.cend(); ++it) {
        if (isCommandRunning(it.key())) {
            running << it.key();
        }
    }
    for (const QString &id : m_processManager->trackedCommandIds()) {
        if (!running.contains(id) && isCommandRunning(id)) {
            running << id;
        }
    }
    for (const QString &id : running) {
        handleKillCommandRequested(id);
    }
}

void MainWindow::noteRecentCommand(const QString &commandId)
{
    m_recentCommandIds.removeAll(commandId);
    m_recentCommandIds.prepend(commandId);
    while (m_recentCommandIds.size() > 8) {
        m_recentCommandIds.removeLast();
    }
}

void MainWindow::showQuickRun()
{
    QuickRunPopup *popup = preparedQuickRun();
    OutputTabsBar *bar = m_terminalDrawer->outputTabs();
    const QRect anchor = bar->quickRunRect().isValid() ? bar->quickRunRect() : bar->rect();
    popup->openBelow(bar->mapToGlobal(QPoint(anchor.left(), bar->height())));
}

void MainWindow::showQuickRunFromTray()
{
    if (isVisible() && !isMinimized()) {
        showQuickRun();
        return;
    }
    QuickRunPopup *popup = preparedQuickRun();
    const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    popup->openOnScreen(screen->availableGeometry());
}

// "Ir para o comando" (painel de processos, notificações): a árvore precisa estar à vista, então o painel Comandos volta
// (fechado, recolhido numa faixa, atrás de outro maximizado ou com o modo foco ligado) antes de a seleção acontecer.
void MainWindow::showCommandInTree(const QString &commandId)
{
    showAndRaise("go to command");
    if (m_commandsCollapsed) {
        setCommandsCollapsed(false);
    }
    if (m_dock) {
        m_dock->revealView(layout::view_id::kCommands);
    }
    m_commandTree->selectCommand(commandId);
}

// Seleciona o item na árvore (que abre o que ele é: saída, documentação, nota); com a lista recolhida ela volta antes.
// `fallback` cobre o que a árvore não alcança (item de outra aba filtrada) e o item que já estava selecionado.
void MainWindow::revealInTree(const QString &itemId, const std::function<void()> &fallback)
{
    if (m_commandsCollapsed) {
        setCommandsCollapsed(false);
    }
    if (m_commandTree->currentSelectionId() == itemId || !m_commandTree->selectCommand(itemId)) {
        fallback();
    }
}

void MainWindow::showCommandPalette()
{
    if (!m_palette) {
        m_palette = new QuickRunPopup(this);
        m_palette->setObjectName(QStringLiteral("commandPalette"));
        m_palette->setPlaceholder(utils::tr(QStringLiteral("palette.placeholder")));
        connect(m_palette, &QuickRunPopup::entryChosen, this, [this](QuickRunPopup::Kind kind, const QString &id) {
            switch (kind) {
            case QuickRunPopup::Kind::Command:
                handleCommandActivated(id);
                break;
            case QuickRunPopup::Kind::Folder:
                revealInTree(id, [this, id]() {
                    m_commandTree->showRootTab(id);
                    showFolderDocumentation(id);
                });
                break;
            case QuickRunPopup::Kind::Note:
                revealInTree(id, [this, id]() { showNoteView(id, false); });
                break;
            case QuickRunPopup::Kind::Environment:
                handleEnvironmentSelected(id);
                break;
            case QuickRunPopup::Kind::Action:
                // Os diálogos abrem depois que a paleta terminou de fechar.
                QTimer::singleShot(0, this, [this, id]() { runPaletteAction(id); });
                break;
            }
        });
    }

    const core::SettingsData settings = m_configManager.loadSettings();
    using Kind = QuickRunPopup::Kind;
    QVector<QuickRunPopup::Entry> entries;
    for (const core::Command &command : m_commandsData.commands) {
        if (!command.hidden) {
            entries.append({command.id, command.name, folderPathOf(command.folderId), Kind::Command});
        }
    }
    for (const core::Folder &folder : m_commandsData.folders) {
        if (!folder.hidden) {
            entries.append({folder.id, folder.name, folder.parentId ? folderPathOf(*folder.parentId) : QString(), Kind::Folder});
        }
    }
    for (const core::Note &note : m_notes) {
        if (!note.hidden) {
            entries.append({note.id, note.name, folderPathOf(note.folderId), Kind::Note});
        }
    }
    for (const core::Environment &environment : settings.environments) {
        const bool active = environment.id == settings.activeEnvironmentId;
        entries.append({environment.id, environment.name, active ? utils::tr(QStringLiteral("palette.active")) : QString(),
                        Kind::Environment});
    }
    struct ActionSpec {
        const char *id;
        const char *labelKey;
        const char *shortcutId; // vazio: a ação não tem atalho configurável
    };
    static const ActionSpec actions[] = {
        {"new_command", "settings.shortcut.new_command", "action.new_command"},
        {"new_folder", "settings.shortcut.new_folder", "action.new_folder"},
        {"new_collection", "settings.shortcut.new_collection", "action.new_collection"},
        {"search", "settings.shortcut.toggle_search", "action.toggle_search"},
        {"show_hidden", "settings.shortcut.show_hidden", "action.show_hidden"},
        {"running_only", "settings.shortcut.show_running_only", "action.show_running_only"},
        {"expand_all", "settings.shortcut.expand_all", "action.expand_all"},
        {"collapse_all", "settings.shortcut.collapse_all", "action.collapse_all"},
        {"history", "menu.file.run_history", ""},
        {"notifications", "statusline.notifications.none", ""},
        {"environments", "env.manage.title", ""},
        {"settings", "menu.settings", ""},
        {"view_document", "layout.view.document", ""},
        {"view_processes", "layout.view.processes", ""},
        {"view_variables", "layout.view.variables", ""},
        {"view_logs", "layout.view.logs", ""},
        {"maximize_panel", "menu.view.maximize_panel", "action.maximize_panel"},
        {"rearrange_panels", "menu.view.rearrange", "action.rearrange_panels"},
        {"restore_layout", "menu.view.restore_layout", "action.restore_layout"},
    };
    for (const ActionSpec &action : actions) {
        const QString shortcutId = QString::fromLatin1(action.shortcutId);
        const QString shortcut = shortcutId.isEmpty() ? QString()
            : QKeySequence(utils::firstShortcutFor(settings, shortcutId)).toString(QKeySequence::NativeText);
        entries.append({QString::fromLatin1(action.id), utils::tr(QString::fromLatin1(action.labelKey)), shortcut, Kind::Action});
    }
    // O chat de IA só existe com um provedor configurado.
    if (m_aiConfigured) {
        const QString shortcut = QKeySequence(utils::firstShortcutFor(settings, QStringLiteral("action.ai_chat")))
                                     .toString(QKeySequence::NativeText);
        entries.append({QStringLiteral("view_ai_chat"), utils::tr(QStringLiteral("layout.view.ai_chat")), shortcut, Kind::Action});
    }
    m_palette->refreshStyle();
    m_palette->setEntries(entries);
    m_palette->openCentered(this);
}

// Buscar item (Ctrl+F): a mesma busca da paleta, mas inerte. Escolher leva até o item na árvore e abre o menu de ações
// dele; nada executa sozinho.
void MainWindow::showFindPalette()
{
    if (!m_findPalette) {
        m_findPalette = new QuickRunPopup(this);
        m_findPalette->setObjectName(QStringLiteral("findPalette"));
        m_findPalette->setPlaceholder(utils::tr(QStringLiteral("find.placeholder")));
        connect(m_findPalette, &QuickRunPopup::entryChosen, this, [this](QuickRunPopup::Kind, const QString &id) {
            // Depois que a paleta fechou: o menu não pode nascer dentro do clique dela.
            QTimer::singleShot(0, this, [this, id]() { revealItemActions(id); });
        });
    }
    using Kind = QuickRunPopup::Kind;
    QVector<QuickRunPopup::Entry> entries;
    for (const core::Command &command : m_commandsData.commands) {
        if (!command.hidden) {
            entries.append({command.id, command.name, folderPathOf(command.folderId), Kind::Command});
        }
    }
    for (const core::Folder &folder : m_commandsData.folders) {
        if (!folder.hidden) {
            entries.append({folder.id, folder.name, folder.parentId ? folderPathOf(*folder.parentId) : QString(), Kind::Folder});
        }
    }
    for (const core::Note &note : m_notes) {
        if (!note.hidden) {
            entries.append({note.id, note.name, folderPathOf(note.folderId), Kind::Note});
        }
    }
    m_findPalette->refreshStyle();
    m_findPalette->setEntries(entries);
    m_findPalette->openCentered(this);
}

// Seleciona o item na árvore (com a lista à vista) e abre o menu dele. Uma pasta de primeiro nível é uma aba: só a mostra.
void MainWindow::revealItemActions(const QString &itemId)
{
    if (m_commandsCollapsed) {
        setCommandsCollapsed(false);
    }
    if (m_dock) {
        m_dock->revealView(layout::view_id::kCommands);
    }
    if (!m_commandTree->selectCommand(itemId)) {
        m_commandTree->showRootTab(itemId);
        return;
    }
    m_commandTree->openContextMenuForCurrent();
}

void MainWindow::runPaletteAction(const QString &actionId)
{
    static const QHash<QString, std::function<void(MainWindow *)>> handlers = {
        {QStringLiteral("new_command"), [](MainWindow *w) { w->handleNewCommandRequested(); }},
        {QStringLiteral("new_folder"), [](MainWindow *w) { w->handleNewFolderRequested(); }},
        {QStringLiteral("new_collection"), [](MainWindow *w) { w->handleNewCollectionRequested(); }},
        {QStringLiteral("search"), [](MainWindow *w) { w->showFindPalette(); }},
        {QStringLiteral("show_hidden"), [](MainWindow *w) { w->triggerToggleShowHidden(); }},
        {QStringLiteral("running_only"), [](MainWindow *w) { w->triggerToggleRunningOnly(); }},
        {QStringLiteral("expand_all"), [](MainWindow *w) { w->triggerExpandAll(); }},
        {QStringLiteral("collapse_all"), [](MainWindow *w) { w->triggerCollapseAll(); }},
        {QStringLiteral("history"), [](MainWindow *w) { w->handleRunHistoryRequested(); }},
        {QStringLiteral("notifications"), [](MainWindow *w) { w->handleNotificationHistoryRequested(); }},
        {QStringLiteral("environments"), [](MainWindow *w) { w->handleManageEnvironmentsRequested(); }},
        {QStringLiteral("settings"), [](MainWindow *w) { w->handleSettingsRequested(); }},
        {QStringLiteral("view_document"), [](MainWindow *w) { w->m_layoutController->focusView(layout::view_id::kDocument); }},
        {QStringLiteral("view_processes"), [](MainWindow *w) { w->m_layoutController->focusView(layout::view_id::kProcesses); }},
        {QStringLiteral("view_variables"), [](MainWindow *w) { w->m_layoutController->focusView(layout::view_id::kVariables); }},
        {QStringLiteral("view_logs"), [](MainWindow *w) { w->m_layoutController->focusView(layout::view_id::kLogs); }},
        {QStringLiteral("view_ai_chat"), [](MainWindow *w) { w->showAiChat(); }},
        {QStringLiteral("maximize_panel"), [](MainWindow *w) { w->m_layoutController->toggleMaximizeFocused(); }},
        {QStringLiteral("rearrange_panels"), [](MainWindow *w) { w->m_dock->setEditMode(!w->m_dock->editMode()); }},
        {QStringLiteral("restore_layout"), [](MainWindow *w) { w->restoreDefaultLayout(); }},
    };
    if (const auto it = handlers.constFind(actionId); it != handlers.constEnd()) {
        it.value()(this);
    }
}

} // namespace kai::ui
