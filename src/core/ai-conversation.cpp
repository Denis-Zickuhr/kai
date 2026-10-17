#include "core/ai-conversation.h"

#include "utils/logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

namespace kai::core {

namespace {
constexpr const char *kLogTag = "AiConversation";

bool isSafeId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return re.match(id).hasMatch();
}

QDateTime parseTime(const QJsonValue &v)
{
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}

QString timeString(const QDateTime &t)
{
    return t.toUTC().toString(Qt::ISODateWithMs);
}
} // namespace

void AiUsage::add(const AiUsage &other)
{
    inputTokens += other.inputTokens;
    outputTokens += other.outputTokens;
    cacheReadTokens += other.cacheReadTokens;
    cacheWriteTokens += other.cacheWriteTokens;
}

QJsonObject AiUsage::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("input")] = inputTokens;
    o[QStringLiteral("output")] = outputTokens;
    if (cacheReadTokens) o[QStringLiteral("cache_read")] = cacheReadTokens;
    if (cacheWriteTokens) o[QStringLiteral("cache_write")] = cacheWriteTokens;
    return o;
}

AiUsage AiUsage::fromJson(const QJsonObject &obj)
{
    AiUsage u;
    u.inputTokens = obj.value(QStringLiteral("input")).toInt();
    u.outputTokens = obj.value(QStringLiteral("output")).toInt();
    u.cacheReadTokens = obj.value(QStringLiteral("cache_read")).toInt();
    u.cacheWriteTokens = obj.value(QStringLiteral("cache_write")).toInt();
    return u;
}

QString aiRoleToString(AiMessage::Role role)
{
    switch (role) {
    case AiMessage::Role::User: return QStringLiteral("user");
    case AiMessage::Role::Assistant: return QStringLiteral("assistant");
    case AiMessage::Role::Tool: return QStringLiteral("tool");
    }
    return QStringLiteral("user");
}

AiMessage::Role aiRoleFromString(const QString &value)
{
    if (value == QLatin1String("assistant")) return AiMessage::Role::Assistant;
    if (value == QLatin1String("tool")) return AiMessage::Role::Tool;
    return AiMessage::Role::User;
}

QJsonObject AiMessage::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("role")] = aiRoleToString(role);
    if (!text.isEmpty()) o[QStringLiteral("text")] = text;
    if (!toolCalls.isEmpty()) {
        QJsonArray arr;
        for (const AiToolCall &c : toolCalls) {
            arr.append(QJsonObject{{QStringLiteral("id"), c.id},
                                   {QStringLiteral("name"), c.name},
                                   {QStringLiteral("arguments"), c.arguments}});
        }
        o[QStringLiteral("tool_calls")] = arr;
    }
    if (!toolResults.isEmpty()) {
        QJsonArray arr;
        for (const AiToolResult &r : toolResults) {
            QJsonObject ro{{QStringLiteral("tool_call_id"), r.toolCallId},
                           {QStringLiteral("name"), r.name},
                           {QStringLiteral("content"), r.content}};
            if (r.isError) ro[QStringLiteral("is_error")] = true;
            if (r.denied) ro[QStringLiteral("denied")] = true;
            if (!r.changeId.isEmpty()) ro[QStringLiteral("change_id")] = r.changeId;
            arr.append(ro);
        }
        o[QStringLiteral("tool_results")] = arr;
    }
    if (!profileId.isEmpty()) o[QStringLiteral("profile_id")] = profileId;
    if (!model.isEmpty()) o[QStringLiteral("model")] = model;
    if (!usage.isEmpty()) o[QStringLiteral("usage")] = usage.toJson();
    if (at.isValid()) o[QStringLiteral("at")] = timeString(at);
    return o;
}

AiMessage AiMessage::fromJson(const QJsonObject &obj)
{
    AiMessage m;
    m.role = aiRoleFromString(obj.value(QStringLiteral("role")).toString());
    m.text = obj.value(QStringLiteral("text")).toString();
    for (const QJsonValue &v : obj.value(QStringLiteral("tool_calls")).toArray()) {
        const QJsonObject c = v.toObject();
        m.toolCalls.append(AiToolCall{c.value(QStringLiteral("id")).toString(), c.value(QStringLiteral("name")).toString(),
                            c.value(QStringLiteral("arguments")).toObject()});
    }
    for (const QJsonValue &v : obj.value(QStringLiteral("tool_results")).toArray()) {
        const QJsonObject r = v.toObject();
        AiToolResult res;
        res.toolCallId = r.value(QStringLiteral("tool_call_id")).toString();
        res.name = r.value(QStringLiteral("name")).toString();
        res.content = r.value(QStringLiteral("content")).toString();
        res.isError = r.value(QStringLiteral("is_error")).toBool();
        res.denied = r.value(QStringLiteral("denied")).toBool();
        res.changeId = r.value(QStringLiteral("change_id")).toString();
        m.toolResults.append(res);
    }
    m.profileId = obj.value(QStringLiteral("profile_id")).toString();
    m.model = obj.value(QStringLiteral("model")).toString();
    m.usage = AiUsage::fromJson(obj.value(QStringLiteral("usage")).toObject());
    m.at = parseTime(obj.value(QStringLiteral("at")));
    return m;
}

QJsonObject AiConversation::toJson() const
{
    QJsonArray arr;
    for (const AiMessage &m : messages) {
        arr.append(m.toJson());
    }
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("title")] = title;
    o[QStringLiteral("created_at")] = timeString(createdAt);
    o[QStringLiteral("updated_at")] = timeString(updatedAt);
    o[QStringLiteral("profile_id")] = profileId;
    o[QStringLiteral("usage")] = usage.toJson();
    o[QStringLiteral("messages")] = arr;
    return o;
}

AiConversation AiConversation::fromJson(const QJsonObject &obj)
{
    AiConversation c;
    c.id = obj.value(QStringLiteral("id")).toString();
    c.title = obj.value(QStringLiteral("title")).toString();
    c.createdAt = parseTime(obj.value(QStringLiteral("created_at")));
    c.updatedAt = parseTime(obj.value(QStringLiteral("updated_at")));
    c.profileId = obj.value(QStringLiteral("profile_id")).toString();
    c.usage = AiUsage::fromJson(obj.value(QStringLiteral("usage")).toObject());
    for (const QJsonValue &v : obj.value(QStringLiteral("messages")).toArray()) {
        c.messages.append(AiMessage::fromJson(v.toObject()));
    }
    return c;
}

QString AiConversation::titleFromText(const QString &text)
{
    QString t = text.simplified();
    if (t.size() > 60) {
        t = t.left(57).trimmed() + QStringLiteral("...");
    }
    return t;
}

AiConversationStore::AiConversationStore(const QString &directory)
    : m_directory(directory)
{
}

QString AiConversationStore::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString AiConversationStore::pathFor(const QString &id) const
{
    return QDir(m_directory).filePath(id + QStringLiteral(".json"));
}

QString AiConversationStore::indexPath() const
{
    return QDir(m_directory).filePath(QStringLiteral("index.json"));
}

QVector<AiConversationInfo> AiConversationStore::scan() const
{
    QVector<AiConversationInfo> infos;
    const QDir dir(m_directory);
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        if (fi.fileName() == QLatin1String("index.json")) {
            continue;
        }
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        const AiConversation c = AiConversation::fromJson(doc.object());
        if (!isSafeId(c.id)) {
            continue;
        }
        infos.append(AiConversationInfo{c.id, c.title, c.updatedAt, c.profileId, int(c.messages.size()), fi.size()});
    }
    std::sort(infos.begin(), infos.end(),
              [](const AiConversationInfo &a, const AiConversationInfo &b) { return a.updatedAt > b.updatedAt; });
    return infos;
}

QVector<AiConversationInfo> AiConversationStore::list() const
{
    QFile f(indexPath());
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            QVector<AiConversationInfo> infos;
            for (const QJsonValue &v : doc.object().value(QStringLiteral("conversations")).toArray()) {
                const QJsonObject o = v.toObject();
                AiConversationInfo info;
                info.id = o.value(QStringLiteral("id")).toString();
                // Entrada cujo arquivo sumiu não conta.
                if (!isSafeId(info.id) || !QFile::exists(pathFor(info.id))) {
                    continue;
                }
                info.title = o.value(QStringLiteral("title")).toString();
                info.updatedAt = parseTime(o.value(QStringLiteral("updated_at")));
                info.profileId = o.value(QStringLiteral("profile_id")).toString();
                info.messageCount = o.value(QStringLiteral("messages")).toInt();
                info.bytes = qint64(o.value(QStringLiteral("bytes")).toDouble());
                infos.append(info);
            }
            std::sort(infos.begin(), infos.end(), [](const AiConversationInfo &a, const AiConversationInfo &b) {
                return a.updatedAt > b.updatedAt;
            });
            return infos;
        }
    }
    return scan();
}

AiConversation AiConversationStore::load(const QString &id) const
{
    if (!isSafeId(id)) {
        return {};
    }
    QFile f(pathFor(id));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        utils::Logger::warning(kLogTag, QStringLiteral("Conversa '%1' ilegível; ignorada.").arg(id));
        return {};
    }
    return AiConversation::fromJson(doc.object());
}

bool AiConversationStore::writeIndex(const QVector<AiConversationInfo> &infos) const
{
    QJsonArray arr;
    for (const AiConversationInfo &i : infos) {
        arr.append(QJsonObject{{QStringLiteral("id"), i.id},
                               {QStringLiteral("title"), i.title},
                               {QStringLiteral("updated_at"), timeString(i.updatedAt)},
                               {QStringLiteral("profile_id"), i.profileId},
                               {QStringLiteral("messages"), i.messageCount},
                               {QStringLiteral("bytes"), double(i.bytes)}});
    }
    QSaveFile f(indexPath());
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("conversations"), arr}}).toJson(QJsonDocument::Compact));
    return f.commit();
}

void AiConversationStore::enforceLimits(QVector<AiConversationInfo> &infos) const
{
    auto totalBytes = [&infos]() {
        qint64 total = 0;
        for (const AiConversationInfo &i : infos) total += i.bytes;
        return total;
    };
    // `infos` está do mais recente para o mais antigo: o que passa do teto sai pelo fim.
    while (!infos.isEmpty() && (infos.size() > kMaxConversations || totalBytes() > kMaxTotalBytes)) {
        if (infos.size() == 1) {
            break; // uma conversa só nunca se apaga sozinha
        }
        QFile::remove(pathFor(infos.last().id));
        infos.removeLast();
    }
}

bool AiConversationStore::save(AiConversation conversation)
{
    if (!isSafeId(conversation.id)) {
        return false;
    }
    if (!QDir().mkpath(m_directory)) {
        return false;
    }
    // Conversa gigante: guarda as mensagens mais recentes (a primeira, que dá o assunto, fica).
    if (conversation.messages.size() > kMaxMessagesPerConversation) {
        const int drop = conversation.messages.size() - kMaxMessagesPerConversation;
        conversation.messages.remove(1, drop);
        // Não deixa uma resposta de ferramenta órfã (sem a chamada que a originou) logo após o início.
        while (conversation.messages.size() > 1 && conversation.messages.at(1).role == AiMessage::Role::Tool) {
            conversation.messages.remove(1);
        }
    }
    if (!conversation.updatedAt.isValid()) {
        conversation.updatedAt = QDateTime::currentDateTimeUtc();
    }
    if (!conversation.createdAt.isValid()) {
        conversation.createdAt = conversation.updatedAt;
    }

    QSaveFile f(pathFor(conversation.id));
    if (!f.open(QIODevice::WriteOnly)) {
        utils::Logger::error(kLogTag, QStringLiteral("Não foi possível gravar a conversa '%1'.").arg(conversation.id));
        return false;
    }
    // O arquivo guarda saídas de comandos: só o dono lê.
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(conversation.toJson()).toJson(QJsonDocument::Compact));
    if (!f.commit()) {
        return false;
    }

    QVector<AiConversationInfo> infos = list();
    infos.removeIf([&conversation](const AiConversationInfo &i) { return i.id == conversation.id; });
    infos.prepend(AiConversationInfo{conversation.id, conversation.title, conversation.updatedAt, conversation.profileId,
                   int(conversation.messages.size()), QFileInfo(pathFor(conversation.id)).size()});
    std::sort(infos.begin(), infos.end(),
              [](const AiConversationInfo &a, const AiConversationInfo &b) { return a.updatedAt > b.updatedAt; });
    enforceLimits(infos);
    return writeIndex(infos);
}

bool AiConversationStore::remove(const QString &id)
{
    if (!isSafeId(id)) {
        return false;
    }
    const bool removed = QFile::remove(pathFor(id));
    QVector<AiConversationInfo> infos = list();
    infos.removeIf([&id](const AiConversationInfo &i) { return i.id == id; });
    writeIndex(infos);
    return removed;
}

int AiConversationStore::clearAll()
{
    int removed = 0;
    const QDir dir(m_directory);
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        if (QFile::remove(fi.absoluteFilePath()) && fi.fileName() != QLatin1String("index.json")) {
            ++removed;
        }
    }
    return removed;
}

} // namespace kai::core
