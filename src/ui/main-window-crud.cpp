

#include "ui/main-window.h"

#include "core/folder-nesting.h"

#include "ui/shared/dialog-utils.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include "ui/features/collections/folder-editor-dialog.h"
#include "ui/features/collections/folder-delete-dialog.h"
#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/collections/collection-editor-dialog.h"
#include "ui/shared/name-uniqueness.h"

#include "ui/features/command-editor/command-json-editor-dialog.h"

#include "core/http-resolve.h"
#include "ui/features/http/quick-folder-http-edit-dialog.h"
#include "ui/features/http/quick-http-edit-dialog.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

void MainWindow::handleTreeStructureChanged(const QVector<TreeNodePlacement> &placements)
{
    // Persiste ordem + reparenting do drag-and-drop: comandos atualizam
    // folderId (pasta OU outro comando); pastas atualizam parentId; todos o `order`.
    bool changed = false;
    bool collectionsChanged = false;
    bool notesChanged = false;
    for (const TreeNodePlacement &p : placements) {
        if (p.isCommand) {
            for (core::Command &command : m_commandsData.commands) {
                if (command.id == p.id) {
                    if (command.order != p.order || command.folderId != p.parentId) {
                        command.order = p.order;
                        command.folderId = p.parentId;
                        changed = true;
                    }
                    break;
                }
            }
        } else if (p.isNote) {
            for (core::Note &note : m_notes) {
                if (note.id == p.id) {
                    if (note.order != p.order || note.folderId != p.parentId) {
                        note.order = p.order;
                        note.folderId = p.parentId;
                        notesChanged = true;
                    }
                    break;
                }
            }
        } else if (p.isCollection) {
            // Coleções guardam order + folderId, como comandos.
            for (core::Collection &collection : m_collections) {
                if (collection.id == p.id) {
                    if (collection.order != p.order || collection.folderId != p.parentId) {
                        collection.order = p.order;
                        collection.folderId = p.parentId;
                        collectionsChanged = true;
                    }
                    break;
                }
            }
        } else {
            for (core::Folder &folder : m_commandsData.folders) {
                if (folder.id == p.id) {
                    const std::optional<QString> newParent =
                        p.parentId.isEmpty() ? std::nullopt : std::make_optional(p.parentId);
                    if (folder.order != p.order || folder.parentId != newParent) {
                        folder.order = p.order;
                        folder.parentId = newParent;
                        changed = true;
                    }
                    break;
                }
            }
        }
    }

    if (notesChanged) {
        if (changed || collectionsChanged) {
            if (changed && !m_configManager.saveCommands(m_commandsData)) {
                QMessageBox::warning(this, QStringLiteral("Kai"), utils::tr(QStringLiteral("mainwindow.error.save_commands")));
            }
            if (collectionsChanged) {
                m_configManager.saveCollections(m_collections);
            }
        }
        persistNotes(); // salva as notas e reconstrói a árvore
        return;
    }
    if (changed || collectionsChanged) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Drag&drop persistido: %1 item(ns) processados (comandos/pastas: %2, coleções: %3).")
                .arg(placements.size())
                .arg(changed ? QStringLiteral("sim") : QStringLiteral("nao"))
                .arg(collectionsChanged ? QStringLiteral("sim") : QStringLiteral("nao")));
        // Coleções vivem em collections.json; comandos/pastas em commands.json.
        // Ambos disparam reloadCommandTree, então o caso duplo salva um à mão e deixa o outro recarregar.
        if (changed && collectionsChanged) {
            if (!m_configManager.saveCommands(m_commandsData)) {
                QMessageBox::warning(this, QStringLiteral("Kai"),
                    utils::tr(QStringLiteral("mainwindow.error.save_commands")));
            }
            persistCollections(); // salva coleções + reloadCommandTree
        } else if (changed) {
            persistCommands();
        } else {
            persistCollections();
        }
    } else {
        utils::Logger::debug(kLogTag,
            QStringLiteral("Drag&drop: nenhuma mudança efetiva de order/hierarquia (nada a persistir)."));
    }
}

void MainWindow::persistCommands()
{
    // Ações de pasta que apontam pra um comando excluído saem junto.
    {
        QSet<QString> commandIds;
        for (const core::Command &c : m_commandsData.commands) {
            commandIds.insert(c.id);
        }
        for (core::Folder &f : m_commandsData.folders) {
            f.actions.removeIf([&commandIds](const QString &id) { return !commandIds.contains(id); });
            f.expansionActions.removeIf([&f](const QString &id) { return !f.actions.contains(id); });
            f.actionGroups.removeIf([&f](const auto &entry) { return !f.actions.contains(entry.key()); });
        }
    }
    if (!m_configManager.saveCommands(m_commandsData)) {
        QMessageBox::warning(this, QStringLiteral("Kai"),
            utils::tr(QStringLiteral("mainwindow.error.persist_commands")));
    }
    reloadCommandTree();
    scheduleSyncDriftCheck();
}

QVector<core::Command> MainWindow::commandsInFolder(const QString &folderId) const
{
    // Hooks elegíveis: comandos da PRÓPRIA pasta MAIS os das ANCESTRAIS (pai,
    // avô...), subindo por parentId.
    QSet<QString> chainFolderIds;
    chainFolderIds.insert(folderId);
    QMap<QString, core::Folder> foldersById;
    for (const core::Folder &f : m_commandsData.folders) {
        foldersById.insert(f.id, f);
    }
    QString current = folderId;
    QSet<QString> guard;
    while (foldersById.contains(current) && !guard.contains(current)) {
        guard.insert(current);
        const core::Folder &f = foldersById.value(current);
        if (!f.parentId.has_value() || f.parentId->isEmpty()) {
            break;
        }
        current = f.parentId.value();
        chainFolderIds.insert(current);
    }

    QVector<core::Command> result;
    for (const core::Command &command : m_commandsData.commands) {
        if (chainFolderIds.contains(command.folderId)) {
            result << command;
        }
    }
    return result;
}

bool MainWindow::hasDuplicateCommandName(const QString &folderId, const QString &name, const QString &excludeId) const
{
    return commandNameCollides(m_commandsData.commands, folderId, name, excludeId);
}

bool MainWindow::hasDuplicateCollectionName(const QString &folderId, const QString &name, const QString &excludeId) const
{
    return collectionNameCollides(m_collections, folderId, name, excludeId);
}

QString MainWindow::resolveTargetFolderId() const
{
    // Prioriza a seleção na árvore (pasta, ou a pasta do comando/nota); sem
    // seleção, a raiz da aba ativa; sem nenhuma aba, vazio.
    const QString selectedId = m_commandTree->currentSelectionId();
    if (!selectedId.isEmpty()) {
        if (m_commandTree->currentSelectionIsFolder()) {
            return selectedId;
        }
        for (const core::Note &note : m_notes) {
            if (note.id == selectedId) {
                return note.folderId; // uma nota selecionada: a pasta dela
            }
        }
        const auto it = m_commandsById.constFind(selectedId);
        if (it != m_commandsById.constEnd()) {
            return it.value().folderId;
        }
    }

    const QString activeRootId = m_commandTree->currentRootFolderId();
    if (!activeRootId.isEmpty()) {
        return activeRootId;
    }

    if (!m_commandsData.folders.isEmpty()) {
        return m_commandsData.folders.constFirst().id;
    }
    return QString();
}

QString MainWindow::uniqueFolderId(const QString &candidate) const
{
    auto idExists = [this](const QString &id) {
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.id == id) return true;
        }
        return false;
    };
    if (!idExists(candidate)) {
        return candidate;
    }
    int n = 2;
    QString unique = QStringLiteral("%1_%2").arg(candidate).arg(n);
    while (idExists(unique)) {
        ++n;
        unique = QStringLiteral("%1_%2").arg(candidate).arg(n);
    }
    utils::Logger::warning(kLogTag,
        QStringLiteral("ID de pasta '%1' já existe; usando '%2' para evitar colisão.")
            .arg(candidate, unique));
    return unique;
}

QVector<QString> MainWindow::folderSubtreeIds(const QString &rootFolderId) const
{
    QVector<QString> ids = {rootFolderId};
    QVector<QString> queue = {rootFolderId};
    while (!queue.isEmpty()) {
        const QString currentId = queue.takeFirst();
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.parentId.has_value() && f.parentId.value() == currentId && !ids.contains(f.id)) {
                ids << f.id;
                queue << f.id;
            }
        }
    }
    return ids;
}

QVector<core::Collection> MainWindow::linkedCollectionsForFolder(const QString &folderId) const
{
    return core::ConfigManager::linkedCollectionsForFolder(folderId, m_commandsData, m_collections);
}

QVector<core::Collection> MainWindow::linkedCollectionsForCommand(const QString &commandId) const
{
    QSet<QString> collectionIds;
    QVector<core::Collection> result;
    auto addIfNew = [&](const core::Collection &col) {
        if (!collectionIds.contains(col.id)) {
            collectionIds.insert(col.id);
            result.append(col);
        }
    };
    const auto cmdIt = m_commandsById.constFind(commandId);
    if (cmdIt == m_commandsById.constEnd()) {
        return result;
    }
    // (1) Guardada na MESMA pasta direta do comando (comando não tem subárvore).
    for (const core::Collection &col : m_collections) {
        if (col.folderId == cmdIt->folderId) {
            addIfNew(col);
        }
    }
    // (2) Referenciada por um parâmetro Select do próprio comando.
    for (const core::Parameter &p : cmdIt->params) {
        if (p.collectionId.isEmpty()) {
            continue;
        }
        const auto it = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
            [&p](const core::Collection &col) { return col.id == p.collectionId; });
        if (it != m_collections.constEnd()) {
            addIfNew(*it);
        }
    }
    return result;
}

// Acrescenta a pasta nova (com `a/b/c` virando a > b > c quando `nested`) e devolve o id da pasta final.
QString MainWindow::commitNewFolder(core::Folder folder, bool nested)
{
    QString leafId;
    if (nested) {
        const core::NestedFolderPlan plan = core::planNestedFolders(folder, m_commandsData.folders);
        if (plan.created.isEmpty()) {
            return QString();
        }
        m_commandsData.folders << plan.created;
        leafId = plan.leafId;
    } else {
        folder.id = uniqueFolderId(folder.id);
        leafId = folder.id;
        m_commandsData.folders << folder;
    }
    persistCommands();
    return leafId;
}

void MainWindow::handleNewFolderRequested()
{
    // Sugere a pasta pai pela seleção/aba atual; o usuário pode trocar antes de salvar.
    const QString suggestedParentId = resolveTargetFolderId();
    FolderEditorDialog dialog(m_commandsData.folders, this, nullptr, suggestedParentId,
                              m_configManager.loadSettings().terminalProfiles, m_commandsData.commands);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    core::Folder folder = dialog.buildFolder();
    if (folder.name.isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("folder.title.new")), utils::tr(QStringLiteral("mainwindow.folder.name_required")));
        return;
    }
    commitNewFolder(folder, dialog.nestedFoldersRequested());
}

void MainWindow::handleNewCommandRequested()
{
    QString targetFolderId = resolveTargetFolderId();

    if (targetFolderId.isEmpty()) {
        // Em vez de só informar, oferece criar a pasta necessária no mesmo fluxo.
        if (!confirmYesNo(this, utils::tr(QStringLiteral("mainwindow.no_folder.title")),
                          utils::tr(QStringLiteral("mainwindow.no_folder.body")), /*defaultToYes=*/true)) {
            return;
        }

        FolderEditorDialog folderDialog(m_commandsData.folders, this, nullptr, QString(),
                                        m_configManager.loadSettings().terminalProfiles, m_commandsData.commands);
        if (folderDialog.exec() != QDialog::Accepted) {
            return;
        }

        core::Folder newFolder = folderDialog.buildFolder();
        if (newFolder.name.isEmpty()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("folder.title.new")), utils::tr(QStringLiteral("mainwindow.folder.name_required")));
            return;
        }
        targetFolderId = commitNewFolder(newFolder, folderDialog.nestedFoldersRequested());
    }

    // Modo simples (formulário) e avançado (JSON cru) alternam sem validar nem
    // salvar: o laço reabre o editor no modo pedido com o comando parcial.
    const core::SettingsData settings = m_configManager.loadSettings();
    core::Command command;
    bool advanced = (settings.commandCreationMode == QStringLiteral("advanced"));
    bool hasDraft = false;      // há rascunho para transportar entre modos
    core::Command draft;        // rascunho preservado no swap
    bool committed = false;     // usuário confirmou (OK), não só trocou de modo

    while (true) {
        if (advanced) {
            CommandJsonEditorDialog dialog(hasDraft ? &draft : nullptr, targetFolderId, this);
            dialog.setAvailableCommands(m_commandsData.commands);
            if (dialog.exec() != QDialog::Accepted) {
                return; // cancelou
            }
            if (dialog.assistApplied()) {
                // Proposta do assistente de IA: volta ao editor como rascunho, sem salvar nada.
                draft = dialog.assistedCommand();
                draft.id.clear();
                hasDraft = true;
                continue;
            }
            draft = dialog.buildCommand();
            hasDraft = true;
            // Nome duplicado na mesma pasta: reabre o editor com o rascunho, no mesmo modo, p/ renomear.
            if (hasDuplicateCommandName(draft.folderId, draft.name, QString())) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.duplicate_name.title")),
                    utils::tr(QStringLiteral("command.error.duplicate_name.body")));
                continue;
            }
            if (dialog.switchToSimpleRequested()) {
                advanced = false;
                continue; // reabre no formulário com o rascunho
            }
            command = draft;
            committed = true;
            break;
        } else {
            CommandEditorDialog dialog(targetFolderId, commandsInFolder(targetFolderId),
                                       m_commandsData.folders, this,
                                       hasDraft ? &draft : nullptr, settings.terminalProfiles,
                                       m_commandsData.commands);
            dialog.setAvailableCollections(m_collections);
            dialog.setAvailableDynamicVarNames(availableDynamicVarNames());
            if (dialog.exec() != QDialog::Accepted) {
                return; // cancelou
            }
            if (dialog.assistApplied()) {
                draft = dialog.assistedCommand();
                draft.id.clear();
                hasDraft = true;
                continue;
            }
            draft = dialog.buildCommand();
            hasDraft = true;
            if (hasDuplicateCommandName(draft.folderId, draft.name, QString())) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.duplicate_name.title")),
                    utils::tr(QStringLiteral("command.error.duplicate_name.body")));
                continue;
            }
            if (dialog.switchToAdvancedRequested()) {
                advanced = true;
                continue; // reabre no JSON com o rascunho (sem validar/salvar)
            }
            command = draft;
            committed = true;
            break;
        }
    }

    if (!committed) {
        return;
    }

    if (command.name.isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("command.title.new")), utils::tr(QStringLiteral("mainwindow.command.name_required")));
        return;
    }

    // ID ÚNICO: dois comandos com o mesmo nome na mesma pasta geravam o mesmo
    // id (colisão em m_commandsById, hooks sumiam); anexa sufixo numérico se já existe.
    {
        auto idExists = [this](const QString &candidate) {
            for (const core::Command &c : m_commandsData.commands) {
                if (c.id == candidate) return true;
            }
            return false;
        };
        if (idExists(command.id)) {
            const QString base = command.id;
            int n = 2;
            QString unique = QStringLiteral("%1_%2").arg(base).arg(n);
            while (idExists(unique)) {
                ++n;
                unique = QStringLiteral("%1_%2").arg(base).arg(n);
            }
            utils::Logger::warning(kLogTag,
                QStringLiteral("ID de comando '%1' já existe; usando '%2' para evitar colisão.")
                    .arg(base, unique));
            command.id = unique;
        }
    }

    m_commandsData.commands << command;
    persistCommands();
}

void MainWindow::handleEditRequested(const QString &itemId, bool isFolder)
{
    if (isFolder) {
        // Pasta PADRÃO "Geral" (sintética): editá-la = oferecer a REMOÇÃO dos
        // órfãos que ela contém, via fluxo de exclusão.
        if (itemId == CommandTreeWidget::defaultFolderId()) {
            handleDeleteRequested(itemId, true);
            return;
        }
        int folderIndex = -1;
        for (int i = 0; i < m_commandsData.folders.size(); ++i) {
            if (m_commandsData.folders.at(i).id == itemId) {
                folderIndex = i;
                break;
            }
        }
        if (folderIndex < 0) {
            return;
        }

        const core::Folder existing = m_commandsData.folders.at(folderIndex);
        FolderEditorDialog dialog(m_commandsData.folders, this, &existing, QString(),
                                  m_configManager.loadSettings().terminalProfiles, m_commandsData.commands);
        const int rc = dialog.exec();

        // Botão "Excluir Pasta" do diálogo: mesmo fluxo de flags do menu contextual.
        if (dialog.deleteWasRequested()) {
            deleteFolderWithDialog(itemId);
            persistCommands();
            return;
        }

        if (rc != QDialog::Accepted) {
            return;
        }

        m_commandsData.folders[folderIndex] = dialog.buildFolder();
        persistCommands();
        return;
    }

    int commandIndex = -1;
    for (int i = 0; i < m_commandsData.commands.size(); ++i) {
        if (m_commandsData.commands.at(i).id == itemId) {
            commandIndex = i;
            break;
        }
    }
    if (commandIndex < 0) {
        return;
    }

    const core::Command existing = m_commandsData.commands.at(commandIndex);

    // "advanced" abre o editor de JSON cru, "standard" o formulário. Cada
    // editor, ao pedir troca, fecha com Accepted levando o Command atual; o
    // laço reabre no outro modo até OK sem troca ou cancelar.
    const core::SettingsData settings = m_configManager.loadSettings();
    core::Command working = existing;
    bool advanced = (settings.commandEditMode == QStringLiteral("advanced"));
    core::Command edited;

    while (true) {
        if (advanced) {
            CommandJsonEditorDialog dialog(&working, working.folderId, this);
            dialog.setAvailableCommands(m_commandsData.commands);
            if (dialog.exec() != QDialog::Accepted) {
                return; // cancelou
            }
            working = dialog.buildCommand();
            if (dialog.assistApplied()) {
                continue; // proposta do assistente de IA: reabre com ela como rascunho
            }
            if (hasDuplicateCommandName(working.folderId, working.name, existing.id)) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.duplicate_name.title")),
                    utils::tr(QStringLiteral("command.error.duplicate_name.body")));
                continue;
            }
            if (dialog.switchToSimpleRequested()) {
                advanced = false;
                continue; // reabre no modo simples com o conteúdo atual
            }
            edited = working;
            break;
        } else {
            CommandEditorDialog dialog(working.folderId, commandsInFolder(working.folderId),
                                       m_commandsData.folders, this, &working, settings.terminalProfiles,
                                       m_commandsData.commands);
            dialog.setAvailableCollections(m_collections);
            dialog.setAvailableDynamicVarNames(availableDynamicVarNames());
            if (dialog.exec() != QDialog::Accepted) {
                return; // cancelou
            }
            if (dialog.assistApplied()) {
                working = dialog.assistedCommand();
                continue; // proposta do assistente de IA: reabre com ela como rascunho
            }
            working = dialog.buildCommand();
            if (hasDuplicateCommandName(working.folderId, working.name, existing.id)) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("command.error.duplicate_name.title")),
                    utils::tr(QStringLiteral("command.error.duplicate_name.body")));
                continue;
            }
            if (dialog.switchToAdvancedRequested()) {
                advanced = true;
                continue; // reabre no modo avançado com o conteúdo atual
            }
            edited = working;
            break;
        }
    }

    m_commandsData.commands[commandIndex] = edited;
    persistCommands();
}

void MainWindow::handleDeleteRequested(const QString &itemId, bool isFolder)
{
    // A pasta PADRÃO "Geral" é sintética (não está em m_commandsData.folders):
    // "excluir" remove os ÓRFÃOS que ela hospeda, com confirmação própria.
    if (isFolder && itemId == CommandTreeWidget::defaultFolderId()) {
        QSet<QString> folderIds;
        for (const core::Folder &f : m_commandsData.folders) {
            folderIds.insert(f.id);
        }
        auto resolves = [this, &folderIds](const QString &parentId) -> bool {
            QString cur = parentId;
            QSet<QString> visited;
            for (int guard = 0; guard < 128; ++guard) {
                if (cur.isEmpty() || visited.contains(cur)) return false;
                visited.insert(cur);
                if (folderIds.contains(cur)) return true;
                const auto it = std::find_if(m_commandsData.commands.constBegin(),
                    m_commandsData.commands.constEnd(),
                    [&cur](const core::Command &c) { return c.id == cur; });
                if (it == m_commandsData.commands.constEnd()) return false;
                cur = it->folderId;
            }
            return false;
        };
        int orphanCmds = 0, orphanCols = 0;
        for (const core::Command &c : m_commandsData.commands) {
            if (!resolves(c.folderId)) ++orphanCmds;
        }
        for (const core::Collection &col : m_collections) {
            if (!folderIds.contains(col.folderId)) ++orphanCols;
        }
        if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                          utils::tr(QStringLiteral("mainwindow.delete_general_folder.confirm"))
                              .arg(orphanCmds).arg(orphanCols))) {
            return;
        }
        m_commandsData.commands.removeIf([&resolves](const core::Command &c) {
            return !resolves(c.folderId);
        });
        bool colsChanged = false;
        m_collections.removeIf([&folderIds, &colsChanged](const core::Collection &col) {
            const bool orphan = !folderIds.contains(col.folderId);
            if (orphan) colsChanged = true;
            return orphan;
        });
        if (colsChanged) {
            persistCollections();
        }
        persistCommands();
        return;
    }

    if (!isFolder) {
        if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                          utils::tr(QStringLiteral("delete.confirm.command")))) {
            return;
        }
        m_commandsData.commands.removeIf([&itemId](const core::Command &c) { return c.id == itemId; });
        persistCommands();
        return;
    }

    // Exclusão de PASTA: o diálogo decide se apaga os filhos e quais TIPOS; o
    // que não for marcado é REPARENTADO para o avô.
    deleteFolderWithDialog(itemId);
    persistCommands();
}

void MainWindow::deleteFolderWithDialog(const QString &folderId)
{
    QVector<QString> descendantFolders;
    {
        QVector<QString> queue = {folderId};
        while (!queue.isEmpty()) {
            const QString currentId = queue.takeFirst();
            for (const core::Folder &folder : m_commandsData.folders) {
                if (folder.parentId.value_or(QString()) == currentId) {
                    descendantFolders << folder.id;
                    queue << folder.id;
                }
            }
        }
    }
    // Conjunto = a própria pasta + descendentes (conta em QUALQUER nível).
    QSet<QString> subtree;
    subtree.insert(folderId);
    for (const QString &id : descendantFolders) {
        subtree.insert(id);
    }

    FolderDeleteDialog::Counts counts;
    counts.folders = descendantFolders.size();
    for (const core::Command &c : m_commandsData.commands) {
        if (subtree.contains(c.folderId)) ++counts.commands;
    }
    for (const core::Collection &col : m_collections) {
        if (subtree.contains(col.folderId)) ++counts.collections;
    }

    QString folderName = folderId;
    QString grandparentId;
    for (const core::Folder &f : m_commandsData.folders) {
        if (f.id == folderId) {
            folderName = f.name;
            grandparentId = f.parentId.value_or(QString());
            break;
        }
    }

    FolderDeleteDialog dialog(folderName, counts, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const FolderDeleteDialog::Result choice = dialog.result();

    // Filhos DIRETOS da pasta removida; o que não for apagado vai para o avô.
    auto reparentDirectChildrenToGrandparent = [&]() {
        for (core::Folder &f : m_commandsData.folders) {
            if (f.parentId.value_or(QString()) == folderId) {
                f.parentId = grandparentId.isEmpty() ? std::nullopt
                                                      : std::make_optional(grandparentId);
            }
        }
        for (core::Command &c : m_commandsData.commands) {
            if (c.folderId == folderId) {
                c.folderId = grandparentId;
            }
        }
        bool colsChanged = false;
        for (core::Collection &col : m_collections) {
            if (col.folderId == folderId) {
                col.folderId = grandparentId;
                colsChanged = true;
            }
        }
        if (colsChanged) persistCollections();
    };

    if (!choice.deleteChildren) {
        // Só a pasta: reparenta os filhos diretos para o avô e remove a pasta.
        reparentDirectChildrenToGrandparent();
        m_commandsData.folders.removeIf([&folderId](const core::Folder &f) { return f.id == folderId; });
        return;
    }

    // Apagar filhos por tipo; o que NÃO for marcado é reparentado para o avô
    // (subpastas não apagadas levam a própria subárvore).
    bool colsChanged = false;

    if (choice.deleteCollections) {
        m_collections.removeIf([&](const core::Collection &col) {
            const bool hit = subtree.contains(col.folderId);
            if (hit) colsChanged = true;
            return hit;
        });
    } else {
        for (core::Collection &col : m_collections) {
            if (col.folderId == folderId) { col.folderId = grandparentId; colsChanged = true; }
        }
    }

    if (choice.deleteCommands) {
        m_commandsData.commands.removeIf([&](const core::Command &c) {
            return subtree.contains(c.folderId);
        });
    } else {
        for (core::Command &c : m_commandsData.commands) {
            if (c.folderId == folderId) c.folderId = grandparentId;
        }
    }

    if (choice.deleteFolders) {
        // Comandos/coleções que sobraram e apontam para subpasta apagada vão para o avô.
        const QSet<QString> removedSubs(descendantFolders.begin(), descendantFolders.end());
        for (core::Command &c : m_commandsData.commands) {
            if (removedSubs.contains(c.folderId)) c.folderId = grandparentId;
        }
        for (core::Collection &col : m_collections) {
            if (removedSubs.contains(col.folderId)) { col.folderId = grandparentId; colsChanged = true; }
        }
        m_commandsData.folders.removeIf([&removedSubs](const core::Folder &f) {
            return removedSubs.contains(f.id);
        });
    } else {
        // Reparenta as subpastas DIRETAS para o avô (as mais fundas seguem as diretas).
        for (core::Folder &f : m_commandsData.folders) {
            if (f.parentId.value_or(QString()) == folderId) {
                f.parentId = grandparentId.isEmpty() ? std::nullopt
                                                      : std::make_optional(grandparentId);
            }
        }
    }

    m_commandsData.folders.removeIf([&folderId](const core::Folder &f) { return f.id == folderId; });

    if (colsChanged) persistCollections();
}

void MainWindow::deleteFolderRecursively(const QString &folderId)
{
    // Remove a subárvore de pastas (a raiz + descendentes) e seus comandos.
    // NÃO persiste nem confirma: o chamador decide.
    QVector<QString> foldersToRemove = {folderId};
    QVector<QString> queue = {folderId};
    while (!queue.isEmpty()) {
        const QString currentId = queue.takeFirst();
        for (const core::Folder &folder : m_commandsData.folders) {
            if (folder.parentId.has_value() && folder.parentId.value() == currentId) {
                foldersToRemove << folder.id;
                queue << folder.id;
            }
        }
    }

    m_commandsData.folders.removeIf([&foldersToRemove](const core::Folder &f) {
        return foldersToRemove.contains(f.id);
    });
    m_commandsData.commands.removeIf([&foldersToRemove](const core::Command &c) {
        return foldersToRemove.contains(c.folderId);
    });
}

void MainWindow::handleDuplicateRequested(const QString &itemId, bool isFolder)
{
    // Id único a partir de uma base, sufixando _copy/_copyN (comandos e pastas
    // compartilham o espaço de ids).
    auto idExists = [this](const QString &id) {
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == id) return true;
        }
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.id == id) return true;
        }
        return false;
    };
    auto uniqueId = [&idExists](const QString &base) {
        QString candidate = base + QStringLiteral("_copy");
        int n = 2;
        while (idExists(candidate)) {
            candidate = base + QStringLiteral("_copy%1").arg(n++);
        }
        return candidate;
    };

    if (!isFolder) {
        const auto it = std::find_if(m_commandsData.commands.constBegin(), m_commandsData.commands.constEnd(),
            [&itemId](const core::Command &c) { return c.id == itemId; });
        if (it == m_commandsData.commands.constEnd()) {
            return;
        }
        core::Command copy = *it;
        copy.id = uniqueId(it->id);
        copy.name = it->name + utils::tr(QStringLiteral("duplicate.name_suffix"));
        copy.order = -1; // sem ordem manual: vai para o fim
        m_commandsData.commands << copy;
        persistCommands();
        return;
    }

    // Duplicação de PASTA: copia a subárvore (pastas + comandos) com novos ids
    // e remapeia parentId/folderId.
    const auto folderIt = std::find_if(m_commandsData.folders.constBegin(), m_commandsData.folders.constEnd(),
        [&itemId](const core::Folder &f) { return f.id == itemId; });
    if (folderIt == m_commandsData.folders.constEnd()) {
        return;
    }

    QVector<QString> subtreeFolderIds = {itemId};
    QVector<QString> queue = {itemId};
    while (!queue.isEmpty()) {
        const QString currentId = queue.takeFirst();
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.parentId.has_value() && f.parentId.value() == currentId) {
                subtreeFolderIds << f.id;
                queue << f.id;
            }
        }
    }

    QMap<QString, QString> idMap;
    for (const QString &oldId : subtreeFolderIds) {
        idMap.insert(oldId, uniqueId(oldId));
    }

    QVector<core::Folder> newFolders;
    for (const core::Folder &f : m_commandsData.folders) {
        if (!subtreeFolderIds.contains(f.id)) {
            continue;
        }
        core::Folder copy = f;
        copy.id = idMap.value(f.id);
        // A raiz duplicada mantém o pai e ganha sufixo no nome; subpastas remapeiam o pai.
        if (f.id == itemId) {
            copy.name = f.name + utils::tr(QStringLiteral("duplicate.name_suffix"));
            copy.order = -1;
        } else if (f.parentId.has_value()) {
            copy.parentId = idMap.value(f.parentId.value(), f.parentId.value());
        }
        newFolders << copy;
    }

    QVector<core::Command> newCommands;
    for (const core::Command &c : m_commandsData.commands) {
        if (!subtreeFolderIds.contains(c.folderId)) {
            continue;
        }
        core::Command copy = c;
        copy.id = uniqueId(c.id);
        copy.folderId = idMap.value(c.folderId, c.folderId);
        newCommands << copy;
    }

    m_commandsData.folders += newFolders;
    m_commandsData.commands += newCommands;
    persistCommands();
}

void MainWindow::handleNewCollectionRequested()
{
    // Coleção nova (schema key/value) na pasta/aba atual; só persiste se o usuário confirmar.
    const QString folderId = resolveTargetFolderId();
    if (folderId.isEmpty()) {
        QMessageBox::information(this, utils::tr(QStringLiteral("sidebar.new_collection")),
            utils::tr(QStringLiteral("mainwindow.collection.need_folder")));
        return;
    }

    core::Collection col;
    col.id = QStringLiteral("col_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    col.folderId = folderId;
    col.name = utils::tr(QStringLiteral("sidebar.new_collection"));
    col.icon = QStringLiteral("database");
    col.schema = core::Collection::defaultSchema();

    CollectionEditorDialog dialog(col, m_commandsData.folders, this);
    core::Collection built;
    while (true) {
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        built = dialog.buildCollection();
        // Nome duplicado na mesma pasta: reabre o diálogo preenchido p/ renomear.
        if (hasDuplicateCollectionName(built.folderId, built.name, QString())) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("collection.error.duplicate_name.title")),
                utils::tr(QStringLiteral("collection.error.duplicate_name.body")));
            continue;
        }
        break;
    }
    m_collections << built;
    persistCollections();
}

void MainWindow::persistCollections()
{
    // Uma coleção nova (ou renomeada) pode ser a que algum parâmetro de um arquivo de projeto aguardava.
    if (core::bindPendingCollectionReferences(m_commandsData.commands, m_collections) > 0) {
        m_configManager.saveCommands(m_commandsData);
    }
    if (!m_configManager.saveCollections(m_collections)) {
        QMessageBox::warning(this, QStringLiteral("Kai"),
            utils::tr(QStringLiteral("mainwindow.error.persist_collections")));
    }
    reloadCommandTree();
}

void MainWindow::handleCollectionEditRequested(const QString &collectionId)
{
    int index = -1;
    for (int i = 0; i < m_collections.size(); ++i) {
        if (m_collections.at(i).id == collectionId) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return;
    }
    const QString existingId = m_collections.at(index).id;
    CollectionEditorDialog dialog(m_collections.at(index), m_commandsData.folders, this);
    core::Collection built;
    while (true) {
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        built = dialog.buildCollection();
        if (hasDuplicateCollectionName(built.folderId, built.name, existingId)) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("collection.error.duplicate_name.title")),
                utils::tr(QStringLiteral("collection.error.duplicate_name.body")));
            continue;
        }
        break;
    }
    m_collections[index] = built;
    persistCollections();
}

void MainWindow::handleCollectionDuplicateRequested(const QString &collectionId)
{
    const auto it = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
        [&collectionId](const core::Collection &c) { return c.id == collectionId; });
    if (it == m_collections.constEnd()) {
        return;
    }
    auto idExists = [this](const QString &id) {
        for (const core::Collection &c : m_collections) {
            if (c.id == id) return true;
        }
        return false;
    };
    QString newId = it->id + QStringLiteral("_copy");
    int n = 2;
    while (idExists(newId)) {
        newId = it->id + QStringLiteral("_copy%1").arg(n++);
    }
    core::Collection copy = *it;
    copy.id = newId;
    copy.name = it->name + utils::tr(QStringLiteral("duplicate.name_suffix"));
    copy.order = -1;
    m_collections << copy;
    persistCollections();
}

void MainWindow::handleCollectionDeleteRequested(const QString &collectionId)
{
    if (!confirmYesNo(this, utils::tr(QStringLiteral("delete.confirm.title")),
                      utils::tr(QStringLiteral("delete.confirm.collection")))) {
        return;
    }
    m_collections.removeIf([&collectionId](const core::Collection &c) { return c.id == collectionId; });
    persistCollections();
}

void MainWindow::handleQuickEditBodyRequested(const QString &commandId)
{
    int commandIndex = -1;
    for (int i = 0; i < m_commandsData.commands.size(); ++i) {
        if (m_commandsData.commands.at(i).id == commandId) {
            commandIndex = i;
            break;
        }
    }
    if (commandIndex < 0) {
        return;
    }

    core::Command &command = m_commandsData.commands[commandIndex];
    if (command.type != core::CommandType::Http || !command.httpConfig.has_value()) {
        return;
    }

    QuickHttpEditDialog dialog(command.name, *command.httpConfig, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    command.httpConfig = dialog.config();
    persistCommands();
}

void MainWindow::handleQuickEditFolderRequested(const QString &folderId)
{
    if (!core::folderHasHttpCommand(folderId, m_commandsData.folders, m_commandsData.commands)) {
        return;
    }
    for (core::Folder &folder : m_commandsData.folders) {
        if (folder.id != folderId) {
            continue;
        }
        QuickFolderHttpEditDialog dialog(folder.name, folder.httpDefaults, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        folder.httpDefaults = dialog.defaults();
        persistCommands();
        return;
    }
}

} // namespace kai::ui
