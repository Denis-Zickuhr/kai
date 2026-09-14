

#include <QJsonParseError>

#include "utils/translation-manager.h"

#include "core/config-manager-internal.h"
#include "core/config-id-free.h"

namespace kai::core {

using namespace detail;

ConfigManager::ImportResult ConfigManager::importFromJson(const QString &jsonText)
{
    ImportResult result;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.errorMessage = utils::tr(QStringLiteral("config_manager.error.invalid_json")).arg(parseError.errorString());
        return result;
    }

    const QJsonObject root = doc.object();
    const QJsonObject header = root.value("kai_export").toObject();
    if (header.isEmpty()) {
        result.errorMessage = utils::tr(QStringLiteral("config_manager.error.not_export"));
        return result;
    }

    result.scope = header.value("scope").toString();

    // Pacote SEM ids: resolve path/nome -> ids frescos ANTES do parsing normal
    // (que só entende o formato id-based). Reimportar sempre adiciona cópias.
    const QJsonObject normalizedRoot = looksIdFree(root) ? resolveIdFreeImport(root) : root;

    for (const QJsonValue &v : normalizedRoot.value("folders").toArray()) {
        const QJsonObject folderObj = v.toObject();
        result.folders.append(Folder::fromJson(folderObj));
        QStringList pendingNames;
        for (const QJsonValue &pv : folderObj.value(QStringLiteral("actions_pending")).toArray()) {
            pendingNames.append(pv.toString());
        }
        if (!pendingNames.isEmpty()) {
            result.pendingFolderActions.insert(result.folders.last().id, pendingNames);
            QStringList pendingExpansion;
            for (const QJsonValue &pv : folderObj.value(QStringLiteral("expansion_actions_pending")).toArray()) {
                pendingExpansion.append(pv.toString());
            }
            if (!pendingExpansion.isEmpty()) {
                result.pendingFolderExpansion.insert(result.folders.last().id, pendingExpansion);
            }
            const QJsonObject pendingGroups = folderObj.value(QStringLiteral("action_groups_pending")).toObject();
            QMap<QString, QString> groupByName;
            for (auto git = pendingGroups.constBegin(); git != pendingGroups.constEnd(); ++git) {
                groupByName.insert(git.key(), git.value().toString());
            }
            if (!groupByName.isEmpty()) {
                result.pendingFolderGroups.insert(result.folders.last().id, groupByName);
            }
        }
    }
    if (normalizedRoot.contains("collections")) {
        for (const QJsonValue &v : normalizedRoot.value("collections").toArray()) {
            result.collections.append(Collection::fromJson(v.toObject()));
        }
        result.hasCollections = !result.collections.isEmpty();
    }
    for (const QJsonValue &v : normalizedRoot.value("notes").toArray()) {
        Note note = Note::fromJson(v.toObject());
        note.local = false; // o que viaja em pacote/arquivo é, por definição, sincronizável
        result.notes.append(note);
    }
    QSet<QString> actionOnlyIds;
    for (const QJsonValue &v : normalizedRoot.value("commands").toArray()) {
        const QJsonObject commandObj = v.toObject();
        Command command = Command::fromJson(commandObj);
        if (commandObj.value(QStringLiteral("action_only")).toBool(false)) {
            actionOnlyIds.insert(command.id);
            result.actionCommands.append(command);
        } else {
            result.commands.append(command);
        }
    }
    // As ações que apontam pra um comando "action_only" viram referência por NOME: só
    // ganham o vínculo se o comando existir de fato (o do usuário, ou o importado).
    if (!actionOnlyIds.isEmpty()) {
        QMap<QString, QString> nameById;
        for (const Command &c : std::as_const(result.actionCommands)) {
            nameById.insert(c.id, c.name);
        }
        for (Folder &f : result.folders) {
            QStringList kept;
            QStringList moved;
            QStringList movedExpansion;
            for (const QString &actionId : std::as_const(f.actions)) {
                if (actionOnlyIds.contains(actionId)) {
                    moved << nameById.value(actionId);
                    if (f.expansionActions.contains(actionId)) {
                        movedExpansion << nameById.value(actionId);
                    }
                    if (f.actionGroups.contains(actionId)) {
                        result.pendingFolderGroups[f.id].insert(nameById.value(actionId), f.actionGroups.value(actionId));
                    }
                } else {
                    kept << actionId;
                }
            }
            if (!moved.isEmpty()) {
                f.actions = kept;
                f.expansionActions.removeIf([&actionOnlyIds](const QString &id) { return actionOnlyIds.contains(id); });
                for (const QString &id : std::as_const(actionOnlyIds)) {
                    f.actionGroups.remove(id);
                }
                result.pendingFolderActions[f.id] += moved;
                if (!movedExpansion.isEmpty()) {
                    result.pendingFolderExpansion[f.id] += movedExpansion;
                }
            }
        }
    }

    if (normalizedRoot.contains("settings") && normalizedRoot.value("settings").isObject()) {
        const QJsonObject actionsSettings = normalizedRoot.value("settings").toObject();
        if (actionsSettings.contains(QStringLiteral("global_actions"))) {
            result.hasGlobalActions = true;
            for (const QJsonValue &av : actionsSettings.value(QStringLiteral("global_actions")).toArray()) {
                const QJsonObject o = av.toObject();
                const QString commandId = o.value(QStringLiteral("command_id")).toString().trimmed();
                const bool onlyProjects = o.value(QStringLiteral("only_projects")).toBool(false);
                const bool expansion = o.value(QStringLiteral("expansion")).toBool(false);
                const QString group = o.value(QStringLiteral("group")).toString().trimmed();
                const QString groupIcon = group.isEmpty() ? QString() : o.value(QStringLiteral("group_icon")).toString().trimmed();
                if (!commandId.isEmpty()) {
                    result.settings.globalActions.append({commandId, onlyProjects, expansion, group, groupIcon});
                } else if (!o.value(QStringLiteral("command")).toString().isEmpty()) {
                    result.pendingGlobalActions.append({o.value(QStringLiteral("command")).toString(), onlyProjects,
                                                        expansion, group, groupIcon});
                }
            }
        }
    }

    if (root.contains("settings") && root.value("settings").isObject()) {
        const QJsonObject s = root.value("settings").toObject();
        // hasSettings/hasEnvironments são independentes (exportSelective): cada
        // um é detectado pela sua chave-marco, não pela existência de `settings`.
        result.hasSettings = s.contains(QStringLiteral("active_theme"));
        result.hasEnvironments = s.contains(QStringLiteral("environments"));
        result.hasTerminalProfiles = s.contains(QStringLiteral("terminal_targets"))
            && !s.value(QStringLiteral("terminal_targets")).toArray().isEmpty();
        result.settings.activeTheme = s.value("active_theme").toString(result.settings.activeTheme);
        result.settings.uiDensity = s.value("ui_density").toString(result.settings.uiDensity);
        result.settings.uiCornerStyle = s.value("ui_corner_style").toInt(result.settings.uiCornerStyle);
        result.settings.treeConnectorStyle = s.value("tree_connector_style").toInt(result.settings.treeConnectorStyle);
        result.settings.commandsBackgroundImage = s.value("commands_background_image").toString(result.settings.commandsBackgroundImage);
        result.settings.commandsBackgroundOpacity = s.value("commands_background_opacity").toInt(result.settings.commandsBackgroundOpacity);
        result.settings.itemActionsPlacement = s.value("item_actions_placement").toString(result.settings.itemActionsPlacement);
        result.settings.displayActionsPlacement = s.value("display_actions_placement").toString(result.settings.displayActionsPlacement);
        result.settings.executionActionsPlacement = s.value("execution_actions_placement").toString(result.settings.executionActionsPlacement);
        result.settings.outputPosition = s.value("output_position").toString(result.settings.outputPosition);
        if (s.value("output_splitter_sizes").isArray()) {
            const QJsonArray arr = s.value("output_splitter_sizes").toArray();
            if (arr.size() == 2) {
                result.settings.outputSplitterSizes = {arr.at(0).toInt(), arr.at(1).toInt()};
            }
        }
        if (s.value("action_shortcuts").isObject()) {
            const QJsonObject shortcutsObj = s.value("action_shortcuts").toObject();
            for (auto it = shortcutsObj.constBegin(); it != shortcutsObj.constEnd(); ++it) {
                result.settings.actionShortcuts[it.key()] = it.value().toString();
            }
        }
        if (s.value("shortcuts_v2").isObject()) {
            const QJsonObject v2 = s.value("shortcuts_v2").toObject();
            for (auto it = v2.constBegin(); it != v2.constEnd(); ++it) {
                QStringList seqs;
                for (const QJsonValue &sv : it.value().toArray()) {
                    seqs << sv.toString();
                }
                result.settings.shortcuts[it.key()] = seqs;
            }
        }
        result.settings.showHiddenCommands = s.value("show_hidden_commands").toBool(result.settings.showHiddenCommands);
        result.settings.fxShadows = s.value("fx_shadows").toBool(result.settings.fxShadows);
        result.settings.fxTranslucency = s.value("fx_translucency").toBool(result.settings.fxTranslucency);
        result.settings.fxBlur = s.value("fx_blur").toBool(result.settings.fxBlur);
        result.settings.fxAnimations = s.value("fx_animations").toBool(result.settings.fxAnimations);
        result.settings.gradientsEnabled = s.value("gradients_enabled").toBool(result.settings.gradientsEnabled);
        result.settings.autoHideOnFocusLoss = s.value("auto_hide_on_focus_loss").toBool(result.settings.autoHideOnFocusLoss);
        result.settings.windowMode = s.value("window_mode").toString(result.settings.windowMode);
        result.settings.windowWidth = s.value("window_width").toInt(result.settings.windowWidth);
        result.settings.windowHeight = s.value("window_height").toInt(result.settings.windowHeight);
        result.settings.globalHotkey = s.value("global_hotkey").toString(result.settings.globalHotkey);
        result.settings.startVisible = s.value("start_visible").toBool(result.settings.startVisible);
        result.settings.language = s.value("language").toString(result.settings.language);
        const QJsonObject envObj = s.value("global_env_vars").toObject();
        for (auto it = envObj.constBegin(); it != envObj.constEnd(); ++it) {
            result.settings.globalEnvVars[it.key()] = it.value().toString();
        }
        for (const QJsonValue &v : s.value("terminal_targets").toArray()) {
            const QJsonObject obj = v.toObject();
            TerminalProfile t;
            t.name = obj.value("name").toString();
            t.commandTemplate = obj.value("command_template").toString();
            t.usePty = obj.value("use_pty").toBool(true);
            t.isDefault = obj.value("is_default").toBool(false);
            t.shell = shellFlavorFromString(obj.value("shell").toString());
            t.icon = obj.value("icon").toString();
            if (!t.name.isEmpty()) {
                result.settings.terminalProfiles.append(t);
            }
        }

        result.settings.commandCreationMode = s.value("command_creation_mode").toString(result.settings.commandCreationMode);
        result.settings.commandEditMode = s.value("command_edit_mode").toString(result.settings.commandEditMode);
        result.settings.terminalCollapsed = s.value("terminal_collapsed").toBool(result.settings.terminalCollapsed);
        result.settings.outputLineNumbers = s.value("output_line_numbers").toBool(result.settings.outputLineNumbers);
        result.settings.outputWrap = s.value("output_wrap").toBool(result.settings.outputWrap);
        result.settings.outputTimestamps = s.value("output_timestamps").toBool(result.settings.outputTimestamps);
        result.settings.outputAutoScroll = s.value("output_autoscroll").toBool(result.settings.outputAutoScroll);
        result.settings.outputCompact = s.value("output_compact").toBool(result.settings.outputCompact);
        result.settings.outputFontSize = s.value("output_font_size").toInt(result.settings.outputFontSize);
        result.settings.outputMaxLogSizeKb = s.value("output_max_log_size_kb").toInt(result.settings.outputMaxLogSizeKb);
        result.settings.gracefulStopTimeoutSec = s.value("graceful_stop_timeout_sec").toInt(result.settings.gracefulStopTimeoutSec);
        result.settings.autostart = s.value("autostart").toBool(result.settings.autostart);
        result.settings.smartFocus = s.value("smart_focus").toBool(result.settings.smartFocus);
        result.settings.inlineActions = s.value("inline_actions").toBool(result.settings.inlineActions);

        for (const QJsonValue &v : s.value("environments").toArray()) {
            const QJsonObject o = v.toObject();
            Environment e;
            e.id = o.value("id").toString();
            e.name = o.value("name").toString();
            const QJsonObject vars = o.value("vars").toObject();
            for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
                e.vars[it.key()] = it.value().toString();
            }
            for (const QJsonValue &sv : o.value("secret_keys").toArray()) {
                e.secretKeys.insert(sv.toString());
                if (!e.vars.contains(sv.toString())) {
                    e.vars.insert(sv.toString(), QString()); // veio sem valor: a variável existe, o valor é de quem importa
                }
            }
            if (!e.id.isEmpty()) {
                result.settings.environments.append(e);
            }
        }
        result.settings.activeEnvironmentId = s.value("active_environment_id").toString(result.settings.activeEnvironmentId);
    }

    result.ok = true;
    return result;
}

} // namespace kai::core
