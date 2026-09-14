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

// Decide, SEM construir nenhum QApplication/QCoreApplication, se `args`
// será tratado como CLI (verbo conhecido, OU invocação solta num terminal
// interativo — ver stdoutIsInteractiveTerminal em cli-client.cpp). Chamado
// pelo main() ANTES de escolher entre QCoreApplication (leve, sem GUI) e
// QApplication (só quando vai realmente abrir a janela) — achado real:
// construir QApplication incondicionalmente pra depois descartar (CLI
// puro) inicializa tema/plataforma gráfica à toa, e é exatamente isso que
// disparava o aviso "QStandardPaths: wrong permissions on runtime
// directory" em `kai list`/`kai ps`/`kai` solto (mas não em `kai ping`,
// que já usava só QCoreApplication via o runner de CLI Path local —
// mesma causa, reportado com print real do terminal).
bool shouldHandleAsCli(const QStringList &args);

} // namespace kai::ipc
