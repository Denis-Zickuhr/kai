

#include "utils/translation-manager.h"

#include "core/config-manager-internal.h"

namespace kai::core {

using namespace detail;

int ConfigManager::applyImportedActions(const ImportResult &result, QVector<Folder> &folders,
                                        const QVector<Command> &allCommands, QVector<GlobalAction> &globalActions)
{
    int unresolved = 0;
    // Por NOME, e só quando é único: dois comandos com o mesmo nome são
    // ambíguos e é melhor não ligar nenhum do que ligar o errado.
    auto idByUniqueName = [&allCommands](const QString &name) {
        QString found;
        int matches = 0;
        for (const Command &c : allCommands) {
            if (c.name == name) {
                found = c.id;
                ++matches;
            }
        }
        return matches == 1 ? found : QString();
    };

    for (auto it = result.pendingFolderActions.constBegin(); it != result.pendingFolderActions.constEnd(); ++it) {
        for (Folder &f : folders) {
            if (f.id != it.key()) {
                continue;
            }
            const QStringList expansionNames = result.pendingFolderExpansion.value(it.key());
            for (const QString &name : it.value()) {
                const QString id = idByUniqueName(name);
                if (id.isEmpty()) {
                    ++unresolved;
                } else if (!f.actions.contains(id)) {
                    f.actions.append(id);
                    if (expansionNames.contains(name) && !f.expansionActions.contains(id)) {
                        f.expansionActions.append(id);
                    }
                    const QString group = result.pendingFolderGroups.value(it.key()).value(name);
                    if (!group.isEmpty()) {
                        f.actionGroups.insert(id, group);
                    }
                }
            }
            break;
        }
    }

    auto hasGlobal = [&globalActions](const QString &commandId) {
        return std::any_of(globalActions.cbegin(), globalActions.cend(),
                           [&commandId](const GlobalAction &g) { return g.commandId == commandId; });
    };
    if (result.hasGlobalActions) {
        for (const GlobalAction &g : result.settings.globalActions) {
            if (!hasGlobal(g.commandId)) {
                globalActions.append(g);
            }
        }
        for (const ImportResult::PendingAction &p : result.pendingGlobalActions) {
            const QString id = idByUniqueName(p.name);
            if (id.isEmpty()) {
                ++unresolved;
            } else if (!hasGlobal(id)) {
                globalActions.append({id, p.onlyProjects, p.expansion, p.group, p.groupIcon});
            }
        }
    }
    return unresolved;
}

QVector<Command> ConfigManager::actionCommandsToImport(const ImportResult &result, const QVector<Command> &existing)
{
    QVector<Command> toImport;
    for (const Command &candidate : result.actionCommands) {
        const bool present = std::any_of(existing.cbegin(), existing.cend(), [&candidate](const Command &c) {
            return c.name == candidate.name || (!candidate.id.isEmpty() && c.id == candidate.id);
        });
        const bool queued = std::any_of(toImport.cbegin(), toImport.cend(),
                                        [&candidate](const Command &c) { return c.name == candidate.name; });
        if (!present && !queued) {
            toImport.append(candidate);
        }
    }
    return toImport;
}

void ConfigManager::appendImportedActionCommands(const QVector<Command> &toImport, CommandsData &data)
{
    if (toImport.isEmpty()) {
        return;
    }
    const QString folderName = utils::tr(QStringLiteral("import.actions_folder"));
    QString folderId;
    for (const Folder &f : std::as_const(data.folders)) {
        if (!f.parentId.has_value() && f.name == folderName) {
            folderId = f.id;
            break;
        }
    }
    if (folderId.isEmpty()) {
        Folder folder;
        folder.id = QStringLiteral("f_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
        folder.name = folderName;
        folder.icon = QStringLiteral("zap");
        folderId = folder.id;
        data.folders.append(folder);
    }
    for (Command command : toImport) {
        command.folderId = folderId;
        data.commands.append(command);
    }
}

bool ConfigManager::mergeImportResult(const ImportResult &result)
{
    if (!result.ok) {
        return false;
    }

    const bool hasActionsToApply = result.hasGlobalActions || !result.pendingFolderActions.isEmpty()
                                   || !result.actionCommands.isEmpty();
    if (!result.commands.isEmpty() || !result.folders.isEmpty() || hasActionsToApply) {
        CommandsData currentCmds = loadCommands();
        currentCmds.folders.append(result.folders);
        currentCmds.commands.append(result.commands);
        appendImportedActionCommands(actionCommandsToImport(result, currentCmds.commands), currentCmds);
        // Ações: casa as referências por nome contra TODOS os comandos (os
        // recém-importados e os que já existiam) e junta as globais.
        QVector<GlobalAction> globalActions;
        if (result.hasGlobalActions) {
            globalActions = loadSettings().globalActions;
        }
        applyImportedActions(result, currentCmds.folders, currentCmds.commands, globalActions);
        if (!saveCommands(currentCmds)) {
            return false;
        }
        if (result.hasGlobalActions) {
            SettingsData currentSettings = loadSettings();
            currentSettings.globalActions = globalActions;
            if (!saveSettings(currentSettings)) {
                return false;
            }
        }
    }

    if (result.hasCollections && !result.collections.isEmpty()) {
        QVector<Collection> currentCols = loadCollections();
        currentCols.append(result.collections);
        if (!saveCollections(currentCols)) {
            return false;
        }
    }

    if (!result.notes.isEmpty()) {
        QVector<Note> currentNotes = loadNotes();
        for (const Note &incoming : result.notes) {
            bool replaced = false;
            for (Note &existing : currentNotes) {
                if (existing.id == incoming.id) {
                    existing = incoming;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                currentNotes.append(incoming);
            }
        }
        if (!saveNotes(currentNotes)) {
            return false;
        }
    }

    // Aplica o snapshot inteiro de preferências do pacote. `hasSettings` só é
    // true se o pacote tinha a seção de preferências gerais ("active_theme"),
    // então uma importação só de Environments não sobrescreve preferências.
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
        // global_env_vars é legado (pré-Environments): mesclado, não sobrescrito,
        // p/ não apagar chaves locais.
        for (auto it = result.settings.globalEnvVars.constBegin(); it != result.settings.globalEnvVars.constEnd(); ++it) {
            currentSettings.globalEnvVars[it.key()] = it.value();
        }
        currentSettings.terminalProfiles.append(result.settings.terminalProfiles);
        saveSettings(currentSettings);
    }

    // ENVIRONMENTS, independente de hasSettings: MESCLA por id (substitui ou
    // adiciona) p/ não apagar Environments criados localmente depois do backup.
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

} // namespace kai::core
