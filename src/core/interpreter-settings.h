#pragma once

#include <QString>

#include "core/models.h"

namespace kai::core {

// Interpretadores globais das linguagens de comando (Configurações →
// Linguagens). Cada valor é uma LINHA DE SHELL resolvida dentro do alvo onde o
// comando roda (ex: no WSL vale o python3 do WSL), então pode ter argumentos
// ("uv run python") ou um caminho de venv/nvm; caminhos com espaço precisam
// vir entre aspas, como em qualquer shell.
struct InterpreterSettings {
    QString python = QStringLiteral("python3");
    QString node = QStringLiteral("node");

    // Interpretador efetivo: o override do comando (se houver) senão o global;
    // vazio/espaços em branco volta ao padrão da linguagem. Native devolve "".
    // `nativeWindowsHost` = comando sem alvo de terminal no Windows, onde o
    // "python3" é só um atalho da Microsoft Store: o padrão vira "python".
    QString resolve(CommandLanguage language, const QString &commandOverride,
                    bool nativeWindowsHost = false) const;

    bool operator==(const InterpreterSettings &other) const = default;
};

} // namespace kai::core
