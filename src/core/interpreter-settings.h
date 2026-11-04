#pragma once

#include <QString>

#include "core/models.h"

namespace kai::core {

// Interpretadores globais das linguagens de comando. Cada valor é uma LINHA DE SHELL resolvida dentro
// do alvo onde o comando roda (no WSL vale o python3 do WSL): aceita argumentos ("uv run python") ou
// caminho de venv/nvm; caminhos com espaço vêm entre aspas.
struct InterpreterSettings {
    QString python = QStringLiteral("python3");
    QString node = QStringLiteral("node");
    QString php = QStringLiteral("php");

    // Interpretador efetivo: o override do comando, senão o global; vazio volta ao padrão da linguagem.
    // Native devolve "". `nativeWindowsHost` = comando sem alvo de terminal no Windows, onde "python3"
    // é só atalho da Microsoft Store (o padrão vira "python").
    QString resolve(CommandLanguage language, const QString &commandOverride,
                    bool nativeWindowsHost = false) const;

    bool operator==(const InterpreterSettings &other) const = default;
};

} // namespace kai::core
