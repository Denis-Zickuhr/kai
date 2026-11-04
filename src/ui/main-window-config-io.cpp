

#include "ui/main-window.h"

#include "core/http-collection-import.h"

#include "ui/shared/dialog-utils.h"

#include "ui/features/collections/export-dialog.h"
#include "ui/features/collections/import-dialog.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QClipboard>

#include <QSaveFile>

#include <QFileInfo>

#include <QJsonDocument>

#include "ui/features/collections/folder-editor-dialog.h"

#include "ui/features/command-editor/command-editor-dialog.h"

#include "core/openapi-parser.h"
#include "core/yaml-bridge.h"

#include "ui/shared/file-dialogs.h"

namespace kai::ui {

// Lê um pacote exportado (sempre YAML; o ConfigManager opera sobre a representação JSON interna). `isPackage` volta
// false quando o arquivo não tem o cabeçalho `kai_export`: provavelmente é o kai.yml de um projeto.
bool MainWindow::readConfigPackage(const QString &path, core::ConfigManager::ImportResult &result, QString &error,
                                   bool *isPackage) const
{
    if (isPackage) {
        *isPackage = true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    bool yamlOk = false;
    QString yamlError;
    const QString jsonText = core::yamlTextToJsonText(text, &yamlOk, &yamlError);
    if (!yamlOk) {
        error = yamlError;
        return false;
    }
    if (isPackage && QJsonDocument::fromJson(jsonText.toUtf8()).object().value(QStringLiteral("kai_export")).toObject().isEmpty()) {
        *isPackage = false;
    }
    result = core::ConfigManager::importFromJson(jsonText);
    if (!result.ok) {
        error = result.errorMessage;
        return false;
    }
    return true;
}

void MainWindow::importConfigFromFile(const QString &path)
{
    core::ConfigManager::ImportResult result;
    QString readError;
    if (!readConfigPackage(path, result, readError)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("importcfg.title")),
            utils::tr(QStringLiteral("importcfg.failed")).arg(readError));
        return;
    }

    // Comandos das ações de pasta que o pacote traz e que o usuário ainda não tem (por nome).
    QVector<core::Command> knownCommands = m_commandsData.commands;
    knownCommands.append(result.commands);
    QStringList missingActionCommandNames;
    for (const core::Command &c : core::ConfigManager::actionCommandsToImport(result, knownCommands)) {
        missingActionCommandNames << c.name;
    }
    ImportSelectionDialog selectionDialog(result, missingActionCommandNames, this);
    if (selectionDialog.exec() != QDialog::Accepted) {
        return;
    }
    QString doneMessage;
    applyConfigImport(result, selectionDialog.selection(), doneMessage);
    QMessageBox::information(this, utils::tr(QStringLiteral("importcfg.title")), doneMessage);
}

// O merge do pacote no que o Kai já tem, sem nenhum diálogo (a interface e a CLI chamam esta mesma função).
void MainWindow::applyConfigImport(const core::ConfigManager::ImportResult &result,
                                   const ImportSelectionDialog::Selection &sel, QString &doneMessage)
{
    // Merge por id: substitui os de mesmo id e adiciona os novos (sem duplicar ao reimportar).
    // Import global também mescla settings, sem sobrescrever tudo cegamente.
    int foldersImported = 0;
    int commandsImported = 0;
    if (sel.commands) {
        for (const core::Folder &f : result.folders) {
            bool replaced = false;
            for (core::Folder &existing : m_commandsData.folders) {
                if (existing.id == f.id) { existing = f; replaced = true; break; }
            }
            if (!replaced) {
                m_commandsData.folders.append(f);
            }
        }
        foldersImported = result.folders.size();
        for (const core::Command &c : result.commands) {
            bool replaced = false;
            for (core::Command &existing : m_commandsData.commands) {
                if (existing.id == c.id) { existing = c; replaced = true; break; }
            }
            if (!replaced) {
                m_commandsData.commands.append(c);
            }
        }
        commandsImported = result.commands.size();
        // Notas do pacote (sincronizáveis): mesmo merge por id.
        if (!result.notes.isEmpty()) {
            for (const core::Note &incoming : result.notes) {
                bool replaced = false;
                for (core::Note &existing : m_notes) {
                    if (existing.id == incoming.id) { existing = incoming; replaced = true; break; }
                }
                if (!replaced) {
                    m_notes.append(incoming);
                }
            }
            m_configManager.saveNotes(m_notes);
        }
        if (sel.actionCommands) {
            const QVector<core::Command> actionCommands = core::ConfigManager::actionCommandsToImport(result, m_commandsData.commands);
            core::ConfigManager::appendImportedActionCommands(actionCommands, m_commandsData);
            commandsImported += actionCommands.size();
        }
    }

    if (result.hasCollections && sel.collections) {
        for (const core::Collection &col : result.collections) {
            bool replaced = false;
            for (core::Collection &existing : m_collections) {
                if (existing.id == col.id) {
                    // Pacote SEM registros (export só da estrutura): preserva os dados locais e atualiza só a estrutura.
                    if (col.entries.isEmpty() && !existing.entries.isEmpty()) {
                        core::Collection merged = col;
                        merged.entries = existing.entries;
                        existing = merged;
                    } else {
                        existing = col;
                    }
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                m_collections.append(col);
            }
        }
        persistCollections();
    }

    // ALVOS DE TERMINAL: TerminalProfile só tem `name`, então "mesmo item" é
    // decidido por CONTEÚDO (template/shell/pty/ícone):
    //   - idêntico a um alvo existente -> não duplica, remapeia terminalTarget p/ o nome existente;
    //   - nome em conflito, conteúdo DIFERENTE -> importa com nome novo gerado;
    //   - sem conflito -> importa como veio.
    // O remapeamento cobre comandos/pastas do MESMO lote (terminalTarget é string solta).
    QMap<QString, QString> terminalTargetRename;
    if (result.hasTerminalProfiles && sel.terminalProfiles) {
        core::SettingsData settings = m_configManager.loadSettings();
        auto sameContent = [](const core::TerminalProfile &a, const core::TerminalProfile &b) {
            return a.commandTemplate == b.commandTemplate && a.shell == b.shell
                && a.usePty == b.usePty && a.icon == b.icon;
        };
        QSet<QString> usedNames;
        for (const core::TerminalProfile &existing : settings.terminalProfiles) {
            usedNames.insert(existing.name);
        }
        for (const core::TerminalProfile &incoming : result.settings.terminalProfiles) {
            const core::TerminalProfile *identical = nullptr;
            for (const core::TerminalProfile &existing : settings.terminalProfiles) {
                if (sameContent(existing, incoming)) { identical = &existing; break; }
            }
            if (identical) {
                if (identical->name != incoming.name) {
                    terminalTargetRename[incoming.name] = identical->name;
                }
                continue;
            }
            const bool nameConflict = usedNames.contains(incoming.name);
            core::TerminalProfile toAdd = incoming;
            if (nameConflict) {
                QString candidate = utils::tr(QStringLiteral("importcfg.terminal_target.renamed"))
                    .arg(incoming.name);
                int suffix = 2;
                while (usedNames.contains(candidate)) {
                    candidate = utils::tr(QStringLiteral("importcfg.terminal_target.renamed_n"))
                        .arg(incoming.name).arg(suffix++);
                }
                toAdd.name = candidate;
                toAdd.isDefault = false; // não rouba o alvo padrão local ao renomear
                terminalTargetRename[incoming.name] = candidate;
            }
            usedNames.insert(toAdd.name);
            settings.terminalProfiles.append(toAdd);
        }
        m_configManager.saveSettings(settings);
    }

    if (!terminalTargetRename.isEmpty() && sel.commands) {
        // Reescreve terminalTarget SÓ nos itens vindos DESTE import (por id),
        // nunca nos locais que já usavam o mesmo nome.
        QSet<QString> importedFolderIds;
        QSet<QString> importedCommandIds;
        for (const core::Folder &f : result.folders) importedFolderIds.insert(f.id);
        for (const core::Command &c : result.commands) importedCommandIds.insert(c.id);
        for (core::Folder &f : m_commandsData.folders) {
            if (!importedFolderIds.contains(f.id)) continue;
            if (terminalTargetRename.contains(f.terminalTarget)) {
                f.terminalTarget = terminalTargetRename.value(f.terminalTarget);
            }
        }
        for (core::Command &c : m_commandsData.commands) {
            if (!importedCommandIds.contains(c.id)) continue;
            if (terminalTargetRename.contains(c.terminalTarget)) {
                c.terminalTarget = terminalTargetRename.value(c.terminalTarget);
            }
        }
    }

    if ((result.hasSettings && sel.settings) || (result.hasEnvironments && sel.environments)) {
        core::SettingsData settings = m_configManager.loadSettings();
        if (sel.settings) {
            for (auto it = result.settings.globalEnvVars.constBegin();
                 it != result.settings.globalEnvVars.constEnd(); ++it) {
                settings.globalEnvVars[it.key()] = it.value();
            }
        }
        // ENVIRONMENTS: merge por id, como pastas/comandos/coleções.
        if (sel.environments) {
            for (const core::Environment &e : result.settings.environments) {
                bool replaced = false;
                for (core::Environment &existing : settings.environments) {
                    if (existing.id == e.id) { existing = e; replaced = true; break; }
                }
                if (!replaced) {
                    settings.environments.append(e);
                }
            }
            if (!result.settings.activeEnvironmentId.isEmpty()) {
                settings.activeEnvironmentId = result.settings.activeEnvironmentId;
            }
        }
        m_configManager.saveSettings(settings);
        m_envManager.setGlobalVars(settings.globalEnvVars);
    }

    // AÇÕES (por pasta e globais): referências por nome casadas com os comandos atuais (novos + antigos).
    int unresolvedActions = 0;
    {
        core::ConfigManager::ImportResult actionsResult = result;
        if (!sel.globalActions) {
            actionsResult.hasGlobalActions = false;
        }
        if (!sel.commands) {
            actionsResult.pendingFolderActions.clear(); // as pastas não vieram
        }
        core::SettingsData settings = m_configManager.loadSettings();
        unresolvedActions = core::ConfigManager::applyImportedActions(
            actionsResult, m_commandsData.folders, m_commandsData.commands, settings.globalActions);
        if (actionsResult.hasGlobalActions) {
            m_configManager.saveSettings(settings);
            m_globalActions = settings.globalActions;
        }
    }

    persistCommands();

    doneMessage = utils::tr(QStringLiteral("importcfg.done")).arg(foldersImported).arg(commandsImported);
    if (unresolvedActions > 0) {
        doneMessage += QStringLiteral("\n\n") + utils::tr(QStringLiteral("importcfg.actions_unresolved")).arg(unresolvedActions);
    }
}

void MainWindow::importOpenApiFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("openapi.title")),
            utils::tr(QStringLiteral("openapi.read_failed")));
        return;
    }
    const QString jsonText = QString::fromUtf8(file.readAll());
    file.close();

    const core::OpenApiParseResult parsed = core::parseOpenApi(jsonText);
    if (!parsed.ok) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("openapi.title")),
            utils::tr(QStringLiteral("openapi.failed")).arg(parsed.errorMessage));
        return;
    }

    // Cria uma pasta com o título da API e um comando HTTP por endpoint.
    core::Folder folder;
    folder.name = parsed.apiTitle;
    folder.id = uniqueFolderId(FolderEditorDialog::generateFolderId(parsed.apiTitle));
    m_commandsData.folders.append(folder);

    QSet<QString> usedIds;
    for (const core::Command &c : m_commandsData.commands) {
        usedIds.insert(c.id);
    }

    int created = 0;
    for (const core::OpenApiEndpoint &ep : parsed.endpoints) {
        core::Command cmd;
        cmd.name = ep.name;
        cmd.folderId = folder.id;
        cmd.type = core::CommandType::Http;
        cmd.httpConfig = ep.config;
        QString id = CommandEditorDialog::generateCommandId(folder.id, ep.name);
        QString baseId = id;
        int suffix = 2;
        while (usedIds.contains(id)) {
            id = baseId + QStringLiteral("_%1").arg(suffix++);
        }
        cmd.id = id;
        usedIds.insert(id);
        m_commandsData.commands.append(cmd);
        ++created;
    }

    persistCommands();
    reloadCommandTree();

    QMessageBox::information(this, utils::tr(QStringLiteral("openapi.title")),
        utils::tr(QStringLiteral("openapi.done")).arg(created).arg(parsed.apiTitle));
}

void MainWindow::importHttpCollectionFromFile(const QString &path, int kind)
{
    const QString title = utils::tr(QStringLiteral("http_import.title"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, title, utils::tr(QStringLiteral("openapi.read_failed")));
        return;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    core::ImportedHttpCollection parsed;
    switch (static_cast<ImportDialog::Kind>(kind)) {
    case ImportDialog::Kind::Postman: parsed = core::parsePostmanCollection(text); break;
    case ImportDialog::Kind::Insomnia: parsed = core::parseInsomniaExport(text); break;
    case ImportDialog::Kind::Har: parsed = core::parseHar(text); break;
    default: return;
    }
    if (!parsed.ok) {
        QMessageBox::warning(this, title, utils::tr(QStringLiteral("http_import.failed")).arg(parsed.errorMessage));
        return;
    }

    // Pasta raiz da importação: leva o nome, as variáveis e os padrões HTTP da coleção.
    core::Folder root;
    root.name = parsed.name;
    root.id = uniqueFolderId(FolderEditorDialog::generateFolderId(parsed.name));
    root.httpDefaults = parsed.rootDefaults;
    root.envVars = parsed.variables;
    m_commandsData.folders.append(root);

    // Subpastas por caminho, criadas sob demanda; os padrões próprios (autenticação...) entram junto.
    QMap<QString, QString> folderIdByPath;
    int nextOrder = 0;
    const std::function<QString(const QStringList &)> folderFor = [&](const QStringList &folderPath) -> QString {
        if (folderPath.isEmpty()) {
            return root.id;
        }
        const QString key = folderPath.join(QLatin1Char('\x1f'));
        if (folderIdByPath.contains(key)) {
            return folderIdByPath.value(key);
        }
        const QString parentId = folderFor(folderPath.mid(0, folderPath.size() - 1));
        core::Folder sub;
        sub.name = folderPath.last();
        sub.id = uniqueFolderId(FolderEditorDialog::generateFolderId(root.id + QLatin1Char('_') + sub.name));
        sub.parentId = parentId;
        sub.order = nextOrder++;
        for (const core::ImportedFolder &f : parsed.folders) {
            if (f.path == folderPath) {
                sub.httpDefaults = f.defaults;
                break;
            }
        }
        m_commandsData.folders.append(sub);
        folderIdByPath.insert(key, sub.id);
        return sub.id;
    };

    QSet<QString> usedIds;
    for (const core::Command &c : m_commandsData.commands) {
        usedIds.insert(c.id);
    }
    int created = 0;
    for (const core::ImportedRequest &request : parsed.requests) {
        core::Command cmd;
        cmd.name = request.name;
        cmd.folderId = folderFor(request.folderPath);
        cmd.type = core::CommandType::Http;
        cmd.httpConfig = request.config;
        cmd.order = created;
        QString id = CommandEditorDialog::generateCommandId(cmd.folderId, request.name);
        const QString baseId = id;
        int suffix = 2;
        while (usedIds.contains(id)) {
            id = baseId + QStringLiteral("_%1").arg(suffix++);
        }
        cmd.id = id;
        usedIds.insert(id);
        m_commandsData.commands.append(cmd);
        ++created;
    }

    persistCommands();
    reloadCommandTree();

    QString message = utils::tr(QStringLiteral("http_import.done")).arg(created).arg(parsed.name);
    if (!parsed.variables.isEmpty()) {
        message += QStringLiteral("\n\n") + utils::tr(QStringLiteral("http_import.variables")).arg(parsed.variables.size());
    }
    QMessageBox::information(this, title, message);
}

void MainWindow::handleExportRequested()
{
    showExportDialog(QString(), false);
}

void MainWindow::showExportDialog(const QString &targetId, bool targetIsFolder)
{
    // Menu único: Global sempre disponível; Pasta/Comando usam um seletor
    // sobre TODAS as pastas/comandos do app, não só a seleção atual da árvore.
    QVector<ExportDialog::TargetChoice> folderChoices;
    for (const core::Folder &f : foldersInTreeOrder(m_commandsData.folders)) {
        folderChoices << ExportDialog::TargetChoice{f.id, folderComboLabel(m_commandsData.folders, f.id)};
    }

    // Comandos: nome + caminho da pasta como hint entre parênteses (vazio na raiz).
    QVector<ExportDialog::TargetChoice> commandChoices;
    for (const core::Command &c : m_commandsData.commands) {
        const QStringList pathSegments = folderPathSegments(m_commandsData.folders, c.folderId);
        const QString label = pathSegments.isEmpty()
            ? c.name
            : QStringLiteral("%1   (%2)").arg(c.name, pathSegments.join(QStringLiteral(" / ")));
        commandChoices << ExportDialog::TargetChoice{c.id, label};
    }

    const QVector<core::TerminalProfile> availableProfiles = m_configManager.loadSettings().terminalProfiles;
    ExportDialog dlg(folderChoices, commandChoices, availableProfiles,
        [this](const QString &folderId) { return linkedCollectionsForFolder(folderId); },
        [this](const QString &commandId) { return linkedCollectionsForCommand(commandId); },
        this);
    dlg.setActionCommandsProvider([this](const QString &folderId) {
        QStringList names;
        for (const core::Command &c : core::ConfigManager::actionCommandsOutsideFolder(folderId, m_commandsData)) {
            names << c.name;
        }
        return names;
    });
    if (!targetId.isEmpty()) {
        dlg.preselectTarget(targetIsFolder ? ExportDialog::Scope::Folder : ExportDialog::Scope::Command, targetId);
    }
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QString format = QStringLiteral("yml"); // o único formato de arquivo
    const QString filter = utils::tr(QStringLiteral("importcfg.filter.yaml_only"));

    // O conteúdo é o mesmo nos dois destinos; só muda para onde vai: arquivo (pede o caminho) ou área de transferência.
    const auto deliver = [&](const QString &suggestedName, const std::function<QString()> &build) {
        if (dlg.copyToClipboard()) {
            copyExportToClipboard(build());
            return;
        }
        const QString path = pickSaveFile(this,
            utils::tr(QStringLiteral("export.dialog_title")), suggestedName, filter);
        if (path.isEmpty()) {
            return;
        }
        writeExportFile(path, build());
    };

    switch (dlg.selectedScope()) {
    case ExportDialog::Scope::Global:
        deliver(QStringLiteral("kai-config.%1").arg(format), [&]() {
            m_commandsData.notes = m_notes;
            return core::ConfigManager::exportSelective(
                dlg.globalSelection(), m_configManager.loadSettings(), m_commandsData, m_collections,
                dlg.leanExport());
        });
        break;
    case ExportDialog::Scope::Folder:
        deliver(QStringLiteral("kai-folder.%1").arg(format), [&]() {
            m_commandsData.notes = m_notes;
            return core::ConfigManager::exportFolder(
                dlg.selectedTargetId(), m_commandsData, dlg.selectedCollections(), dlg.selectedTerminalProfiles(),
                dlg.leanExport(), dlg.includeActionCommands());
        });
        break;
    case ExportDialog::Scope::Command:
        deliver(QStringLiteral("kai-command.%1").arg(format), [&]() {
            return core::ConfigManager::exportCommand(
                dlg.selectedTargetId(), m_commandsData, dlg.selectedCollections(), dlg.selectedTerminalProfiles(),
                dlg.leanExport());
        });
        break;
    }
}

namespace {
// O export é sempre YAML: o ConfigManager monta o documento em JSON (representação interna) e é convertido aqui.
QString exportAsYaml(const QString &content)
{
    const QString yaml = core::jsonTextToYamlText(content);
    return yaml.isEmpty() ? content : yaml;
}
} // namespace

void MainWindow::copyExportToClipboard(const QString &content)
{
    QGuiApplication::clipboard()->setText(exportAsYaml(content));
    QMessageBox::information(this, utils::tr(QStringLiteral("export.title")),
        utils::tr(QStringLiteral("export.copied")));
}

void MainWindow::writeExportFile(const QString &path, const QString &content)
{
    const QString finalContent = exportAsYaml(content);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("export.title")),
            utils::tr(QStringLiteral("export.save_failed")));
        return;
    }
    file.write(finalContent.toUtf8());
    if (!file.commit()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("export.title")),
            utils::tr(QStringLiteral("export.save_failed")));
        return;
    }
    QMessageBox::information(this, utils::tr(QStringLiteral("export.title")),
        utils::tr(QStringLiteral("export.saved")).arg(path));
}

} // namespace kai::ui
