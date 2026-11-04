#pragma once

#include <QString>
#include <QStringList>

class QTextStream;

namespace kai::utils {

// De onde vêm stdin/stdout/stderr do processo e se há um terminal humano atrás deles.
//
// Windows: kai.exe é WIN32 subsystem (sem console próprio). Chamado do WSL, a interop já entrega stdin/stdout/stderr
// como pipes válidos e o pai é o wsl.exe. Anexar com AttachConsole(ATTACH_PARENT_PROCESS) levava ao console desse
// wsl.exe e o freopen("CONOUT$") desviava a saída da interop: o teclado nunca chegava, o win32-input-mode de ConPTYs
// filhos vazava para a aba e Ctrl+C/encerramento de filhos derrubava a aba (0xc000013a).
// Regra: handles herdados válidos (interop do WSL, `> arquivo`, pipe) são usados como vieram; só sem nenhum handle
// (Explorer/atalho, ou cmd que não repassa handles) tenta o AttachConsole no console do pai.
//
// Chamar uma vez, no começo do main, antes de qualquer escrita. No-op fora do Windows.
void initConsoleForCli();

// Streams de texto da saída/erro padrão da CLI. No Windows, quando a saída é um CONSOLE (cmd, PowerShell, o terminal
// do PhpStorm via kai.com...), o texto vai por WriteConsoleW (UTF-16) e os acentos saem certos qualquer que seja a
// página de código do console; o QTextStream(stdout) comum usa a página ANSI do sistema, que o console lê como OEM e
// troca os acentos. Saída redirecionada (arquivo, pipe, interop do WSL) e as outras plataformas ficam como estavam.
// Não são thread-safe (como o QTextStream que substituem): a CLI escreve da thread principal.
QTextStream &cliOut();
QTextStream &cliErr();

// true = há terminal interativo atrás da saída padrão ("kai" solto no terminal vs. aberto por launcher/ícone).
// KAI_FORCE_GUI força false (relançada do docker/watch.sh roda sob um pty). No Windows, uma chamada vinda do WSL
// conta como interativa mesmo com pipes (é sempre o shell do usuário do outro lado).
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
