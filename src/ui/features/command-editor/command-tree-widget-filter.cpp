#include "ui/features/command-editor/command-tree-widget.h"

#include <QLabel>

#include "ui/shared/tab-bar-style.h"

#include "utils/translation-manager.h"
#include "ui/features/command-editor/command-tree-widget-internal.h"

namespace kai::ui {

using namespace cmdtree;

QString CommandTreeWidget::formatCommandLabel(const core::Command &command)
{
    // Só o nome; os parâmetros aparecem ao editar o comando.
    return command.name;
}

void CommandTreeWidget::snapshotExpansion()
{
    m_expansionBeforeFilter.clear();
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        const QString id = item->data(0, kCommandIdRole).toString();
        if (!id.isEmpty()) {
            m_expansionBeforeFilter.insert(id, item->isExpanded());
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
}

void CommandTreeWidget::restoreExpansion()
{
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        const auto it = m_expansionBeforeFilter.constFind(item->data(0, kCommandIdRole).toString());
        if (it != m_expansionBeforeFilter.constEnd()) {
            item->setExpanded(it.value());
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
    m_expansionBeforeFilter.clear();
}

void CommandTreeWidget::updateRootTabVisibility()
{
    if (!m_tabWidget) {
        return;
    }
    int firstVisible = -1;
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        const int index = m_tabWidget->indexOf(tree);
        if (index < 0) {
            continue;
        }
        bool hasVisible = false;
        for (int i = 0; i < tree->topLevelItemCount() && !hasVisible; ++i) {
            hasVisible = !tree->topLevelItem(i)->isHidden();
        }
        const bool show = !m_showRunningOnly || hasVisible;
        m_tabWidget->setTabVisible(index, show);
        if (show && firstVisible < 0) {
            firstVisible = index;
        }
    }
    // A aba atual sumiu: vai para a primeira que sobrou.
    const int current = m_tabWidget->currentIndex();
    if (firstVisible >= 0 && current >= 0 && !m_tabWidget->isTabVisible(current)) {
        m_tabWidget->setCurrentIndex(firstVisible);
    }
}

void CommandTreeWidget::setFilterQuery(const QString &query)
{
    const bool filtering = !query.isEmpty() || m_showRunningOnly;
    if (filtering && !m_filterSnapshotTaken) {
        snapshotExpansion();
        m_filterSnapshotTaken = true;
    }
    m_currentFilter = query;
    for (QTreeWidget *tree : m_treesByRootId) {
        applyFilterToTree(tree, query);
    }
    if (!filtering && m_filterSnapshotTaken) {
        restoreExpansion(); // parou de filtrar: as pastas voltam ao que estavam
        m_filterSnapshotTaken = false;
    }
    updateRootTabVisibility();
    updateRunningOnlyHint();
}

void CommandTreeWidget::setShowRunningOnly(bool runningOnly)
{
    if (m_showRunningOnly == runningOnly) {
        return;
    }
    m_showRunningOnly = runningOnly;
    setFilterQuery(m_currentFilter);
}

void CommandTreeWidget::updateRunningOnlyHint()
{
    if (!m_tabWidget) {
        return;
    }
    // Dica "nenhum em execução": filtro ligado e aba sem item visível.
    bool nothingVisible = false;
    if (m_showRunningOnly) {
        nothingVisible = true;
        if (auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget())) {
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                if (!tree->topLevelItem(i)->isHidden()) {
                    nothingVisible = false;
                    break;
                }
            }
        }
    }
    if (!nothingVisible) {
        if (m_runningOnlyHint) {
            m_runningOnlyHint->hide();
        }
        return;
    }
    if (!m_runningOnlyHint) {
        m_runningOnlyHint = new QLabel(utils::tr(QStringLiteral("tree.running_only_empty")), m_tabWidget);
        m_runningOnlyHint->setAlignment(Qt::AlignCenter);
        m_runningOnlyHint->setWordWrap(true);
        m_runningOnlyHint->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_runningOnlyHint->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                             .arg(utils::tokens::mutedFg()));
        m_tabWidget->installEventFilter(this);
        connect(m_tabWidget, &QTabWidget::currentChanged, this, [this]() { updateRunningOnlyHint(); });
    }
    const QTabBar *tabBar = m_tabWidget->tabBar();
    const int top = tabBar->geometry().bottom() + 1;
    m_runningOnlyHint->setGeometry(utils::tokens::space(4), top,
                                   qMax(0, m_tabWidget->width() - 2 * utils::tokens::space(4)),
                                   qMax(0, m_tabWidget->height() - top));
    m_runningOnlyHint->show();
    m_runningOnlyHint->raise();
}

void CommandTreeWidget::setShowHidden(bool show)
{
    if (m_showHidden == show) {
        return;
    }
    m_showHidden = show;
    // Reconstrói (ver createTreeForRoot): diferente da busca, que só esconde itens já criados.
    applyDataAndRebuild();
}

void CommandTreeWidget::applyFilterToTree(QTreeWidget *tree, const QString &query)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        applyFilterToItem(tree->topLevelItem(i), query);
    }
}

bool CommandTreeWidget::applyFilterToItem(QTreeWidgetItem *item, const QString &query)
{
    const bool wasHidden = item->isHidden();
    bool childMatches = false;
    for (int i = 0; i < item->childCount(); ++i) {
        childMatches = applyFilterToItem(item->child(i), query) || childMatches;
    }

    const bool selfMatches = query.isEmpty() || FuzzyMatcher::score(query, item->text(0)) >= 0;

    bool visible;
    if (m_showRunningOnly) {
        // Só comandos rodando (que casam com a busca) e as pastas que os contêm.
        const bool selfRunning = item->data(0, kIsCommandRole).toBool()
            && m_runningCommandIds.contains(item->data(0, kCommandIdRole).toString());
        visible = (selfRunning && selfMatches) || childMatches;
    } else {
        visible = selfMatches || childMatches;
    }
    item->setHidden(!visible);

    // Expande até o que casou: sempre p/ a busca; p/ "só em execução" só quando
    // o item acabou de aparecer (senão reabriria pastas que o usuário fechou).
    if (visible && childMatches && (!query.isEmpty() || (m_showRunningOnly && wasHidden))) {
        item->setExpanded(true);
    }
    return visible;
}

} // namespace kai::ui
