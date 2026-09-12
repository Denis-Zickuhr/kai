#pragma once

#include <QObject>
#include <QVector>
#include <QMap>
#include <QStringList>
#include <QSet>
#include <map>
#include <functional>
#include <memory>

#include "core/environment-manager.h"
#include "core/models.h"
#include "core/config-manager.h"
#include "engine/process-runner.h"
#include "engine/http-runner.h"

namespace kai::engine {

enum class PipelineStage {
    PreHooks,
    Cleanup,
    MainCommand,
    PostHooks
};

// Resultado final de uma execução completa do pipeline.
struct PipelineResult {
    bool success = false;
    PipelineStage failedStage = PipelineStage::MainCommand;
    QString failedCommandId;
    QString errorMessage;
};

// Orquestra a sequência Pre-Hooks -> Comando Principal -> Post-Hooks
//. Cada hook é resolvido a partir de `allCommands` pelo
// id referenciado em `Hooks::pre`/`Hooks::post`. Se um Pre-Hook falhar
// (exitCode != 0 para shell, ou status HTTP >= 400 para http), a execução é
// abortada imediatamente e o Comando Principal NUNCA é disparado.
class ExecutionPipeline : public QObject {
    Q_OBJECT

public:
    explicit ExecutionPipeline(QObject *parent = nullptr);

    // Timeout aplicado a cada hook individualmente (default 30s).
    void setHookTimeoutMs(int ms);

    // Dispara os hooks de CLEANUP do comando informado. Roda em TODOS os
    // términos: sucesso, falha, crash, Stop/Force-stop manual e Reset.
    // Fire-and-forget: cada cleanup é independente, não altera o resultado do
    // pipeline e não reabre a execução. É a saída de emergência para casos em
    // que matar o processo não desmonta o que ele subiu.
    void runCleanupHooks(const core::Command &command,
                         const QMap<QString, core::Command> &allCommands);

    // Alvos de terminal configuráveis (feedback do usuário). Quando
    // um comando shell define command.terminalTarget não vazio, o pipeline
    // busca o alvo de mesmo nome aqui e envolve o comando interpolado no
    // seu template (placeholder {{command}}). Alvo não encontrado ou vazio
    // = execução local direta (comportamento anterior).
    void setTerminalProfiles(const QVector<core::TerminalProfile> &targets);

    // Hierarquia de pastas (id -> Folder), usada para RESOLVER o perfil de
    // terminal herdado: um comando com terminalTarget == "@parent" (ou vazio,
    // caindo no default) sobe pela cadeia de pastas até achar um perfil
    // concreto. Deve ser mantida em sincronia com o modelo (o MainWindow
    // republica ao carregar/salvar). Sem isto, a herança degrada para o
    // comportamento anterior (comando -> default global).
    void setFolders(const QVector<core::Folder> &folders);

    // Executa `command`, resolvendo hooks a partir de `allCommands`
    // (mapa id -> Command). `envManager` já deve estar carregado com as
    // variáveis Global/Pasta/Parâmetros; variáveis dinâmicas extraídas por
    // hooks HTTP são acumuladas nele progressivamente.
    void run(const core::Command &command,
             const QMap<QString, core::Command> &allCommands,
             core::EnvironmentManager &envManager);

    // Retorna o ProcessRunner do comando shell atualmente em execução (ou
    // nullptr se nenhum estiver ativo, ou o comando em curso for HTTP).
    // Usado pelo Terminal Drawer para permitir interação real via stdin
    // (item de UX: terminal não deve ser apenas leitura).
    ProcessRunner *activeProcessRunner() const;

    // Runner de um comando ESPECÍFICO (nullptr se não houver/não estiver
    // rodando). Fonte correta para roteamento de stdin e para o Stop: antes
    // havia UM slot único de runner, então rodar um 2º comando DESTRUÍA o
    // runner do 1º (perdendo o Stop/entrada e deixando processo fantasma).
    // Agora cada comando em execução tem seu próprio runner no registry.
    ProcessRunner *runnerFor(const QString &commandId) const;

    // Ids dos comandos com runner de processo VIVO neste pipeline.
    QStringList runningCommandIds() const;

    // Id do comando cujo runner foi o último disparado (compat).
    QString activeCommandId() const { return m_activeRunnerCommandId; }

    // Transfere a ownership do ProcessRunner de um comando para o chamador
    // (usado após backgroundProcessStarted, para o receptor manter o
    // processo vivo além do ciclo de vida deste comando).
    std::unique_ptr<ProcessRunner> releaseRunnerFor(const QString &commandId);
    // Compat: libera o runner do comando principal em curso.
    std::unique_ptr<ProcessRunner> releaseActiveProcessRunner();

    // Nome do alvo de terminal EFETIVO de um comando, após a RESOLUÇÃO
    // HIERÁRQUICA do perfil herdado (@parent): o próprio terminalTarget do
    // comando se for um nome; se "@parent", sobe pela cadeia de pastas
    // (setFolders) até achar um perfil concreto; senão, o alvo marcado como
    // PADRÃO (isDefault); senão, o único alvo válido; senão vazio (local).
    // Público para permitir teste unitário direto da resolução, sem executar.
    QString effectiveTerminalProfileName(const core::Command &command) const;

signals:
    void logMessage(const QString &commandId, const QString &text, bool isError);
    // SAÍDA V2: entrega o resultado HTTP ESTRUTURADO (status, headers, tempo,
    // corpo, request enviada) para a interface montar as abas Corpo/Headers/
    // Raw. O logMessage continua existindo para o texto do terminal.
    void httpResultReady(const QString &commandId, const engine::HttpResult &result);
    // Comando HTTP pulado por Execution Condition, com conditionSkipBehavior
    // == "success" (o caso "failure" já usa o fluxo normal de erro — a UI
    // de pulo/painel só existe pro caso ambíguo "sucesso silencioso", que é
    // o que mascarava resultado antigo em cache — feedback do usuário:
    // "perco o feedback visual que isso ocorreu, e perco acesso as abas
    // gerais do comando"). Só emitido pra HTTP (Shell não tem esse problema
    // de abas — sem Resposta/Headers/Request pra ficarem com dado velho).
    void commandSkippedByCondition(const QString &commandId, const QString &reasonLabel);
    // Repassa HttpRunner::dynamicVarPersistRequested — o MainWindow (dono
    // do ConfigManager) grava em dynamic-vars.json. Ver EnvExtractor::persist.
    void dynamicVarPersistRequested(const QString &scopeKey, const QString &name, const QString &value);
    void stageStarted(const QString &commandId, PipelineStage stage);
    void pipelineFinished(const PipelineResult &result);

    // Emitido quando um comando com is_background=true inicia com sucesso
    //. O pipeline não espera o processo terminar — considera
    // sucesso imediato ao disparar. O receptor deve chamar
    // releaseActiveProcessRunner() *síncrona e imediatamente* dentro do
    // slot conectado a este sinal, para assumir a ownership do
    // ProcessRunner antes que o pipeline prossiga (ex: para transferi-lo a
    // um ProcessManager de longa duração).
    void backgroundProcessStarted(const QString &commandId, ProcessRunner *runner);

private:
    void runQueue(QStringList queue, PipelineStage stage, std::function<void()> onAllSucceeded);
    void runSingleCommand(const core::Command &command, std::function<void(bool success, const QString &errorMessage)> onDone);
    void abort(PipelineStage stage, const QString &commandId, const QString &errorMessage);
    // Resultado de avaliar as Condições de Execução de um comando: além do
    // veredito, carrega o NOME (ou resumo, se sem nome) da condição que
    // decidiu o resultado — pedido do usuário: a mensagem de pulo/falha deve
    // dizer QUAL condição barrou, essencial pra debugar quando há várias.
    struct ConditionEvalResult {
        bool passed = true;
        QString decidingConditionLabel; // só relevante quando !passed
    };
    // CONDIÇÕES DE EXECUÇÃO (core::Command::executionConditions): avalia
    // todas as condições do comando combinando por conditionCombinator
    // ("and"/"or") via m_envManager->evaluateCondition() por linha. Lista
    // vazia sempre passa (sem guarda = sempre roda, comportamento atual
    // preservado). Chamado do TOPO de runSingleCommand(), único pedágio por
    // onde passam comando principal, pre/post/cleanup hooks.
    ConditionEvalResult evaluateConditions(const core::Command &command) const;

    // Captura de ambiente por hook (feedback do usuário): envolve o comando
    // do hook para imprimir todo o ambiente após um sentinela único, faz o
    // parse das linhas KEY=VALUE emitidas depois do sentinela e injeta cada
    // uma como variável dinâmica no EnvironmentManager, disponibilizando-as
    // ao comando principal e aos hooks seguintes.
    QString wrapForEnvCapture(const QString &interpolatedCommand) const;
    void ingestCapturedEnv(const QString &rawOutput);
    // Aplica o template do alvo de terminal ao comando interpolado, se o
    // comando definir um terminalTarget existente. Caso contrário retorna
    // o comando inalterado. Quando há terminalTarget E workingDir, injeta
    // um `cd` no início do comando (o CWD do QProcess não atravessa a
    // fronteira cmd->wsl->bash de forma confiável, e `bash -l` volta ao
    // $HOME — por isso o working_dir precisa ser aplicado DENTRO do shell).
    QString applyTerminalProfile(const core::Command &command,
                                const QString &interpolatedCommand,
                                const QString &interpolatedWorkingDir,
                                const QString &remoteRunId = QString()) const;
    // Monta a linha de comando que MATA o processo do lado remoto (dentro do
    // WSL), usando o MESMO template do alvo — o processo Linux não é filho
    // Windows do wsl.exe, então só um kill executado LÁ DENTRO o encerra.
    QString buildRemoteKillCommandLine(const QString &targetName, const QString &remoteRunId) const;
    // Injeta o script no template do alvo (base64 ou {{command}}).
    QString wrapScriptInTarget(const core::TerminalProfile &target, const QString &script) const;

    const QMap<QString, core::Command> *m_allCommands = nullptr;
    // GUARDA DE IDEMPOTÊNCIA do cleanup, POR COMANDO: no Stop manual havia DOIS
    // caminhos disparando os mesmos hooks (a UI e o abort() quando o processo
    // morre), então um "docker compose down" rodava DUAS vezes em paralelo — o
    // segundo topava recurso já removido e parecia que o cleanup falhou.
    // É por comando (e não um bool global) porque comandos de background
    // terminam fora do fluxo do pipeline: um bool único fazia o término de um
    // comando bloquear o cleanup de outro.
    QSet<QString> m_cleanupFiredFor;
    core::EnvironmentManager *m_envManager = nullptr;
    core::Command m_mainCommand;
    // Ids de TODOS os comandos desta cadeia (principal + seus próprios
    // pre/post hooks), montado no início de run(). abort() usa isto para
    // só encerrar runners que pertencem a ESTA cadeia — nunca comandos de
    // OUTRAS execuções em paralelo (ver comentário em abort()).
    QSet<QString> m_currentChainCommandIds;
    int m_hookTimeoutMs = 30000;
    QVector<core::TerminalProfile> m_terminalProfiles;
    QVector<core::Folder> m_folders;

    // Estado de captura de ambiente do hook em execução (T9): buffer do
    // stdout do hook atual e flag indicando se ele deve ter o ambiente
    // capturado ao terminar.
    bool m_captureEnvActive = false;
    QString m_captureBuffer;

    // REGISTRY de runners por commandId (antes era UM slot único, e rodar
    // um 2º comando destruía o runner do 1º — causa raiz de "perde o Stop",
    // "entrada se perde" e processos fantasmas). Cada comando em execução
    // mantém seu runner vivo aqui até terminar.
    std::map<QString, std::unique_ptr<ProcessRunner>> m_runners;
    // Id do comando cujo runner é o "atual" (último disparado) — usado só
    // por activeProcessRunner() para compat.
    QString m_activeRunnerCommandId;
    std::unique_ptr<HttpRunner> m_activeHttpRunner;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::PipelineResult)
Q_DECLARE_METATYPE(kai::engine::PipelineStage)
