#pragma once

#include <QMap>
#include <QString>

#include "core/config-manager.h"

namespace kai::engine::detail {

inline constexpr const char *kEnvSentinel = "__KAI_ENV_CAPTURE_7f3a91__";

core::ShellFlavor effectiveShellFlavor(const core::TerminalProfile &t);
QString posixEnvExports(const QMap<QString, QString> &env);
QString buildTargetedCommand(core::ShellFlavor flavor,
                             const QMap<QString, QString> &resolvedEnv,
                             const QString &interpolatedCommand,
                             const QString &interpolatedWorkingDir,
                             const QString &remotePrefixPosix);

} // namespace kai::engine::detail
