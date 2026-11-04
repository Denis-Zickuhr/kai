#include "core/project-tree-scan.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace kai::core {

namespace {

bool isSkippedDirectoryName(const QString &name)
{
    static const QSet<QString> skipped = {
        QStringLiteral("node_modules"), QStringLiteral("vendor"), QStringLiteral("__pycache__")};
    return name.startsWith(QLatin1Char('.')) || skipped.contains(name);
}

void scan(const QDir &dir, int depthLeft, QStringList &found)
{
    if (QFileInfo(dir.filePath(QStringLiteral("kai.yml"))).isFile()) {
        found << dir.absolutePath();
    }
    if (depthLeft <= 0) {
        return;
    }
    const QFileInfoList children =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &child : children) {
        if (!isSkippedDirectoryName(child.fileName())) {
            scan(QDir(child.absoluteFilePath()), depthLeft - 1, found);
        }
    }
}

} // namespace

QStringList findProjectDirectories(const QString &rootDirectory, int maxDepth)
{
    QStringList found;
    const QDir root(rootDirectory);
    if (root.exists()) {
        scan(root, maxDepth, found);
    }
    return found;
}

} // namespace kai::core
