#pragma once

#include <QStringList>

namespace kai::ipc {

// Resultado da tentativa de tratar os argumentos como um comando de CLI.
struct CliOutcome {
    bool handled = false; // true = era um comando de CLI (o main deve sair)
    int exitCode = 0;
};

// Interpreta os argumentos da linha de comando. Se for um verbo de CLI
// (run/list/env/show/help), conecta ao Kai em execução via IPC, imprime a
// resposta em stdout/stderr e retorna handled=true (o main deve sair com
// exitCode). Se não for um verbo de CLI (ex: abrir a GUI normalmente),
// retorna handled=false.
//
// Verbos suportados:
//   kai run <nome-do-comando>
//   kai list
//   kai env list
//   kai env use <nome-do-environment>
//   kai validate <arquivo.json|arquivo.yml>
//   kai show
//   kai help
CliOutcome runCliIfRequested(const QStringList &args);

} // namespace kai::ipc
