#pragma once

#include <QMap>
#include <QString>

#include "core/models.h"

namespace kai::engine {

// Comandos com linguagem (Python/Node): o texto do comando é CÓDIGO, não uma
// linha de shell. O Kai o entrega ao interpretador como argumento de -c / -e,
// sem passar por aspas de shell: o código vai comprimido + base64 e é
// decodificado pelo PRÓPRIO interpretador. Assim
//   - não existe problema de aspas/heredoc/quebras de linha;
//   - o stdin fica livre (o KIP lê as respostas dele);
//   - a linha resultante é só mais um comando de shell, então passa por
//     qualquer alvo de terminal (WSL, Docker...) e sabor (POSIX/PowerShell/
//     Cmd), sem depender de `base64` ou `$(...)` do outro lado;
//   - o comprimido mantém o comprimento abaixo dos limites de linha de
//     comando (cmd.exe: 8191).
//
// `interpreter` já vem resolvido (core::InterpreterSettings::resolve) e é
// usado cru — pode ter argumentos. O módulo `kai` embutido (Python: `import kai`;
// Node: global `kai` e require('kai')) é registrado antes do código; com
// `withKip`, o `kip` também. Retorna vazio para CommandLanguage::Native.
QString buildInterpreterCommandLine(core::CommandLanguage language, const QString &interpreter,
                                    const QString &code, bool withKip);

// Variáveis de ambiente PADRÃO da linguagem (KAI_IPC_SOCKET para o módulo
// `kai`; saída sem buffer e UTF-8 no Python). Quem monta o ambiente deixa o
// ambiente do usuário sobrepor.
QMap<QString, QString> languageEnvDefaults(core::CommandLanguage language);

// O Windows executa a linha por `cmd.exe /c`, que corta em 8191 caracteres — e o
// template do alvo WSL ainda faz um segundo base64 por cima. Acima deste limite
// (com folga) o comando corre o risco de ser cortado em silêncio.
constexpr int kWindowsCommandLineSoftLimit = 8000;
inline bool exceedsWindowsCommandLine(const QString &finalCommand)
{
    return finalCommand.size() > kWindowsCommandLineSoftLimit;
}

// Fontes dos módulos embutidos (vazio para Native): `kip` (protocolo KIP) e
// `kai` (fala com o app pelo IPC: notificações, ambientes, processos...).
QString kipModuleSource(core::CommandLanguage language);
QString kaiModuleSource(core::CommandLanguage language);

} // namespace kai::engine
