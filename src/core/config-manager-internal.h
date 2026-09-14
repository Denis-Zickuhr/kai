#pragma once

#include <QString>

#include "core/config-manager.h"

namespace kai::core::detail {

QString shellFlavorToString(ShellFlavor s);
ShellFlavor shellFlavorFromString(const QString &s);
bool migratePowerShellTemplate(QString &tpl);

} // namespace kai::core::detail
