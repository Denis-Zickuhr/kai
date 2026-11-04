#include "utils/last-directory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>

namespace kai::utils {

namespace {
QMutex g_mutex;
QString g_filePath;
QString g_cached;
bool g_loaded = false;

QString stateFile()
{
    if (g_filePath.isEmpty()) {
        g_filePath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                         .filePath(QStringLiteral("last-directory.txt"));
    }
    return g_filePath;
}

void loadLocked()
{
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    QFile file(stateFile());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        g_cached = QString::fromUtf8(file.readAll()).trimmed();
    }
}
} // namespace

QString LastDirectory::get()
{
    QMutexLocker lock(&g_mutex);
    loadLocked();
    return (!g_cached.isEmpty() && QFileInfo(g_cached).isDir()) ? g_cached : QString();
}

void LastDirectory::remember(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QFileInfo info(trimmed);
    const QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        return;
    }
    QMutexLocker lock(&g_mutex);
    loadLocked();
    if (dir == g_cached) {
        return;
    }
    g_cached = dir;
    QDir().mkpath(QFileInfo(stateFile()).absolutePath());
    QSaveFile file(stateFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(dir.toUtf8());
        file.commit();
    }
}

QString LastDirectory::startFor(const QString &hint)
{
    const QString trimmed = hint.trimmed();
    if (!trimmed.isEmpty()) {
        const QFileInfo info(trimmed);
        if (info.exists()) {
            return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        }
    }
    return get();
}

QString LastDirectory::saveStartFor(const QString &suggestedName)
{
    const QString dir = get();
    return dir.isEmpty() ? suggestedName : QDir(dir).filePath(suggestedName);
}

void LastDirectory::resetForTests(const QString &filePath)
{
    QMutexLocker lock(&g_mutex);
    g_filePath = filePath;
    g_cached.clear();
    g_loaded = false;
}

} // namespace kai::utils
