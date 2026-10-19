#pragma once

#include <QString>
#include <QStringList>

class QTextStream;

namespace kai::utils {

// De onde vêm stdin/stdout/stderr do processo e se há um terminal humano
// atrás deles. Centraliza o que antes estava duplicado em ipc/cli-client.cpp
// e cli/cli-local-executor.cpp.
//
// Windows: kai.exe é WIN32 subsystem (sem console próprio). Medido com um
// .exe de diagnóstico chamado do WSL: a interop JÁ entrega stdin/stdout/
// stderr como pipes válidos, ligados ao pty do Linux — e o pai do processo é
// o wsl.exe. O AttachConsole(ATTACH_PARENT_PROCESS) antigo se anexava ao
// console DESSE wsl.exe (o da aba do terminal) e o freopen("CONOUT$")
// desviava a saída pra lá, por fora da interop. Consequências reais: o
// teclado nunca chegava ao kai (ninguém lia o pipe de stdin), o win32-input-
// mode de ConPTYs filhos vazava pra aba, e Ctrl+C/encerramento de filhos
// presos a esse console derrubavam a aba (0xc000013a).
//
// Regra agora: handles herdados válidos (interop do WSL, redirecionamento
// `> arquivo`, pipe) são usados como vieram. Só sem nenhum handle (aberto
// pelo Explorer/atalho, ou chamado de um cmd que não repassa handles) é que
// tentamos o AttachConsole no console do pai.
//
// Chamar UMA vez, no começo do main, antes de qualquer escrita. No-op fora
// do Windows.
void initConsoleForCli();

// Streams de texto da saída/erro padrão da CLI. No Windows, quando a saída é um CONSOLE (cmd, PowerShell, o terminal
// do PhpStorm via kai.com...), o texto vai por WriteConsoleW (UTF-16) e os acentos saem certos qualquer que seja a
// página de código do console; o QTextStream(stdout) comum usa a página ANSI do sistema, que o console lê como OEM e
// troca os acentos. Saída redirecionada (arquivo, pipe, interop do WSL) e as outras plataformas ficam como estavam.
// Não são thread-safe (como o QTextStream que substituem): a CLI escreve da thread principal.
QTextStream &cliOut();
QTextStream &cliErr();

// true = há um terminal interativo atrás da saída padrão — distingue "rodei
// `kai` solto no terminal" de "o launcher/ícone abriu o app". KAI_FORCE_GUI
// força false (relançada automática do docker/watch.sh roda sob um pty).
// No Windows, uma chamada vinda do WSL conta como interativa mesmo com
// pipes (é sempre o shell do usuário do outro lado da interop).
bool stdoutIsInteractiveTerminal();

// Idem para a entrada padrão: dá pra perguntar algo ao usuário e ler a resposta.
bool stdinIsInteractiveTerminal();

// Nome do executável do processo pai em minúsculas ("powershell.exe", "bash",
// "wsl.exe"...), ou vazio quando não dá pra descobrir.
QString parentProcessName();

// Windows: o processo foi lançado pela interop do WSL (pai = wsl.exe). false
// em qualquer outra plataforma/caso. O `kai.com` (shim de console) fica entre quem chamou e o kai.exe: ele não conta,
// vale o primeiro ancestral que não é do Kai.
bool launchedFromWslInterop();

// A parte pura da decisão acima: `ancestorsNearestFirst` são os nomes (minúsculos) dos ancestrais, do pai para cima.
bool ancestorsAreWslInterop(const QStringList &ancestorsNearestFirst);

} // namespace kai::utils
