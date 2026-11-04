#pragma once

#include "core/config-manager.h"

#include <QStringList>

#include <optional>

namespace kai::cli {

// Flags do CLI, sempre ANTES do caminho; as curtas combinam (-gdn). A palavra solta "global" não é aceita.
//   -g/--global   caminhos do app mesmo com kai.yml na pasta
//   -d/--detached dispara e devolve o terminal
//   -n/--notify   notificação na bandeja ao terminar
//   -w/--window   saída numa janela desacoplada do app (só com comandos do app, -g)
//   --dry-run     mostra o que seria executado sem executar
//   --json        listagem/--dry-run em JSON
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
    // true = era uma tentativa de CLI Path LOCAL (havia kai.yml no diretório atual): main() deve sair
    // com `exitCode` sem seguir o fluxo normal. false = nenhum efeito colateral, main() segue.
    bool handled = false;
    int exitCode = 0;
};

// Ponto de entrada do modo LOCAL de CLI Paths: resolve o Command pelos tokens
// (CliPathResolver), valida parâmetros (CliParamBinder), lê o kai.yml (ProjectSelector) e executa
// com o ExecutionPipeline.
// Exige um QCoreApplication vivo; roda seu PRÓPRIO QEventLoop aninhado até o comando terminar,
// então quem chama só faz `return outcome.exitCode;`, nunca `app.exec()`.
LocalExecutionOutcome runLocalCliPath(const QStringList &args, const CliFlags &flags = CliFlags());

// Checagem barata e SEM QCoreApplication (só QDir/QFile): `args` parece um CLI Path local (2+ args,
// 1º token não reservado, kai.yml no diretório atual). main() precisa decidir entre
// QCoreApplication e QApplication antes de construir qualquer uma.
bool looksLikeLocalCliPathAttempt(const QStringList &args);

// Há kai.yml no diretório atual.
bool hasLocalKaiFile();

// Modo GLOBAL: mesma mecânica do local, mas as pastas/comandos vêm do commands.json persistido do
// app (ConfigManager); só aparecem os comandos com cli_path, nunca por nome puro como no `kai run`.
LocalExecutionOutcome runGlobalCliDiscover(const QStringList &args, const CliFlags &flags = CliFlags());

// Como looksLikeLocalCliPathAttempt, pro modo GLOBAL: terminal interativo (nunca intercepta o
// launcher/ícone) e 1º token, se houver, não reservado. main() só chama depois de a checagem local falhar.
bool looksLikeGlobalCliPathAttempt(const QStringList &args);

// Consome as flags do início de `args` (args[0] = executável, preservado) e
// devolve o resto. Para no primeiro token que não é flag.
QStringList stripLeadingCliFlags(const QStringList &args, CliFlags &flags);

// kai.exe chamado de uma pasta do WSL via interop (cwd `\\wsl.localhost\<distro>\...` ou `\\wsl$\...`):
// o modo LOCAL roda o comando DENTRO da distro, não no cmd.exe. Devolve o alvo de terminal sintético
// (sabor Posix) dessa ponte; nullopt para cwd que não seja UNC do WSL. Lógica pura, só APLICADA no Windows.
std::optional<core::TerminalProfile> localWslBridgeProfile(const QString &cwd);

// Árvore de CLI do kai.yml de `directoryPath`. false se não há arquivo ou ele não pôde ser lido.
bool loadLocalCliTree(const QString &directoryPath, QVector<core::Folder> &folders,
                      QVector<core::Command> &commands);

} // namespace kai::cli
