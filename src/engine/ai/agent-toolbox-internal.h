#pragma once

// Detalhes compartilhados entre agent-toolbox.cpp e agent-toolbox-tools.cpp (não é API).
#include "engine/ai/agent-toolbox.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace kai::engine {

struct AgentToolbox::Tool {
    QString name;
    QString description;
    QJsonObject schema;
    ToolRisk risk = ToolRisk::Read;
    std::function<ToolPlan(const QJsonObject &)> planFn;   // nulo = plano padrão
    std::function<ToolOutcome(const QJsonObject &)> runFn; // run_command e ask_user não usam
};

struct AgentToolbox::Snapshot {
    QVector<core::Folder> folders;
    QVector<core::Command> commands;
    QStringList noteIds;
    QVector<core::Environment> environments;
};

namespace toolutil {

inline QString compact(const QJsonValue &v)
{
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isString()) return v.toString();
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isNull()) return QStringLiteral("null");
    return QString::number(v.toDouble(), 'g', 15);
}

inline QString clip(const QString &s, int max)
{
    return s.size() <= max ? s : s.left(max - 1) + QStringLiteral("…");
}

// Página de lista que sempre cabe no limite do resultado: os itens que não cabem ficam para a próxima chamada
// (`next_offset`). Sem isso o JSON era cortado no meio e o modelo nem via o total.
inline QJsonObject fitPage(const QJsonArray &items, int total, int offset)
{
    const int budget = AgentToolbox::kMaxResultChars - 600;
    int used = 0;
    QJsonArray kept;
    for (const QJsonValue &v : items) {
        const int size = compact(v).size() + 1;
        if (!kept.isEmpty() && used + size > budget) break;
        used += size;
        kept.append(v);
    }
    QJsonObject o{{QStringLiteral("total"), total}, {QStringLiteral("items"), kept}};
    if (offset + kept.size() < total) o[QStringLiteral("next_offset")] = offset + kept.size();
    return o;
}

inline ToolOutcome okValue(const QJsonValue &v) { return {AgentToolbox::limited(compact(v)), false, {}}; }
inline ToolOutcome okText(const QString &t) { return {AgentToolbox::limited(t), false, {}}; }
inline ToolOutcome fail(const QString &m) { return {m, true, {}}; }

inline ToolPlan invalid(const QString &m)
{
    ToolPlan p;
    p.valid = false;
    p.error = m;
    return p;
}

inline QJsonObject prop(const QString &type, const QString &description)
{
    return {{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
}

inline QJsonObject objectSchema(const QJsonObject &properties, const QStringList &required = {})
{
    QJsonObject s{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}};
    if (!required.isEmpty()) s[QStringLiteral("required")] = QJsonArray::fromStringList(required);
    return s;
}

inline int intArg(const QJsonObject &a, const QString &key, int def, int lo, int hi)
{
    return a.contains(key) ? std::clamp(a.value(key).toInt(def), lo, hi) : def;
}

inline QString strArg(const QJsonObject &a, const QString &key) { return a.value(key).toString().trimmed(); }

inline QString slug(const QString &name)
{
    QString s = name.toLower().trimmed();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    return s.isEmpty() ? QStringLiteral("sem_nome") : s;
}

} // namespace toolutil
} // namespace kai::engine
