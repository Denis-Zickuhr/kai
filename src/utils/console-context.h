#pragma once

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

// true = há um terminal interativo atrás da saída padrão — distingue "rodei
// `kai` solto no terminal" de "o launcher/ícone abriu o app". KAI_FORCE_GUI
// força false (relançada automática do docker/watch.sh roda sob um pty).
// No Windows, uma chamada vinda do WSL conta como interativa mesmo com
// pipes (é sempre o shell do usuário do outro lado da interop).
bool stdoutIsInteractiveTerminal();

// Windows: o processo foi lançado pela interop do WSL (pai = wsl.exe). false
// em qualquer outra plataforma/caso.
bool launchedFromWslInterop();

} // namespace kai::utils
