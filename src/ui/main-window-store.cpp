#include "ui/main-window.h"

#include <QDir>
#include <QFileInfo>

#include "core/store-local.h"
#include "ui/features/collections/project-selector.h"
#include "utils/logger.h"
#include "utils/path-format.h"
#include "utils/translation-manager.h"

namespace kai::ui {

bool MainWindow::installStorePackage(const core::StoreInstallRequest &request, QString &message)
{
    const core::StoreLocalState local(m_configManager.configDirPath());
    core::StoreInstalledRecord record;
    record.provider = request.providerId;
    record.package = request.packageId;
    record.version = request.version;
    record.kind = request.kind;
    record.installedAt = QDateTime::currentDateTimeUtc();

    if (request.kaiFilePath.isEmpty() || !QFileInfo::exists(request.kaiFilePath)) {
        message = utils::tr(QStringLiteral("store.install.missing_content"));
        return false;
    }

    // Ambientes e perfis de terminal vêm num pacote de configuração: só essa categoria entra.
    if (request.kind == QLatin1String("environment") || request.kind == QLatin1String("profile")) {
        core::ConfigManager::ImportResult package;
        QString readError;
        if (!readConfigPackage(request.kaiFilePath, package, readError)) {
            message = utils::tr(QStringLiteral("importcfg.failed")).arg(readError);
            return false;
        }
        ImportSelectionDialog::Selection selection;
        selection.commands = false;
        selection.collections = false;
        selection.settings = false;
        selection.globalActions = false;
        selection.actionCommands = false;
        selection.environments = request.kind == QLatin1String("environment");
        selection.terminalProfiles = request.kind == QLatin1String("profile");
        applyConfigImport(package, selection, message);
        local.recordInstall(record);
        return true;
    }

    const QString directory = QFileInfo(request.kaiFilePath).absolutePath();
    ProjectImportResult result = m_projectSelector->importFromDirectory(directory, utils::toPosixPath(directory), false);
    if (!result.success) {
        message = result.errorMessage;
        return false;
    }

    // Aplica as regras com a raiz e as subpastas juntas, depois separa de novo.
    QVector<core::Folder> folders;
    folders << result.folder << result.subFolders;
    // Instalação parcial: o que o usuário desmarcou na prévia não entra.
    core::removeExcludedStoreCommands(folders, result.commands, QSet<QString>(request.excludeCommands.begin(), request.excludeCommands.end()));
    core::applyStoreInstallRules(folders, result.commands, result.folder.id, request.choices(), request.secrets);
    // Os ids do pacote são determinísticos: numa cópia/atualização eles colidem com a instalação anterior.
    QSet<QString> existingIds;
    for (const core::Folder &f : m_commandsData.folders) existingIds.insert(f.id);
    for (const core::Command &c : m_commandsData.commands) existingIds.insert(c.id);
    core::remapCollidingStoreIds(folders, result.commands, result.folder.id, existingIds);
    result.folder = folders.takeFirst();
    result.subFolders = folders;
    // Um pacote embutido não aponta para a pasta temporária de staging; um projeto por referência aponta para o clone.
    if (request.cloneDir.isEmpty()) {
        result.folder.workingDirMode = core::WorkingDirMode::Inherit;
        result.folder.workingDir.clear();
        result.folder.isProject = false; // não é um projeto com kai.yml em disco para sincronizar
    }

    // Atualização: a pasta nova entra no mesmo lugar da antiga.
    QString parentId = request.parentFolderId;
    if (!request.replaceFolderId.isEmpty() && parentId.isEmpty()) {
        for (const core::Folder &f : m_commandsData.folders) {
            if (f.id == request.replaceFolderId && f.parentId.has_value()) parentId = f.parentId.value();
        }
    }
    QString newRootId;
    if (!mergeImportedProject(result, parentId, &newRootId)) {
        message = utils::tr(QStringLiteral("mainwindow.import_project.save_failed"));
        return false;
    }
    record.folderId = newRootId;
    record.cloneDir = request.cloneDir;
    record.contentHash = core::storeContentHash(newRootId, m_commandsData);
    if (!local.recordInstall(record)) {
        utils::Logger::warning(QStringLiteral("Store"), QStringLiteral("Não foi possível gravar a procedência da instalação."));
    }
    if (!request.replaceFolderId.isEmpty() && request.replaceFolderId != newRootId) {
        QString ignored;
        uninstallStorePackage(request.replaceFolderId, ignored);
    }
    message = utils::tr(QStringLiteral("store.install.done")).arg(result.folder.name).arg(result.commands.size());
    return true;
}

bool MainWindow::uninstallStorePackage(const QString &folderId, QString &message)
{
    const core::StoreLocalState local(m_configManager.configDirPath());
    const QVector<core::StoreInstalledRecord> records = local.loadInstalled();
    const core::StoreInstalledRecord *tracked = nullptr;
    for (const core::StoreInstalledRecord &r : records) {
        if (r.folderId == folderId) tracked = &r;
    }
    if (!tracked) {
        message = utils::tr(QStringLiteral("store.uninstall.not_tracked"));
        return false;
    }
    const QString cloneDir = tracked->cloneDir;
    deleteFolderRecursively(folderId);
    const bool saved = m_configManager.saveCommands(m_commandsData);
    local.removeRecordForFolder(folderId);
    // Só o clone que o próprio Kai criou (sob a pasta de dados da Store) é apagado.
    if (!cloneDir.isEmpty() && QDir::cleanPath(cloneDir).contains(QStringLiteral("/store/"))) {
        QDir(cloneDir).removeRecursively();
    }
    reloadCommandTree();
    message = utils::tr(QStringLiteral("store.uninstall.done"));
    return saved;
}

} // namespace kai::ui
