#include "ui/shared/window-translucency.h"

#include <QGuiApplication>
#include <QString>

namespace kai::ui {

bool shouldUseTranslucentWindow()
{
#if defined(Q_OS_WIN)
    return false;
#else
    if (qEnvironmentVariableIsSet("KAI_NO_TRANSLUCENT_WINDOW")
        || qEnvironmentVariableIsSet("WSL_DISTRO_NAME")
        || qEnvironmentVariableIsSet("WSL_INTEROP")) {
        return false;
    }
    const QString platform = QGuiApplication::platformName();
    return !(platform.contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)
             || platform.contains(QStringLiteral("minimal"), Qt::CaseInsensitive));
#endif
}

} // namespace kai::ui
