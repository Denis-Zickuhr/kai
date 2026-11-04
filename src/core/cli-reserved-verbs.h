#pragma once

#include <QSet>
#include <QString>

namespace kai::core {

// Verbos/flags reservados do CLI. ÚNICA fonte de verdade, compartilhada por kai-file-validator.cpp
// (colisão de cli_path) e pelo runner de CLI Paths local (argv[1] é verbo ou candidato a cli_path).
inline const QSet<QString> &reservedCliVerbs()
{
    static const QSet<QString> tokens = {
        QStringLiteral("run"), QStringLiteral("list"), QStringLiteral("env"),
        QStringLiteral("show"), QStringLiteral("help"), QStringLiteral("import"),
        QStringLiteral("validate"), QStringLiteral("ps"), QStringLiteral("attach"),
        QStringLiteral("kill"), QStringLiteral("raise"),
        QStringLiteral("history"), QStringLiteral("last"), QStringLiteral("init"),
        QStringLiteral("completion"), QStringLiteral("__complete"), QStringLiteral("kip"), QStringLiteral("mcp"),
        QStringLiteral("--help"), QStringLiteral("-h"),
        QStringLiteral("--global"), QStringLiteral("-g"),
        QStringLiteral("--detached"), QStringLiteral("-d"),
        QStringLiteral("--notify"), QStringLiteral("-n"),
        QStringLiteral("--dry-run"), QStringLiteral("--json"),
    };
    return tokens;
}

} // namespace kai::core
