#include "ui/features/command-editor/command-tree-widget.h"

#include <QHBoxLayout>

#include <QTimer>

#include "ui/shared/icon-picker-widget.h"

#include "ui/shared/draggable-tree-widget.h"
#include "ui/shared/overflow-indicator.h"
#include "ui/shared/tab-bar-style.h"
#include "ui/shared/tab-strip-background.h"

#include "utils/translation-manager.h"
#include "ui/features/command-editor/command-tree-widget-internal.h"

namespace kai::ui {

using namespace cmdtree;

CommandTreeWidget::CommandTreeWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void CommandTreeWidget::setTabBarHeight(int height)
{
    if (m_tabWidget) {
        m_tabWidget->tabBar()->setStyleSheet(flatTabBarQss(height));
    }
}

void CommandTreeWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget(this);
    // Abas rolam lateralmente quando há muitas; nomes longos ganham reticências.
    m_tabWidget->setUsesScrollButtons(true);
    m_tabWidget->setElideMode(Qt::ElideRight);
    m_tabWidget->tabBar()->setExpanding(false);
    // Barra de rolagem (arrastável) na cor de destaque logo ABAIXO da barra de
    // abas, quando as abas das pastas raiz não cabem: mostra quanto está visível
    // e onde, e não conflita com o sublinhado da aba selecionada.
    QTabBar *rootTabBar = m_tabWidget->tabBar();
    auto *tabOverflow = new OverflowIndicator(m_tabWidget, OverflowIndicator::forTabBar(rootTabBar),
                                              OverflowIndicator::scrollToForTabBar(rootTabBar));
    tabOverflow->setAnchor(rootTabBar);
    tabOverflow->setBottomInset(1); // 1px de respiro abaixo da barra

    setTabBarHeight(standardTabBarHeight());

    // Faixa de abas com fundo de outra cor (contraste com a lista abaixo).
    new TabStripBackground(m_tabWidget, m_tabWidget->tabBar());
    // Ao trocar de aba o foco vai p/ a árvore e seleciona o primeiro item, p/ ↑/↓ navegarem sem clique.
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int) {
        // Reconstruir a árvore (ex.: comando editado pelo agente de IA) troca de aba sozinho: o
        // teclado só vai p/ a árvore se já era dela (rebuildTabs devolve o foco no fim).
        if (!m_rebuilding) {
            focusFirstVisibleItem();
        }
        // A Saída acompanha o item ATUAL da nova aba; o currentItemChanged da
        // árvore NÃO dispara só por trocar de aba, então emite a seleção aqui.
        auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
        if (tree && tree->currentItem()) {
            QTreeWidgetItem *item = tree->currentItem();
            emit selectionChanged(item->data(0, kCommandIdRole).toString(),
                                  !item->data(0, kIsCommandRole).toBool());
        } else {
            emit selectionChanged(QString(), false);
        }
    });
    // Abas movíveis; a nova ordem das pastas-raiz é persistida ao mover.
    m_tabWidget->tabBar()->setMovable(true);
    // Menu de contexto na ABA (pasta-raiz).
    m_tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabWidget->tabBar(), &QTabBar::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        const int index = m_tabWidget->tabBar()->tabAt(pos);
        if (index < 0) {
            return;
        }
        showTabContextMenu(index, m_tabWidget->tabBar()->mapToGlobal(pos));
    });
    connect(m_tabWidget->tabBar(), &QTabBar::tabMoved, this, [this](int, int) {
        // Deferido: tratar o reorder (structureChanged -> persist -> possível
        // rebuild das abas) dentro do tabMoved reentra no QTabBar e crashava.
        QTimer::singleShot(0, this, [this]() { handleTabsReordered(); });
    });
    layout->addWidget(m_tabWidget);
}

void CommandTreeWidget::setData(const QVector<core::Folder> &folders,
                                const QVector<core::Command> &commands)
{
    m_folders = folders;
    m_commands = commands;
    // Versão 2-arg legada (testes): zera coleções/notas para não exibir dados órfãos.
    m_collections.clear();
    m_notes.clear();
    applyDataAndRebuild();
}

void CommandTreeWidget::setData(const QVector<core::Folder> &folders,
                                const QVector<core::Command> &commands,
                                const QVector<core::Collection> &collections)
{
    m_folders = folders;
    m_commands = commands;
    m_collections = collections;
    m_notes.clear();
    applyDataAndRebuild();
}

void CommandTreeWidget::setData(const QVector<core::Folder> &folders, const QVector<core::Command> &commands,
                                const QVector<core::Collection> &collections, const QVector<core::Note> &notes)
{
    m_folders = folders;
    m_commands = commands;
    m_collections = collections;
    m_notes = notes;
    applyDataAndRebuild();
}

QString CommandTreeWidget::defaultFolderId()
{
    return QString::fromLatin1(kDefaultFolderId);
}

void CommandTreeWidget::applyDataAndRebuild()
{
    m_commandIndex.clear();
    for (const core::Command &c : m_commands) {
        m_commandIndex.insert(c.id, c);
    }
    // Ordem de exibição: itens com `order` >= 0 primeiro, em ordem crescente;
    // os demais (-1) preservam a ordem de inserção (stable_sort). Sem
    // auto-ordenação alfabética, para manter a ordem das pastas raiz/abas.
    auto lessThan = [](int orderA, int orderB) {
        const bool manualA = orderA >= 0;
        const bool manualB = orderB >= 0;
        if (manualA != manualB) {
            return manualA; // itens com ordem manual primeiro
        }
        if (manualA && manualB) {
            return orderA < orderB;
        }
        return false; // ambos sem ordem manual: preserva ordem original
    };

    std::stable_sort(m_folders.begin(), m_folders.end(),
        [&lessThan](const core::Folder &a, const core::Folder &b) {
            return lessThan(a.order, b.order);
        });
    std::stable_sort(m_commands.begin(), m_commands.end(),
        [&lessThan](const core::Command &a, const core::Command &b) {
            return lessThan(a.order, b.order);
        });
    std::stable_sort(m_collections.begin(), m_collections.end(),
        [&lessThan](const core::Collection &a, const core::Collection &b) {
            return lessThan(a.order, b.order);
        });
    std::stable_sort(m_notes.begin(), m_notes.end(),
        [&lessThan](const core::Note &a, const core::Note &b) {
            return lessThan(a.order, b.order);
        });

    rebuildTabs();
}

void CommandTreeWidget::setRunningCommandIds(const QSet<QString> &runningCommandIds)
{
    // Marca o INÍCIO dos que passaram a rodar e descarta os que terminaram
    // (o contador de tempo é por execução).
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &id : runningCommandIds) {
        if (!m_runStartedAt.contains(id)) {
            m_runStartedAt.insert(id, now);
        }
    }
    for (auto it = m_runStartedAt.begin(); it != m_runStartedAt.end();) {
        it = runningCommandIds.contains(it.key()) ? std::next(it) : m_runStartedAt.erase(it);
    }

    const bool runningSetChanged = m_runningCommandIds != runningCommandIds;
    m_runningCommandIds = runningCommandIds;
    refreshCommandVisuals();
    refreshActionViews(); // a cor (verde/accent) dos ícones de ação acompanha
    // "Só em execução": quem começa/termina de rodar entra/sai da lista na hora.
    if (m_showRunningOnly && runningSetChanged) {
        setFilterQuery(m_currentFilter);
    }

    // Tique de 1s só existe enquanto HÁ algo rodando (sem timer ocioso).
    if (!m_runStartedAt.isEmpty()) {
        if (!m_elapsedTimer) {
            m_elapsedTimer = new QTimer(this);
            m_elapsedTimer->setInterval(1000);
            connect(m_elapsedTimer, &QTimer::timeout, this, &CommandTreeWidget::updateElapsedLabels);
        }
        if (!m_elapsedTimer->isActive()) {
            m_elapsedTimer->start();
        }
        updateElapsedLabels(); // mostra "0s" imediatamente, sem esperar 1s
    } else if (m_elapsedTimer) {
        m_elapsedTimer->stop();
    }
}

void CommandTreeWidget::resetRunTimer(const QString &commandId)
{
    // Sobrescreve INCONDICIONALMENTE (o laço de setRunningCommandIds só grava
    // se ausente); o MainWindow chama no instante síncrono em que a execução
    // começa, sem esperar o próximo poll.
    m_runStartedAt.insert(commandId, QDateTime::currentDateTime());
    if (!m_elapsedTimer) {
        m_elapsedTimer = new QTimer(this);
        m_elapsedTimer->setInterval(1000);
        connect(m_elapsedTimer, &QTimer::timeout, this, &CommandTreeWidget::updateElapsedLabels);
    }
    if (!m_elapsedTimer->isActive()) {
        m_elapsedTimer->start();
    }
    updateElapsedLabels();
}

QString CommandTreeWidget::formatElapsed(qint64 seconds)
{
    // Progressivo: "0s" até 59s, "0m 0s" a partir de 1 min, "0h 0m 0s" a partir de 1 h.
    if (seconds < 0) {
        seconds = 0;
    }
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 sec = seconds % 60;

    if (h > 0) {
        return QStringLiteral("%1h %2m %3s").arg(h).arg(m).arg(sec);
    }
    if (m > 0) {
        return QStringLiteral("%1m %2s").arg(m).arg(sec);
    }
    return QStringLiteral("%1s").arg(sec);
}

void CommandTreeWidget::updateElapsedLabels()
{
    if (m_runStartedAt.isEmpty()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();
        std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
            if (item->data(0, kIsCommandRole).toBool()) {
                const QString id = item->data(0, kCommandIdRole).toString();
                const auto startIt = m_runStartedAt.constFind(id);
                if (startIt != m_runStartedAt.constEnd()) {
                    const qint64 secs = startIt.value().secsTo(now);
                    item->setText(kStatusColumn, formatElapsed(secs));
                }
            }
            for (int i = 0; i < item->childCount(); ++i) {
                visit(item->child(i));
            }
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
}

void CommandTreeWidget::setFailedCommandIds(const QSet<QString> &failedCommandIds)
{
    m_failedCommandIds = failedCommandIds;
    refreshCommandVisuals();
}

void CommandTreeWidget::rebuildTabs()
{
    struct RebuildGuard {
        bool &flag;
        explicit RebuildGuard(bool &f) : flag(f) { flag = true; }
        ~RebuildGuard() { flag = false; }
    } rebuildGuard(m_rebuilding);
    QString activeRootId;
    if (m_tabWidget->currentIndex() >= 0) {
        activeRootId = m_tabWidget->tabBar()->tabData(m_tabWidget->currentIndex()).toString();
    }

    // Preserva expansão das pastas e item selecionado (rebuildTabs recria tudo
    // do zero) para reaplicar após reconstruir.
    const QSet<QString> expandedIds = collectExpandedIds();
    const QString selectedId = currentSelectionId();
    // Sem nada com o foco (a primeira carga) ou com ele na própria árvore, ela o recebe de volta no fim; com o foco em
    // outro painel (o campo do chat da IA, o Documento) ele fica onde está.
    const QWidget *focused = QApplication::focusWidget();
    const bool keepsFocus = !focused || isAncestorOf(focused);

    m_tabWidget->clear();
    m_treesByRootId.clear();

    QMap<QString, core::Folder> foldersById;
    for (const core::Folder &folder : m_folders) {
        foldersById.insert(folder.id, folder);
    }

    QVector<core::Folder> rootFolders;
    for (const core::Folder &folder : m_folders) {
        const bool hasMissingParent = folder.parentId.has_value()
            && !foldersById.contains(folder.parentId.value());
        if (!folder.parentId.has_value() || hasMissingParent) {
            rootFolders << folder;
        }
    }

    // Pasta PADRÃO para órfãos: comando/coleção com folderId vazio ou que não
    // resolve para pasta/comando existente. A aba só é criada se houver órfãos.
    // Pastas nunca são órfãs aqui (sem pai já viram raiz acima).
    const bool hasDefaultFolder = foldersById.contains(kDefaultFolderId);
    if (!hasDefaultFolder) {
        auto resolvesToExisting = [this, &foldersById](const QString &parentId) -> bool {
            QString cur = parentId;
            QSet<QString> visited;
            for (int guard = 0; guard < 128; ++guard) {
                if (cur.isEmpty() || visited.contains(cur)) return false;
                visited.insert(cur);
                if (foldersById.contains(cur)) return true;
                const auto it = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                    [&cur](const core::Command &c) { return c.id == cur; });
                if (it == m_commands.constEnd()) return false;
                cur = it->folderId;
            }
            return false;
        };
        bool hasOrphans = false;
        for (const core::Command &c : m_commands) {
            if (!resolvesToExisting(c.folderId)) { hasOrphans = true; break; }
        }
        if (!hasOrphans) {
            for (const core::Collection &col : m_collections) {
                if (!foldersById.contains(col.folderId)) { hasOrphans = true; break; }
            }
        }
        if (!hasOrphans) {
            for (const core::Note &note : m_notes) {
                if (!foldersById.contains(note.folderId)) { hasOrphans = true; break; }
            }
        }
        if (hasOrphans) {
            core::Folder defaultFolder;
            defaultFolder.id = kDefaultFolderId;
            defaultFolder.name = utils::tr(QStringLiteral("folder.default_name"));
            defaultFolder.icon = QStringLiteral("folder");
            defaultFolder.order = -1; // vai para o fim das abas manuais
            rootFolders << defaultFolder;
            foldersById.insert(defaultFolder.id, defaultFolder);
        }
    }

    for (const core::Folder &rootFolder : rootFolders) {
        // Pasta raiz oculta some a ABA inteira, salvo com "Mostrar ocultos".
        if (rootFolder.hidden && !m_showHidden) {
            continue;
        }
        auto *tree = createTreeForRoot(rootFolder);
        const int tabIndex = m_tabWidget->addTab(tree, folderIconFor(rootFolder), rootFolder.name);
        const QString driftTip = syncTooltipFor(rootFolder.id);
        m_tabWidget->setTabToolTip(tabIndex, driftTip.isEmpty() ? rootFolder.name
                                                                  : rootFolder.name + QStringLiteral("\n") + driftTip);
        m_tabWidget->tabBar()->setTabData(tabIndex, rootFolder.id);
        m_treesByRootId.insert(rootFolder.id, tree);
    }

    if (m_tabWidget->count() == 0) {
        return;
    }

    int restoredIndex = -1;
    if (!activeRootId.isEmpty()) {
        restoredIndex = m_tabWidget->indexOf(m_treesByRootId.value(activeRootId, nullptr));
    }
    m_tabWidget->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : 0);
    setFilterQuery(m_currentFilter);

    // Reexpande só o que estava expandido; pastas novas nascem colapsadas.
    if (!expandedIds.isEmpty()) {
        restoreExpandedIds(expandedIds);
    }

    if (!selectedId.isEmpty()) {
        selectItemById(selectedId);
    }

    // Seleciona o primeiro item da aba ativa (↑/↓/Enter sem clique), após o
    // filtro p/ não pegar item escondido; só se nada foi restaurado.
    if (auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget())) {
        if (!tree->currentItem() || tree->currentItem()->isHidden()) {
            selectFirstVisible(tree);
        }
    }

    if (keepsFocus) {
        if (auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget())) {
            tree->setFocus();
        }
    }

    // O rebuild recria os QTreeWidget, perdendo o estilo de setBackground.
    applyBackgroundStyle();
    refreshActionViews(); // largura da coluna de ações nas árvores novas
}

QIcon CommandTreeWidget::folderIconFor(const core::Folder &folder) const
{
    QIcon icon = IconPickerWidget::iconForName(folder.icon);
    const auto drift = m_syncDrift.constFind(folder.id);
    if (drift != m_syncDrift.constEnd() && drift.value() != core::SyncDrift::InSync) {
        icon = withDriftBadge(icon);
    }
    if (m_draftFolderIds.contains(folder.id)) {
        icon = withDraftBadge(icon);
    }
    return icon;
}

QString CommandTreeWidget::syncTooltipFor(const QString &folderId) const
{
    QString tip;
    switch (m_syncDrift.value(folderId, core::SyncDrift::InSync)) {
    case core::SyncDrift::KaiChanged: tip = utils::tr(QStringLiteral("tree.sync.kai_changed")); break;
    case core::SyncDrift::FileChanged: tip = utils::tr(QStringLiteral("tree.sync.file_changed")); break;
    case core::SyncDrift::BothChanged: tip = utils::tr(QStringLiteral("tree.sync.both_changed")); break;
    case core::SyncDrift::InSync: break;
    }
    if (m_draftFolderIds.contains(folderId)) {
        const QString draft = utils::tr(QStringLiteral("tree.drafts.folder_tooltip"));
        tip = tip.isEmpty() ? draft : tip + QStringLiteral("\n") + draft;
    }
    return tip;
}

QIcon CommandTreeWidget::noteIconFor(const core::Note &note) const
{
    const QIcon icon = IconPickerWidget::iconForName(note.icon.isEmpty() ? QStringLiteral("notebook") : note.icon);
    return m_draftNoteIds.contains(note.id) ? withDraftBadge(icon) : icon;
}

QString CommandTreeWidget::noteTooltipFor(const core::Note &note) const
{
    QString tip = note.local ? utils::tr(QStringLiteral("note.tooltip.local")) : utils::tr(QStringLiteral("note.tooltip.synced"));
    if (m_draftNoteIds.contains(note.id)) {
        tip += QStringLiteral("\n") + utils::tr(QStringLiteral("tree.drafts.note_tooltip"));
    }
    return tip;
}

void CommandTreeWidget::setDraftMarks(const QSet<QString> &folderIds, const QSet<QString> &noteIds)
{
    QMap<QString, core::Folder> foldersById;
    for (const core::Folder &f : std::as_const(m_folders)) {
        foldersById.insert(f.id, f);
    }
    QSet<QString> withAncestors;
    for (const QString &id : folderIds) {
        QString current = id;
        while (!current.isEmpty() && !withAncestors.contains(current)) {
            withAncestors.insert(current);
            const auto it = foldersById.constFind(current);
            current = it != foldersById.constEnd() ? it->parentId.value_or(QString()) : QString();
        }
    }
    if (withAncestors == m_draftFolderIds && noteIds == m_draftNoteIds) {
        return;
    }
    m_draftFolderIds = withAncestors;
    m_draftNoteIds = noteIds;
    refreshSyncMarks();
}

void CommandTreeWidget::setSyncDrift(const QMap<QString, core::SyncDrift> &driftByFolderId)
{
    if (m_syncDrift == driftByFolderId) {
        return;
    }
    m_syncDrift = driftByFolderId;
    refreshSyncMarks();
}

void CommandTreeWidget::refreshSyncMarks()
{
    QMap<QString, core::Folder> foldersById;
    for (const core::Folder &f : std::as_const(m_folders)) {
        foldersById.insert(f.id, f);
    }
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
            if (item->data(0, kIsNoteRole).toBool()) {
                const QString noteId = item->data(0, kCommandIdRole).toString();
                const auto note = std::find_if(m_notes.constBegin(), m_notes.constEnd(),
                                               [&noteId](const core::Note &n) { return n.id == noteId; });
                if (note != m_notes.constEnd()) {
                    item->setIcon(0, noteIconFor(*note));
                    item->setToolTip(0, noteTooltipFor(*note));
                }
            } else if (!item->data(0, kIsCommandRole).toBool() && !item->data(0, kIsCollectionRole).toBool()) {
                const auto folder = foldersById.constFind(item->data(0, kCommandIdRole).toString());
                if (folder != foldersById.constEnd()) {
                    item->setIcon(0, folderIconFor(*folder));
                    item->setToolTip(0, syncTooltipFor(folder->id));
                }
            }
            for (int i = 0; i < item->childCount(); ++i) {
                visit(item->child(i));
            }
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        const auto folder = foldersById.constFind(m_tabWidget->tabBar()->tabData(i).toString());
        if (folder == foldersById.constEnd()) {
            continue;
        }
        m_tabWidget->setTabIcon(i, folderIconFor(*folder));
        const QString tip = syncTooltipFor(folder->id);
        m_tabWidget->setTabToolTip(i, tip.isEmpty() ? folder->name : folder->name + QStringLiteral("\n") + tip);
    }
}

void CommandTreeWidget::refreshCommandVisuals()
{
    // Atualiza só o indicador de status e os controles inline de cada item
    // existente, SEM recriar as árvores (perderia seleção e scroll a cada
    // mudança de status de execução).
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();

        std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
            const bool isCommand = item->data(0, kIsCommandRole).toBool();
            if (isCommand) {
                const QString commandId = item->data(0, kCommandIdRole).toString();
                const auto commandIt = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                    [&commandId](const core::Command &c) { return c.id == commandId; });
                if (commandIt != m_commands.constEnd()) {
                    item->setIcon(0, IconPickerWidget::iconForName(commandIt->icon));
                    applyRunningIndicator(item, m_runningCommandIds.contains(commandId));
                }
            }
            for (int i = 0; i < item->childCount(); ++i) {
                visit(item->child(i));
            }
        };

        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
}

QString CommandTreeWidget::rootIdForFolder(const core::Folder &folder,
                                            const QMap<QString, core::Folder> &foldersById)
{
    QString currentId = folder.id;
    QSet<QString> visited;

    while (foldersById.contains(currentId)) {
        if (visited.contains(currentId)) {
            // Ciclo em dados corrompidos: mantém a pasta inicial como raiz de fallback.
            return folder.id;
        }
        visited.insert(currentId);

        const core::Folder current = foldersById.value(currentId);
        if (!current.parentId.has_value() || !foldersById.contains(current.parentId.value())) {
            return current.id;
        }
        currentId = current.parentId.value();
    }

    return folder.id;
}

} // namespace kai::ui
