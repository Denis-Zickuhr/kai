#include "ui/features/command-editor/command-tree-widget.h"

#include <QMenu>

#include <QTimer>

#include "core/http-resolve.h"
#include "ui/shared/lucide-icons.h"

#include "ui/shared/tab-bar-style.h"

#include "utils/translation-manager.h"
#include "ui/features/command-editor/command-tree-widget-internal.h"

namespace kai::ui {

using namespace cmdtree;

void CommandTreeWidget::handleItemActivated(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (item->data(0, kIsNoteRole).toBool()) {
        const QString noteId = item->data(0, kCommandIdRole).toString();
        if (!noteId.isEmpty()) {
            emit noteActivated(noteId); // abre a nota no editor do leitor
        }
        return;
    }
    if (item->data(0, kIsCollectionRole).toBool()) {
        const QString collectionId = item->data(0, kCommandIdRole).toString();
        if (!collectionId.isEmpty()) {
            emit collectionEditRequested(collectionId);
        }
        return;
    }
    if (!item->data(0, kIsCommandRole).toBool()) {
        item->setExpanded(!item->isExpanded());
        return;
    }

    const QString commandId = item->data(0, kCommandIdRole).toString();
    if (!commandId.isEmpty()) {
        emit commandActivated(commandId);
    }
}

void CommandTreeWidget::handleTreeContextMenuRequested(const QPoint &pos)
{
    auto *tree = qobject_cast<QTreeWidget *>(sender());
    if (!tree) {
        return;
    }
    QTreeWidgetItem *item = tree->itemAt(pos);
    const QPoint globalPos = tree->viewport()->mapToGlobal(pos);
    showContextMenu(item, globalPos);
}

void CommandTreeWidget::openContextMenuForCurrent()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree) {
        return;
    }
    QTreeWidgetItem *item = tree->currentItem();
    // Ancora o menu no item corrente (ou no canto da árvore).
    QPoint globalPos;
    if (item) {
        const QRect r = tree->visualItemRect(item);
        globalPos = tree->viewport()->mapToGlobal(r.center());
    } else {
        globalPos = tree->mapToGlobal(QPoint(tree->width() / 2, 20));
    }
    showContextMenu(item, globalPos);
}

void CommandTreeWidget::showContextMenu(QTreeWidgetItem *item, const QPoint &globalPos)
{
    const QColor accent(utils::tokens::accent());
    const QColor playColor(80, 250, 123);
    const QColor stopColor(241, 250, 140);
    const QColor killColor(255, 85, 85);

    // Todos os itens do menu levam ícone Lucide colorido.
    auto addIconAction = [&](QMenu *m, const QString &icon, const QColor &color,
                             const QString &text) -> QAction * {
        QAction *a = m->addAction(LucideIcons::icon(icon, color, 16), text);
        return a;
    };

    // Navegação por teclado: em alguns compositores (Wayland) o popup abre sem
    // foco de teclado e as setas não navegam; agenda foco e ativação da
    // primeira ação habilitada. Chamar ANTES de cada menu.exec().
    auto armKeyboardNav = [](QMenu *m) {
        QTimer::singleShot(0, m, [m]() {
            m->activateWindow();
            m->raise();
            m->setFocus(Qt::OtherFocusReason);
            const auto acts = m->actions();
            for (QAction *a : acts) {
                if (a->isEnabled() && !a->isSeparator()) {
                    m->setActiveAction(a);
                    break;
                }
            }
        });
    };

    // Menu ancorado na aba atual: sem um parent na hierarquia da janela o QMenu
    // não herda o stylesheet global e aparece com o estilo nativo.
    QWidget *anchor = m_tabWidget->currentWidget();
    if (!anchor) {
        anchor = this;
    }
    QMenu menu(anchor);

    if (item) {
        const bool isCommand = item->data(0, kIsCommandRole).toBool();
        const bool isCollection = item->data(0, kIsCollectionRole).toBool();
        const QString itemId = item->data(0, kCommandIdRole).toString();

        if (item->data(0, kIsNoteRole).toBool() && !itemId.isEmpty()) {
            QAction *openNote = addIconAction(&menu, QStringLiteral("pencil"), accent,
                                              utils::tr(QStringLiteral("ctx.note.open")));
            QAction *editNote = addIconAction(&menu, QStringLiteral("settings"), accent,
                                              utils::tr(QStringLiteral("ctx.note.edit")));
            QAction *dupNote = addIconAction(&menu, QStringLiteral("copy"), accent,
                                             utils::tr(QStringLiteral("ctx.note.duplicate")));
            menu.addSeparator();
            QAction *saveNote = addIconAction(&menu, QStringLiteral("download"), accent,
                                              utils::tr(QStringLiteral("ctx.note.save_as")));
            QAction *openCopyNote = addIconAction(&menu, QStringLiteral("external-link"), accent,
                                                  utils::tr(QStringLiteral("ctx.note.open_copy")));
            QAction *copyNote = addIconAction(&menu, QStringLiteral("clipboard-copy"), accent,
                                              utils::tr(QStringLiteral("ctx.note.copy_content")));
            menu.addSeparator();
            QAction *delNote = addIconAction(&menu, QStringLiteral("trash-2"), killColor,
                                             utils::tr(QStringLiteral("ctx.note.delete")));
            menu.addSeparator();
            addCreationActions(&menu, addIconAction, accent);
            armKeyboardNav(&menu);
            QAction *chosen = menu.exec(globalPos);
            if (chosen == openNote) emit noteActivated(itemId);
            else if (chosen == editNote) emit noteEditRequested(itemId);
            else if (chosen == dupNote) emit noteDuplicateRequested(itemId);
            else if (chosen == saveNote) emit noteFileActionRequested(itemId, core::NoteFileAction::SaveAs);
            else if (chosen == openCopyNote) emit noteFileActionRequested(itemId, core::NoteFileAction::OpenCopy);
            else if (chosen == copyNote) emit noteFileActionRequested(itemId, core::NoteFileAction::CopyContent);
            else if (chosen == delNote) emit noteDeleteRequested(itemId);
            else handleCreationChoice(chosen);
            return;
        }

        if (isCollection && !itemId.isEmpty()) {
            QAction *editCol = addIconAction(&menu, QStringLiteral("pencil"), accent,
                                             utils::tr(QStringLiteral("ctx.collection.edit")));
            QAction *dupCol = addIconAction(&menu, QStringLiteral("copy"), accent,
                                            utils::tr(QStringLiteral("ctx.collection.duplicate")));
            menu.addSeparator();
            QAction *delCol = addIconAction(&menu, QStringLiteral("trash-2"), killColor,
                                            utils::tr(QStringLiteral("ctx.collection.delete")));
            menu.addSeparator();
            addCreationActions(&menu, addIconAction, accent);
            armKeyboardNav(&menu);
            QAction *chosen = menu.exec(globalPos);
            if (chosen == editCol) emit collectionEditRequested(itemId);
            else if (chosen == dupCol) emit collectionDuplicateRequested(itemId);
            else if (chosen == delCol) emit collectionDeleteRequested(itemId);
            else handleCreationChoice(chosen); // roteia Nova Pasta/Comando/Coleção
            return;
        }

        if (!itemId.isEmpty()) {
            // Ações de comando (Rodar/Parar/Forçar/Resetar) no topo do menu.
            QAction *runAction = nullptr;
            QAction *stopAction = nullptr;
            QAction *forceAction = nullptr;
            QAction *resetAction = nullptr;
            if (isCommand) {
                const bool running = m_runningCommandIds.contains(itemId);
                runAction = addIconAction(&menu, QStringLiteral("play"), playColor,
                                          utils::tr(QStringLiteral("tree.run")));
                runAction->setEnabled(!running);
                stopAction = addIconAction(&menu, QStringLiteral("square"), stopColor,
                                           utils::tr(QStringLiteral("tree.stop")));
                stopAction->setEnabled(running);
                forceAction = addIconAction(&menu, QStringLiteral("octagon-x"), killColor,
                                            utils::tr(QStringLiteral("sidebar.force_stop")));
                forceAction->setEnabled(running);
                resetAction = addIconAction(&menu, QStringLiteral("rotate-ccw"), accent,
                                            utils::tr(QStringLiteral("tree.reset")));
                resetAction->setEnabled(running || m_failedCommandIds.contains(itemId));
            }
            QAction *quickEditAction = nullptr;
            if ((isCommand && item->data(0, kIsHttpRole).toBool())
                || (!isCommand && core::folderHasHttpCommand(itemId, m_folders, m_commands))) {
                quickEditAction = addIconAction(&menu, QStringLiteral("pencil"), accent,
                                                utils::tr(QStringLiteral("sidebar.edit_body")));
            }
            QAction *showOutputAction = nullptr;
            QAction *showDocumentAction = nullptr;
            if (isCommand && m_commandHasOutput && m_commandHasOutput(itemId)) {
                showOutputAction = addIconAction(&menu, QStringLiteral("square-terminal"), accent,
                                                 utils::tr(QStringLiteral("ctx.show_output")));
            }
            if (m_itemHasDocument && m_itemHasDocument(itemId)) {
                showDocumentAction = addIconAction(&menu, QStringLiteral("file-text"), accent,
                                                   utils::tr(QStringLiteral("ctx.show_document")));
            }
            if (!menu.actions().isEmpty()) {
                menu.addSeparator();
            }

            QAction *editAction = addIconAction(&menu, QStringLiteral("pencil"), accent,
                isCommand ? utils::tr(QStringLiteral("ctx.command.edit"))
                          : utils::tr(QStringLiteral("ctx.folder.edit")));
            QAction *duplicateAction = addIconAction(&menu, QStringLiteral("copy"), accent,
                isCommand ? utils::tr(QStringLiteral("ctx.command.duplicate"))
                          : utils::tr(QStringLiteral("ctx.folder.duplicate")));
            QAction *syncToFileAction = nullptr;
            QAction *syncFromFileAction = nullptr;
            if (!isCommand && isProjectFolder(itemId)) {
                menu.addSeparator();
                syncToFileAction = addIconAction(&menu, QStringLiteral("upload"), accent,
                                                 utils::tr(QStringLiteral("ctx.folder.sync_to_file")));
                syncFromFileAction = addIconAction(&menu, QStringLiteral("download"), accent,
                                                   utils::tr(QStringLiteral("ctx.folder.sync_from_file")));
            }
            QAction *exportAction = addIconAction(&menu, QStringLiteral("share-2"), accent,
                isCommand ? utils::tr(QStringLiteral("ctx.command.export"))
                          : utils::tr(QStringLiteral("ctx.folder.export")));
            // Um comando KIP pode virar um painel fixo da janela (dashboard de status, formulário de deploy).
            QAction *pinAction = nullptr;
            if (isCommand) {
                for (const core::Command &command : std::as_const(m_commands)) {
                    if (command.id == itemId) {
                        if (command.kip && command.type == core::CommandType::Command) {
                            pinAction = addIconAction(&menu, QStringLiteral("pin"), accent,
                                                      utils::tr(QStringLiteral("ctx.command.pin_panel")));
                        }
                        break;
                    }
                }
            }
            menu.addSeparator();
            QAction *deleteAction = addIconAction(&menu, QStringLiteral("trash-2"), killColor,
                isCommand ? utils::tr(QStringLiteral("ctx.command.delete"))
                          : utils::tr(QStringLiteral("ctx.folder.delete")));
            menu.addSeparator();
            addCreationActions(&menu, addIconAction, accent);

            armKeyboardNav(&menu);

            QAction *chosen = menu.exec(globalPos);
            if (!chosen) return;
            if (chosen == runAction) emit playRequested(itemId);
            else if (chosen == stopAction) emit killRequested(itemId);
            else if (chosen == forceAction) emit forceStopRequested(itemId);
            else if (chosen == resetAction) emit resetRequested(itemId);
            else if (quickEditAction && chosen == quickEditAction) {
                if (isCommand) emit quickEditBodyRequested(itemId);
                else emit quickEditFolderRequested(itemId);
            }
            else if (showOutputAction && chosen == showOutputAction) emit commandOutputRequested(itemId);
            else if (showDocumentAction && chosen == showDocumentAction) emit documentRequested(itemId);
            else if (chosen == editAction) emit editRequested(itemId, !isCommand);
            else if (chosen == duplicateAction) emit duplicateRequested(itemId, !isCommand);
            else if (chosen == exportAction) emit exportRequested(itemId, !isCommand);
            else if (pinAction && chosen == pinAction) emit pinAsPanelRequested(itemId);
            else if (chosen == deleteAction) emit deleteRequested(itemId, !isCommand);
            else if (syncToFileAction && chosen == syncToFileAction) emit projectSyncRequested(itemId, true);
            else if (syncFromFileAction && chosen == syncFromFileAction) emit projectSyncRequested(itemId, false);
            else handleCreationChoice(chosen); // roteia Nova Pasta/Comando/Coleção
            return;
        }
    }

    addCreationActions(&menu, addIconAction, accent);
    armKeyboardNav(&menu);
    QAction *chosen = menu.exec(globalPos);
    handleCreationChoice(chosen);
}

void CommandTreeWidget::showTabContextMenu(int tabIndex, const QPoint &globalPos)
{
    const QString rootId = m_tabWidget->tabBar()->tabData(tabIndex).toString();
    if (rootId.isEmpty()) {
        return;
    }

    // Estado atual de "oculta": rotula "Ocultar" vs "Mostrar" e permite
    // reverter uma pasta que ficou oculta e virou raiz (órfã), pois o form de
    // edição não tem o campo e o atalho normal só enxerga itens DENTRO da árvore.
    bool isHidden = false;
    for (const core::Folder &f : m_folders) {
        if (f.id == rootId) { isHidden = f.hidden; break; }
    }

    QMenu menu(this);
    const QColor accent(utils::tokens::accent());
    const QColor killColor(255, 85, 85);
    QAction *editAction = menu.addAction(LucideIcons::icon(QStringLiteral("pencil"), accent, 16),
        utils::tr(QStringLiteral("ctx.folder.edit")));
    QAction *toggleHiddenAction = menu.addAction(
        LucideIcons::icon(isHidden ? QStringLiteral("eye") : QStringLiteral("eye-off"), accent, 16),
        utils::tr(isHidden ? QStringLiteral("ctx.folder.show") : QStringLiteral("ctx.folder.hide")));
    QAction *syncToFileAction = nullptr;
    QAction *syncFromFileAction = nullptr;
    if (isProjectFolder(rootId)) {
        menu.addSeparator();
        syncToFileAction = menu.addAction(LucideIcons::icon(QStringLiteral("upload"), accent, 16),
            utils::tr(QStringLiteral("ctx.folder.sync_to_file")));
        syncFromFileAction = menu.addAction(LucideIcons::icon(QStringLiteral("download"), accent, 16),
            utils::tr(QStringLiteral("ctx.folder.sync_from_file")));
    }
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(LucideIcons::icon(QStringLiteral("trash-2"), killColor, 16),
        utils::tr(QStringLiteral("ctx.folder.delete")));

    QTimer::singleShot(0, &menu, [&menu]() {
        menu.activateWindow();
        menu.raise();
        menu.setFocus(Qt::OtherFocusReason);
        if (!menu.actions().isEmpty()) {
            menu.setActiveAction(menu.actions().first());
        }
    });

    QAction *chosen = menu.exec(globalPos);
    if (chosen == editAction) {
        emit editRequested(rootId, /*isFolder=*/true);
    } else if (chosen == toggleHiddenAction) {
        emit toggleFolderHiddenRequested(rootId);
    } else if (chosen == deleteAction) {
        emit deleteRequested(rootId, /*isFolder=*/true);
    } else if (syncToFileAction && chosen == syncToFileAction) {
        emit projectSyncRequested(rootId, true);
    } else if (syncFromFileAction && chosen == syncFromFileAction) {
        emit projectSyncRequested(rootId, false);
    }
}

bool CommandTreeWidget::isProjectFolder(const QString &folderId) const
{
    for (const core::Folder &f : m_folders) {
        if (f.id == folderId) {
            return f.isProject;
        }
    }
    return false;
}

void CommandTreeWidget::addCreationActions(
    QMenu *menu,
    const std::function<QAction *(QMenu *, const QString &, const QColor &, const QString &)> &addIconAction,
    const QColor &accent)
{
    // Inclusão direta de Pasta/Comando/Coleção; o MainWindow resolve a pasta destino pela seleção/aba.
    QAction *newFolder = addIconAction(menu, QStringLiteral("folder-plus"), accent,
                                       utils::tr(QStringLiteral("ctx.new.folder")));
    QAction *newCommand = addIconAction(menu, QStringLiteral("square-terminal"), accent,
                                        utils::tr(QStringLiteral("ctx.new.command")));
    QAction *newCollection = addIconAction(menu, QStringLiteral("database"), accent,
                                           utils::tr(QStringLiteral("ctx.new.collection")));
    QAction *newNote = addIconAction(menu, QStringLiteral("notebook"), accent,
                                     utils::tr(QStringLiteral("ctx.new.note")));
    newNote->setData(QStringLiteral("new-note"));
    newNote->setProperty("kaiCreation", true);
    newFolder->setData(QStringLiteral("new-folder"));
    newCommand->setData(QStringLiteral("new-command"));
    newCollection->setData(QStringLiteral("new-collection"));
    // Distingue estas ações no roteador de escolha de criação.
    newFolder->setProperty("kaiCreation", true);
    newCommand->setProperty("kaiCreation", true);
    newCollection->setProperty("kaiCreation", true);
}

void CommandTreeWidget::handleCreationChoice(QAction *chosen)
{
    if (!chosen) {
        return;
    }
    const QString kind = chosen->data().toString();
    if (kind == QStringLiteral("new-folder")) emit newFolderRequested();
    else if (kind == QStringLiteral("new-command")) emit newCommandRequested();
    else if (kind == QStringLiteral("new-collection")) emit newCollectionRequested();
    else if (kind == QStringLiteral("new-note")) emit newNoteRequested();
}

} // namespace kai::ui
