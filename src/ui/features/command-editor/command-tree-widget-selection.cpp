#include "ui/features/command-editor/command-tree-widget.h"

#include "ui/features/command-editor/command-tree-widget-internal.h"

namespace kai::ui {

using namespace cmdtree;

void CommandTreeWidget::handleTreeCurrentItemChanged(QTreeWidgetItem *current,
                                                       QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    // Outra seleção tira o foco do ícone de ação (restaurar após rebuild não conta).
    if (!m_rebuilding && !m_focusedActionId.isEmpty()) {
        setFocusedAction(QString());
    }
    if (!current) {
        emit selectionChanged(QString(), false);
        return;
    }

    const QString itemId = current->data(0, kCommandIdRole).toString();
    const bool isCommand = current->data(0, kIsCommandRole).toBool();
    emit selectionChanged(itemId, !isCommand);
}

QString CommandTreeWidget::currentSelectionId() const
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return QString();
    }
    return tree->currentItem()->data(0, kCommandIdRole).toString();
}

void CommandTreeWidget::expandCurrentItem()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return;
    }
    tree->currentItem()->setExpanded(true);
}

void CommandTreeWidget::collapseCurrentItem()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return;
    }
    tree->currentItem()->setExpanded(false);
}

void CommandTreeWidget::expandAll()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree) {
        return;
    }
    tree->expandAll();
}

void CommandTreeWidget::collapseAll()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree) {
        return;
    }
    tree->collapseAll();
}

bool CommandTreeWidget::currentSelectionIsFolder() const
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return false;
    }
    // Coleção NÃO é pasta: o botão de editar da sidebar roteia ao editor certo.
    if (tree->currentItem()->data(0, kIsCollectionRole).toBool()) {
        return false;
    }
    return !tree->currentItem()->data(0, kIsCommandRole).toBool();
}

bool CommandTreeWidget::currentSelectionIsCollection() const
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return false;
    }
    // Nota é um item não-executável como a coleção, mas tem fluxo próprio (ver currentSelectionIsNote).
    return tree->currentItem()->data(0, kIsCollectionRole).toBool() && !tree->currentItem()->data(0, kIsNoteRole).toBool();
}

bool CommandTreeWidget::currentSelectionIsNote() const
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return false;
    }
    return tree->currentItem()->data(0, kIsNoteRole).toBool();
}

QString CommandTreeWidget::currentRootFolderId() const
{
    const int index = m_tabWidget->currentIndex();
    if (index < 0) {
        return QString();
    }
    return m_tabWidget->tabBar()->tabData(index).toString();
}

void CommandTreeWidget::focusFirstVisibleItem()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (tree) {
        focusFirstVisibleItem(tree);
    }
}

void CommandTreeWidget::focusFirstVisibleItem(QTreeWidget *tree)
{
    tree->setFocus();
    // Garante o item corrente visível também SELECIONADO (highlight).
    if (QTreeWidgetItem *current = tree->currentItem(); current && !current->isHidden()) {
        if (!current->isSelected()) {
            tree->clearSelection();
            current->setSelected(true);
        }
        return;
    }
    selectFirstVisible(tree);
}

void CommandTreeWidget::selectFirstVisible(QTreeWidget *tree)
{
    if (!tree) {
        return;
    }
    // setCurrentItem sozinho não garante o :selected que o QSS pinta, sobretudo
    // antes de a árvore ter foco/estar visível (rebuild das abas).
    auto applySelection = [tree](QTreeWidgetItem *item) {
        tree->setCurrentItem(item);
        tree->clearSelection();
        item->setSelected(true);
        tree->scrollToItem(item);
    };

    // Seleciona o primeiro item VISÍVEL sem expandir pastas colapsadas: só
    // desce em itens já expandidos; senão seleciona a própria pasta.
    std::function<bool(QTreeWidgetItem *)> selectFirst = [&](QTreeWidgetItem *item) {
        if (item->isHidden()) {
            return false;
        }
        if (item->childCount() > 0 && item->isExpanded()) {
            for (int i = 0; i < item->childCount(); ++i) {
                if (selectFirst(item->child(i))) {
                    return true;
                }
            }
        }
        applySelection(item);
        return true;
    };

    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (selectFirst(tree->topLevelItem(i))) {
            return;
        }
    }
}

QSet<QString> CommandTreeWidget::collectExpandedIds() const
{
    QSet<QString> expanded;
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        if (item->childCount() > 0 && item->isExpanded()) {
            const QString id = item->data(0, kCommandIdRole).toString();
            if (!id.isEmpty()) {
                expanded.insert(id);
            }
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
    return expanded;
}

void CommandTreeWidget::restoreExpandedIds(const QSet<QString> &expandedIds)
{
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        const QString id = item->data(0, kCommandIdRole).toString();
        if (!id.isEmpty() && expandedIds.contains(id)) {
            item->setExpanded(true);
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
}

bool CommandTreeWidget::selectItemById(const QString &id)
{
    if (id.isEmpty()) {
        return false;
    }
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();
        std::function<QTreeWidgetItem *(QTreeWidgetItem *)> find =
            [&](QTreeWidgetItem *item) -> QTreeWidgetItem * {
                if (item->data(0, kCommandIdRole).toString() == id) {
                    return item;
                }
                for (int i = 0; i < item->childCount(); ++i) {
                    if (QTreeWidgetItem *found = find(item->child(i))) {
                        return found;
                    }
                }
                return nullptr;
            };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (QTreeWidgetItem *found = find(tree->topLevelItem(i)); found && !found->isHidden()) {
                m_tabWidget->setCurrentWidget(tree);
                tree->setCurrentItem(found);
                tree->clearSelection();
                found->setSelected(true);
                tree->scrollToItem(found);
                return true;
            }
        }
    }
    return false;
}

bool CommandTreeWidget::showRootTab(const QString &rootFolderId)
{
    QTreeWidget *tree = m_treesByRootId.value(rootFolderId, nullptr);
    const int index = tree ? m_tabWidget->indexOf(tree) : -1;
    if (index < 0 || !m_tabWidget->isTabVisible(index)) {
        return false;
    }
    m_tabWidget->setCurrentIndex(index);
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    return true;
}

void CommandTreeWidget::selectNextTab()
{
    const int count = m_tabWidget->count();
    for (int step = 1; step < count; ++step) { // pula as abas escondidas pelo filtro
        const int index = (m_tabWidget->currentIndex() + step) % count;
        if (m_tabWidget->isTabVisible(index)) {
            m_tabWidget->setCurrentIndex(index);
            return;
        }
    }
}

void CommandTreeWidget::selectPreviousTab()
{
    const int count = m_tabWidget->count();
    for (int step = 1; step < count; ++step) {
        const int index = (m_tabWidget->currentIndex() - step + count * 2) % count;
        if (m_tabWidget->isTabVisible(index)) {
            m_tabWidget->setCurrentIndex(index);
            return;
        }
    }
}

} // namespace kai::ui
