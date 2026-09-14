#include "core/working-dir.h"

#include <QRegularExpression>
#include <QSet>

namespace kai::core {

namespace {

QString joinPath(const QString &base, const QString &relative)
{
    if (base.isEmpty()) {
        return relative;
    }
    // Base no estilo Windows (só barras invertidas) continua nele.
    const QChar sep = base.contains(QLatin1Char('\\')) && !base.contains(QLatin1Char('/'))
        ? QLatin1Char('\\') : QLatin1Char('/');
    QString trimmedBase = base;
    while (trimmedBase.size() > 1 && (trimmedBase.endsWith(QLatin1Char('/')) || trimmedBase.endsWith(QLatin1Char('\\')))) {
        trimmedBase.chop(1);
    }
    return trimmedBase + sep + relative;
}

const Folder *findFolder(const QString &id, const QVector<Folder> &folders)
{
    for (const Folder &f : folders) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

// Resolve (mode, path) de um nível sobre o que vem de cima.
QString resolveLevel(WorkingDirMode mode, const QString &path, const QString &inherited)
{
    switch (mode) {
    case WorkingDirMode::None:
        return QString();
    case WorkingDirMode::Custom: {
        const QString trimmed = path.trimmed();
        if (trimmed.isEmpty()) {
            return inherited;
        }
        return isAbsoluteWorkingDirPath(trimmed) ? trimmed : joinPath(inherited, trimmed);
    }
    case WorkingDirMode::Inherit:
        break;
    }
    return inherited;
}

QString folderDirRecursive(const Folder *folder, const QVector<Folder> &folders, QSet<QString> &visited)
{
    if (!folder || visited.contains(folder->id)) {
        return QString(); // dado corrompido (ciclo): nunca travar
    }
    visited.insert(folder->id);
    QString inherited;
    if (folder->workingDirMode != WorkingDirMode::None
        && !(folder->workingDirMode == WorkingDirMode::Custom && isAbsoluteWorkingDirPath(folder->workingDir.trimmed()))
        && folder->parentId.has_value()) {
        inherited = folderDirRecursive(findFolder(*folder->parentId, folders), folders, visited);
    }
    return resolveLevel(folder->workingDirMode, folder->workingDir, inherited);
}

} // namespace

bool isAbsoluteWorkingDirPath(const QString &path)
{
    static const QRegularExpression absolute(QStringLiteral(R"(^(/|\\|~|\{\{|[A-Za-z]:[\\/]))"));
    return absolute.match(path).hasMatch();
}

QString effectiveFolderWorkingDir(const QString &folderId, const QVector<Folder> &folders)
{
    QSet<QString> visited;
    return folderDirRecursive(findFolder(folderId, folders), folders, visited);
}

QString effectiveCommandWorkingDir(const Command &command, const QVector<Folder> &folders)
{
    if (command.workingDirMode == WorkingDirMode::None) {
        return QString();
    }
    const QString trimmed = command.workingDir.trimmed();
    if (command.workingDirMode == WorkingDirMode::Custom && isAbsoluteWorkingDirPath(trimmed)) {
        return trimmed;
    }
    return resolveLevel(command.workingDirMode, command.workingDir,
                        effectiveFolderWorkingDir(command.folderId, folders));
}

ProjectRoot projectRootFor(const QString &folderId, const QVector<Folder> &folders)
{
    QSet<QString> visited;
    const Folder *folder = findFolder(folderId, folders);
    while (folder && !visited.contains(folder->id)) {
        visited.insert(folder->id);
        if (folder->isProject) {
            return {folder->id, effectiveFolderWorkingDir(folder->id, folders)};
        }
        folder = folder->parentId.has_value() ? findFolder(*folder->parentId, folders) : nullptr;
    }
    return {};
}

} // namespace kai::core
