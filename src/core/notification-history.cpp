#include "core/notification-history.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

namespace kai::core {

NotificationHistory::NotificationHistory(QObject *parent)
    : QObject(parent)
{
}

QString NotificationHistory::filePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("notifications.json"));
}

QVector<NotificationRecord> NotificationHistory::readAll() const
{
    QVector<NotificationRecord> records;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return records;
    }
    const QByteArray data = f.readAll();
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        return records; // corrupção -> vazio (seguro)
    }
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        NotificationRecord r;
        r.id = o.value("id").toString();
        r.eventKey = o.value("event_key").toString();
        r.title = o.value("title").toString();
        r.body = o.value("body").toString();
        r.createdAt = QDateTime::fromString(o.value("created_at").toString(), Qt::ISODate);
        r.read = o.value("read").toBool();
        records.append(r);
    }
    return records;
}

bool NotificationHistory::writeAll(const QVector<NotificationRecord> &records) const
{
    QJsonArray arr;
    for (const NotificationRecord &r : records) {
        QJsonObject o;
        o["id"] = r.id;
        o["event_key"] = r.eventKey;
        o["title"] = r.title;
        o["body"] = r.body;
        o["created_at"] = r.createdAt.toString(Qt::ISODate);
        o["read"] = r.read;
        arr.append(o);
    }
    const QString path = filePath();
    QFile tmp(path + QStringLiteral(".tmp"));
    if (!tmp.open(QIODevice::WriteOnly)) {
        return false;
    }
    tmp.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    tmp.close();
    QFile::remove(path);
    return tmp.rename(path);
}

QVector<NotificationRecord> NotificationHistory::load() const
{
    return readAll();
}

void NotificationHistory::append(const NotificationRecord &record)
{
    QVector<NotificationRecord> records = readAll();
    NotificationRecord r = record;
    if (r.id.isEmpty()) {
        r.id = QUuid::createUuid().toString(QUuid::Id128);
    }
    if (!r.createdAt.isValid()) {
        r.createdAt = QDateTime::currentDateTime();
    }
    records.prepend(r); // mais recente primeiro
    while (records.size() > kMaxRecords) {
        records.removeLast();
    }
    writeAll(records);
}

void NotificationHistory::markRead(const QString &id)
{
    QVector<NotificationRecord> records = readAll();
    bool changed = false;
    for (NotificationRecord &r : records) {
        if (r.id == id && !r.read) {
            r.read = true;
            changed = true;
            break;
        }
    }
    if (changed) {
        writeAll(records);
    }
}

void NotificationHistory::markAllRead()
{
    QVector<NotificationRecord> records = readAll();
    bool changed = false;
    for (NotificationRecord &r : records) {
        if (!r.read) {
            r.read = true;
            changed = true;
        }
    }
    if (changed) {
        writeAll(records);
    }
}

void NotificationHistory::clear()
{
    writeAll({});
}

int NotificationHistory::unreadCount() const
{
    int n = 0;
    for (const NotificationRecord &r : readAll()) {
        if (!r.read) {
            ++n;
        }
    }
    return n;
}

} // namespace kai::core
