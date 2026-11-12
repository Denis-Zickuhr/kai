#include "core/palette-usage.h"

#include <QJsonValue>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace kai::core {

namespace {
constexpr double kHalfLifeDays = 14.0;
constexpr qint64 kMsPerDay = 24LL * 60 * 60 * 1000;
}

double PaletteUsage::weight(const Use &use, qint64 nowMs)
{
    const double days = std::max<qint64>(0, nowMs - use.lastMs) / double(kMsPerDay);
    return use.count * std::pow(0.5, days / kHalfLifeDays);
}

void PaletteUsage::record(const QString &key, qint64 nowMs)
{
    if (key.isEmpty()) {
        return;
    }
    Use &use = m_uses[key];
    use.count = std::min(use.count + 1, 1000000);
    use.lastMs = nowMs;

    if (m_uses.size() > kMaxEntries) {
        QVector<QPair<double, QString>> ranked;
        ranked.reserve(int(m_uses.size()));
        for (auto it = m_uses.constBegin(); it != m_uses.constEnd(); ++it) {
            ranked.append({weight(it.value(), nowMs), it.key()});
        }
        std::sort(ranked.begin(), ranked.end());
        const int drop = int(m_uses.size()) - kMaxEntries;
        for (int i = 0; i < drop; ++i) {
            m_uses.remove(ranked.at(i).second);
        }
    }
}

double PaletteUsage::score(const QString &key, qint64 nowMs) const
{
    const auto it = m_uses.constFind(key);
    return it == m_uses.constEnd() ? 0.0 : weight(it.value(), nowMs);
}

QJsonObject PaletteUsage::toJson() const
{
    QJsonObject uses;
    for (auto it = m_uses.constBegin(); it != m_uses.constEnd(); ++it) {
        QJsonObject entry;
        entry["count"] = it.value().count;
        entry["last_ms"] = double(it.value().lastMs);
        uses[it.key()] = entry;
    }
    QJsonObject root;
    root["usage"] = uses;
    return root;
}

PaletteUsage PaletteUsage::fromJson(const QJsonObject &root)
{
    PaletteUsage usage;
    const QJsonObject uses = root.value("usage").toObject();
    for (auto it = uses.constBegin(); it != uses.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        Use use;
        use.count = entry.value("count").toInt();
        use.lastMs = qint64(entry.value("last_ms").toDouble());
        if (use.count > 0 && use.lastMs > 0) {
            usage.m_uses.insert(it.key(), use);
        }
    }
    return usage;
}

} // namespace kai::core
