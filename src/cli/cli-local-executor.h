#pragma once

#include "core/config-manager.h"

#include <QStringList>

#include <optional>

namespace kai::cli {

// Flags do CLI, sempre ANTES do caminho. Curtas combinam (-gdn):
//   -g/--global   caminhos do app mesmo com kai.json/kai.yml na pasta
//   -d/--detached dispara e devolve o terminal
//   -n/--notify   notificação na bandeja quando o comando terminar
//   -w/--window   abre a saída numa janela própria (desacoplada) do app;
//                 só vale com comandos do app (-g)
//   --dry-run     mostra o que seria executado (template renderizado,
//                 diretório, alvo, hooks) sem executar
//   --json        listagem/--dry-run em JSON, pra scripts
// A palavra solta "global" NÃO é aceita (pedido do usuário: só por flag).
struct CliFlags {
    bool global = false;
    bool detached = false;
    bool notify = false;
    bool window = false;
    bool dryRun = false;
    bool json = false;

    bool any() const { return global || detached || notify || window || dryRun || json; }
};

struct LocalExecutionOutcome {
    // true = reconhecemos isto como uma tentativa de CLI Path LOCAL (havia
    // um kai.json/kai.yml no diretório atual) — main() deve sair com
    // `exitCode`, SEM prosseguir pro fluxo normal (GUI/verbos de CLI via
    // IPC). false = nenhum efeito colateral algum, main() segue como
    // sempre (menos de 2 args, 1º token é um verbo reservado, ou não há
    // kai.json/kai.yml no diretório atual).
    bool handled = false;
    int exitCode = 0;
};

// ============================================================================
// PONTO DE ENTRADA do modo LOCAL de CLI Paths (ver a conversa de design:
// "usar o kai como CLI app é ruim... quero que o kai leia arquivos de
// definição automáticos dentro da pasta em que foi executado"). Junta as
// peças já construídas e testadas isoladamente:
//   core::CliPathResolver   — acha o Command a partir dos tokens
//   core::CliParamBinder    — liga/valida os parâmetros restantes
//   ui::ProjectSelector     — parser já existente de kai.json/kai.yml
//   engine::ExecutionPipeline — motor de execução já existente (shell/http,
//                               hooks, condições) — reaproveitado tal como
//                               é, sem duplicar lógica de execução
//
// PRECISA rodar sob um QCoreApplication já vivo no processo (o motor usa
// QProcess/QNetworkAccessManager via sinais/slots — precisa de um event
// loop). Esta função roda seu PRÓPRIO QEventLoop aninhado internamente até
// o comando terminar, então já devolve o resultado final pronto — quem
// chama só precisa dar `return outcome.exitCode;` na sequência, nunca
// `app.exec()`.
// ============================================================================
LocalExecutionOutcome runLocalCliPath(const QStringList &args, const CliFlags &flags = CliFlags());

// Checagem BARATA e SEM QCoreApplication nenhuma (só QDir/QFile — funciona
// antes de qualquer app Qt existir no processo): true quando `args` parece
// uma tentativa de CLI Path local o bastante pra justificar construir um
// QCoreApplication (em vez de QApplication) e chamar runLocalCliPath.
// Mesmos dois primeiros critérios de runLocalCliPath (2+ args, 1º token
// não é verbo reservado), MAIS a existência de fato de um kai.json/kai.yml
// no diretório atual — main() PRECISA decidir qual classe de app construir
// (QCoreApplication vs QApplication) ANTES de construir qualquer uma das
// duas, então esta checagem tem que ser separada e vir primeiro.
bool looksLikeLocalCliPathAttempt(const QStringList &args);

// Há kai.json/kai.yml/kai.yaml no diretório atual.
bool hasLocalKaiFile();

// ============================================================================
// MODO GLOBAL de CLI Paths — pedido do usuário: "se a gente for rodar o kai
// onde nem tem kai file, ele já lista os globais do [cli_]path". Mesma
// mecânica do modo LOCAL (mesmo CliPathResolver/CliParamBinder/
// ExecutionPipeline), mas as pastas/comandos vêm do commands.json
// PERSISTIDO do app inteiro (core::ConfigManager), não de um kai.json/
// kai.yml no diretório atual — cobre exatamente o caso em que NÃO há
// projeto local: `kai` solto (ou `kai <cli_path>`) fora de qualquer pasta
// de projeto ainda assim descobre/roda os comandos com cli_path
// configurados em QUALQUER pasta do app (só os marcados com cli_path
// aparecem — nunca por nome puro, como no `kai run`).
// ============================================================================
LocalExecutionOutcome runGlobalCliDiscover(const QStringList &args, const CliFlags &flags = CliFlags());

// Mesmo espírito de looksLikeLocalCliPathAttempt, mas pro modo GLOBAL:
// terminal interativo de verdade (nunca intercepta o launcher/ícone) e o
// 1º token, se houver, não é um verbo reservado (run/list/env/...). Não
// depende de nenhum arquivo existir — main() só chama isto DEPOIS de
// looksLikeLocalCliPathAttempt falhar (sem kai.json/kai.yml local).
bool looksLikeGlobalCliPathAttempt(const QStringList &args);

// Consome as flags do início de `args` (args[0] = executável, preservado) e
// devolve o resto. Para no primeiro token que não é flag.
QStringList stripLeadingCliFlags(const QStringList &args, CliFlags &flags);

// kai.exe (Windows) chamado a partir de uma pasta do WSL via interop: o cwd
// chega como \\wsl.localhost\<distro>\... (ou \\wsl$\...). Nesse caso o
// modo LOCAL roda o comando DENTRO dessa mesma distro — exatamente o que um
// kai nativo do Linux rodando ali faria — em vez do cmd.exe, onde comandos
// Linux ("docker", "./release.sh") nem existem. Devolve o alvo de terminal
// sintético (padrão, sabor Posix) que faz essa ponte; nullopt para qualquer
// cwd que não seja UNC do WSL. Lógica pura (testável em qualquer SO); só é
// APLICADA no Windows.
std::optional<core::TerminalProfile> localWslBridgeProfile(const QString &cwd);

// Árvore de CLI do kai.json/kai.yml do diretório `directoryPath` (mesma
// leitura do modo local). false se não há arquivo ou ele não pôde ser lido.
bool loadLocalCliTree(const QString &directoryPath, QVector<core::Folder> &folders,
                      QVector<core::Command> &commands);

} // namespace kai::cli
