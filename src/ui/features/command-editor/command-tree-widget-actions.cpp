#include "ui/features/command-editor/command-tree-widget.h"

#include "core/folder-actions.h"

#include "ui/features/command-editor/inline-row-actions.h"

#include "ui/features/command-editor/command-tree-widget-internal.h"

namespace kai::ui {

using namespace cmdtree;

void CommandTreeWidget::setGlobalActions(const QVector<core::GlobalAction> &actions)
{
    m_globalActions = actions;
    refreshActionViews();
}

void CommandTreeWidget::setFocusedAction(const QString &virtualId)
{
    if (m_focusedActionId == virtualId) {
        return;
    }
    m_focusedActionId = virtualId;
    refreshActionViews();
}

void CommandTreeWidget::setActionsWithOutput(const QSet<QString> &virtualIds)
{
    m_actionsWithOutput = virtualIds;
    refreshActionViews();
}

void CommandTreeWidget::setOutputMarksEnabled(bool enabled)
{
    if (m_outputMarks == enabled) {
        return;
    }
    m_outputMarks = enabled;
    refreshActionViews();
}

void CommandTreeWidget::setShownCommand(const QString &commandId)
{
    if (m_shownCommandId == commandId) {
        return;
    }
    m_shownCommandId = commandId;
    refreshActionViews();
}

void CommandTreeWidget::setInlineActionsEnabled(bool enabled)
{
    m_inlineActions = enabled;
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        for (auto *delegate : tree->findChildren<InlineRowActions *>(QString(), Qt::FindDirectChildrenOnly)) {
            delegate->setEnabled(enabled);
        }
    }
}

void CommandTreeWidget::refreshActionViews()
{
    // Os ícones de ação dividem a coluna de status/tempo com o play; ela cresce
    // só se a pasta com MAIS ações precisar.
    int most = 0;
    for (const core::Folder &folder : std::as_const(m_folders)) {
        if (folder.parentId.has_value()) {
            most = std::max(most, FolderActionsView::slotCount(folderActionIcons(folder.id)));
        }
    }
    const int width = FolderActionsView::columnWidthFor(most);
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        const int markWidth = m_outputMarks ? FolderActionsView::kMargin + FolderActionsView::kBox : 0;
        const int target = std::max(width, statusColumnBaseWidth(tree) + markWidth);
        if (tree->columnWidth(FolderActionsView::kActionsColumn) != target) {
            tree->setColumnWidth(FolderActionsView::kActionsColumn, target);
        }
        tree->viewport()->update();
    }
}

QVector<FolderActionIcon> CommandTreeWidget::folderActionIcons(const QString &folderId) const
{
    QVector<FolderActionIcon> icons;
    const auto folderIt = std::find_if(m_folders.cbegin(), m_folders.cend(),
                                       [&folderId](const core::Folder &f) { return f.id == folderId; });
    // Pasta de primeiro nível é uma ABA, sem linha: só as subpastas têm ações.
    if (folderIt == m_folders.cend() || !folderIt->parentId.has_value()) {
        return icons;
    }
    for (const core::FolderAction &action : core::actionsForFolder(*folderIt, m_globalActions, m_commandIndex)) {
        const core::Command &command = m_commandIndex.value(action.commandId);
        icons.append({action.commandId, core::folderActionCommandId(action.commandId, folderId), folderId,
                      command.name, command.icon, action.expansion, action.group, action.groupIcon});
    }
    return icons;
}

} // namespace kai::ui
