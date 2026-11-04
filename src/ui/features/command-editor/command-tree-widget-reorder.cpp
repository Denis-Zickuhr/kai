#include "ui/features/command-editor/command-tree-widget.h"

#include "utils/logger.h"

#include "ui/features/command-editor/command-tree-widget-internal.h"

namespace kai::ui {

using namespace cmdtree;

void CommandTreeWidget::handleItemsReordered()
{
    // Após um drop, percorre a árvore ativa em pré-ordem e captura de cada item
    // a nova POSIÇÃO e o novo PAI (reparenting só entre pastas: o
    // DraggableTreeWidget restringe a zona "On" a itens que não são
    // comando/coleção). O pai é o id do item-pai na árvore ou, no topo, a pasta
    // raiz da aba ativa. Emite structureChanged p/ o MainWindow persistir
    // folderId/parentId/order de uma vez.
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Drop recebido mas a árvore ativa não pôde ser resolvida — ignorado."));
        return;
    }

    const QString rootFolderId = currentRootFolderId();
    utils::Logger::info(kLogTag,
        QStringLiteral("Drag&drop: drop aceito na aba raiz '%1' (itens no topo: %2).")
            .arg(rootFolderId).arg(tree->topLevelItemCount()));

    QVector<TreeNodePlacement> placements;
    int seq = 0;

    std::function<void(QTreeWidgetItem *, const QString &)> visit =
        [&](QTreeWidgetItem *item, const QString &parentId) {
            const QString id = item->data(0, kCommandIdRole).toString();
            const bool isCommand = item->data(0, kIsCommandRole).toBool();
            const bool isNote = item->data(0, kIsNoteRole).toBool();
            const bool isCollection = item->data(0, kIsCollectionRole).toBool() && !isNote;
            if (!id.isEmpty()) {
                TreeNodePlacement placement;
                placement.id = id;
                placement.isCommand = isCommand;
                placement.isCollection = isCollection;
                placement.isNote = isNote;
                placement.parentId = parentId;
                // Sequência ÚNICA e global (pré-ordem): pastas, coleções e comandos
                // compartilham a escala de `order` e o rebuild os intercala na ordem exata.
                placement.order = seq++;
                placements.append(placement);
            }
            const QString childParentId = id.isEmpty() ? parentId : id;
            for (int i = 0; i < item->childCount(); ++i) {
                visit(item->child(i), childParentId);
            }
        };

    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        visit(tree->topLevelItem(i), rootFolderId);
    }

    // Atualiza o estado local antes do round-trip de persistência.
    for (const TreeNodePlacement &p : placements) {
        if (p.isCommand) {
            for (core::Command &command : m_commands) {
                if (command.id == p.id) {
                    command.order = p.order;
                    command.folderId = p.parentId;
                    break;
                }
            }
        } else if (p.isNote) {
            for (core::Note &note : m_notes) {
                if (note.id == p.id) {
                    note.order = p.order;
                    note.folderId = p.parentId;
                    break;
                }
            }
        } else if (p.isCollection) {
            for (core::Collection &collection : m_collections) {
                if (collection.id == p.id) {
                    collection.order = p.order;
                    collection.folderId = p.parentId;
                    break;
                }
            }
        } else {
            for (core::Folder &folder : m_folders) {
                if (folder.id == p.id) {
                    folder.order = p.order;
                    folder.parentId = p.parentId.isEmpty() ? std::nullopt : std::make_optional(p.parentId);
                    break;
                }
            }
        }
    }

    utils::Logger::info(kLogTag,
        QStringLiteral("Drag&drop: %1 item(ns) reordenado(s). Emitindo structureChanged.")
            .arg(placements.size()));
    for (const TreeNodePlacement &p : placements) {
        utils::Logger::debug(kLogTag,
            QStringLiteral("  -> %1 id='%2' order=%3 parent='%4'")
                .arg(p.isCommand ? QStringLiteral("cmd")
                     : p.isCollection ? QStringLiteral("colecao")
                     : p.isNote ? QStringLiteral("nota")
                     : QStringLiteral("pasta"))
                .arg(p.id).arg(p.order).arg(p.parentId));
    }

    emit structureChanged(placements);
}

void CommandTreeWidget::handleTabsReordered()
{
    // Lê a ordem visual das abas (id da pasta-raiz em tabData) e atribui
    // `order` sequencial às pastas-raiz; as não-raiz não são tocadas.
    QVector<TreeNodePlacement> placements;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        const QString rootId = m_tabWidget->tabBar()->tabData(i).toString();
        if (rootId.isEmpty()) {
            continue;
        }
        TreeNodePlacement placement;
        placement.id = rootId;
        placement.isCommand = false;
        placement.parentId = QString(); // pasta-raiz: sem pai
        placement.order = i;
        placements.append(placement);

        for (core::Folder &folder : m_folders) {
            if (folder.id == rootId) {
                folder.order = i;
                break;
            }
        }
    }
    if (!placements.isEmpty()) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Drag&drop de abas: %1 aba(s) reordenada(s). Emitindo tabsReordered.")
                .arg(placements.size()));
        // Sinal dedicado: o structureChanged genérico recriava as abas no meio
        // do reorder e travava o segundo arraste.
        emit tabsReordered(placements);
    }
}

} // namespace kai::ui
