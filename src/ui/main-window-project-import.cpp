#include "core/project-format.h"
#include "core/folder-actions.h"
#include "core/working-dir.h"
#include "ui/features/docs/document-panel.h"
#include "ui/features/docs/note-dialog.h"
#include "ui/main-window.h"

#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/panel-card.h"
#include "ui/layout/layout-store.h"
#include "ui/layout/view-registry.h"
#include "ui/features/ai/ai-chat-window.h"
#include "ui/features/ai/main-window-agent-host.h"
#include "ui/features/ai/mcp-bridge.h"
#include "core/folder-nesting.h"
#include "core/project-tree-scan.h"

#include "core/http-collection-import.h"
#include "core/http-resolve.h"
#include "engine/http-cookie-store.h"
#include "utils/path-format.h"
#include "utils/duration-format.h"
#include "ui/external-run-session.h"
#include "ui/shared/notification-gate.h"
#include "ui/shared/native-window-corners.h"
#include "ui/shared/native-window-snap.h"
#include "ui/shared/expand-collapse-bar.h"
#include "ui/shared/panel-metrics.h"
#include "ui/app-stylesheet.h"
#include "ui/shared/dialog-utils.h"
#include "utils/action-shortcuts.h"
#include "ui/features/collections/export-dialog.h"
#include "ui/features/collections/import-dialog.h"
#include "ui/features/collections/import-selection-dialog.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QClipboard>
#include <QScopeGuard>
#include <QShortcut>
#include <QWidget>
#include <QCloseEvent>
#include <QShowEvent>
#include <QTimer>
#include <QSet>
#include <QHash>
#include <QScreen>
#include <QCursor>
#include <QToolTip>
#include <QGuiApplication>
#include <QMenu>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <memory>
#include "ui/features/docs/doc-links.h"
#include "ui/shared/lucide-icons.h"
#include <QAction>
#include <QMessageBox>
#include <QFileDialog>
#include <QSaveFile>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QPixmap>
#include <QInputDialog>
#include <QUuid>
#include <QCoreApplication>
#include <QWindow>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRegion>
#include <QStyle>
#include "qhotkey.h"

#include "ui/features/command-editor/parameter-form-dialog.h"
#include "ui/features/docs/doc-session-store.h"
#include "ui/features/docs/doc-viewer.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "ui/features/collections/folder-delete-dialog.h"
#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/collections/collection-editor-dialog.h"
#include "ui/shared/name-uniqueness.h"
#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/loading-overlay.h"
#include "ui/features/command-editor/command-json-editor-dialog.h"
#include "ui/features/settings/settings-dialog.h"
#include "ui/features/environments/environment-manager-dialog.h"
#include "ui/features/environments/dynamic-vars-inspector-widget.h"
#include "ui/features/history/run-history-dialog.h"
#include "ui/features/history/notification-history-dialog.h"
#include "engine/module-files.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/output/run-mirror-panel.h"
#include "ui/features/output/quick-run-popup.h"
#include "engine/script-spill.h"
#include "ui/shared/help-dialog.h"
#include "ui/shared/manifesto-dialog.h"
#include "core/openapi-parser.h"
#include "core/yaml-bridge.h"
#include "ui/features/http/quick-http-edit-dialog.h"
#include "ui/shared/welcome-screen.h"
#include "utils/asset-paths.h"
#include "utils/logger.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "utils/autostart-manager.h"

#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <functional>
#include "ui/shared/tray-menu.h"
#include "ui/shared/window-translucency.h"
#include "ui/shared/file-dialogs.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

// O ImportDialog só decide a fonte e o tipo; cada fluxo específico faz o resto.
void MainWindow::handleImportRequested()
{
    ImportDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    switch (dialog.kind()) {
    case ImportDialog::Kind::Project:
        if (dialog.recursive()) {
            importProjectTreeFromDirectory(dialog.path());
        } else {
            importProjectFromDirectory(dialog.path());
        }
        break;
    case ImportDialog::Kind::OpenApi:
        importOpenApiFromFile(dialog.path());
        break;
    case ImportDialog::Kind::Config:
        importConfigFromFile(dialog.path());
        break;
    case ImportDialog::Kind::Postman:
    case ImportDialog::Kind::Insomnia:
    case ImportDialog::Kind::Har:
        importHttpCollectionFromFile(dialog.path(), static_cast<int>(dialog.kind()));
        break;
    }
}

void MainWindow::importProjectFromDirectory(const QString &directory)
{
    // Sob WSL/WSLg o seletor nativo às vezes devolve um path Windows para um projeto que roda no WSL: o diálogo
    // permite conferir o path e escolher o formato salvo como PROJECT_PATH.
    ProjectImportOptionsDialog optionsDialog(directory, m_commandsData.folders, this);
    if (optionsDialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString confirmedDirectory = optionsDialog.directory();
    if (confirmedDirectory.isEmpty()) {
        return;
    }

    LoadingScope loading(m_loadingOverlay, utils::tr(QStringLiteral("mainwindow.import_project.loading")));
    ProjectImportResult importResult =
        m_projectSelector->importFromDirectory(confirmedDirectory, optionsDialog.projectPath(),
                                                optionsDialog.detectGenericDefinitions());
    if (!importResult.success) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("mainwindow.import_project.title")), importResult.errorMessage);
        return;
    }
    if (!importResult.detectedEcosystems.isEmpty()) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Importação genérica detectou: %1").arg(importResult.detectedEcosystems.join(QStringLiteral(", "))));
    }

    // Pasta de destino vazia = raiz.
    if (!mergeImportedProject(importResult, optionsDialog.parentFolderId())) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("mainwindow.import_project.title")),
            utils::tr(QStringLiteral("mainwindow.import_project.save_failed")));
    }
}

MainWindow::TreeImportOutcome MainWindow::importProjectTree(const QString &rootDirectory, const QStringList &directories,
                                                            const QString &rootProjectPath, bool detectGeneric,
                                                            const QString &parentFolderId)
{
    TreeImportOutcome outcome;
    const QDir root(rootDirectory);
    const QChar separator = rootProjectPath.contains(QLatin1Char('\\')) && !rootProjectPath.contains(QLatin1Char('/'))
        ? QLatin1Char('\\') : QLatin1Char('/');
    // Pasta do disco -> pasta do Kai criada agora: o pai de um projeto é o projeto mais próximo que o contém DENTRO
    // desta importação. Pastas que já existiam no Kai só decidem o pai quando o destino não foi escolhido (raiz); senão
    // o projeto de fora puxava tudo para si e o destino escolhido era ignorado.
    QHash<QString, QString> importedFolderIds;
    const auto importedParentFor = [&importedFolderIds](const QString &directory) {
        QString current = QDir::cleanPath(directory);
        for (;;) {
            const int cut = current.lastIndexOf(QLatin1Char('/'));
            if (cut <= 0) {
                return QString();
            }
            current.truncate(cut);
            const auto found = importedFolderIds.constFind(current);
            if (found != importedFolderIds.constEnd()) {
                return found.value();
            }
        }
    };
    for (const QString &directory : directories) {
        const QString relative = root.relativeFilePath(directory);
        QString projectPath = rootProjectPath;
        if (relative != QLatin1String(".")) {
            QString suffix = relative;
            suffix.replace(QLatin1Char('/'), separator);
            projectPath = rootProjectPath + (rootProjectPath.endsWith(separator) ? QString() : QString(separator)) + suffix;
        }
        const ProjectImportResult result = m_projectSelector->importFromDirectory(directory, projectPath, detectGeneric);
        if (!result.success) {
            outcome.failures << QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(directory), result.errorMessage);
            continue;
        }
        QString parentId = importedParentFor(directory);
        if (parentId.isEmpty()) {
            parentId = parentFolderId.isEmpty() ? enclosingFolderIdFor(directory) : parentFolderId;
        }
        QString newRootId;
        if (!mergeImportedProject(result, parentId, &newRootId, false)) {
            outcome.failures << QStringLiteral("%1: %2").arg(
                QDir::toNativeSeparators(directory), utils::tr(QStringLiteral("mainwindow.import_project.save_failed")));
            continue;
        }
        importedFolderIds.insert(QDir::cleanPath(directory), newRootId);
        ++outcome.imported;
    }
    if (outcome.imported > 0) {
        reloadCommandTree(); // uma vez só: recarregar a cada projeto refazia a árvore inteira N vezes
    }
    return outcome;
}

void MainWindow::importProjectTreeFromDirectory(const QString &directory)
{
    const QString title = utils::tr(QStringLiteral("mainwindow.import_project.title"));
    if (core::findProjectDirectories(directory).isEmpty()) {
        QMessageBox::warning(this, title,
            utils::tr(QStringLiteral("import.recursive.none")).arg(QDir::toNativeSeparators(directory)));
        return;
    }
    ProjectImportOptionsDialog optionsDialog(directory, m_commandsData.folders, this);
    if (optionsDialog.exec() != QDialog::Accepted || optionsDialog.directory().isEmpty()) {
        return;
    }
    const QString root = optionsDialog.directory();
    LoadingScope loading(m_loadingOverlay, utils::tr(QStringLiteral("mainwindow.import_project.loading")));
    const TreeImportOutcome outcome = importProjectTree(root, core::findProjectDirectories(root),
        optionsDialog.projectPath(), optionsDialog.detectGenericDefinitions(), optionsDialog.parentFolderId());
    QString text = utils::tr(QStringLiteral("import.recursive.done")).arg(outcome.imported);
    if (!outcome.failures.isEmpty()) {
        text += QStringLiteral("\n\n") + outcome.failures.join(QLatin1Char('\n'));
    }
    if (outcome.failures.isEmpty()) {
        QMessageBox::information(this, title, text);
    } else {
        QMessageBox::warning(this, title, text);
    }
}

bool MainWindow::importProjectTreeFromCli(const QString &rootDirectory, QString &message, const QString &parentFolderId)
{
    const QStringList directories = core::findProjectDirectories(rootDirectory);
    if (directories.isEmpty()) {
        message = utils::tr(QStringLiteral("import.recursive.none")).arg(QDir::toNativeSeparators(rootDirectory));
        return false;
    }
    const QString root = QFileInfo(rootDirectory).absoluteFilePath();
    const TreeImportOutcome outcome =
        importProjectTree(root, directories, utils::toPosixPath(root), false, parentFolderId);
    message = utils::tr(QStringLiteral("import.recursive.done")).arg(outcome.imported);
    if (!outcome.failures.isEmpty()) {
        message += QStringLiteral("\n") + outcome.failures.join(QLatin1Char('\n'));
    }
    return outcome.failures.isEmpty();
}

QString MainWindow::folderDisplayName(const QString &folderId) const
{
    for (const core::Folder &f : m_commandsData.folders) {
        if (f.id == folderId) {
            return f.name;
        }
    }
    return folderId;
}

void MainWindow::startProjectSync(const QString &folderId, bool kaiToFile)
{
    if (m_projectSyncBusy.contains(folderId)) {
        return;
    }
    m_projectSyncBusy.insert(folderId);
    m_commandsData.notes = m_notes; // as notas sincronizáveis viajam com as pastas e comandos
    m_projectSync->requestPlan(kaiToFile ? engine::SyncDirection::KaiToFile : engine::SyncDirection::FileToKai,
                               folderId, m_commandsData, m_collections);
}

void MainWindow::finishProjectSync(const QString &folderId)
{
    m_projectSyncBusy.remove(folderId);
    scheduleSyncDriftCheck();
}

void MainWindow::scheduleSyncDriftCheck()
{
    if (m_syncDriftTimer) {
        m_syncDriftTimer->start(); // reinicia: edições seguidas viram uma checagem só
    }
}

void MainWindow::handleProjectSyncPlan(const engine::SyncPlan &plan)
{
    using Status = engine::SyncPlan::Status;
    const QString title = utils::tr(QStringLiteral("project_sync.title"));
    const bool kaiToFile = plan.direction == engine::SyncDirection::KaiToFile;

    if (plan.status == Status::Failed) {
        const QString reason = plan.errorDetail.isEmpty()
            ? utils::tr(plan.errorKey) : utils::tr(plan.errorKey).arg(plan.errorDetail);
        QMessageBox::warning(this, title, reason);
        finishProjectSync(plan.folderId);
        return;
    }
    if (plan.status == Status::NothingToDo) {
        QMessageBox::information(this, title, utils::tr(QStringLiteral("project_sync.nothing_to_do")));
        finishProjectSync(plan.folderId);
        return;
    }
    if (plan.status == Status::NeedsConfirmation) {
        const QString question = kaiToFile
            ? utils::tr(QStringLiteral("project_sync.confirm.kai_to_file"))
                  .arg(plan.filePath).arg(plan.fileCommands).arg(plan.fileFolders)
                  .arg(plan.kaiCommands).arg(plan.kaiFolders)
            : utils::tr(QStringLiteral("project_sync.confirm.file_to_kai"))
                  .arg(folderDisplayName(plan.folderId)).arg(plan.kaiCommands).arg(plan.kaiFolders)
                  .arg(plan.filePath).arg(plan.fileCommands).arg(plan.fileFolders);
        if (QMessageBox::question(this, title, question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                != QMessageBox::Yes) {
            finishProjectSync(plan.folderId);
            return;
        }
    }

    if (kaiToFile) {
        m_projectSync->applyKaiToFile(plan); // assíncrono: termina em kaiToFileFinished
        return;
    }

    // A pasta pode ter sido apagada enquanto o plano era calculado.
    const bool folderStillExists = std::any_of(m_commandsData.folders.cbegin(), m_commandsData.folders.cend(),
        [&plan](const core::Folder &f) { return f.id == plan.folderId; });
    int unresolvedActions = 0;
    m_commandsData.notes = m_notes;
    if (!folderStillExists || !m_projectSync->applyFileToKai(plan, m_commandsData, m_collections, &unresolvedActions)) {
        QMessageBox::warning(this, title, utils::tr(QStringLiteral("project_sync.error.folder_not_found")));
        finishProjectSync(plan.folderId);
        return;
    }
    const bool saved = m_configManager.saveCommands(m_commandsData);
    // As notas sincronizáveis vieram do arquivo; as locais ficam como estavam.
    m_notes = m_commandsData.notes;
    m_configManager.saveNotes(m_notes);
    reloadCommandTree();
    refreshDocNotes();
    if (saved) {
        QString done = utils::tr(QStringLiteral("project_sync.done.file_to_kai"))
            .arg(folderDisplayName(plan.folderId)).arg(plan.fileCommands).arg(plan.fileFolders);
        if (plan.ignoredCollections > 0) {
            done += utils::tr(QStringLiteral("project_sync.done.ignored_collections")).arg(plan.ignoredCollections);
        }
        if (!plan.unresolvedCollections.isEmpty()) {
            done += utils::tr(QStringLiteral("project_sync.done.unresolved_collections"))
                        .arg(plan.unresolvedCollections.join(QStringLiteral(", ")));
        }
        if (unresolvedActions > 0) {
            done += utils::tr(QStringLiteral("project_sync.done.unresolved_actions")).arg(unresolvedActions);
        }
        QMessageBox::information(this, title, done);
    } else {
        QMessageBox::warning(this, title, utils::tr(QStringLiteral("project_sync.save_failed")));
    }
    finishProjectSync(plan.folderId);
}

bool MainWindow::mergeImportedProject(ProjectImportResult importResult, const QString &parentFolderId,
                                      QString *newRootId, bool reloadTree)
{
    if (!parentFolderId.isEmpty()) {
        importResult.folder.parentId = parentFolderId;
    }
    const QString oldRootId = importResult.folder.id;
    const QString uniqueRootId = uniqueFolderId(oldRootId);
    if (uniqueRootId != oldRootId) {
        importResult.folder.id = uniqueRootId;
        for (core::Folder &sub : importResult.subFolders) {
            if (sub.parentId == oldRootId) {
                sub.parentId = uniqueRootId;
            }
        }
        for (core::Command &command : importResult.commands) {
            if (command.folderId == oldRootId) {
                command.folderId = uniqueRootId;
            }
        }
    }
    if (newRootId) {
        *newRootId = uniqueRootId;
    }

    m_commandsData.folders << importResult.folder;
    for (const core::Folder &sub : importResult.subFolders) {
        m_commandsData.folders << sub;
    }
    for (const core::Command &command : importResult.commands) {
        m_commandsData.commands << command;
    }

    const bool saved = m_configManager.saveCommands(m_commandsData);

    if (!importResult.collections.isEmpty()) {
        for (const core::Collection &collection : importResult.collections) {
            m_collections << collection;
        }
        persistCollections();
    }

    if (!importResult.notes.isEmpty()) {
        m_notes += importResult.notes;
        persistNotes();
    }

    if (reloadTree) {
        reloadCommandTree();
    }
    return saved;
}

ExternalRunSession *MainWindow::startAttachSession(const QString &target, QString &errorMessage)
{
    QString commandId;
    QString commandName;
    if (!resolveProcessTarget(target, commandId, commandName, errorMessage)) {
        return nullptr;
    }
    auto *session = new ExternalRunSession(commandId, [this, commandId](const QString &text) {
        engine::ProcessRunner *runner = runnerFor(commandId);
        if (!runner) {
            runner = m_processManager->runnerFor(commandId);
        }
        if (runner && runner->isRunning()) {
            runner->writeRaw(text);
        }
    }, this);

    // Só o fim do log, para não despejar o histórico inteiro.
    constexpr int kReplayChars = 16 * 1024;
    const QString backlog = m_backgroundProcessLogs.contains(commandId)
        ? m_backgroundProcessLogs.value(commandId) : m_commandLogs.value(commandId);
    const QString replay = backlog.right(kReplayChars);

    connect(m_pipeline, &engine::ExecutionPipeline::logMessage, session,
        [session, commandId](const QString &cid, const QString &text, bool isError) {
            if (cid == commandId) {
                session->publishOutput(text, isError);
            }
        });
    connect(m_processManager, &engine::ProcessManager::outputReady, session,
        [session, commandId](const QString &cid, const QString &text, bool isError) {
            if (cid == commandId) {
                session->publishOutput(text, isError);
            }
        });
    connect(m_pipeline, &engine::ExecutionPipeline::pipelineFinished, session,
        [session, commandId](const engine::PipelineResult &result) {
            if (result.mainCommandId == commandId) {
                session->publishFinished(result.success ? 0 : qMax(1, result.exitCode), result.errorMessage);
            }
        });
    connect(m_processManager, &engine::ProcessManager::statusChanged, session,
        [session, commandId](const QString &cid, engine::ProcessStatus status) {
            if (cid == commandId && status != engine::ProcessStatus::Running) {
                session->publishFinished(status == engine::ProcessStatus::Success ? 0 : 1, QString());
            }
        });
    // Adiado: quem chamou ainda vai conectar os sinais da sessão.
    if (!replay.isEmpty()) {
        QMetaObject::invokeMethod(session, [session, replay]() {
            session->publishOutput(replay, false);
        }, Qt::QueuedConnection);
    }
    return session;
}

void MainWindow::notifyExternalRunFinished(const QString &commandId, int exitCode, qint64 elapsedMs)
{
    const auto it = m_commandsById.constFind(commandId);
    const QString name = it != m_commandsById.constEnd() ? it->name : commandId;
    const QString body = exitCode == 0
        ? utils::tr(QStringLiteral("cli.notify.done")).arg(name, utils::formatShortDuration(elapsedMs))
        : utils::tr(QStringLiteral("cli.notify.failed")).arg(name).arg(exitCode).arg(utils::formatShortDuration(elapsedMs));
    QString ignored;
    raiseNotificationFromCli(exitCode == 0 ? QStringLiteral("info") : QStringLiteral("error"), QString(), body, ignored);
}

QJsonArray MainWindow::runningProcessItems()
{
    QJsonArray items;
    auto nameOf = [this](const QString &id) -> QString {
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == id) return c.name;
        }
        return id;
    };
    QSet<QString> seen;
    auto add = [&](const QString &id, engine::ProcessRunner *runner, const QString &status) {
        if (seen.contains(id)) {
            return;
        }
        seen.insert(id);
        QJsonObject item;
        item[QStringLiteral("id")] = id;
        item[QStringLiteral("name")] = nameOf(id);
        item[QStringLiteral("pid")] = static_cast<double>(runner ? runner->processId() : 0);
        item[QStringLiteral("status")] = status;
        items.append(item);
    };
    for (const QString &id : m_processManager->trackedCommandIds()) {
        const engine::ProcessStatus st = m_processManager->statusOf(id);
        add(id, m_processManager->runnerFor(id),
            st == engine::ProcessStatus::Running ? QStringLiteral("running")
            : st == engine::ProcessStatus::Success ? QStringLiteral("success") : QStringLiteral("error"));
    }
    for (const QString &id : allPipelineRunningCommandIds()) {
        add(id, runnerFor(id), QStringLiteral("running"));
    }
    return items;
}

// kai.yml com cabeçalho de export de pasta (scope "folder") é o arquivo de projeto dessa pasta: entra como projeto.
static bool isProjectFile(const QString &path)
{
    if (QFileInfo(path).fileName().toLower() != QStringLiteral("kai.yml")) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    bool ok = false;
    const QString json = core::yamlTextToJsonText(QString::fromUtf8(file.readAll()), &ok, nullptr);
    return ok && core::isFolderScopeExport(QJsonDocument::fromJson(json.toUtf8()).object());
}

bool MainWindow::importProjectFromCli(const QString &path, const QStringList &only, QString &message)
{
    // Um pacote exportado (cabeçalho kai_export) entra inteiro e sem perguntas, como "Importar configuração" com tudo
    // marcado; `only` (ver cliImportSelection) escolhe as categorias. Qualquer outro kai.yml é um projeto.
    const QFileInfo info(path);
    if (info.isFile() && !isProjectFile(info.absoluteFilePath())) {
        core::ConfigManager::ImportResult package;
        QString readError;
        bool isPackage = false;
        if (readConfigPackage(info.absoluteFilePath(), package, readError, &isPackage)) {
            ImportSelectionDialog::Selection selection;
            if (!cliImportSelection(only, selection, message)) {
                return false;
            }
            applyConfigImport(package, selection, message);
            return true;
        }
        if (isPackage) {
            message = utils::tr(QStringLiteral("importcfg.failed")).arg(readError);
            return false;
        }
    }
    if (!only.isEmpty()) {
        message = utils::tr(QStringLiteral("cli.import.only_needs_package"));
        return false;
    }
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!info.isDir()) {
        const QString fileName = info.fileName().toLower();
        if (fileName != QStringLiteral("kai.yml")) {
            message = utils::tr(QStringLiteral("cli.import.unsupported_file")).arg(info.fileName());
            return false;
        }
    }
    const ProjectImportResult importResult =
        m_projectSelector->importFromDirectory(directory, utils::toPosixPath(directory), false);
    if (!importResult.success) {
        message = importResult.errorMessage;
        return false;
    }
    // Um projeto dentro da pasta de outro (monorepo) entra dentro dele, como no disco; senão vai para a raiz.
    const QString parentId = enclosingFolderIdFor(directory);
    if (!mergeImportedProject(importResult, parentId)) {
        message = utils::tr(QStringLiteral("mainwindow.import_project.save_failed"));
        return false;
    }
    message = parentId.isEmpty()
        ? utils::tr(QStringLiteral("cli.import.done")).arg(importResult.folder.name).arg(importResult.commands.size())
        : utils::tr(QStringLiteral("cli.import.done_in"))
              .arg(importResult.folder.name).arg(importResult.commands.size()).arg(folderDisplayName(parentId));
    return true;
}

QString MainWindow::enclosingFolderIdFor(const QString &directory) const
{
    const auto normalized = [](const QString &path) { return QDir::cleanPath(utils::toPosixPath(path)); };
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    const QString target = normalized(directory);
    QString bestId;
    qsizetype bestLength = -1;
    for (const core::Folder &folder : m_commandsData.folders) {
        if (folder.workingDirMode != core::WorkingDirMode::Custom || folder.workingDir.trimmed().isEmpty()) {
            continue;
        }
        const QString base = normalized(folder.workingDir);
        if (base.isEmpty() || base == QLatin1String("/") || !target.startsWith(base + QLatin1Char('/'), sensitivity)) {
            continue; // o próprio diretório (já importado antes) não conta como "contém"
        }
        if (base.size() > bestLength) {
            bestLength = base.size();
            bestId = folder.id;
        }
    }
    return bestId;
}

// Os nomes de `--only` da CLI -> as categorias do pacote. `actions` = as ações globais e os comandos que as ações
// usam (as ações de cada pasta vêm junto de `commands`).
bool MainWindow::cliImportSelection(const QStringList &only, ImportSelectionDialog::Selection &selection, QString &error)
{
    selection = ImportSelectionDialog::Selection();
    if (only.isEmpty()) {
        return true;
    }
    selection = ImportSelectionDialog::Selection{false, false, false, false, false, false, false};
    for (const QString &raw : only) {
        const QString name = raw.trimmed().toLower();
        if (name == QStringLiteral("commands")) selection.commands = true;
        else if (name == QStringLiteral("collections")) selection.collections = true;
        else if (name == QStringLiteral("settings")) selection.settings = true;
        else if (name == QStringLiteral("environments")) selection.environments = true;
        else if (name == QStringLiteral("terminal-profiles")) selection.terminalProfiles = true;
        else if (name == QStringLiteral("global-actions")) selection.globalActions = true;
        else if (name == QStringLiteral("action-commands")) selection.actionCommands = true;
        else if (name == QStringLiteral("actions")) { selection.globalActions = true; selection.actionCommands = true; }
        else {
            error = utils::tr(QStringLiteral("cli.import.unknown_only")).arg(raw);
            return false;
        }
    }
    return true;
}

bool MainWindow::raiseNotificationFromCli(const QString &level, const QString &title,
                                          const QString &body, QString &message)
{
    // Pedido explícito: não passa pelos interruptores de evento (maybeShowNotification), só precisa da bandeja para
    // aparecer. Entra no histórico de qualquer jeito.
    const QSystemTrayIcon::MessageIcon icon = level == QStringLiteral("error") ? QSystemTrayIcon::Critical
        : level == QStringLiteral("warning") ? QSystemTrayIcon::Warning
                                              : QSystemTrayIcon::Information;
    const QString defaultTitle = level == QStringLiteral("error") ? utils::tr(QStringLiteral("cli.raise.title.error"))
        : level == QStringLiteral("warning") ? utils::tr(QStringLiteral("cli.raise.title.warning"))
                                              : utils::tr(QStringLiteral("cli.raise.title.info"));
    const QString shownTitle = title.isEmpty() ? defaultTitle : title;

    core::NotificationRecord record;
    record.eventKey = QStringLiteral("cli_raise_%1").arg(level);
    record.title = shownTitle;
    record.body = body;
    m_notificationHistory.append(record);

    if (!trayAvailable() || !m_trayIcon) {
        message = utils::tr(QStringLiteral("cli.raise.no_tray"));
        return false;
    }
    m_trayIcon->showMessage(shownTitle, body, icon, 6000);
    return true;
}

} // namespace kai::ui
