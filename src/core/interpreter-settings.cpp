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
    QString defaultValue;
    QString configured;
    switch (language) {
    case CommandLanguage::Python: defaultValue = QStringLiteral("python3"); configured = python; break;
    case CommandLanguage::Node:   defaultValue = QStringLiteral("node");    configured = node;   break;
    case CommandLanguage::Php:    defaultValue = QStringLiteral("php");     configured = php;    break;
    case CommandLanguage::Native: break;
    }
    configured = configured.trimmed();
    if (configured.isEmpty()) {
        configured = defaultValue;
    }
    if (nativeWindowsHost && configured == defaultValue && language == CommandLanguage::Python) {
        return QStringLiteral("python");
    }
    return configured;
}

} // namespace kai::core
