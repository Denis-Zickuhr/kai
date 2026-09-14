#pragma once

#include <QString>

namespace kai::utils {

// Duração curta pra mensagens: "12s", "2m 05s", "1h 02m".
inline QString formatShortDuration(qint64 ms)
{
    const qint64 totalSeconds = ms / 1000;
    if (totalSeconds < 60) {
        return QStringLiteral("%1s").arg(totalSeconds);
    }
    if (totalSeconds < 3600) {
        return QStringLiteral("%1m %2s").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1h %2m").arg(totalSeconds / 3600).arg((totalSeconds % 3600) / 60, 2, 10, QLatin1Char('0'));
}

} // namespace kai::utils
