#pragma once

#include <QSet>
#include <QString>

namespace kai::core {

// Verbos/flags reservados do CLI do Kai (ver ipc::runCliIfRequested e o
// futuro despacho de CLI Paths locais) — compartilhado entre
// kai-file-validator.cpp (checagem estática de colisão de cli_path) e o
// runner de CLI Paths local (decide se argv[1] é um verbo conhecido ou um
// candidato a cli_path). ÚNICA fonte de verdade — duplicar esta lista em
// dois lugares seria um risco real de os dois desalinharem com o tempo.
inline const QSet<QString> &reservedCliVerbs()
{
    static const QSet<QString> tokens = {
        QStringLiteral("run"), QStringLiteral("list"), QStringLiteral("env"),
        QStringLiteral("show"), QStringLiteral("help"), QStringLiteral("import"),
        QStringLiteral("validate"), QStringLiteral("ps"), QStringLiteral("attach"),
        QStringLiteral("kill"), QStringLiteral("raise"),
        QStringLiteral("history"), QStringLiteral("last"), QStringLiteral("init"),
        QStringLiteral("completion"), QStringLiteral("__complete"), QStringLiteral("kip"),
        QStringLiteral("--help"), QStringLiteral("-h"),
        QStringLiteral("--global"), QStringLiteral("-g"),
        QStringLiteral("--detached"), QStringLiteral("-d"),
        QStringLiteral("--notify"), QStringLiteral("-n"),
        QStringLiteral("--dry-run"), QStringLiteral("--json"),
    };
    return tokens;
}

} // namespace kai::core
