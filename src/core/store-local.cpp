#include "core/store-local.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace kai::core {

namespace {

QJsonDocument readDoc(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = file.readAll();
    file.close();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        QFile::remove(path + QStringLiteral(".corrupt"));
        QFile::rename(path, path + QStringLiteral(".corrupt"));
        return {};
    }
    return doc;
}

bool writeDoc(const QString &path, const QJsonDocument &doc, bool ownerOnly)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (ownerOnly) {
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    return file.commit();
}

} // namespace

StoreLocalState::StoreLocalState(const QString &configDir) : m_dir(configDir) {}

QString StoreLocalState::providersPath() const
{
    return QDir(m_dir).filePath(QStringLiteral("store-providers.json"));
}

QString StoreLocalState::installedPath() const
{
    return QDir(m_dir).filePath(QStringLiteral("store-installed.json"));
}

QString StoreLocalState::newProviderId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

QVector<StoreProviderConfig> StoreLocalState::loadProviders() const
{
    QVector<StoreProviderConfig> out;
    for (const QJsonValue &v : readDoc(providersPath()).object().value(QStringLiteral("providers")).toArray()) {
        StoreProviderConfig p = StoreProviderConfig::fromJson(v.toObject());
        if (p.id.isEmpty() || p.location.isEmpty()) continue;
        out << p;
    }
    return out;
}

bool StoreLocalState::saveProviders(const QVector<StoreProviderConfig> &providers) const
{
    QJsonArray array;
    for (const StoreProviderConfig &p : providers) array.append(p.toJson());
    return writeDoc(providersPath(), QJsonDocument(QJsonObject{{QStringLiteral("providers"), array}}), true);
}

QVector<StoreInstalledRecord> StoreLocalState::loadInstalled() const
{
    QVector<StoreInstalledRecord> out;
    for (const QJsonValue &v : readDoc(installedPath()).object().value(QStringLiteral("installed")).toArray()) {
        const QJsonObject o = v.toObject();
        StoreInstalledRecord r;
        r.provider = o.value(QStringLiteral("provider")).toString();
        r.package = o.value(QStringLiteral("package")).toString();
        r.version = o.value(QStringLiteral("version")).toString();
        r.kind = o.value(QStringLiteral("kind")).toString();
        r.contentHash = o.value(QStringLiteral("sha256")).toString();
        r.folderId = o.value(QStringLiteral("folderId")).toString();
        r.cloneDir = o.value(QStringLiteral("cloneDir")).toString();
        r.installedAt = QDateTime::fromString(o.value(QStringLiteral("installedAt")).toString(), Qt::ISODate);
        if (r.package.isEmpty()) continue;
        out << r;
    }
    return out;
}

bool StoreLocalState::saveInstalled(const QVector<StoreInstalledRecord> &records) const
{
    QJsonArray array;
    for (const StoreInstalledRecord &r : records) {
        QJsonObject o;
        o.insert(QStringLiteral("provider"), r.provider);
        o.insert(QStringLiteral("package"), r.package);
        o.insert(QStringLiteral("version"), r.version);
        o.insert(QStringLiteral("kind"), r.kind);
        o.insert(QStringLiteral("sha256"), r.contentHash);
        if (!r.folderId.isEmpty()) o.insert(QStringLiteral("folderId"), r.folderId);
        if (!r.cloneDir.isEmpty()) o.insert(QStringLiteral("cloneDir"), r.cloneDir);
        o.insert(QStringLiteral("installedAt"), r.installedAt.toUTC().toString(Qt::ISODate));
        array.append(o);
    }
    return writeDoc(installedPath(), QJsonDocument(QJsonObject{{QStringLiteral("installed"), array}}), false);
}

bool StoreLocalState::recordInstall(const StoreInstalledRecord &record) const
{
    QVector<StoreInstalledRecord> all = loadInstalled();
    // Uma instalação "como cópia" convive com a original: só a MESMA pasta é substituída.
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&record](const StoreInstalledRecord &r) {
                                 return r.provider == record.provider && r.package == record.package
                                     && r.folderId == record.folderId;
                             }),
              all.end());
    all << record;
    return saveInstalled(all);
}

bool StoreLocalState::removeRecordForFolder(const QString &folderId) const
{
    QVector<StoreInstalledRecord> all = loadInstalled();
    const int before = all.size();
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&folderId](const StoreInstalledRecord &r) { return r.folderId == folderId; }),
              all.end());
    return all.size() == before ? true : saveInstalled(all);
}

bool StoreLocalState::removeRecord(const QString &provider, const QString &package, const QString &folderId) const
{
    QVector<StoreInstalledRecord> all = loadInstalled();
    const int before = all.size();
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&](const StoreInstalledRecord &r) {
                                 return r.provider == provider && r.package == package && r.folderId == folderId;
                             }),
              all.end());
    return all.size() == before ? true : saveInstalled(all);
}

// --- cache --------------------------------------------------------------------------------------------------------

static QString cacheRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("store"));
}

QString storeCacheDir(const QString &providerId)
{
    return QDir(cacheRoot()).filePath(providerId.isEmpty() ? QStringLiteral("_") : providerId);
}

qint64 storeCacheSize()
{
    qint64 total = 0;
    QDirIterator it(cacheRoot(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

void trimStoreCache(qint64 maxBytes)
{
    struct Entry {
        QString path;
        qint64 size;
        QDateTime time;
    };
    QVector<Entry> files;
    qint64 total = 0;
    QDirIterator it(cacheRoot(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        // Um checkout do git não é "cache de arquivos soltos": apagá-lo pela metade quebraria o repositório.
        if (it.filePath().contains(QStringLiteral("/checkout/"))) continue;
        files << Entry{it.filePath(), it.fileInfo().size(), it.fileInfo().lastModified()};
        total += it.fileInfo().size();
    }
    std::sort(files.begin(), files.end(), [](const Entry &a, const Entry &b) { return a.time < b.time; });
    for (const Entry &f : files) {
        if (total <= maxBytes) break;
        if (QFile::remove(f.path)) total -= f.size;
    }
}

bool clearStoreCache()
{
    return QDir(cacheRoot()).removeRecursively();
}

} // namespace kai::core
