#include "core/interpreter-settings.h"

namespace kai::core {

QString InterpreterSettings::resolve(CommandLanguage language, const QString &commandOverride,
                                     bool nativeWindowsHost) const
{
    if (language == CommandLanguage::Native) {
        return QString();
    }
    const QString override = commandOverride.trimmed();
    if (!override.isEmpty()) {
        return override;
    }
    const bool isPython = language == CommandLanguage::Python;
    const QString defaultValue = isPython ? QStringLiteral("python3") : QStringLiteral("node");
    QString configured = (isPython ? python : node).trimmed();
    if (configured.isEmpty()) {
        configured = defaultValue;
    }
    if (isPython && nativeWindowsHost && configured == defaultValue) {
        return QStringLiteral("python");
    }
    return configured;
}

} // namespace kai::core
