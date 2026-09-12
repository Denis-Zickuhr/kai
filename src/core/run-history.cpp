#include "core/run-history.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

namespace kai::core {

RunHistory::RunHistory(QObject *parent)
    : QObject(parent)
{
}

QString RunHistory::filePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("runs.json"));
}

QVector<RunRecord> RunHistory::readAll() const
{
    QVector<RunRecord> records;
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
        RunRecord r;
        r.id = o.value("id").toString();
        r.commandId = o.value("command_id").toString();
        r.commandName = o.value("command_name").toString();
        r.commandType = o.value("command_type").toString();
        r.startedAt = QDateTime::fromString(o.value("started_at").toString(), Qt::ISODate);
        r.durationMs = static_cast<qint64>(o.value("duration_ms").toDouble());
        r.success = o.value("success").toBool();
        r.output = o.value("output").toString();
        records.append(r);
    }
    return records;
}

bool RunHistory::writeAll(const QVector<RunRecord> &records) const
{
    QJsonArray arr;
    for (const RunRecord &r : records) {
        QJsonObject o;
        o["id"] = r.id;
        o["command_id"] = r.commandId;
        o["command_name"] = r.commandName;
        o["command_type"] = r.commandType;
        o["started_at"] = r.startedAt.toString(Qt::ISODate);
        o["duration_ms"] = static_cast<double>(r.durationMs);
        o["success"] = r.success;
        o["output"] = r.output;
        arr.append(o);
    }
    // Escrita atômica (tmp + rename).
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

QVector<RunRecord> RunHistory::load() const
{
    return readAll();
}

void RunHistory::append(const RunRecord &record)
{
    QVector<RunRecord> records = readAll();
    RunRecord r = record;
    if (r.id.isEmpty()) {
        r.id = QUuid::createUuid().toString(QUuid::Id128);
    }
    if (r.output.size() > kMaxOutputChars) {
        r.output = r.output.right(kMaxOutputChars);
    }
    records.prepend(r); // mais recente primeiro
    while (records.size() > kMaxRecords) {
        records.removeLast();
    }
    writeAll(records);
}

void RunHistory::clear()
{
    writeAll({});
}

} // namespace kai::core
