#pragma once

#include <QStringList>

namespace kai::ipc {

// Resultado da tentativa de tratar os argumentos como um comando de CLI.
struct CliOutcome {
    bool handled = false; // true = era um comando de CLI (o main deve sair)
    int exitCode = 0;
};

// Interpreta os argumentos da linha de comando. Se for um verbo de CLI, conecta ao Kai em execução via IPC, imprime a
// resposta em stdout/stderr e retorna handled=true (o main deve sair com exitCode); senão handled=false (abre a GUI).
CliOutcome runCliIfRequested(const QStringList &args);

// Decide, SEM construir QApplication/QCoreApplication, se `args` será tratado como CLI (verbo conhecido, ou `kai` solto
// num terminal interativo; ver stdoutIsInteractiveTerminal). O main() chama antes de escolher entre QCoreApplication
// (leve) e QApplication: construir o segundo à toa inicializa o tema/plataforma gráfica e dispara o aviso
// "QStandardPaths: wrong permissions on runtime directory" em `kai list`/`kai ps`.
bool shouldHandleAsCli(const QStringList &args);

} // namespace kai::ipc
