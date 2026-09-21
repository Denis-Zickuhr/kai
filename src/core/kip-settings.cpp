#include "core/kip-settings.h"

#include <QtGlobal>

namespace kai::core {

namespace {
KipSettings &current()
{
    static KipSettings settings;
    return settings;
}
} // namespace

bool KipSettings::isValidDetachedWindowMode(const QString &mode)
{
    return mode == QLatin1String("preference") || mode == QLatin1String("normal")
        || mode == QLatin1String("maximized") || mode == QLatin1String("fullscreen");
}

KipSettings KipSettings::clamped() const
{
    KipSettings out = *this;
    out.handshakeTimeoutSec = qBound(kMinTimeoutSec, handshakeTimeoutSec, kMaxTimeoutSec);
    out.changeTimeoutSec = qBound(kMinTimeoutSec, changeTimeoutSec, kMaxTimeoutSec);
    out.cancelGraceSec = qBound(kMinGraceSec, cancelGraceSec, kMaxGraceSec);
    if (!isValidDetachedWindowMode(out.detachedWindowMode)) {
        out.detachedWindowMode = QStringLiteral("preference");
    }
    return out;
}

const KipSettings &kipSettings()
{
    return current();
}

void setKipSettings(const KipSettings &settings)
{
    current() = settings.clamped();
}

} // namespace kai::core
