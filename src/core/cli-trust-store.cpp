#include "core/cli-trust-store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace kai::core {

CliTrustStore::CliTrustStore(const QString &storePath)
    : m_storePath(storePath)
{
}

QString CliTrustStore::filePath() const
{
    if (!m_storePath.isEmpty()) {
        return m_storePath;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("cli-trust.json"));
}

QString CliTrustStore::hashContent(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool CliTrustStore::isTrusted(const QString &directoryPath, const QString &contentHash) const
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) {
        return false;
    }
    return doc.object().value(directoryPath).toString() == contentHash;
}

void CliTrustStore::trust(const QString &directoryPath, const QString &contentHash)
{
    QJsonObject obj;
    QFile readF(filePath());
    if (readF.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(readF.readAll());
        readF.close();
        if (doc.isObject()) {
            obj = doc.object();
        }
    }
    obj.insert(directoryPath, contentHash);

    const QString path = filePath();
    QFile tmp(path + QStringLiteral(".tmp"));
    if (!tmp.open(QIODevice::WriteOnly)) {
        return;
    }
    tmp.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    tmp.close();
    QFile::remove(path);
    tmp.rename(path);
}

} // namespace kai::core
