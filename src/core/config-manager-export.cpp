
#include "core/field-scopes.h"

#include <QJsonParseError>

#include "core/config-manager-internal.h"
#include "core/config-id-free.h"

namespace kai::core {

using namespace detail;

namespace {
// Remove os valores de campos "secret" das entries de UMA coleção; usado em
// TODO caminho de export que inclua entries. Ver CollectionField::secret.
Collection redactSecretFieldsForExport(Collection col)
{
    QStringList secretFieldNames;
    for (const CollectionField &field : col.schema) {
        if (field.secret) {
            secretFieldNames << field.name;
        }
    }
    if (secretFieldNames.isEmpty()) {
        return col;
    }
    for (CollectionEntry &entry : col.entries) {
        for (const QString &fieldName : secretFieldNames) {
            entry.values.remove(fieldName);
        }
    }
    return col;
}

// O que é ESTADO LOCAL (últimos valores digitados, histórico de uso dos parâmetros, caminho de origem
// de uma coleção) não vai para arquivo nenhum: exportar, sincronizar. Continua no armazenamento do app.
// Quais campos são locais vem da tabela de escopos (core/field-scopes), não de uma lista aqui.
QJsonObject commandToExportJson(const Command &command)
{
    QJsonObject obj = command.toJson();
    for (const QString &key : keysWithScope(FieldOwner::Command, FieldScope::Local)) {
        obj.remove(key);
    }
    return obj;
}

// Pasta como vai pra um arquivo: o valor das variáveis SECRETAS nunca sai (fica só o nome, em `secret_env_keys`).
QJsonObject folderToExportJson(const Folder &folder)
{
    QJsonObject obj = folder.toJson();
    if (folder.secretEnvKeys.isEmpty()) {
        return obj;
    }
    QJsonObject env = obj.value(QStringLiteral("env_vars")).toObject();
    for (const QString &key : folder.secretEnvKeys) {
        env.remove(key);
    }
    if (env.isEmpty()) {
        obj.remove(QStringLiteral("env_vars"));
    } else {
        obj[QStringLiteral("env_vars")] = env;
    }
    return obj;
}

QJsonObject collectionToExportJson(const Collection &collection)
{
    QJsonObject obj = collection.toJson();
    for (const QString &key : keysWithScope(FieldOwner::Collection, FieldScope::Local)) {
        obj.remove(key);
    }
    return obj;
}

// Notas SINCRONIZÁVEIS (não locais) das pastas em `folderIds`, no formato do
// pacote (com folder_id; o export enxuto troca por "folder" e tira o id).
QJsonArray notesForExport(const QVector<Note> &notes, const QSet<QString> &folderIds)
{
    QJsonArray array;
    for (const Note &note : notes) {
        if (!note.local && folderIds.contains(note.folderId)) {
            QJsonObject o = note.toJson();
            o.insert(QStringLiteral("local"), false);
            array.append(o);
        }
    }
    return array;
}

// Uma pasta, todas as subpastas descendentes e seus comandos (export por pasta).
void collectFolderSubtree(const QString &rootFolderId, const CommandsData &commands,
                          QVector<Folder> &outFolders, QVector<Command> &outCommands)
{
    QVector<QString> folderIds = {rootFolderId};
    QVector<QString> queue = {rootFolderId};
    while (!queue.isEmpty()) {
        const QString currentId = queue.takeFirst();
        for (const Folder &f : commands.folders) {
            if (f.parentId.has_value() && f.parentId.value() == currentId
                && !folderIds.contains(f.id)) {
                folderIds.append(f.id);
                queue.append(f.id);
            }
        }
    }
    for (const Folder &f : commands.folders) {
        if (folderIds.contains(f.id)) {
            outFolders.append(f);
        }
    }
    // Comandos de uma pasta do subtree OU de um comando já incluído
    // (agrupamento); duas passadas pegam os aninhados.
    QVector<QString> containerIds = folderIds;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const Command &c : commands.commands) {
            if (containerIds.contains(c.folderId) && !containerIds.contains(c.id)) {
                outCommands.append(c);
                containerIds.append(c.id);
                changed = true;
            }
        }
    }
}

QJsonObject exportHeader(const QString &scope)
{
    QJsonObject header;
    header["scope"] = scope;
    header["version"] = 1;
    return header;
}

// id -> nome de TODOS os comandos (não só os do pacote): as ações de uma
// pasta podem apontar pra um comando que mora fora dela.
QMap<QString, QString> commandNamesById(const CommandsData &commands)
{
    QMap<QString, QString> names;
    for (const Command &c : commands.commands) {
        names.insert(c.id, c.name);
    }
    return names;
}
}

QString ConfigManager::exportSelective(const ExportSelection &selection,
                                       const SettingsData &settings,
                                       const CommandsData &commands,
                                       const QVector<Collection> &collections,
                                       bool lean)
{
    // Mesmo formato do escopo "global" (p/ importFromJson entender), mas só
    // com as seções marcadas — e inclui COLEÇÕES, que o global não leva.
    QJsonObject root;
    root["kai_export"] = exportHeader(QStringLiteral("global"));

    if (selection.settings || selection.environments || selection.terminalProfiles || selection.globalActions) {
        // Só o que foi pedido, p/ não vazar seções não selecionadas; as três
        // seções (preferências, environments, alvos de terminal) são independentes.
        const QJsonDocument full = QJsonDocument::fromJson(
            exportGlobal(settings, commands).toUtf8());
        const QJsonObject fullSettingsObj = full.object().value("settings").toObject();
        QJsonObject settingsObj;
        if (selection.settings) {
            settingsObj = fullSettingsObj;
            if (!selection.environments) {
                settingsObj.remove(QStringLiteral("environments"));
                settingsObj.remove(QStringLiteral("global_env_vars"));
                settingsObj.remove(QStringLiteral("active_environment_id"));
            }
            if (!selection.terminalProfiles) {
                settingsObj.remove(QStringLiteral("terminal_targets"));
            }
            if (!selection.globalActions) {
                settingsObj.remove(QStringLiteral("global_actions"));
            }
        } else {
            // `settings` não marcado: objeto enxuto, sem tema/janela/atalhos.
            if (selection.environments) {
                const QJsonValue envs = fullSettingsObj.value("environments");
                const QJsonValue legacy = fullSettingsObj.value("global_env_vars");
                const QJsonValue active = fullSettingsObj.value("active_environment_id");
                if (!envs.isUndefined()) settingsObj["environments"] = envs;
                if (!legacy.isUndefined()) settingsObj["global_env_vars"] = legacy;
                if (!active.isUndefined()) settingsObj["active_environment_id"] = active;
            }
            if (selection.terminalProfiles) {
                const QJsonValue targets = fullSettingsObj.value("terminal_targets");
                if (!targets.isUndefined()) settingsObj["terminal_targets"] = targets;
            }
            if (selection.globalActions) {
                const QJsonValue actions = fullSettingsObj.value("global_actions");
                if (!actions.isUndefined()) settingsObj["global_actions"] = actions;
            }
        }
        // Formato enxuto: ações globais só pelo NOME do comando (sem id).
        if (lean && settingsObj.contains(QStringLiteral("global_actions"))) {
            QJsonArray named;
            for (const QJsonValue &v : settingsObj.value(QStringLiteral("global_actions")).toArray()) {
                QJsonObject o = v.toObject();
                if (!o.contains(QStringLiteral("command"))) {
                    continue; // comando sem nome conhecido: não há o que levar
                }
                o.remove(QStringLiteral("command_id"));
                named.append(o);
            }
            settingsObj["global_actions"] = named;
        }
        root["settings"] = settingsObj;
    }

    if (selection.commands) {
        QJsonArray foldersArr;
        for (const Folder &f : commands.folders) {
            foldersArr.append(folderToExportJson(f));
        }
        QJsonArray commandsArr;
        for (const Command &cmd : commands.commands) {
            commandsArr.append(commandToExportJson(cmd));
        }
        root["folders"] = foldersArr;
        root["commands"] = commandsArr;
        QSet<QString> allFolderIds;
        for (const Folder &f : commands.folders) {
            allFolderIds.insert(f.id);
        }
        const QJsonArray notesArr = notesForExport(commands.notes, allFolderIds);
        if (!notesArr.isEmpty()) {
            root["notes"] = notesArr;
        }
    }

    if (selection.collections) {
        QJsonArray collectionsArr;
        for (const Collection &col : collections) {
            // Sem "dados de coleção": só a ESTRUTURA (nome/pasta/schema), entries zeradas.
            if (selection.collectionEntries) {
                // Campos "secret" NUNCA saem, mesmo com entries ligadas.
                collectionsArr.append(collectionToExportJson(redactSecretFieldsForExport(col)));
            } else {
                Collection structureOnly = col;
                structureOnly.entries.clear();
                collectionsArr.append(collectionToExportJson(structureOnly));
            }
        }
        root["collections"] = collectionsArr;
    }

    // Escopo global/seletivo não tem "raiz excluída": todos mantêm o path completo.
    if (lean) {
        root = stripIdsFromExport(root, QString(), commandNamesById(commands));
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString ConfigManager::exportGlobal(const SettingsData &settings, const CommandsData &commands)
{
    QJsonObject root;
    root["kai_export"] = exportHeader(QStringLiteral("global"));

    // Mesmo formato de saveSettings, serializado inline.
    QJsonObject settingsObj;
    QJsonObject envObj;
    for (auto it = settings.globalEnvVars.constBegin(); it != settings.globalEnvVars.constEnd(); ++it) {
        envObj[it.key()] = it.value();
    }
    settingsObj["active_theme"] = settings.activeTheme;
    settingsObj["ui_density"] = settings.uiDensity;
    settingsObj["ui_corner_style"] = settings.uiCornerStyle;
    settingsObj["tree_connector_style"] = settings.treeConnectorStyle;
    settingsObj["commands_background_image"] = settings.commandsBackgroundImage;
    settingsObj["commands_background_opacity"] = settings.commandsBackgroundOpacity;
    settingsObj["item_actions_placement"] = settings.itemActionsPlacement;
    settingsObj["display_actions_placement"] = settings.displayActionsPlacement;
    settingsObj["execution_actions_placement"] = settings.executionActionsPlacement;
    settingsObj["output_position"] = settings.outputPosition;
    if (settings.outputSplitterSizes.size() == 2) {
        QJsonArray sizesArr;
        sizesArr.append(settings.outputSplitterSizes.at(0));
        sizesArr.append(settings.outputSplitterSizes.at(1));
        settingsObj["output_splitter_sizes"] = sizesArr;
    }
    {
        QJsonObject shortcutsObj;
        for (auto it = settings.actionShortcuts.constBegin(); it != settings.actionShortcuts.constEnd(); ++it) {
            shortcutsObj[it.key()] = it.value();
        }
        settingsObj["action_shortcuts"] = shortcutsObj;
    }
    {
        QJsonObject v2;
        for (auto it = settings.shortcuts.constBegin(); it != settings.shortcuts.constEnd(); ++it) {
            QJsonArray seqs;
            for (const QString &s : it.value()) {
                seqs.append(s);
            }
            v2[it.key()] = seqs;
        }
        settingsObj["shortcuts_v2"] = v2;
    }
    settingsObj["show_hidden_commands"] = settings.showHiddenCommands;
    settingsObj["fx_shadows"] = settings.fxShadows;
    settingsObj["fx_translucency"] = settings.fxTranslucency;
    settingsObj["fx_blur"] = settings.fxBlur;
    settingsObj["fx_animations"] = settings.fxAnimations;
    settingsObj["gradients_enabled"] = settings.gradientsEnabled;
    settingsObj["auto_hide_on_focus_loss"] = settings.autoHideOnFocusLoss;
    settingsObj["window_mode"] = settings.windowMode;
    settingsObj["window_width"] = settings.windowWidth;
    settingsObj["window_height"] = settings.windowHeight;
    settingsObj["global_hotkey"] = settings.globalHotkey;
    settingsObj["start_visible"] = settings.startVisible;
    settingsObj["language"] = settings.language;
    settingsObj["global_env_vars"] = envObj;
    // Campos de SettingsData gravados por saveSettings() que o export global
    // precisa levar para o backup voltar completo.
    settingsObj["command_creation_mode"] = settings.commandCreationMode;
    settingsObj["command_edit_mode"] = settings.commandEditMode;
    settingsObj["terminal_collapsed"] = settings.terminalCollapsed;
    settingsObj["output_line_numbers"] = settings.outputLineNumbers;
    settingsObj["output_wrap"] = settings.outputWrap;
    settingsObj["output_timestamps"] = settings.outputTimestamps;
    settingsObj["output_autoscroll"] = settings.outputAutoScroll;
    settingsObj["output_compact"] = settings.outputCompact;
    settingsObj["output_font_size"] = settings.outputFontSize;
    settingsObj["output_max_log_size_kb"] = settings.outputMaxLogSizeKb;
    settingsObj["graceful_stop_timeout_sec"] = settings.gracefulStopTimeoutSec;
    settingsObj["autostart"] = settings.autostart;
    settingsObj["smart_focus"] = settings.smartFocus;
    settingsObj["inline_actions"] = settings.inlineActions;
    QJsonArray targetsArr;
    for (const TerminalProfile &t : settings.terminalProfiles) {
        QJsonObject obj;
        obj["name"] = t.name;
        obj["command_template"] = t.commandTemplate;
        obj["use_pty"] = t.usePty;
        obj["is_default"] = t.isDefault;
        obj["shell"] = shellFlavorToString(t.shell);
        obj["icon"] = t.icon;
        targetsArr.append(obj);
    }
    settingsObj["terminal_targets"] = targetsArr;

    // Ações globais: o id (pacote completo) e o NOME (o formato enxuto só
    // leva o nome — ver exportSelective).
    {
        QMap<QString, QString> nameById;
        for (const Command &c : commands.commands) {
            nameById.insert(c.id, c.name);
        }
        QJsonArray actionsArr;
        for (const GlobalAction &g : settings.globalActions) {
            QJsonObject o{{QStringLiteral("command_id"), g.commandId},
                          {QStringLiteral("only_projects"), g.onlyProjects},
                          {QStringLiteral("expansion"), g.expansion}};
            if (!g.group.isEmpty()) {
                o.insert(QStringLiteral("group"), g.group);
                if (!g.groupIcon.isEmpty()) o.insert(QStringLiteral("group_icon"), g.groupIcon);
            }
            if (nameById.contains(g.commandId)) {
                o.insert(QStringLiteral("command"), nameById.value(g.commandId));
            }
            actionsArr.append(o);
        }
        settingsObj["global_actions"] = actionsArr;
    }

    // ENVIRONMENTS (pacotes selecionáveis + segredos); exportSelective lê esta
    // chave daqui. Mesmo formato de saveSettings().
    {
        QJsonArray envsArr;
        for (const Environment &e : settings.environments) {
            QJsonObject o;
            o["id"] = e.id;
            o["name"] = e.name;
            QJsonObject vars;
            for (auto it = e.vars.constBegin(); it != e.vars.constEnd(); ++it) {
                if (!e.secretKeys.contains(it.key())) { // o valor de uma variável secreta não sai do Kai
                    vars[it.key()] = it.value();
                }
            }
            o["vars"] = vars;
            QJsonArray secrets;
            for (const QString &k : e.secretKeys) {
                secrets.append(k);
            }
            o["secret_keys"] = secrets;
            envsArr.append(o);
        }
        settingsObj["environments"] = envsArr;
        settingsObj["active_environment_id"] = settings.activeEnvironmentId;
    }

    root["settings"] = settingsObj;

    QJsonArray foldersArr;
    for (const Folder &f : commands.folders) {
        foldersArr.append(folderToExportJson(f));
    }
    QJsonArray commandsArr;
    for (const Command &c : commands.commands) {
        commandsArr.append(commandToExportJson(c));
    }
    root["folders"] = foldersArr;
    root["commands"] = commandsArr;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

namespace {
// Mesmo formato de settingsObj["terminal_targets"] do export global, p/
// export de pasta/comando levar alvos sem duplicar o parser de import.
void appendTerminalProfilesSection(QJsonObject &root, const QVector<TerminalProfile> &terminalProfiles)
{
    if (terminalProfiles.isEmpty()) {
        return;
    }
    QJsonArray targetsArr;
    for (const TerminalProfile &t : terminalProfiles) {
        QJsonObject obj;
        obj["name"] = t.name;
        obj["command_template"] = t.commandTemplate;
        obj["use_pty"] = t.usePty;
        obj["is_default"] = t.isDefault;
        obj["shell"] = shellFlavorToString(t.shell);
        obj["icon"] = t.icon;
        targetsArr.append(obj);
    }
    QJsonObject settingsObj = root.value("settings").toObject();
    settingsObj["terminal_targets"] = targetsArr;
    root["settings"] = settingsObj;
}
}

QVector<Collection> ConfigManager::linkedCollectionsForFolder(const QString &folderId, const CommandsData &commands,
                                                              const QVector<Collection> &collections)
{
    QVector<Folder> folders;
    QVector<Command> cmds;
    collectFolderSubtree(folderId, commands, folders, cmds);
    QSet<QString> folderIds{folderId};
    for (const Folder &f : std::as_const(folders)) {
        folderIds.insert(f.id);
    }
    QSet<QString> seen;
    QVector<Collection> result;
    auto addIfNew = [&](const Collection &col) {
        if (!seen.contains(col.id)) {
            seen.insert(col.id);
            result.append(col);
        }
    };
    for (const Collection &col : collections) {
        if (folderIds.contains(col.folderId)) {
            addIfNew(col);
        }
    }
    for (const Command &c : std::as_const(cmds)) {
        for (const Parameter &p : c.params) {
            if (p.collectionId.isEmpty()) {
                continue;
            }
            const auto it = std::find_if(collections.cbegin(), collections.cend(),
                                         [&p](const Collection &col) { return col.id == p.collectionId; });
            if (it != collections.cend()) {
                addIfNew(*it);
            }
        }
    }
    return result;
}

QVector<Command> ConfigManager::actionCommandsOutsideFolder(const QString &folderId, const CommandsData &commands)
{
    QVector<Folder> folders;
    QVector<Command> inside;
    collectFolderSubtree(folderId, commands, folders, inside);
    QSet<QString> insideIds;
    for (const Command &c : inside) {
        insideIds.insert(c.id);
    }
    QVector<Command> result;
    QSet<QString> seen;
    for (const Folder &f : folders) {
        for (const QString &commandId : f.actions) {
            if (insideIds.contains(commandId) || seen.contains(commandId)) {
                continue;
            }
            const auto it = std::find_if(commands.commands.cbegin(), commands.commands.cend(),
                                         [&commandId](const Command &c) { return c.id == commandId; });
            if (it != commands.commands.cend()) {
                seen.insert(commandId);
                result.append(*it);
            }
        }
    }
    return result;
}

QString ConfigManager::exportFolder(const QString &folderId, const CommandsData &commands,
                                    const QVector<Collection> &linkedCollections,
                                    const QVector<TerminalProfile> &terminalProfiles,
                                    bool lean, bool includeActionCommands)
{
    QVector<Folder> folders;
    QVector<Command> cmds;
    collectFolderSubtree(folderId, commands, folders, cmds);

    QJsonObject root;
    root["kai_export"] = exportHeader(QStringLiteral("folder"));
    QJsonArray foldersArr;
    for (const Folder &f : folders) {
        foldersArr.append(folderToExportJson(f));
    }
    QJsonArray commandsArr;
    for (const Command &c : cmds) {
        commandsArr.append(commandToExportJson(c));
    }
    if (includeActionCommands) {
        for (const Command &c : actionCommandsOutsideFolder(folderId, commands)) {
            QJsonObject o = commandToExportJson(c);
            o.insert(QStringLiteral("action_only"), true);
            commandsArr.append(o);
        }
    }
    root["folders"] = foldersArr;
    root["commands"] = commandsArr;
    {
        QSet<QString> subtreeFolderIds;
        for (const Folder &f : folders) {
            subtreeFolderIds.insert(f.id);
        }
        const QJsonArray notesArr = notesForExport(commands.notes, subtreeFolderIds);
        if (!notesArr.isEmpty()) {
            root["notes"] = notesArr;
        }
    }
    if (!linkedCollections.isEmpty()) {
        QJsonArray collectionsArr;
        for (const Collection &col : linkedCollections) {
            collectionsArr.append(collectionToExportJson(redactSecretFieldsForExport(col)));
        }
        root["collections"] = collectionsArr;
    }
    appendTerminalProfilesSection(root, terminalProfiles);
    // A pasta exportada (`folderId`) é o topo implícito: path == "", fora da lista de pastas.
    if (lean) {
        root = stripIdsFromExport(root, folderId, commandNamesById(commands));
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString ConfigManager::exportCommand(const QString &commandId, const CommandsData &commands,
                                     const QVector<Collection> &linkedCollections,
                                     const QVector<TerminalProfile> &terminalProfiles,
                                     bool lean)
{
    QJsonObject root;
    root["kai_export"] = exportHeader(QStringLiteral("command"));
    QJsonArray commandsArr;
    for (const Command &c : commands.commands) {
        if (c.id == commandId) {
            commandsArr.append(commandToExportJson(c));
            break;
        }
    }
    root["commands"] = commandsArr;
    if (!linkedCollections.isEmpty()) {
        QJsonArray collectionsArr;
        for (const Collection &col : linkedCollections) {
            collectionsArr.append(collectionToExportJson(redactSecretFieldsForExport(col)));
        }
        root["collections"] = collectionsArr;
    }
    appendTerminalProfilesSection(root, terminalProfiles);
    if (lean) {
        root = stripIdsFromExport(root, QString(), commandNamesById(commands));
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace kai::core
