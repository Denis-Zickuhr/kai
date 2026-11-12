#include "ui/main-window.h"

#include "core/http-resolve.h"
#include "core/tree-view.h"
#include "ui/features/collections/tree-view-editor-dialog.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/panel-card.h"
#include "ui/layout/view-registry.h"
#include "ui/shared/action-group-container.h"
#include "ui/shared/action-sidebar.h"
#include "ui/shared/expand-collapse-bar.h"
#include "ui/shared/item-actions-bar.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include "utils/logger.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {
QString treeIconName(const core::TreeViewDef &view)
{
    return LucideIcons::has(view.icon) ? view.icon : QStringLiteral("folder-tree");
}
} // namespace

// `tree:<id>`: uma árvore extra de comandos (Exibir → Commands ▸). A definição vem do tree-views.json; o painel é um painel
// como os outros do quadro e entra pela primeira vez na célula da lista de comandos.
void MainWindow::registerTreeViews()
{
    using namespace layout;
    m_viewRegistry->registerPrefix(view_id::kTreePrefix, [this](const QString &id) -> std::optional<ViewSpec> {
        const QString treeId = id.mid(view_id::kTreePrefix.size());
        const auto it = std::find_if(m_treeViews.cbegin(), m_treeViews.cend(),
                                     [&treeId](const core::TreeViewDef &d) { return d.id == treeId; });
        if (it == m_treeViews.cend()) {
            return std::nullopt;
        }
        ViewSpec view;
        view.id = id;
        view.titleProvider = [this, treeId]() {
            const auto found = std::find_if(m_treeViews.cbegin(), m_treeViews.cend(),
                                            [&treeId](const core::TreeViewDef &d) { return d.id == treeId; });
            return found != m_treeViews.cend() ? found->name : treeId;
        };
        view.icon = treeIconName(*it);
        view.home = Home::Primary;
        view.minWidth = 220;
        view.factory = [this, treeId]() -> QWidget * { return createTreePanel(treeId); };
        view.menuExtras = [this, treeId](QMenu *menu) {
            connect(menu->addAction(utils::tr(QStringLiteral("tree_view.panel.edit"))), &QAction::triggered, this,
                    [this, treeId]() { editTreeView(treeId); });
        };
        view.chevronMenu = [this](QMenu *menu) { populateTreesMenu(menu); };
        return view;
    });
}

QWidget *MainWindow::createTreePanel(const QString &treeId)
{
    const auto it = std::find_if(m_treeViews.cbegin(), m_treeViews.cend(),
                                 [&treeId](const core::TreeViewDef &d) { return d.id == treeId; });
    if (it == m_treeViews.cend()) {
        return nullptr;
    }
    auto *tree = new CommandTreeWidget();
    tree->setView(*it);
    wireExtraTree(tree);
    // A principal passa a repassar dados e estado a esta árvore (e já entrega o de agora).
    m_commandTree->addFollower(tree);
    // Os grupos de ações (Execução, Exibição, Item) em cima da árvore, como na principal.
    auto *body = new QWidget();
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    if (ActionGroupContainer *bars = buildExtraTreeBars(tree, body)) {
        bodyLayout->addWidget(bars);
    }
    bodyLayout->addWidget(tree, 1);
    return layout::wrapInPanelCard(body, QSize(260, 220));
}

ActionGroupContainer *MainWindow::buildExtraTreeBars(CommandTreeWidget *tree, QWidget *parent)
{
    const core::SettingsData settings = m_configManager.loadSettings();
    const QColor neutral(tk::mutedFg());
    ExtraTreeBars bars;
    bars.tree = tree;
    QVector<QWidget *> groups;
    // Mesmas regras da principal: "oculto" some, e com as ações inline o grupo de execução seria redundante.
    const bool showExec = !settings.inlineActions && settings.executionActionsPlacement != QLatin1String("hidden");
    if (showExec) {
        auto *exec = new ActionSidebar(parent);
        bars.exec = exec;
        groups << exec;
        connect(exec, &ActionSidebar::playSelectedRequested, this, &MainWindow::triggerPlaySelected);
        connect(exec, &ActionSidebar::stopSelectedRequested, this, &MainWindow::triggerStopSelected);
        connect(exec, &ActionSidebar::forceStopSelectedRequested, this, &MainWindow::triggerForceStopSelected);
        connect(exec, &ActionSidebar::resetSelectedRequested, this, &MainWindow::triggerResetSelected);
        connect(exec, &ActionSidebar::editBodySelectedRequested, this, &MainWindow::triggerEditBodySelected);
        connect(exec, &ActionSidebar::showOutputSelectedRequested, this, [this]() {
            const QString id = m_commandTree->currentSelectionId();
            if (canShowOutput(id)) {
                showCommandOutput(id);
            }
        });
        connect(exec, &ActionSidebar::showDocumentSelectedRequested, this,
                [this]() { showDocumentFor(m_commandTree->currentSelectionId()); });
    }
    if (settings.displayActionsPlacement != QLatin1String("hidden")) {
        auto *display = new ExpandCollapseBar(parent);
        bars.display = display;
        groups << display;
        // Expandir, recolher e os filtros valem para ESTA árvore; ocultar o item selecionado segue a seleção (a principal).
        connect(display, &ExpandCollapseBar::expandSelectedRequested, tree, &CommandTreeWidget::expandCurrentItem);
        connect(display, &ExpandCollapseBar::collapseSelectedRequested, tree, &CommandTreeWidget::collapseCurrentItem);
        connect(display, &ExpandCollapseBar::expandAllRequested, tree, &CommandTreeWidget::expandAll);
        connect(display, &ExpandCollapseBar::collapseAllRequested, tree, &CommandTreeWidget::collapseAll);
        connect(display, &ExpandCollapseBar::toggleHiddenSelectedRequested, this, &MainWindow::triggerToggleHiddenSelected);
        connect(display, &ExpandCollapseBar::toggleShowHiddenRequested, this, [tree, display]() {
            tree->setShowHidden(!tree->showHidden());
            display->setShowingHidden(tree->showHidden());
        });
        connect(display, &ExpandCollapseBar::toggleRunningOnlyRequested, this, [tree, display]() {
            tree->setShowRunningOnly(!tree->showRunningOnly());
            display->setShowingRunningOnly(tree->showRunningOnly());
        });
        connect(display, &ExpandCollapseBar::eyeEasterEggTriggered, this, &MainWindow::showEyeEasterEgg);
        display->setShowingHidden(tree->showHidden());
        display->setShowingRunningOnly(tree->showRunningOnly());
    }
    if (settings.itemActionsPlacement != QLatin1String("hidden")) {
        auto *item = new ItemActionsBar(parent);
        bars.item = item;
        groups << item;
        // Criar: dentro do item selecionado na árvore extra; sem seleção, na pasta raiz da aba dela.
        auto createHere = [this, tree](const std::function<void()> &create) {
            QString target;
            if (tree->currentSelectionId().isEmpty()) {
                const QString root = tree->currentRootFolderId();
                target = root == QLatin1String(core::kTreeDefaultRootId) ? QString() : root;
            }
            withCreationTarget(target, create);
        };
        connect(item, &ItemActionsBar::editCurrentFolderRequested, this, &MainWindow::triggerEditCurrentFolder);
        connect(item, &ItemActionsBar::newFolderRequested, this, [createHere, this]() { createHere([this]() { handleNewFolderRequested(); }); });
        connect(item, &ItemActionsBar::newCommandRequested, this, [createHere, this]() { createHere([this]() { handleNewCommandRequested(); }); });
        connect(item, &ItemActionsBar::newPipelineRequested, this, [createHere, this]() { createHere([this]() { handleNewPipelineRequested(); }); });
        connect(item, &ItemActionsBar::newCollectionRequested, this, [createHere, this]() { createHere([this]() { handleNewCollectionRequested(); }); });
        connect(item, &ItemActionsBar::newNoteRequested, this, [createHere, this]() { createHere([this]() { handleNewNoteRequested(); }); });
        connect(item, &ItemActionsBar::editSelectedRequested, this, &MainWindow::triggerEditSelected);
        connect(item, &ItemActionsBar::deleteSelectedRequested, this, &MainWindow::triggerDeleteSelected);
    }
    if (groups.isEmpty()) {
        return nullptr;
    }
    if (bars.exec) {
        bars.exec->setOrientation(Qt::Horizontal);
        bars.exec->applyAccentColor(neutral);
    }
    if (bars.display) {
        bars.display->setOrientation(Qt::Horizontal);
        bars.display->applyAccentColor(neutral);
    }
    if (bars.item) {
        bars.item->setOrientation(Qt::Horizontal);
        bars.item->applyAccentColor(neutral);
    }
    auto *container = new ActionGroupContainer(Qt::Horizontal, parent);
    container->setEdgeSeparator(Qt::BottomEdge);
    container->setGroups(groups);
    m_extraBars.append(bars);
    syncExtraBars();
    return container;
}

void MainWindow::syncExtraBars()
{
    m_extraBars.removeIf([](const ExtraTreeBars &b) { return !b.tree; });
    if (m_extraBars.isEmpty() || !m_commandTree) {
        return;
    }
    const QString id = m_commandTree->currentSelectionId();
    const bool hasSelection = !id.isEmpty();
    const bool isCommand = hasSelection && !m_commandTree->currentSelectionIsFolder();
    const bool isRunning = isCommand && isCommandRunning(id);
    const bool hasFailed = isCommand && m_failedCommandIds.contains(id);
    bool isHttp = false;
    bool isHidden = false;
    if (isCommand) {
        const auto it = m_commandsById.constFind(id);
        if (it != m_commandsById.constEnd()) {
            isHttp = it->type == core::CommandType::Http;
            isHidden = it->hidden;
        }
    } else if (hasSelection) {
        isHttp = core::folderHasHttpCommand(id, m_commandsData.folders, m_commandsData.commands);
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.id == id) {
                isHidden = f.hidden;
                break;
            }
        }
    }
    for (const ExtraTreeBars &bars : std::as_const(m_extraBars)) {
        if (bars.exec) {
            bars.exec->setRowContext(hasSelection, isCommand, isRunning, hasFailed, isHttp);
            bars.exec->setFocusActions(!m_smartFocus, canShowOutput(id), !documentTargetFor(id).isEmpty());
        }
        if (bars.item) {
            bars.item->setRowContext(hasSelection);
        }
        if (bars.display) {
            bars.display->setSelectedHidden(hasSelection && isHidden);
        }
    }
}

// As ações de uma árvore extra valem como as da principal (mesmos ids, mesmos tratadores). Selecionar um item numa extra
// seleciona o mesmo item na principal: saída, documento e os botões de ação seguem a seleção como sempre.
void MainWindow::wireExtraTree(CommandTreeWidget *tree)
{
    using T = CommandTreeWidget;
    connect(tree, &T::commandActivated, this, &MainWindow::handleCommandActivated);
    connect(tree, &T::editRequested, this, &MainWindow::handleEditRequested);
    connect(tree, &T::deleteRequested, this, &MainWindow::handleDeleteRequested);
    connect(tree, &T::projectSyncRequested, this, &MainWindow::startProjectSync);
    connect(tree, &T::pinAsPanelRequested, this, &MainWindow::pinKipPanel);
    connect(tree, &T::exportRequested, this, [this](const QString &id, bool isFolder) { showExportDialog(id, isFolder); });
    connect(tree, &T::toggleFolderHiddenRequested, this, [this](const QString &folderId) {
        for (core::Folder &f : m_commandsData.folders) {
            if (f.id == folderId) {
                f.hidden = !f.hidden;
                break;
            }
        }
        m_configManager.saveCommands(m_commandsData);
        reloadCommandTree();
    });
    connect(tree, &T::duplicateRequested, this, &MainWindow::handleDuplicateRequested);
    connect(tree, &T::quickEditBodyRequested, this, &MainWindow::handleQuickEditBodyRequested);
    connect(tree, &T::quickEditFolderRequested, this, &MainWindow::handleQuickEditFolderRequested);
    connect(tree, &T::collectionEditRequested, this, &MainWindow::handleCollectionEditRequested);
    connect(tree, &T::collectionDuplicateRequested, this, &MainWindow::handleCollectionDuplicateRequested);
    connect(tree, &T::collectionDeleteRequested, this, &MainWindow::handleCollectionDeleteRequested);
    connect(tree, &T::newFolderRequested, this, [this](const QString &folderId) {
        withCreationTarget(folderId, [this]() { handleNewFolderRequested(); });
    });
    connect(tree, &T::newCommandRequested, this, [this](const QString &folderId) {
        withCreationTarget(folderId, [this]() { handleNewCommandRequested(); });
    });
    connect(tree, &T::newPipelineRequested, this, [this](const QString &folderId) {
        withCreationTarget(folderId, [this]() { handleNewPipelineRequested(); });
    });
    connect(tree, &T::newCollectionRequested, this, [this](const QString &folderId) {
        withCreationTarget(folderId, [this]() { handleNewCollectionRequested(); });
    });
    connect(tree, &T::newNoteRequested, this, [this](const QString &folderId) {
        withCreationTarget(folderId, [this]() { handleNewNoteRequested(); });
    });
    connect(tree, &T::noteActivated, this, &MainWindow::handleNoteActivated);
    connect(tree, &T::noteEditRequested, this, &MainWindow::handleNoteEditRequested);
    connect(tree, &T::noteDuplicateRequested, this, &MainWindow::handleNoteDuplicateRequested);
    connect(tree, &T::noteDeleteRequested, this, &MainWindow::handleNoteDeleteRequested);
    connect(tree, &T::noteFileActionRequested, this, &MainWindow::handleNoteFileActionRequested);
    connect(tree, &T::playRequested, this, &MainWindow::handleCommandActivated);
    connect(tree, &T::killRequested, this, [this](const QString &id) { handleKillCommandRequested(id, false); });
    connect(tree, &T::forceStopRequested, this, [this](const QString &id) { handleKillCommandRequested(id, true); });
    connect(tree, &T::resetRequested, this, &MainWindow::handleResetCommandRequested);
    connect(tree, &T::structureChanged, this, &MainWindow::handleTreeStructureChanged);
    connect(tree, &T::folderActionActivated, this, &MainWindow::handleFolderActionActivated);
    connect(tree, &T::folderActionContextRequested, this, &MainWindow::showFolderActionMenu);
    connect(tree, &T::commandOutputRequested, this, &MainWindow::showCommandOutput);
    connect(tree, &T::documentRequested, this, &MainWindow::showDocumentFor);
    // Só a escolha do usuário (clique ou teclado, com o foco na árvore) vai para a principal. Reconstruir a árvore (a criação do
    // painel, um recarregamento dos dados) também emite selectionChanged; repassar isso mexeria na principal e na Saída no meio
    // da montagem do quadro de painéis.
    connect(tree, &T::selectionChanged, this, [this, tree](const QString &itemId, bool) {
        if (itemId.isEmpty() || !tree->isAncestorOf(QApplication::focusWidget())) {
            return;
        }
        m_commandTree->selectCommand(itemId);
    });
}

// O conteúdo da setinha de Commands (e de cada árvore extra): as árvores existentes (abrir/fechar), Nova árvore… e Editar.
void MainWindow::populateTreesMenu(QMenu *menu)
{
    const QColor muted(tk::mutedFg());
    const QColor accent(tk::accent());
    for (const core::TreeViewDef &view : std::as_const(m_treeViews)) {
        const QString viewId = layout::view_id::kTreePrefix + view.id;
        const bool placed = isViewPlaced(viewId);
        QAction *action = menu->addAction(LucideIcons::icon(treeIconName(view), placed ? accent : muted, 16), view.name);
        action->setCheckable(true);
        action->setChecked(placed);
        connect(action, &QAction::triggered, this, [this, viewId](bool checked) {
            if (checked) {
                m_layoutController->focusView(viewId);
            } else {
                m_dock->closeView(viewId);
            }
        });
    }
    if (!m_treeViews.isEmpty()) {
        menu->addSeparator();
    }
    connect(menu->addAction(LucideIcons::icon(QStringLiteral("plus"), muted, 16),
                            utils::tr(QStringLiteral("tree_view.menu.new"))),
            &QAction::triggered, this, [this]() { newTreeView(); });
    if (!m_treeViews.isEmpty()) {
        QMenu *edit = menu->addMenu(LucideIcons::icon(QStringLiteral("pencil"), muted, 16),
                                    utils::tr(QStringLiteral("tree_view.menu.edit")));
        for (const core::TreeViewDef &view : std::as_const(m_treeViews)) {
            const QString treeId = view.id;
            connect(edit->addAction(LucideIcons::icon(treeIconName(view), muted, 16), view.name), &QAction::triggered, this,
                    [this, treeId]() { editTreeView(treeId); });
        }
    }
}

void MainWindow::persistTreeViews()
{
    if (!m_configManager.saveTreeViews(m_treeViews)) {
        utils::Logger::warning(QStringLiteral("MainWindow"), QStringLiteral("Não foi possível gravar tree-views.json."));
    }
}

void MainWindow::newTreeView()
{
    core::TreeViewDef view;
    view.id = QStringLiteral("t_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    view.name = utils::tr(QStringLiteral("tree_view.default_name"));
    view.icon = QStringLiteral("folder-tree");
    // Nasce com a pasta da seleção atual, a coisa mais provável de o usuário querer isolar.
    const QString rootId = m_commandTree->currentRootFolderId();
    if (!rootId.isEmpty() && rootId != core::kTreeDefaultRootId) {
        view.folderIds << rootId;
    }
    TreeViewEditorDialog dialog(view, m_commandsData.folders, true, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    view = dialog.result();
    m_treeViews << view;
    persistTreeViews();
    m_layoutController->focusView(layout::view_id::kTreePrefix + view.id);
}

void MainWindow::editTreeView(const QString &treeId)
{
    const auto it = std::find_if(m_treeViews.begin(), m_treeViews.end(),
                                 [&treeId](const core::TreeViewDef &d) { return d.id == treeId; });
    if (it == m_treeViews.end()) {
        return;
    }
    TreeViewEditorDialog dialog(*it, m_commandsData.folders, false, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString viewId = layout::view_id::kTreePrefix + treeId;
    if (dialog.deleteRequested()) {
        m_treeViews.erase(it);
        persistTreeViews();
        // O painel some do layout (o id deixa de ser conhecido) e o widget é descartado.
        m_dock->revalidate();
        m_viewRegistry->forget(viewId);
        return;
    }
    *it = dialog.result();
    persistTreeViews();
    // Painel já aberto: a mesma árvore passa a aplicar os filtros novos (ela guarda o conjunto completo).
    if (QWidget *card = m_viewRegistry->existingWidget(viewId)) {
        if (auto *tree = card->findChild<CommandTreeWidget *>()) {
            tree->setView(*it);
        }
    }
    m_dock->refreshTabs();
}

} // namespace kai::ui
