#include "core/config-manager.h"

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

namespace kai::core {

namespace {
constexpr const char *kLogTag = "ConfigManager";
constexpr const char *kCommandsFileName = "commands.json";
constexpr const char *kSettingsFileName = "settings.json";
constexpr const char *kCollectionsFileName = "collections.json";
constexpr const char *kDynamicVarsFileName = "dynamic-vars.json";
constexpr const char *kCollectionFiltersFileName = "collection-filters.json";

QString shellFlavorToString(ShellFlavor s)
{
    switch (s) {
        case ShellFlavor::Posix:      return QStringLiteral("posix");
        case ShellFlavor::PowerShell: return QStringLiteral("powershell");
        case ShellFlavor::Cmd:        return QStringLiteral("cmd");
        case ShellFlavor::Auto:       break;
    }
    return QStringLiteral("auto");
}

ShellFlavor shellFlavorFromString(const QString &s)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("posix"))      return ShellFlavor::Posix;
    if (v == QLatin1String("powershell")) return ShellFlavor::PowerShell;
    if (v == QLatin1String("cmd"))        return ShellFlavor::Cmd;
    return ShellFlavor::Auto;
}

// MIGRAÇÃO: alvos PowerShell antigos traziam a gambiarra `$l[2..(...)]` que
// descartava as 2 primeiras linhas do script para se livrar do prefixo bash
// (`set -m` + `echo $$`) que o Kai injetava indevidamente. Agora o prefixo é
// gerado em sintaxe PowerShell, então esse corte passaria a comer 2 linhas
// LEGÍTIMAS do script. Reescreve o template para decodificar e executar
// direto. Retorna true se migrou (para logar).
bool migratePowerShellTemplate(QString &tpl)
{
    if (!tpl.contains(QLatin1String("[2..")) || !tpl.toLower().contains(QLatin1String("powershell"))) {
        return false;
    }
    tpl = QStringLiteral(
        "cmd.exe /d /s /c powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
        "^\"Invoke-Expression([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{{command_b64}}')))^\"");
    return true;
}
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(configDirPath());
}

QString ConfigManager::configDirPath() const
{
    // QStandardPaths resolve para ~/.config/kai (Linux) e %APPDATA%/kai
    // (Windows) automaticamente, respeitando AppName definido em main.cpp.
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString ConfigManager::commandsFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kCommandsFileName));
}

QString ConfigManager::settingsFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kSettingsFileName));
}

QString ConfigManager::collectionsFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kCollectionsFileName));
}

QString ConfigManager::dynamicVarsFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kDynamicVarsFileName));
}

QMap<QString, QMap<QString, QString>> ConfigManager::loadPersistedDynamicVars()
{
    QMap<QString, QMap<QString, QString>> result;
    if (!QFile::exists(dynamicVarsFilePath())) {
        return result; // primeira execução / nada persistido ainda — normal.
    }
    const QJsonObject root = readJsonWithRecovery(dynamicVarsFilePath());
    for (auto scopeIt = root.constBegin(); scopeIt != root.constEnd(); ++scopeIt) {
        const QJsonObject scopeObj = scopeIt.value().toObject();
        QMap<QString, QString> vars;
        for (auto varIt = scopeObj.constBegin(); varIt != scopeObj.constEnd(); ++varIt) {
            vars[varIt.key()] = varIt.value().toString();
        }
        // "" (escopo Global) vira a chave literal "__global__" no JSON —
        // QJsonObject não aceita bem uma chave vazia em todo backend.
        const QString scopeKey = (scopeIt.key() == QLatin1String("__global__")) ? QString() : scopeIt.key();
        result[scopeKey] = vars;
    }
    return result;
}

bool ConfigManager::savePersistedDynamicVars(const QMap<QString, QMap<QString, QString>> &data)
{
    QJsonObject root;
    for (auto scopeIt = data.constBegin(); scopeIt != data.constEnd(); ++scopeIt) {
        QJsonObject scopeObj;
        for (auto varIt = scopeIt.value().constBegin(); varIt != scopeIt.value().constEnd(); ++varIt) {
            scopeObj[varIt.key()] = varIt.value();
        }
        const QString jsonKey = scopeIt.key().isEmpty() ? QStringLiteral("__global__") : scopeIt.key();
        root[jsonKey] = scopeObj;
    }
    return writeJsonAtomic(dynamicVarsFilePath(), QJsonDocument(root));
}

QString ConfigManager::collectionFiltersFilePath() const
{
    return QDir(configDirPath()).filePath(QString::fromLatin1(kCollectionFiltersFileName));
}

QMap<QString, CollectionFilterState> ConfigManager::loadCollectionFilters()
{
    QMap<QString, CollectionFilterState> result;
    if (!QFile::exists(collectionFiltersFilePath())) {
        return result; // primeira execução / nada persistido ainda — normal.
    }
    const QJsonObject root = readJsonWithRecovery(collectionFiltersFilePath());
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        CollectionFilterState state;
        state.search = obj.value(QStringLiteral("search")).toString();
        state.favoritesOnly = obj.value(QStringLiteral("favoritesOnly")).toBool();
        result[it.key()] = state;
    }
    return result;
}

bool ConfigManager::saveCollectionFilters(const QMap<QString, CollectionFilterState> &data)
{
    QJsonObject root;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        QJsonObject obj;
        obj[QStringLiteral("search")] = it.value().search;
        obj[QStringLiteral("favoritesOnly")] = it.value().favoritesOnly;
        root[it.key()] = obj;
    }
    return writeJsonAtomic(collectionFiltersFilePath(), QJsonDocument(root));
}

QString ConfigManager::backupCorruptedFile(const QString &filePath)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
    const QString backupPath = QStringLiteral("%1.bak.%2").arg(filePath, timestamp);

    if (QFile::copy(filePath, backupPath)) {
        return backupPath;
    }
    return QString();
}

QJsonObject ConfigManager::readJsonWithRecovery(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        // Primeira execução: não é corrupção, apenas ausência do arquivo.
        return QJsonObject();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Não foi possível abrir '%1' para leitura.").arg(filePath));
        return QJsonObject();
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // JSON corrompido: backup + fallback seguro + notificação.
        const QString backupPath = backupCorruptedFile(filePath);

        utils::Logger::error(kLogTag,
            QStringLiteral("Arquivo '%1' corrompido (%2). Backup criado em '%3'. "
                            "Restaurando configuração vazia.")
                .arg(filePath, parseError.errorString(), backupPath));

        emit configRecovered(filePath, backupPath);
        return QJsonObject();
    }

    return doc.object();
}

bool ConfigManager::writeJsonAtomic(const QString &filePath, const QJsonDocument &doc)
{
    // QSaveFile já implementa o padrão "grava em .tmp e faz rename atômico
    // ao commit", para evitar perda de dados.
    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        utils::Logger::error(kLogTag,
            QStringLiteral("Não foi possível abrir '%1' para escrita atômica.").arg(filePath));
        return false;
    }

    saveFile.write(doc.toJson(QJsonDocument::Indented));

    if (!saveFile.commit()) {
        utils::Logger::error(kLogTag,
            QStringLiteral("Falha ao efetivar escrita atômica em '%1'.").arg(filePath));
        return false;
    }

    return true;
}

CommandsData ConfigManager::loadCommands()
{
    const QJsonObject root = readJsonWithRecovery(commandsFilePath());

    CommandsData data;
    for (const QJsonValue &v : root.value("folders").toArray()) {
        data.folders << Folder::fromJson(v.toObject());
    }
    for (const QJsonValue &v : root.value("commands").toArray()) {
        data.commands << Command::fromJson(v.toObject());
    }

    // DEDUPLICAÇÃO DE IDs (bug real: o commands.json podia conter DOIS ou
    // mais comandos com o MESMO id — ex: criados por importação/versões
    // antigas antes da garantia de id único. No carregamento, o mapa
    // id->comando sobrescrevia e um comando "vencia" o outro de forma
    // imprevisível — sintoma: params/hooks "sumiam" ao executar). Aqui,
    // qualquer id repetido (entre comandos e pastas, que compartilham o
    // espaço de ids) recebe um sufixo numérico único, preservando TODOS os
    // itens em vez de perder silenciosamente.
    {
        QSet<QString> seen;
        auto uniquify = [&seen](const QString &id) -> QString {
            if (id.isEmpty() || !seen.contains(id)) {
                seen.insert(id);
                return id;
            }
            int n = 2;
            QString candidate = QStringLiteral("%1_%2").arg(id).arg(n);
            while (seen.contains(candidate)) {
                candidate = QStringLiteral("%1_%2").arg(id).arg(++n);
            }
            seen.insert(candidate);
            return candidate;
        };
        for (Folder &f : data.folders) {
            f.id = uniquify(f.id);
        }
        for (Command &c : data.commands) {
            c.id = uniquify(c.id);
        }
    }
    return data;
}

bool ConfigManager::saveCommands(const CommandsData &data)
{
    QJsonArray foldersArr;
    for (const Folder &f : data.folders) {
        foldersArr.append(f.toJson());
    }

    QJsonArray commandsArr;
    for (const Command &c : data.commands) {
        commandsArr.append(c.toJson());
    }

    QJsonObject root;
    root["folders"] = foldersArr;
    root["commands"] = commandsArr;

    const bool ok = writeJsonAtomic(commandsFilePath(), QJsonDocument(root));
    if (ok) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Comandos salvos: %1 pasta(s), %2 comando(s).")
                .arg(data.folders.size()).arg(data.commands.size()));
    }
    return ok;
}

QVector<Collection> ConfigManager::loadCollections()
{
    const QJsonObject root = readJsonWithRecovery(collectionsFilePath());
    QVector<Collection> collections;
    for (const QJsonValue &v : root.value("collections").toArray()) {
        collections << Collection::fromJson(v.toObject());
    }
    return collections;
}

bool ConfigManager::saveCollections(const QVector<Collection> &collections)
{
    QJsonArray arr;
    for (const Collection &c : collections) {
        arr.append(c.toJson());
    }
    QJsonObject root;
    root["collections"] = arr;

    const bool ok = writeJsonAtomic(collectionsFilePath(), QJsonDocument(root));
    if (ok) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Coleções salvas: %1 coleção(ões).").arg(collections.size()));
    }
    return ok;
}

SettingsData ConfigManager::loadSettings()
{
    const QJsonObject root = readJsonWithRecovery(settingsFilePath());

    SettingsData data;
    if (root.contains("active_theme")) {
        data.activeTheme = root.value("active_theme").toString();
    }
    // Aparência (repaginação visual): ausentes -> mantém o default do modelo.
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
    // Tamanhos do splitter externo (árvore+ações vs. Saída) lembrados entre
    // sessões — pedido do usuário: "o que eu salvei redimensionando fica".
    // Vazio (ausente/tamanho != 2) = usa os defaults calculados no boot.
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
    // Shortcuts Manager v2 (chave nova "shortcuts_v2" — não reaproveita
    // "action_shortcuts" de propósito: o formato do valor mudou de string
    // única para array, então uma instalação antiga nunca teria essa
    // chave, e uma v2 recém-salva não seria lida por engano por um Kai
    // mais antigo como se fosse o formato de string única).
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
        // MIGRAÇÃO (pedido do usuário: detectar o formato antigo e
        // converter automaticamente, preservando todos os atalhos
        // existentes, sem o resto do app precisar saber a origem) — mesmo
        // padrão de globalEnvVars->environments: sintetiza a estrutura
        // nova a partir da antiga, mantendo os campos legados intocados no
        // struct/disco (idempotente — não há "shortcuts_v2" ainda, então
        // isto roda de novo a cada carga até o usuário salvar as
        // Configurações uma vez, quando saveSettings passa a gravar
        // "shortcuts_v2" e esta migração para de ser necessária).
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
    // Opções de exibição da Saída (persistência global).
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
    if (root.contains("language")) {
        data.language = root.value("language").toString();
    }

    for (const QJsonValue &v : root.value("terminal_targets").toArray()) {
        const QJsonObject obj = v.toObject();
        TerminalProfile target;
        target.name = obj.value("name").toString();
        target.commandTemplate = obj.value("command_template").toString();
        // usePty: default true agora (terminal interativo). Alvos antigos que
        // NÃO gravaram a chave assumem o novo default.
        target.usePty = obj.value("use_pty").toBool(true);
        target.isDefault = obj.value("is_default").toBool(false);
        target.shell = shellFlavorFromString(obj.value("shell").toString());
        target.icon = obj.value("icon").toString();
        // Migração: remove a gambiarra $l[2..] de alvos PowerShell antigos.
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

    // Alvo "Default" (pseudo-alvo SEMPRE presente, pedido do usuário: "vou
    // precisar que exista um alvo de terminal fake chamado default, pra eu
    // poder aplicar algumas configs nele"): template VAZIO = execução
    // local direta, idêntica a não escolher nenhum alvo (mesmo
    // comportamento de sempre) — existe só pra dar um lugar de configurar
    // Saída/TTY sem precisar de nenhum wrapping (WSL, docker, etc.).
    // Auto-criado se ausente, em QUALQUER plataforma (diferente do seed do
    // WSL abaixo, que é só Windows e só quando a lista inteira está
    // vazia). Se o usuário apagar, reaparece no próximo load — mais
    // simples/robusto que impedir a exclusão na UI.
    {
        bool hasDefaultNamed = false;
        bool hasAnyMarkedDefault = false;
        for (const TerminalProfile &t : data.terminalProfiles) {
            if (t.name == QStringLiteral("Default")) { hasDefaultNamed = true; }
            if (t.isDefault) { hasAnyMarkedDefault = true; }
        }
        if (!hasDefaultNamed) {
            TerminalProfile def;
            def.name = QStringLiteral("Default");
            def.commandTemplate = QString(); // vazio = local direto, sem wrapping
            def.usePty = true;
            // Marcado como PADRÃO de cara (pedido do usuário: "o sistema já
            // cria um terminal padrão... e já vem marcado como padrão") —
            // só quando NENHUM outro perfil já está marcado, pra não roubar
            // a marcação de um perfil real que o usuário configurou.
            def.isDefault = !hasAnyMarkedDefault;
            data.terminalProfiles.prepend(def);
        }
    }

    const QJsonObject envObj = root.value("global_env_vars").toObject();
    for (auto it = envObj.constBegin(); it != envObj.constEnd(); ++it) {
        data.globalEnvVars[it.key()] = it.value().toString();
    }

#if defined(Q_OS_WIN)
    // Alvo "WSL" DEFAULT (feedback do usuário: "queria só um terminal, um
    // chamado wsl, que roda qualquer comando e flag"). Só é seedado no
    // Windows quando o usuário ainda não tem NENHUM alvo configurado.
    // É o "ultimate terminal": TTY ligado (roda como o Windows Terminal:
    // entra no WSL sob um terminal real, então read/docker -it/scripts
    // interativos funcionam) + injeção via base64+eval (imune a &&, aspas,
    // pipes — o comando é decodificado e executado no MESMO bash, cujo
    // stdin é o tty do ConPTY, então tudo funciona). Um alvo só, universal.
    if (data.terminalProfiles.isEmpty()) {
        TerminalProfile wsl;
        wsl.name = QStringLiteral("WSL");
        // O b64 vai como ARGUMENTO POSICIONAL ($1) do bash — assim o cmd.exe
        // não vê nenhum '|'/'<' solto (eles ficam dentro das aspas duplas
        // "$(...)" que o cmd respeita), evitando o erro "'base64' não é
        // reconhecido" (o cmd tentava rodar base64 no Windows). eval roda o
        // comando decodificado no MESMO bash (stdin = tty do ConPTY): read,
        // docker -it, &&, aspas, pipes — tudo funciona.
        // -i ALÉM do -l/-c (pergunta do usuário: "esse cmd considera meu
        // bashrc?"): -l sozinho é shell de LOGIN, não interativo — lê
        // ~/.profile (que só encadeia pro ~/.bashrc se o usuário não tiver
        // mexido nele), mas o .bashrc padrão do Ubuntu começa com uma
        // guarda que sai cedo em shells NÃO-interativos ("case $- in *i*)
        // ;; *) return;; esac") — sem -i, $- nunca tem 'i', e tudo depois
        // dessa guarda (aliases/funções customizados, ex: um "nav" no fim
        // do .bashrc) nunca chegava a carregar. Mesmo fix já aplicado no
        // ProcessRunner interno (setInteractiveShell) — aqui é o template
        // do Alvo de Terminal "WSL" default, template diferente, mesma
        // causa.
        wsl.commandTemplate = QStringLiteral(
            "wsl.exe -- bash -lic 'eval \"$(echo \"$1\" | base64 -d)\"' kai {{command_b64}}");
        wsl.usePty = true;
        wsl.isDefault = true; // já vira o padrão: comandos sem alvo usam o WSL
        data.terminalProfiles.append(wsl);
    }
#endif

    // Environments (pacotes selecionáveis). Desserializa a lista e o ativo.
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

    // MIGRAÇÃO suave: nenhum environment definido ainda. Cria um pacote
    // "Global" a partir do global_env_vars legado (mesmo se vazio, garante
    // que sempre exista ao menos um pacote selecionável).
    if (data.environments.isEmpty()) {
        Environment global;
        global.id = QStringLiteral("env_global");
        global.name = QStringLiteral("Global");
        global.vars = data.globalEnvVars;
        data.environments.append(global);
        data.activeEnvironmentId = global.id;
    }
    // Garante que o ativo aponte para um pacote existente.
    bool activeExists = false;
    for (const Environment &e : data.environments) {
        if (e.id == data.activeEnvironmentId) { activeExists = true; break; }
    }
    if (!activeExists) {
        data.activeEnvironmentId = data.environments.first().id;
    }

    // Notificações (ausentes -> mantém o default do modelo).
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
        // Shortcuts Manager v2: única fonte de verdade gravada a partir de
        // agora. Os campos/chave legados acima continuam gravados
        // (idempotente, nunca lidos de volta depois da 1ª migração) só
        // pra uma instalação mais antiga do Kai não perder a config se o
        // usuário abrir uma versão anterior por engano.
        QJsonObject v2;
        for (auto it = data.shortcuts.constBegin(); it != data.shortcuts.constEnd(); ++it) {
            QJsonArray seqs;
            for (const QString &s : it.value()) {
                seqs.append(s);
            }
            v2[it.key()] = seqs;
        }
        root["shortcuts_v2"] = v2;
    }
    root["show_hidden_commands"] = data.showHiddenCommands;
    root["command_creation_mode"] = data.commandCreationMode;
    root["command_edit_mode"] = data.commandEditMode;
    root["terminal_collapsed"] = data.terminalCollapsed;
    root["output_line_numbers"] = data.outputLineNumbers;
    root["output_wrap"] = data.outputWrap;
    root["output_timestamps"] = data.outputTimestamps;
    root["output_autoscroll"] = data.outputAutoScroll;
    root["output_compact"] = data.outputCompact;
    root["output_font_size"] = data.outputFontSize;
    root["output_max_log_size_kb"] = data.outputMaxLogSizeKb;
    root["graceful_stop_timeout_sec"] = data.gracefulStopTimeoutSec;
    root["autostart"] = data.autostart;
    root["language"] = data.language;
    root["global_env_vars"] = envObj;

    // Environments (pacotes selecionáveis) + o pacote ativo.
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

    root["notifications_enabled"] = data.notificationsEnabled;
    root["notify_on_command_failure"] = data.notifyOnCommandFailure;
    root["notify_on_background_crash"] = data.notifyOnBackgroundProcessCrash;
    root["notify_on_background_success"] = data.notifyOnBackgroundProcessSuccess;
    root["notify_on_config_recovered"] = data.notifyOnConfigRecovered;
    root["notify_on_first_error_in_formatted_output"] = data.notifyOnFirstErrorInFormattedOutput;
    root["notify_even_when_focused"] = data.notifyEvenWhenFocused;

    return writeJsonAtomic(settingsFilePath(), QJsonDocument(root));
}

bool ConfigManager::mergeImportResult(const ImportResult &result)
{
    if (!result.ok) {
        return false;
    }

    if (!result.commands.isEmpty() || !result.folders.isEmpty()) {
        CommandsData currentCmds = loadCommands();
        currentCmds.folders.append(result.folders);
        currentCmds.commands.append(result.commands);
        if (!saveCommands(currentCmds)) {
            return false;
        }
    }

    if (result.hasCollections && !result.collections.isEmpty()) {
        QVector<Collection> currentCols = loadCollections();
        currentCols.append(result.collections);
        if (!saveCollections(currentCols)) {
            return false;
        }
    }

    // AUDITORIA de import/export (achado real): este bloco só aplicava
    // activeTheme + terminalProfiles — os OUTROS 25+ campos que
    // importFromJson lê pacientemente do pacote (densidade, posicionamento,
    // janela, atalhos v2, efeitos visuais, preferências da Saída, hotkey
    // global, idioma...) eram descartados aqui, nunca chegando a
    // saveSettings(). "Importar Configurações Globais" na prática só
    // restaurava o tema. Corrigido para aplicar o snapshot inteiro; seguro
    // porque `hasSettings` só é true quando o pacote de fato TINHA a seção
    // de preferências gerais (ver detecção por "active_theme" em
    // importFromJson) — uma importação só-de-Environments não acorda este
    // bloco e não sobrescreve preferências não relacionadas.
    if (result.hasSettings) {
        SettingsData currentSettings = loadSettings();
        currentSettings.activeTheme = result.settings.activeTheme;
        currentSettings.uiDensity = result.settings.uiDensity;
        currentSettings.uiCornerStyle = result.settings.uiCornerStyle;
        currentSettings.treeConnectorStyle = result.settings.treeConnectorStyle;
        currentSettings.commandsBackgroundImage = result.settings.commandsBackgroundImage;
        currentSettings.commandsBackgroundOpacity = result.settings.commandsBackgroundOpacity;
        currentSettings.itemActionsPlacement = result.settings.itemActionsPlacement;
        currentSettings.displayActionsPlacement = result.settings.displayActionsPlacement;
        currentSettings.executionActionsPlacement = result.settings.executionActionsPlacement;
        currentSettings.outputPosition = result.settings.outputPosition;
        if (result.settings.outputSplitterSizes.size() == 2) {
            currentSettings.outputSplitterSizes = result.settings.outputSplitterSizes;
        }
        if (!result.settings.actionShortcuts.isEmpty()) {
            currentSettings.actionShortcuts = result.settings.actionShortcuts;
        }
        if (!result.settings.shortcuts.isEmpty()) {
            currentSettings.shortcuts = result.settings.shortcuts;
        }
        currentSettings.showHiddenCommands = result.settings.showHiddenCommands;
        currentSettings.fxShadows = result.settings.fxShadows;
        currentSettings.fxTranslucency = result.settings.fxTranslucency;
        currentSettings.fxBlur = result.settings.fxBlur;
        currentSettings.fxAnimations = result.settings.fxAnimations;
        currentSettings.gradientsEnabled = result.settings.gradientsEnabled;
        currentSettings.autoHideOnFocusLoss = result.settings.autoHideOnFocusLoss;
        currentSettings.windowMode = result.settings.windowMode;
        currentSettings.windowWidth = result.settings.windowWidth;
        currentSettings.windowHeight = result.settings.windowHeight;
        currentSettings.globalHotkey = result.settings.globalHotkey;
        currentSettings.startVisible = result.settings.startVisible;
        currentSettings.language = result.settings.language;
        currentSettings.commandCreationMode = result.settings.commandCreationMode;
        currentSettings.commandEditMode = result.settings.commandEditMode;
        currentSettings.terminalCollapsed = result.settings.terminalCollapsed;
        currentSettings.outputLineNumbers = result.settings.outputLineNumbers;
        currentSettings.outputWrap = result.settings.outputWrap;
        currentSettings.outputTimestamps = result.settings.outputTimestamps;
        currentSettings.outputAutoScroll = result.settings.outputAutoScroll;
        currentSettings.outputCompact = result.settings.outputCompact;
        currentSettings.outputFontSize = result.settings.outputFontSize;
        currentSettings.outputMaxLogSizeKb = result.settings.outputMaxLogSizeKb;
        currentSettings.gracefulStopTimeoutSec = result.settings.gracefulStopTimeoutSec;
        // global_env_vars é legado (pré-Environments) — mesclado (não
        // sobrescrito) para não apagar chaves locais que o pacote importado
        // não conhecia.
        for (auto it = result.settings.globalEnvVars.constBegin(); it != result.settings.globalEnvVars.constEnd(); ++it) {
            currentSettings.globalEnvVars[it.key()] = it.value();
        }
        currentSettings.terminalProfiles.append(result.settings.terminalProfiles);
        saveSettings(currentSettings);
    }

    // ENVIRONMENTS: seção independente de hasSettings (ver comentário no
    // header) — MESCLA por id (substitui se já existir um pacote com o
    // mesmo id, senão adiciona), em vez de sobrescrever a lista inteira,
    // pra reimportar um backup não apagar Environments criados localmente
    // depois daquele backup.
    if (result.hasEnvironments) {
        SettingsData currentSettings = loadSettings();
        for (const Environment &imported : result.settings.environments) {
            auto it = std::find_if(currentSettings.environments.begin(), currentSettings.environments.end(),
                [&imported](const Environment &existing) { return existing.id == imported.id; });
            if (it != currentSettings.environments.end()) {
                *it = imported;
            } else {
                currentSettings.environments.append(imported);
            }
        }
        if (!result.settings.activeEnvironmentId.isEmpty()) {
            currentSettings.activeEnvironmentId = result.settings.activeEnvironmentId;
        }
        saveSettings(currentSettings);
    }

    return true;
}

namespace {
// Remove os valores de campos marcados "secret" de todas as entries de
// UMA coleção, antes de serializar para export — usado em TODO caminho de
// export que possa incluir dados de entries (global, por pasta, por
// comando), não só o global/lean. Ver CollectionField::secret.
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

// Coleta uma pasta, todas as suas subpastas descendentes e todos os
// comandos pertencentes a qualquer uma delas (usado no export por pasta).
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
    // Comandos cujo folderId é uma das pastas do subtree OU um comando já
    // incluído (agrupamento comando-em-comando). Faz duas passadas para
    // pegar comandos aninhados sob comandos incluídos.
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

// Declaradas aqui, definidas mais abaixo (perto de appendTerminalProfilesSection)
// — precisam ser visíveis já em exportSelective/exportGlobal, que vêm antes.
QJsonObject stripIdsFromExport(QJsonObject root, const QString &exportRootFolderId);
}


QString ConfigManager::exportSelective(const ExportSelection &selection,
                                       const SettingsData &settings,
                                       const CommandsData &commands,
                                       const QVector<Collection> &collections,
                                       bool lean)
{
    // EXPORTAÇÃO SELETIVA: reutiliza o mesmo formato do escopo "global" (para o
    // importFromJson existente continuar entendendo), mas inclui APENAS as
    // seções marcadas — e passa a incluir COLEÇÕES, que a exportação global
    // simplesmente ignorava (backup nunca era completo).
    QJsonObject root;
    root["kai_export"] = exportHeader(QStringLiteral("global"));

    if (selection.settings || selection.environments || selection.terminalProfiles) {
        // Parte do JSON de settings vem do export global; aqui montamos só o
        // que foi pedido para não vazar seções não selecionadas. As três
        // seções (preferências gerais, environments, alvos de terminal) são
        // independentes entre si — cada uma pode ser exportada sozinha.
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
        } else {
            // `settings` (preferências gerais) não marcado: monta um objeto
            // enxuto só com o que foi pedido, sem vazar tema/janela/atalhos.
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
        }
        root["settings"] = settingsObj;
    }

    if (selection.commands) {
        QJsonArray foldersArr;
        for (const Folder &f : commands.folders) {
            foldersArr.append(f.toJson());
        }
        QJsonArray commandsArr;
        for (const Command &cmd : commands.commands) {
            commandsArr.append(cmd.toJson());
        }
        root["folders"] = foldersArr;
        root["commands"] = commandsArr;
    }

    if (selection.collections) {
        QJsonArray collectionsArr;
        for (const Collection &col : collections) {
            // Sem "dados de coleção": exporta a ESTRUTURA (nome/pasta/schema) e
            // zera as entries, permitindo levar o formato sem os registros.
            if (selection.collectionEntries) {
                // Campos marcados "secret" NUNCA saem no export, mesmo com
                // "incluir dados das entries" ligado — ver
                // redactSecretFieldsForExport.
                collectionsArr.append(redactSecretFieldsForExport(col).toJson());
            } else {
                Collection structureOnly = col;
                structureOnly.entries.clear();
                collectionsArr.append(structureOnly.toJson());
            }
        }
        root["collections"] = collectionsArr;
    }

    // Sem "raiz excluída" aqui (escopo global/seletivo não tem uma única
    // pasta "topo") — todo mundo mantém o path completo desde a raiz de
    // verdade.
    if (lean) {
        root = stripIdsFromExport(root, QString());
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString ConfigManager::exportGlobal(const SettingsData &settings, const CommandsData &commands)
{
    QJsonObject root;
    root["kai_export"] = exportHeader(QStringLiteral("global"));

    // Settings (reaproveita o mesmo formato de saveSettings, sem escrever
    // em disco — serializa inline).
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
    // AUDITORIA de import/export: estes campos existem em SettingsData e são
    // gravados normalmente por saveSettings() (settings.json), mas o export
    // "Configurações Globais" nunca os incluía — um backup completo não
    // voltava completo (perdia modo de criação/edição, estado do painel de
    // Saída, e preferências de exibição da Saída).
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

    // ENVIRONMENTS (pacotes selecionáveis + segredos): faltavam por completo
    // no export global — "Exportar Configurações Globais" achando que era um
    // backup completo na verdade perdia todos os pacotes de ambiente
    // configurados (bug real, confirmado em auditoria: exportSelective com
    // `selection.environments` tentava ler esta chave daqui e sempre achava
    // vazio). Mesmo formato de saveSettings().
    {
        QJsonArray envsArr;
        for (const Environment &e : settings.environments) {
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
        settingsObj["environments"] = envsArr;
        settingsObj["active_environment_id"] = settings.activeEnvironmentId;
    }

    root["settings"] = settingsObj;

    QJsonArray foldersArr;
    for (const Folder &f : commands.folders) {
        foldersArr.append(f.toJson());
    }
    QJsonArray commandsArr;
    for (const Command &c : commands.commands) {
        commandsArr.append(c.toJson());
    }
    root["folders"] = foldersArr;
    root["commands"] = commandsArr;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

namespace {
// Mesmo formato de settingsObj["terminal_targets"] do export global (ver
// mais abaixo) — reaproveitado aqui pra export de pasta/comando poder
// "levar alvos juntos (como no global)" sem duplicar o parser de import
// (que já lê "settings.terminal_targets" independente do scope do pacote).
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

// SEM IDS no export (pedido do usuário: "IDs tbm não devem ter no
// export/import, visto que o APP deve gerar em runtime, certo?" —
// confirmado explicitamente, aceitando a troca: reimportar o MESMO
// arquivo passa a sempre ADICIONAR cópias novas, não atualizar no lugar —
// não há mais id estável pra decidir "isto já existe"). Mesmo espírito
// já usado pelo kai.json de projeto (ver ProjectSelector/manifesto §11):
// pasta por CAMINHO de nomes ("folder": "A/B"), hooks pelo NOME do
// comando, parâmetro Select pelo NOME da coleção — nada de id cru vazando
// pro arquivo que o usuário lê/edita/versiona.
//
// Passo final de toda função exportXxx, aplicado no QJsonObject já
// pronto (id-based, o formato interno de sempre) — reescreve só as
// referências cruzadas, sem duplicar a lógica de construção de cada
// seção. `exportRootFolderId`: para export de PASTA, o id da própria
// pasta exportada (ela vira o "topo" implícito do pacote, path == "",
// e não entra na lista de pastas — mesmo papel de project_name no
// kai.json de projeto); vazio para export global/seletivo (não há pasta
// "raiz" alguma sendo excluída, todo mundo mantém o path completo).
QJsonObject stripIdsFromExport(QJsonObject root, const QString &exportRootFolderId)
{
    const QJsonArray foldersArr = root.value(QStringLiteral("folders")).toArray();
    const QJsonArray commandsArr = root.value(QStringLiteral("commands")).toArray();
    const bool hasCollectionsKey = root.contains(QStringLiteral("collections"));
    const QJsonArray collectionsArr = root.value(QStringLiteral("collections")).toArray();

    QMap<QString, QJsonObject> foldersById;
    for (const QJsonValue &v : foldersArr) {
        const QJsonObject f = v.toObject();
        foldersById.insert(f.value(QStringLiteral("id")).toString(), f);
    }
    QMap<QString, QString> commandNameById;
    for (const QJsonValue &v : commandsArr) {
        const QJsonObject c = v.toObject();
        commandNameById.insert(c.value(QStringLiteral("id")).toString(), c.value(QStringLiteral("name")).toString());
    }
    QMap<QString, QString> collectionNameById;
    for (const QJsonValue &v : collectionsArr) {
        const QJsonObject c = v.toObject();
        collectionNameById.insert(c.value(QStringLiteral("id")).toString(), c.value(QStringLiteral("name")).toString());
    }

    // Caminho (nomes separados por "/") de `folderId` até — mas SEM
    // incluir — `exportRootFolderId`. Cadeia quebrada (parent_id fora do
    // pacote, dados corrompidos) simplesmente para onde a cadeia acaba;
    // nunca trava.
    std::function<QString(const QString &)> pathFor = [&](const QString &folderId) -> QString {
        if (folderId.isEmpty() || folderId == exportRootFolderId || !foldersById.contains(folderId)) {
            return QString();
        }
        QStringList parts;
        QString cur = folderId;
        QSet<QString> visited;
        while (foldersById.contains(cur) && cur != exportRootFolderId && !visited.contains(cur)) {
            visited.insert(cur);
            const QJsonObject &f = foldersById.value(cur);
            parts.prepend(f.value(QStringLiteral("name")).toString());
            const QJsonValue parentVal = f.value(QStringLiteral("parent_id"));
            if (!parentVal.isString()) {
                break;
            }
            cur = parentVal.toString();
        }
        return parts.join(QStringLiteral("/"));
    };

    QJsonArray newFolders;
    for (const QJsonValue &v : foldersArr) {
        QJsonObject f = v.toObject();
        const QString id = f.value(QStringLiteral("id")).toString();
        if (!exportRootFolderId.isEmpty() && id == exportRootFolderId) {
            // A própria pasta exportada não entra na lista de FILHAS — é
            // o topo do pacote — mas seu NOME/ícone/etc não podem se
            // perder: sem isto, um comando direto na raiz exportada
            // ficava com folder_id VAZIO na reimportação (achado real,
            // testado: "revise as outras cfg, todas devem ter importar
            // se ter" — um campo simplesmente sumia sem aviso). Vira um
            // campo à parte "root_folder", igual a project_name faz pro
            // kai.json de projeto.
            QJsonObject rootMeta = f;
            rootMeta.remove(QStringLiteral("id"));
            rootMeta.remove(QStringLiteral("parent_id"));
            root.insert(QStringLiteral("root_folder"), rootMeta);
            continue;
        }
        const QString path = pathFor(id);
        f.remove(QStringLiteral("id"));
        f.remove(QStringLiteral("parent_id"));
        f.insert(QStringLiteral("path"), path);
        newFolders.append(f);
    }

    QJsonArray newCommands;
    for (const QJsonValue &v : commandsArr) {
        QJsonObject c = v.toObject();
        const QString path = pathFor(c.value(QStringLiteral("folder_id")).toString());
        c.remove(QStringLiteral("id"));
        c.remove(QStringLiteral("folder_id"));
        if (!path.isEmpty()) {
            c.insert(QStringLiteral("folder"), path);
        }

        if (c.value(QStringLiteral("hooks")).isObject()) {
            QJsonObject hooksObj = c.value(QStringLiteral("hooks")).toObject();
            for (const char *key : {"pre", "post", "cleanup"}) {
                const QJsonArray idsArr = hooksObj.value(QLatin1String(key)).toArray();
                if (idsArr.isEmpty()) {
                    continue;
                }
                QJsonArray namesArr;
                for (const QJsonValue &idv : idsArr) {
                    namesArr.append(commandNameById.value(idv.toString(), idv.toString()));
                }
                hooksObj.insert(QLatin1String(key), namesArr);
            }
            c.insert(QStringLiteral("hooks"), hooksObj);
        }

        if (c.value(QStringLiteral("params")).isArray()) {
            QJsonArray newParams;
            for (const QJsonValue &pv : c.value(QStringLiteral("params")).toArray()) {
                QJsonObject p = pv.toObject();
                if (p.contains(QStringLiteral("collection_id"))) {
                    const QString colId = p.value(QStringLiteral("collection_id")).toString();
                    p.remove(QStringLiteral("collection_id"));
                    p.insert(QStringLiteral("collection"), collectionNameById.value(colId, colId));
                }
                newParams.append(p);
            }
            c.insert(QStringLiteral("params"), newParams);
        }
        newCommands.append(c);
    }

    QJsonArray newCollections;
    for (const QJsonValue &v : collectionsArr) {
        QJsonObject c = v.toObject();
        const QString path = pathFor(c.value(QStringLiteral("folder_id")).toString());
        c.remove(QStringLiteral("id"));
        c.remove(QStringLiteral("folder_id"));
        if (!path.isEmpty()) {
            c.insert(QStringLiteral("folder"), path);
        }
        // FALTAVA: id de cada ENTRY (achado numa auditoria mais ampla —
        // "queria bem enxuto os arquivos", "IDs não deveriam ter no
        // export/import" — CollectionEntry::toJson escreve "id" sempre,
        // incondicional, e stripIdsFromExport nunca olhava dentro de
        // "entries" pra removê-lo; um export "enxuto" ainda vazava um id
        // por linha da coleção). Seguro remover: Collection::fromJson já
        // gera um QUuid novo pra qualquer entry sem "id" na releitura.
        if (c.value(QStringLiteral("entries")).isArray()) {
            QJsonArray strippedEntries;
            for (const QJsonValue &ev : c.value(QStringLiteral("entries")).toArray()) {
                QJsonObject e = ev.toObject();
                e.remove(QStringLiteral("id"));
                strippedEntries.append(e);
            }
            c.insert(QStringLiteral("entries"), strippedEntries);
        }
        newCollections.append(c);
    }

    // Só reescreve a chave se ela já existia (pedido do usuário: "quero
    // BEM enxuto os arquivos" — um export com "commands" desmarcado não
    // deveria ganhar um "commands": [] à toa).
    if (root.contains(QStringLiteral("folders"))) {
        root.insert(QStringLiteral("folders"), newFolders);
    }
    if (root.contains(QStringLiteral("commands"))) {
        root.insert(QStringLiteral("commands"), newCommands);
    }
    if (hasCollectionsKey) {
        root.insert(QStringLiteral("collections"), newCollections);
    }
    QJsonObject header = root.value(QStringLiteral("kai_export")).toObject();
    // Marca o pacote como "sem ids" — decide, na importação, qual dos dois
    // caminhos rodar (ver resolveIdFreeImport). Detecção estrutural
    // (ausência de "id" no primeiro item) também vale como fallback, pra
    // um kai.yml escrito à mão sem essa chave "funcionar" igual.
    header.insert(QStringLiteral("id_free"), true);
    root.insert(QStringLiteral("kai_export"), header);
    return root;
}

// Caminho inverso de stripIdsFromExport — roda ANTES do parsing normal
// (Folder::fromJson/Command::fromJson/Collection::fromJson) em
// importFromJson, reescrevendo o "path"/"folder"/hooks-por-nome/
// collection-por-nome de volta para id/folder_id/hooks-por-id/
// collection_id recém-GERADOS. Depois disto rodar, o resto de
// importFromJson não muda NADA — ele já espera exatamente esse formato
// id-based de sempre.
QJsonObject resolveIdFreeImport(QJsonObject root)
{
    const QJsonArray foldersArr = root.value(QStringLiteral("folders")).toArray();
    const QJsonArray commandsArr = root.value(QStringLiteral("commands")).toArray();
    const bool hasCollectionsKey = root.contains(QStringLiteral("collections"));
    const QJsonArray collectionsArr = root.value(QStringLiteral("collections")).toArray();

    QMap<QString, QJsonObject> explicitByPath;
    for (const QJsonValue &v : foldersArr) {
        const QJsonObject f = v.toObject();
        explicitByPath.insert(f.value(QStringLiteral("path")).toString(), f);
    }

    QJsonArray newFolders;
    auto generateFolderId = []() {
        return QStringLiteral("f_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    };

    // A PRÓPRIA pasta exportada (export de escopo Pasta) — pedido/achado
    // real: "revise as outras cfg, todas devem ter importar se ter" — um
    // comando direto na raiz exportada ficava com folder_id VAZIO na
    // reimportação, já que o nome/ícone da raiz nunca eram salvos em
    // lugar nenhum (mesmo bug que já foi corrigido uma vez pro
    // capture_env, agora achado aqui). "root_folder" (ver
    // stripIdsFromExport) guarda essa identidade; recria a pasta ANTES de
    // qualquer outra resolução, pra path=="" apontar pra ela em vez de
    // "nenhuma pasta".
    QString rootFolderId;
    if (root.value(QStringLiteral("root_folder")).isObject()) {
        QJsonObject rf = root.value(QStringLiteral("root_folder")).toObject();
        rootFolderId = generateFolderId();
        rf.insert(QStringLiteral("id"), rootFolderId);
        newFolders.append(rf);
    }

    // Resolução de "folder"/"path" -> id, criando pastas intermediárias sob
    // demanda — COMPARTILHADA com o Import de Projeto (ver
    // core::FolderPathResolver e o comentário na classe: as duas
    // implementações divergiam em detalhe antes desta extração).
    core::FolderPathResolver resolver(rootFolderId, generateFolderId, explicitByPath);
    std::function<QString(const QString &)> ensurePath = [&](const QString &path) -> QString {
        return resolver.resolve(path, [&](const QString &id, const QString &name,
                                           const QString &parentId, const QString &p) {
            QJsonObject f = resolver.explicitMetadataFor(p); // pasta "implícita" (só do meio-do-caminho) fica com objeto vazio — ok, fromJson cai em defaults
            f.insert(QStringLiteral("id"), id);
            f.insert(QStringLiteral("name"), name);
            if (!parentId.isEmpty()) {
                f.insert(QStringLiteral("parent_id"), parentId);
            }
            f.remove(QStringLiteral("path"));
            newFolders.append(f);
        });
    };
    // Garante TODAS as pastas declaradas explicitamente, mesmo uma sem
    // nenhum comando/coleção diretamente nela (só subpastas).
    for (const QString &path : resolver.explicitPaths()) {
        ensurePath(path);
    }

    // ids de comando/coleção pré-gerados numa passada única (hooks/params
    // referenciam por NOME e precisam resolver pra um id que já existe).
    QVector<QString> commandIds;
    QMap<QString, QString> commandIdByName;
    for (const QJsonValue &v : commandsArr) {
        const QString newId = QStringLiteral("c_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
        commandIds.append(newId);
        const QString name = v.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && !commandIdByName.contains(name)) {
            commandIdByName.insert(name, newId);
        }
    }
    QVector<QString> collectionIds;
    QMap<QString, QString> collectionIdByName;
    for (const QJsonValue &v : collectionsArr) {
        const QString newId = QStringLiteral("col_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
        collectionIds.append(newId);
        const QString name = v.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && !collectionIdByName.contains(name)) {
            collectionIdByName.insert(name, newId);
        }
    }

    QJsonArray newCommands;
    for (int i = 0; i < commandsArr.size(); ++i) {
        QJsonObject c = commandsArr.at(i).toObject();
        c.insert(QStringLiteral("id"), commandIds.at(i));
        const QString path = c.value(QStringLiteral("folder")).toString();
        c.remove(QStringLiteral("folder"));
        const QString folderId = ensurePath(path);
        if (!folderId.isEmpty()) {
            c.insert(QStringLiteral("folder_id"), folderId);
        }

        if (c.value(QStringLiteral("hooks")).isObject()) {
            QJsonObject hooksObj = c.value(QStringLiteral("hooks")).toObject();
            for (const char *key : {"pre", "post", "cleanup"}) {
                const QJsonArray namesArr = hooksObj.value(QLatin1String(key)).toArray();
                if (namesArr.isEmpty()) {
                    continue;
                }
                QJsonArray idsArr;
                for (const QJsonValue &nv : namesArr) {
                    // Nome sem comando correspondente NO MESMO pacote: ignorado
                    // (mesma regra do import de projeto — ver manifesto §9).
                    const QString resolved = commandIdByName.value(nv.toString());
                    if (!resolved.isEmpty()) {
                        idsArr.append(resolved);
                    }
                }
                hooksObj.insert(QLatin1String(key), idsArr);
            }
            c.insert(QStringLiteral("hooks"), hooksObj);
        }

        if (c.value(QStringLiteral("params")).isArray()) {
            QJsonArray newParams;
            for (const QJsonValue &pv : c.value(QStringLiteral("params")).toArray()) {
                QJsonObject p = pv.toObject();
                if (p.contains(QStringLiteral("collection"))) {
                    const QString colName = p.value(QStringLiteral("collection")).toString();
                    p.remove(QStringLiteral("collection"));
                    const QString resolved = collectionIdByName.value(colName);
                    if (!resolved.isEmpty()) {
                        p.insert(QStringLiteral("collection_id"), resolved);
                    }
                }
                newParams.append(p);
            }
            c.insert(QStringLiteral("params"), newParams);
        }
        newCommands.append(c);
    }

    QJsonArray newCollections;
    for (int i = 0; i < collectionsArr.size(); ++i) {
        QJsonObject c = collectionsArr.at(i).toObject();
        c.insert(QStringLiteral("id"), collectionIds.at(i));
        const QString path = c.value(QStringLiteral("folder")).toString();
        c.remove(QStringLiteral("folder"));
        const QString folderId = ensurePath(path);
        if (!folderId.isEmpty()) {
            c.insert(QStringLiteral("folder_id"), folderId);
        }
        newCollections.append(c);
    }

    root.insert(QStringLiteral("folders"), newFolders);
    root.insert(QStringLiteral("commands"), newCommands);
    if (hasCollectionsKey) {
        root.insert(QStringLiteral("collections"), newCollections);
    }
    return root;
}

// Detecta um pacote SEM ids — a marca explícita (id_free no cabeçalho,
// ver stripIdsFromExport) OU, na ausência dela (kai.yml escrito à mão,
// nunca passou pelo export do Kai), a AUSÊNCIA estrutural de "id" no
// primeiro item de folders/commands.
bool looksIdFree(const QJsonObject &root)
{
    const QJsonObject header = root.value(QStringLiteral("kai_export")).toObject();
    if (header.value(QStringLiteral("id_free")).toBool(false)) {
        return true;
    }
    const QJsonArray foldersArr = root.value(QStringLiteral("folders")).toArray();
    if (!foldersArr.isEmpty()) {
        return !foldersArr.first().toObject().contains(QStringLiteral("id"));
    }
    const QJsonArray commandsArr = root.value(QStringLiteral("commands")).toArray();
    if (!commandsArr.isEmpty()) {
        return !commandsArr.first().toObject().contains(QStringLiteral("id"));
    }
    return false;
}
} // namespace

QString ConfigManager::exportFolder(const QString &folderId, const CommandsData &commands,
                                    const QVector<Collection> &linkedCollections,
                                    const QVector<TerminalProfile> &terminalProfiles,
                                    bool lean)
{
    QVector<Folder> folders;
    QVector<Command> cmds;
    collectFolderSubtree(folderId, commands, folders, cmds);

    QJsonObject root;
    root["kai_export"] = exportHeader(QStringLiteral("folder"));
    QJsonArray foldersArr;
    for (const Folder &f : folders) {
        foldersArr.append(f.toJson());
    }
    QJsonArray commandsArr;
    for (const Command &c : cmds) {
        commandsArr.append(c.toJson());
    }
    root["folders"] = foldersArr;
    root["commands"] = commandsArr;
    if (!linkedCollections.isEmpty()) {
        QJsonArray collectionsArr;
        for (const Collection &col : linkedCollections) {
            collectionsArr.append(redactSecretFieldsForExport(col).toJson());
        }
        root["collections"] = collectionsArr;
    }
    appendTerminalProfilesSection(root, terminalProfiles);
    // A própria pasta exportada (`folderId`) é o topo implícito do pacote
    // — path == "", não entra na lista de pastas (mesmo papel do
    // project_name no kai.json de projeto).
    if (lean) {
        root = stripIdsFromExport(root, folderId);
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
            commandsArr.append(c.toJson());
            break;
        }
    }
    root["commands"] = commandsArr;
    if (!linkedCollections.isEmpty()) {
        QJsonArray collectionsArr;
        for (const Collection &col : linkedCollections) {
            collectionsArr.append(redactSecretFieldsForExport(col).toJson());
        }
        root["collections"] = collectionsArr;
    }
    appendTerminalProfilesSection(root, terminalProfiles);
    if (lean) {
        root = stripIdsFromExport(root, QString());
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

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

    // Pacote SEM ids (pedido do usuário: "IDs tbm não devem ter no
    // export/import, visto que o APP deve gerar em runtime") — resolve
    // path->folder_id, nome->hook id, nome->collection_id ANTES do parsing
    // normal abaixo, que continua exatamente igual (só entende o formato
    // id-based de sempre). Ids são gerados AQUI, frescos, a cada import —
    // reimportar o mesmo arquivo sempre adiciona cópias novas, nunca
    // atualiza no lugar (sem id estável pra reconhecer "isto já existe").
    const QJsonObject normalizedRoot = looksIdFree(root) ? resolveIdFreeImport(root) : root;

    for (const QJsonValue &v : normalizedRoot.value("folders").toArray()) {
        result.folders.append(Folder::fromJson(v.toObject()));
    }
    // COLEÇÕES no pacote: a importação antiga as ignorava, então um export
    // "completo" não voltava completo.
    if (normalizedRoot.contains("collections")) {
        for (const QJsonValue &v : normalizedRoot.value("collections").toArray()) {
            result.collections.append(Collection::fromJson(v.toObject()));
        }
        result.hasCollections = !result.collections.isEmpty();
    }
    for (const QJsonValue &v : normalizedRoot.value("commands").toArray()) {
        result.commands.append(Command::fromJson(v.toObject()));
    }

    if (root.contains("settings") && root.value("settings").isObject()) {
        const QJsonObject s = root.value("settings").toObject();
        // Ver comentário de hasSettings/hasEnvironments no header: os dois
        // podem vir independentemente (exportSelective com só um dos dois
        // marcado), então cada um é detectado pela presença da sua própria
        // chave-marco, não por "o objeto settings existe".
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
            // AUDITORIA: faltavam usePty/isDefault/shell/icon — exportGlobal
            // grava os 4, mas só name/command_template voltavam na
            // importação; um alvo de terminal reimportado perdia o sabor de
            // shell, virava "roda sob PTY" incondicionalmente e nunca era
            // reconhecido como o alvo padrão.
            t.usePty = obj.value("use_pty").toBool(true);
            t.isDefault = obj.value("is_default").toBool(false);
            t.shell = shellFlavorFromString(obj.value("shell").toString());
            t.icon = obj.value("icon").toString();
            if (!t.name.isEmpty()) {
                result.settings.terminalProfiles.append(t);
            }
        }

        // AUDITORIA: faltava por completo — export global perdia todos os
        // Environments (pacotes selecionáveis + segredos) num "backup
        // completo" (ver comentário de hasEnvironments no header e o bloco
        // espelhado adicionado em exportGlobal()).
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
