#pragma once

#include <QStringList>

namespace kai::cli {

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
LocalExecutionOutcome runLocalCliPath(const QStringList &args);

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
LocalExecutionOutcome runGlobalCliDiscover(const QStringList &args);

// Mesmo espírito de looksLikeLocalCliPathAttempt, mas pro modo GLOBAL:
// terminal interativo de verdade (nunca intercepta o launcher/ícone) e o
// 1º token, se houver, não é um verbo reservado (run/list/env/...). Não
// depende de nenhum arquivo existir — main() só chama isto DEPOIS de
// looksLikeLocalCliPathAttempt falhar (sem kai.json/kai.yml local).
bool looksLikeGlobalCliPathAttempt(const QStringList &args);

} // namespace kai::cli
