#include "core/config-manager.h"
#include "core/field-scopes.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDateTime>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <functional>
#include "utils/logger.h"
#include "utils/translation-manager.h"
#include "core/folder-path-resolver.h"
#include "core/config-manager-internal.h"
#include "core/config-id-free.h"

namespace kai::core {

using namespace detail;

namespace {
constexpr const char *kLogTag = "ConfigManager";
}

SettingsData ConfigManager::loadSettings()
{
    const QJsonObject root = readJsonWithRecovery(settingsFilePath());

    SettingsData data;
    if (root.contains("active_theme")) {
        data.activeTheme = root.value("active_theme").toString();
    }
    if (root.contains("ui_density")) {
        data.uiDensity = root.value("ui_density").toString();
    }
    if (root.contains("ui_corner_style")) {
        data.uiCornerStyle = root.value("ui_corner_style").toInt(data.uiCornerStyle);
    }
    if (root.contains("tree_connector_style")) {
        data.treeConnectorStyle = root.value("tree_connector_style").toInt(data.treeConnectorStyle);
    }
    data.commandsBackgroundImage = root.value("commands_background_image").toString(data.commandsBackgroundImage);
    data.commandsBackgroundOpacity = root.value("commands_background_opacity").toInt(data.commandsBackgroundOpacity);
    if (root.contains("fx_shadows")) {
        data.fxShadows = root.value("fx_shadows").toBool();
    }
    if (root.contains("fx_translucency")) {
        data.fxTranslucency = root.value("fx_translucency").toBool();
    }
    if (root.contains("fx_blur")) {
        data.fxBlur = root.value("fx_blur").toBool();
    }
    if (root.contains("fx_animations")) {
        data.fxAnimations = root.value("fx_animations").toBool();
    }
    if (root.contains("gradients_enabled")) {
        data.gradientsEnabled = root.value("gradients_enabled").toBool();
    }
    if (root.contains("auto_hide_on_focus_loss")) {
        data.autoHideOnFocusLoss = root.value("auto_hide_on_focus_loss").toBool();
    }
    if (root.contains("window_mode")) {
        data.windowMode = root.value("window_mode").toString();
    }
    if (root.contains("window_width")) {
        data.windowWidth = root.value("window_width").toInt(data.windowWidth);
    }
    if (root.contains("window_height")) {
        data.windowHeight = root.value("window_height").toInt(data.windowHeight);
    }
    if (root.contains("detached_window_width")) {
        data.detachedWindowWidth = root.value("detached_window_width").toInt(data.detachedWindowWidth);
    }
    if (root.contains("detached_window_height")) {
        data.detachedWindowHeight = root.value("detached_window_height").toInt(data.detachedWindowHeight);
    }
    if (root.contains("global_hotkey")) {
        data.globalHotkey = root.value("global_hotkey").toString();
    }
    if (root.contains("start_visible")) {
        data.startVisible = root.value("start_visible").toBool(data.startVisible);
    }
    if (root.contains("next_tab_shortcut")) {
        data.nextTabShortcut = root.value("next_tab_shortcut").toString();
    }
    if (root.contains("previous_tab_shortcut")) {
        data.previousTabShortcut = root.value("previous_tab_shortcut").toString();
    }
    if (root.contains("edit_item_shortcut")) {
        data.editItemShortcut = root.value("edit_item_shortcut").toString();
    }
    if (root.contains("delete_item_shortcut")) {
        data.deleteItemShortcut = root.value("delete_item_shortcut").toString();
    }
    if (root.contains("new_folder_shortcut")) {
        data.newFolderShortcut = root.value("new_folder_shortcut").toString();
    }
    if (root.contains("new_command_shortcut")) {
        data.newCommandShortcut = root.value("new_command_shortcut").toString();
    }
    if (root.contains("quit_app_shortcut")) {
        data.quitAppShortcut = root.value("quit_app_shortcut").toString();
    }
    if (root.contains("toggle_search_shortcut")) {
        data.toggleSearchShortcut = root.value("toggle_search_shortcut").toString();
    }
    if (root.contains("context_menu_shortcut")) {
        data.contextMenuShortcut = root.value("context_menu_shortcut").toString();
    }
    if (root.contains("focus_output_shortcut")) {
        data.focusOutputShortcut = root.value("focus_output_shortcut").toString();
    }
    if (root.contains("toggle_edit_mode_shortcut")) {
        data.toggleEditModeShortcut = root.value("toggle_edit_mode_shortcut").toString();
    }
    if (root.contains("item_actions_placement")) {
        data.itemActionsPlacement = root.value("item_actions_placement").toString();
    }
    if (root.contains("display_actions_placement")) {
        data.displayActionsPlacement = root.value("display_actions_placement").toString();
    }
    if (root.contains("execution_actions_placement")) {
        data.executionActionsPlacement = root.value("execution_actions_placement").toString();
    }
    if (root.contains("output_position")) {
        data.outputPosition = root.value("output_position").toString();
    }
    // Tamanhos do splitter externo (árvore+ações vs. Saída); vazio (ausente ou
    // tamanho != 2) = defaults calculados no boot.
    if (root.contains("output_splitter_sizes")) {
        const QJsonArray arr = root.value("output_splitter_sizes").toArray();
        if (arr.size() == 2) {
            data.outputSplitterSizes = {arr.at(0).toInt(), arr.at(1).toInt()};
        }
    }
    if (root.contains("action_shortcuts") && root.value("action_shortcuts").isObject()) {
        const QJsonObject shortcutsObj = root.value("action_shortcuts").toObject();
        for (auto it = shortcutsObj.constBegin(); it != shortcutsObj.constEnd(); ++it) {
            data.actionShortcuts[it.key()] = it.value().toString();
        }
    }
    // Chave nova "shortcuts_v2" de propósito (não reaproveita "action_shortcuts"):
    // o valor passou de string única p/ array, e um Kai antigo não deve lê-la
    // como o formato antigo.
    if (root.contains("shortcuts_v2") && root.value("shortcuts_v2").isObject()) {
        const QJsonObject v2 = root.value("shortcuts_v2").toObject();
        for (auto it = v2.constBegin(); it != v2.constEnd(); ++it) {
            QStringList seqs;
            for (const QJsonValue &sv : it.value().toArray()) {
                seqs << sv.toString();
            }
            data.shortcuts[it.key()] = seqs;
        }
    } else {
        // MIGRAÇÃO do formato antigo: sintetiza a estrutura nova mantendo os
        // campos legados. Idempotente: roda a cada carga até o usuário salvar
        // as Configurações (aí saveSettings grava "shortcuts_v2").
        auto migrateOne = [&data](const QString &actionId, const QString &legacyValue) {
            if (!legacyValue.isEmpty()) {
                data.shortcuts[actionId] = {legacyValue};
            }
        };
        migrateOne(QStringLiteral("action.next_tab"), data.nextTabShortcut);
        migrateOne(QStringLiteral("action.previous_tab"), data.previousTabShortcut);
        migrateOne(QStringLiteral("action.edit_item"), data.editItemShortcut);
        migrateOne(QStringLiteral("action.delete_item"), data.deleteItemShortcut);
        migrateOne(QStringLiteral("action.new_folder"), data.newFolderShortcut);
        migrateOne(QStringLiteral("action.new_command"), data.newCommandShortcut);
        migrateOne(QStringLiteral("action.quit_app"), data.quitAppShortcut);
        migrateOne(QStringLiteral("action.toggle_search"), data.toggleSearchShortcut);
        migrateOne(QStringLiteral("action.context_menu"), data.contextMenuShortcut);
        migrateOne(QStringLiteral("action.focus_output"), data.focusOutputShortcut);
        migrateOne(QStringLiteral("action.toggle_edit_mode"), data.toggleEditModeShortcut);
        for (auto it = data.actionShortcuts.constBegin(); it != data.actionShortcuts.constEnd(); ++it) {
            migrateOne(it.key(), it.value());
        }
        if (!data.shortcuts.isEmpty()) {
            utils::Logger::info(kLogTag,
                QStringLiteral("Atalhos migrados do formato legado para o Shortcuts Manager v2 (%1 ação(ões)).")
                    .arg(data.shortcuts.size()));
        }
    }
    for (const QJsonValue &v : root.value("applied_migrations").toArray()) {
        data.appliedMigrations << v.toString();
    }
    // O padrão de "Focar saída" trocou de Ctrl+` p/ Ctrl+' (a crase é tecla
    // morta no ABNT2). shortcuts_v2 grava TODOS os atalhos, então quem já salvou
    // não veria o novo padrão: troca só quem está EXATAMENTE no antigo, uma
    // única vez (o marcador impede reverter quem voltar de propósito).
    const QString focusOutputMigration = QStringLiteral("focus_output_apostrophe");
    if (!data.appliedMigrations.contains(focusOutputMigration)) {
        const QString oldDefault = QStringLiteral("Ctrl+`");
        const QString newDefault = QStringLiteral("Ctrl+'");
        const QString actionId = QStringLiteral("action.focus_output");
        if (data.shortcuts.value(actionId) == QStringList{oldDefault}) {
            data.shortcuts[actionId] = {newDefault};
        }
        if (data.focusOutputShortcut == oldDefault) {
            data.focusOutputShortcut = newDefault;
        }
        data.appliedMigrations << focusOutputMigration;
    }
    if (root.contains("show_hidden_commands")) {
        data.showHiddenCommands = root.value("show_hidden_commands").toBool();
    }
    if (root.contains("command_creation_mode")) {
        data.commandCreationMode = root.value("command_creation_mode").toString();
    }
    if (root.contains("command_edit_mode")) {
        data.commandEditMode = root.value("command_edit_mode").toString();
    }
    if (root.contains("terminal_collapsed")) {
        data.terminalCollapsed = root.value("terminal_collapsed").toBool(false);
    }
    if (root.contains("commands_collapsed")) {
        data.commandsCollapsed = root.value("commands_collapsed").toBool(false);
    }
    data.outputLineNumbers = root.value("output_line_numbers").toBool(data.outputLineNumbers);
    data.outputWrap        = root.value("output_wrap").toBool(data.outputWrap);
    data.outputTimestamps  = root.value("output_timestamps").toBool(data.outputTimestamps);
    data.outputAutoScroll  = root.value("output_autoscroll").toBool(data.outputAutoScroll);
    data.outputCompact     = root.value("output_compact").toBool(data.outputCompact);
    data.outputFontSize    = root.value("output_font_size").toInt(data.outputFontSize);
    data.outputMaxLogSizeKb = root.value("output_max_log_size_kb").toInt(data.outputMaxLogSizeKb);
    data.gracefulStopTimeoutSec = root.value("graceful_stop_timeout_sec").toInt(data.gracefulStopTimeoutSec);
    if (root.contains("autostart")) {
        data.autostart = root.value("autostart").toBool(false);
    }
    data.smartFocus = root.value("smart_focus").toBool(data.smartFocus);
    data.inlineActions = root.value("inline_actions").toBool(data.inlineActions);
    if (root.contains("language")) {
        data.language = root.value("language").toString();
    }

    for (const QJsonValue &v : root.value("terminal_targets").toArray()) {
        const QJsonObject obj = v.toObject();
        TerminalProfile target;
        target.name = obj.value("name").toString();
        target.commandTemplate = obj.value("command_template").toString();
        // Alvos antigos sem a chave assumem o novo default (true).
        target.usePty = obj.value("use_pty").toBool(true);
        target.isDefault = obj.value("is_default").toBool(false);
        target.shell = shellFlavorFromString(obj.value("shell").toString());
        target.icon = obj.value("icon").toString();
        // Remove a gambiarra $l[2..] de alvos PowerShell antigos.
        if (migratePowerShellTemplate(target.commandTemplate)) {
            utils::Logger::info(kLogTag,
                QStringLiteral("Alvo de terminal '%1' migrado: removida a gambiarra de corte de linhas "
                               "do template PowerShell (o prefixo de env agora é gerado em sintaxe PS).")
                    .arg(target.name));
            if (target.shell == ShellFlavor::Auto) {
                target.shell = ShellFlavor::PowerShell;
            }
        }
        if (!target.name.isEmpty()) {
            data.terminalProfiles.append(target);
        }
    }

    for (const QJsonValue &v : root.value("global_actions").toArray()) {
        const QJsonObject o = v.toObject();
        GlobalAction g;
        g.commandId = o.value("command_id").toString().trimmed();
        g.onlyProjects = o.value("only_projects").toBool(false);
        g.expansion = o.value("expansion").toBool(false);
        g.group = o.value("group").toString().trimmed();
        g.groupIcon = g.group.isEmpty() ? QString() : o.value("group_icon").toString().trimmed();
        if (!g.commandId.isEmpty()) {
            data.globalActions.append(g);
        }
    }

    const QJsonObject envObj = root.value("global_env_vars").toObject();
    for (auto it = envObj.constBegin(); it != envObj.constEnd(); ++it) {
        data.globalEnvVars[it.key()] = it.value().toString();
    }

#if defined(Q_OS_WIN)
    // Alvo "WSL" default, seedado só no Windows na primeira execução (sem
    // settings.json) quando não há NENHUM alvo. TTY ligado (read/docker -it
    // funcionam) + injeção via base64+eval (imune a &&, aspas, pipes).
    if (root.isEmpty() && data.terminalProfiles.isEmpty()) {
        TerminalProfile wsl;
        wsl.name = QStringLiteral("WSL");
        // O b64 vai como ARGUMENTO POSICIONAL ($1) do bash: o cmd.exe não vê
        // '|'/'<' soltos (ficam dentro de "$(...)"). eval roda no MESMO bash
        // (stdin = tty do ConPTY).
        // -i além de -l/-c: o .bashrc padrão do Ubuntu sai cedo em shells
        // NÃO-interativos (`case $- in *i*)`), e aliases/funções do usuário
        // nunca carregariam. Mesmo fix do ProcessRunner (setInteractiveShell).
        wsl.commandTemplate = QStringLiteral(
            "wsl.exe -- bash -lic 'eval \"$(echo \"$1\" | base64 -d)\"' kai {{command_b64}}");
        wsl.usePty = true;
        wsl.isDefault = true; // já vira o padrão: comandos sem alvo usam o WSL
        data.terminalProfiles.append(wsl);
    }
#endif

    const QJsonArray envsArr = root.value("environments").toArray();
    for (const QJsonValue &v : envsArr) {
        const QJsonObject o = v.toObject();
        Environment e;
        e.id = o.value("id").toString();
        e.name = o.value("name").toString();
        const QJsonObject vars = o.value("vars").toObject();
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
            e.vars[it.key()] = it.value().toString();
        }
        const QJsonArray secrets = o.value("secret_keys").toArray();
        for (const QJsonValue &sv : secrets) {
            e.secretKeys.insert(sv.toString());
        }
        if (!e.id.isEmpty()) {
            data.environments.append(e);
        }
    }
    data.activeEnvironmentId = root.value("active_environment_id").toString();

    // MIGRAÇÃO: sem environments, cria o pacote "Global" a partir do
    // global_env_vars legado (garante ao menos um pacote selecionável).
    if (data.environments.isEmpty()) {
        Environment global;
        global.id = QStringLiteral("env_global");
        global.name = QStringLiteral("Global");
        global.vars = data.globalEnvVars;
        data.environments.append(global);
        data.activeEnvironmentId = global.id;
    }
    bool activeExists = false;
    for (const Environment &e : data.environments) {
        if (e.id == data.activeEnvironmentId) { activeExists = true; break; }
    }
    if (!activeExists) {
        data.activeEnvironmentId = data.environments.first().id;
    }

    if (root.contains("notifications_enabled")) {
        data.notificationsEnabled = root.value("notifications_enabled").toBool();
    }
    if (root.contains("notify_on_command_failure")) {
        data.notifyOnCommandFailure = root.value("notify_on_command_failure").toBool();
    }
    if (root.contains("notify_on_background_crash")) {
        data.notifyOnBackgroundProcessCrash = root.value("notify_on_background_crash").toBool();
    }
    if (root.contains("notify_on_background_success")) {
        data.notifyOnBackgroundProcessSuccess = root.value("notify_on_background_success").toBool();
    }
    if (root.contains("notify_on_config_recovered")) {
        data.notifyOnConfigRecovered = root.value("notify_on_config_recovered").toBool();
    }
    if (root.contains("notify_on_first_error_in_formatted_output")) {
        data.notifyOnFirstErrorInFormattedOutput = root.value("notify_on_first_error_in_formatted_output").toBool();
    }
    if (root.contains("notify_even_when_focused")) {
        data.notifyEvenWhenFocused = root.value("notify_even_when_focused").toBool();
    }
    if (root.value("kip").isObject()) {
        const QJsonObject k = root.value("kip").toObject();
        KipSettings kip;
        kip.handshakeTimeoutSec = k.value("handshake_timeout_sec").toInt(kip.handshakeTimeoutSec);
        kip.changeTimeoutSec = k.value("change_timeout_sec").toInt(kip.changeTimeoutSec);
        kip.cancelGraceSec = k.value("cancel_grace_sec").toInt(kip.cancelGraceSec);
        kip.rememberAnswers = k.value("remember_answers").toBool(kip.rememberAnswers);
        kip.expandDetailsOnFailure = k.value("expand_details_on_failure").toBool(kip.expandDetailsOnFailure);
        kip.detachedWindowMode = k.value("detached_window_mode").toString(kip.detachedWindowMode);
        data.kip = kip.clamped();
    }
    for (const QJsonValue &v : root.value("kip_panel_autostart").toArray()) {
        if (v.isString() && !v.toString().isEmpty() && !data.kipPanelAutoStart.contains(v.toString())) {
            data.kipPanelAutoStart << v.toString();
        }
    }
    if (root.value("interpreters").isObject()) {
        const QJsonObject i = root.value("interpreters").toObject();
        data.interpreters.python = i.value("python").toString(data.interpreters.python);
        data.interpreters.node = i.value("node").toString(data.interpreters.node);
        data.interpreters.php = i.value("php").toString(data.interpreters.php);
    }

    return data;
}

bool ConfigManager::saveSettings(const SettingsData &data)
{
    QJsonObject envObj;
    for (auto it = data.globalEnvVars.constBegin(); it != data.globalEnvVars.constEnd(); ++it) {
        envObj[it.key()] = it.value();
    }

    QJsonObject root;
    root["active_theme"] = data.activeTheme;
    root["ui_density"] = data.uiDensity;
    root["ui_corner_style"] = data.uiCornerStyle;
    root["tree_connector_style"] = data.treeConnectorStyle;
    root["commands_background_image"] = data.commandsBackgroundImage;
    root["commands_background_opacity"] = data.commandsBackgroundOpacity;
    root["fx_shadows"] = data.fxShadows;
    root["fx_translucency"] = data.fxTranslucency;
    root["fx_blur"] = data.fxBlur;
    root["fx_animations"] = data.fxAnimations;
    root["gradients_enabled"] = data.gradientsEnabled;
    root["auto_hide_on_focus_loss"] = data.autoHideOnFocusLoss;
    root["window_mode"] = data.windowMode;
    root["window_width"] = data.windowWidth;
    root["window_height"] = data.windowHeight;
    root["detached_window_width"] = data.detachedWindowWidth;
    root["detached_window_height"] = data.detachedWindowHeight;
    root["global_hotkey"] = data.globalHotkey;
    root["start_visible"] = data.startVisible;
    root["next_tab_shortcut"] = data.nextTabShortcut;
    root["previous_tab_shortcut"] = data.previousTabShortcut;
    root["edit_item_shortcut"] = data.editItemShortcut;
    root["delete_item_shortcut"] = data.deleteItemShortcut;
    root["new_folder_shortcut"] = data.newFolderShortcut;
    root["new_command_shortcut"] = data.newCommandShortcut;
    root["quit_app_shortcut"] = data.quitAppShortcut;
    root["toggle_search_shortcut"] = data.toggleSearchShortcut;
    root["context_menu_shortcut"] = data.contextMenuShortcut;
    root["focus_output_shortcut"] = data.focusOutputShortcut;
    root["toggle_edit_mode_shortcut"] = data.toggleEditModeShortcut;
    root["item_actions_placement"] = data.itemActionsPlacement;
    root["display_actions_placement"] = data.displayActionsPlacement;
    root["execution_actions_placement"] = data.executionActionsPlacement;
    root["output_position"] = data.outputPosition;
    if (data.outputSplitterSizes.size() == 2) {
        QJsonArray sizesArr;
        sizesArr.append(data.outputSplitterSizes.at(0));
        sizesArr.append(data.outputSplitterSizes.at(1));
        root["output_splitter_sizes"] = sizesArr;
    }
    {
        QJsonObject shortcutsObj;
        for (auto it = data.actionShortcuts.constBegin(); it != data.actionShortcuts.constEnd(); ++it) {
            shortcutsObj[it.key()] = it.value();
        }
        root["action_shortcuts"] = shortcutsObj;
    }
    {
        // Shortcuts v2: fonte de verdade. Os campos legados acima continuam
        // gravados só p/ um Kai mais antigo não perder a config.
        QJsonObject v2;
        for (auto it = data.shortcuts.constBegin(); it != data.shortcuts.constEnd(); ++it) {
            QJsonArray seqs;
            for (const QString &s : it.value()) {
                seqs.append(s);
            }
            v2[it.key()] = seqs;
        }
        root["shortcuts_v2"] = v2;
        root["applied_migrations"] = QJsonArray::fromStringList(data.appliedMigrations);
    }
    root["show_hidden_commands"] = data.showHiddenCommands;
    root["command_creation_mode"] = data.commandCreationMode;
    root["command_edit_mode"] = data.commandEditMode;
    root["terminal_collapsed"] = data.terminalCollapsed;
    root["commands_collapsed"] = data.commandsCollapsed;
    root["output_line_numbers"] = data.outputLineNumbers;
    root["output_wrap"] = data.outputWrap;
    root["output_timestamps"] = data.outputTimestamps;
    root["output_autoscroll"] = data.outputAutoScroll;
    root["output_compact"] = data.outputCompact;
    root["output_font_size"] = data.outputFontSize;
    root["output_max_log_size_kb"] = data.outputMaxLogSizeKb;
    root["graceful_stop_timeout_sec"] = data.gracefulStopTimeoutSec;
    root["autostart"] = data.autostart;
    root["smart_focus"] = data.smartFocus;
    root["inline_actions"] = data.inlineActions;
    root["language"] = data.language;
    root["global_env_vars"] = envObj;

    QJsonArray envsArr;
    for (const Environment &e : data.environments) {
        QJsonObject o;
        o["id"] = e.id;
        o["name"] = e.name;
        QJsonObject vars;
        for (auto it = e.vars.constBegin(); it != e.vars.constEnd(); ++it) {
            vars[it.key()] = it.value();
        }
        o["vars"] = vars;
        QJsonArray secrets;
        for (const QString &k : e.secretKeys) {
            secrets.append(k);
        }
        o["secret_keys"] = secrets;
        envsArr.append(o);
    }
    root["environments"] = envsArr;
    root["active_environment_id"] = data.activeEnvironmentId;

    QJsonArray targetsArr;
    for (const TerminalProfile &t : data.terminalProfiles) {
        QJsonObject obj;
        obj["name"] = t.name;
        obj["command_template"] = t.commandTemplate;
        obj["use_pty"] = t.usePty;
        obj["is_default"] = t.isDefault;
        obj["shell"] = shellFlavorToString(t.shell);
        obj["icon"] = t.icon;
        targetsArr.append(obj);
    }
    root["terminal_targets"] = targetsArr;

    QJsonArray globalActionsArr;
    for (const GlobalAction &g : data.globalActions) {
        QJsonObject o{{QStringLiteral("command_id"), g.commandId},
                      {QStringLiteral("only_projects"), g.onlyProjects},
                      {QStringLiteral("expansion"), g.expansion}};
        if (!g.group.isEmpty()) {
            o.insert(QStringLiteral("group"), g.group);
            if (!g.groupIcon.isEmpty()) o.insert(QStringLiteral("group_icon"), g.groupIcon);
        }
        globalActionsArr.append(o);
    }
    root["global_actions"] = globalActionsArr;

    root["notifications_enabled"] = data.notificationsEnabled;
    root["notify_on_command_failure"] = data.notifyOnCommandFailure;
    root["notify_on_background_crash"] = data.notifyOnBackgroundProcessCrash;
    root["notify_on_background_success"] = data.notifyOnBackgroundProcessSuccess;
    root["notify_on_config_recovered"] = data.notifyOnConfigRecovered;
    root["notify_on_first_error_in_formatted_output"] = data.notifyOnFirstErrorInFormattedOutput;
    root["notify_even_when_focused"] = data.notifyEvenWhenFocused;
    {
        const KipSettings kip = data.kip.clamped();
        QJsonObject k;
        k["handshake_timeout_sec"] = kip.handshakeTimeoutSec;
        k["change_timeout_sec"] = kip.changeTimeoutSec;
        k["cancel_grace_sec"] = kip.cancelGraceSec;
        k["remember_answers"] = kip.rememberAnswers;
        k["expand_details_on_failure"] = kip.expandDetailsOnFailure;
        k["detached_window_mode"] = kip.detachedWindowMode;
        root["kip"] = k;
    }
    root["kip_panel_autostart"] = QJsonArray::fromStringList(data.kipPanelAutoStart);
    {
        QJsonObject i;
        i["python"] = data.interpreters.python;
        i["node"] = data.interpreters.node;
        i["php"] = data.interpreters.php;
        root["interpreters"] = i;
    }

    return writeJsonAtomic(settingsFilePath(), QJsonDocument(root));
}

} // namespace kai::core
