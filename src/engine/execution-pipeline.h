#pragma once

#include <QObject>
#include <QVector>
#include <QMap>
#include <QStringList>
#include <QSet>
#include <QHash>
#include <QPointer>
#include <map>
#include <functional>
#include <memory>

#include "core/environment-manager.h"
#include "core/models.h"
#include "core/config-manager.h"
#include "core/interpreter-settings.h"
#include "engine/process-runner.h"
#include "engine/http-runner.h"
#include "engine/kip-session.h"

class TestExecutionPipeline; // ver friend em ExecutionPipeline (tests/test_execution_pipeline.cpp)

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
    // Comando PRINCIPAL desta execução (failedCommandId pode ser um hook).
    QString mainCommandId;
    // Encerramento PEDIDO (Parar, `kai kill`, reset, re-execução): não é erro; quem notifica deve ignorar.
    bool stoppedByRequest = false;
    // Código de saída para a CLI: 0 em sucesso; o real do passo shell que falhou; 1 sem um (HTTP, falha ao iniciar).
    int exitCode = 0;
};

// Resultado de ExecutionPipeline::captureScriptOutput.
struct ScriptCaptureResult {
    bool success = false;
    QString output;       // stdout inteiro (só preenchido em sucesso)
    QString errorMessage; // stderr / motivo da falha (só preenchido em falha)
};

// Orquestra Pre-Hooks -> Comando Principal -> Post-Hooks; os hooks vêm de `allCommands` pelos ids em `Hooks::pre`/`post`.
// Pre-Hook que falha (exit != 0, ou HTTP >= 400) aborta: o principal nunca dispara.
class ExecutionPipeline : public QObject {
    Q_OBJECT

    // Expõe wrapForEnvCapture() (privado): o texto gerado para Cmd/PowerShell não dá para validar rodando o pipeline na CI (só bash).
    friend class ::TestExecutionPipeline;

public:
    explicit ExecutionPipeline(QObject *parent = nullptr);
    // Desliga os runners antes de destruí-los: o fim forçado de um processo ainda vivo emitiria `finished` para lambdas
    // que já não têm onde pousar (o dono, ex.: a MainWindow, está sendo destruído).
    ~ExecutionPipeline() override;

    // Timeout aplicado a cada hook individualmente (default 30s).
    void setHookTimeoutMs(int ms);

    // Espera entre SIGTERM e SIGKILL no "Parar" (forceStop ignora). Aplicado a cada ProcessRunner ao criá-lo.
    void setGracefulStopTimeoutMs(int ms);

    // Dispara os hooks de CLEANUP do comando em todo término (sucesso, falha, crash, Stop/Force-stop, Reset).
    // Fire-and-forget: não altera o resultado nem reabre a execução.
    void runCleanupHooks(const core::Command &command,
                         const QMap<QString, core::Command> &allCommands);

    // Um comando shell com terminalTarget não vazio é envolvido no template do alvo de mesmo nome ({{command}}).
    // Alvo ausente = execução local direta.
    void setTerminalProfiles(const QVector<core::TerminalProfile> &targets);

    // Interpretadores globais de Python/Node (Configurações → Linguagens).
    void setInterpreters(const core::InterpreterSettings &interpreters);

    // A linha de shell que executa o comando, antes do alvo de terminal. Native = texto com {{VAR}} interpolado;
    // linguagens = chamada ao interpretador com o código embutido, SEM interpolar (variáveis vão pelo ambiente).
    // Usada também pelo --dry-run.
    QString commandLineFor(const core::Command &command, const core::EnvironmentManager &env) const;

    // Modo CLI local (`kai <path>`): os passos shell usam stdin/stdout/stderr do próprio processo kai em vez de um PTY
    // capturado. Sem a saída passar pelo Kai, auto-responsores/open_last_link/compact_output não se aplicam;
    // hooks com capture_env continuam capturados.
    void setInheritTerminal(bool enabled);

    // Hierarquia de pastas (id -> Folder) para resolver o perfil de terminal herdado ("@parent"); o MainWindow
    // republica ao carregar/salvar. Sem ela a herança cai no default global.
    void setFolders(const QVector<core::Folder> &folders);
    // Interrompe a requisição HTTP em curso deste comando (botão Parar). false se não há nenhuma.
    bool abortHttp(const QString &commandId);

    // Executa `command`, resolvendo hooks em `allCommands` (id -> Command). `envManager` já vem carregado
    // (Global/Pasta/Parâmetros); variáveis dinâmicas de hooks HTTP se acumulam nele.
    void run(const core::Command &command,
             const QMap<QString, core::Command> &allCommands,
             core::EnvironmentManager &envManager);

    // Roda `script` (shell) NO CONTEXTO de `command` — pasta, alvo de terminal (WSL...), variáveis de `env` —
    // sem tocar na Saída nem no registry de runners, e entrega o stdout em `done`. Usado pelas listas
    // dinâmicas (Parameter::optionsScript). Assíncrono; `timeoutMs` encerra o script travado. O pipeline deve
    // ficar vivo até `done` (um pipeline descartável por chamada é o uso esperado).
    void captureScriptOutput(const core::Command &command, const QString &script, core::EnvironmentManager &env,
                             int timeoutMs, std::function<void(const ScriptCaptureResult &)> done);

    // Runner do comando shell em execução (nullptr se nenhum ou se for HTTP); o Terminal Drawer usa para stdin.
    ProcessRunner *activeProcessRunner() const;

    // Runner de um comando ESPECÍFICO (nullptr se não houver): fonte para roteamento de stdin e Stop.
    ProcessRunner *runnerFor(const QString &commandId) const;

    // Ids dos comandos com runner de processo VIVO neste pipeline.
    QStringList runningCommandIds() const;

    // Sessão KIP da última execução do comando (nullptr se nunca rodou); sobrevive ao processo até nova execução.
    KipSession *kipSessionFor(const QString &commandId) const;

    // Id do comando cujo runner foi o último disparado (compat).
    QString activeCommandId() const { return m_activeRunnerCommandId; }

    // Transfere a ownership do runner ao chamador (após backgroundProcessStarted), que mantém o processo vivo.
    std::unique_ptr<ProcessRunner> releaseRunnerFor(const QString &commandId);
    // Compat: libera o runner do comando principal em curso.
    std::unique_ptr<ProcessRunner> releaseActiveProcessRunner();

    // Alvo de terminal EFETIVO, com a herança resolvida: o do comando; "@parent" sobe as pastas (setFolders); senão o
    // PADRÃO; senão o único válido; senão vazio (local). Público para teste.
    QString effectiveTerminalProfileName(const core::Command &command) const;
    // PROJECT_PATH: diretório da pasta-projeto mais próxima, no formato que o alvo enxerga (POSIX em bash/WSL); sem projeto, some.
    void applyProjectBuiltins(const core::Command &command, core::EnvironmentManager &env) const;
    // Diretório de trabalho EFETIVO (herança pasta -> comando, já com
    // PROJECT_PATH e {{VAR}} interpolados); vazio = padrão do alvo.
    QString effectiveWorkingDir(const core::Command &command, core::EnvironmentManager &env) const;
    // Distro WSL em que o alvo de terminal do comando executa (vazio = não é WSL).
    QString wslDistroFor(const core::Command &command) const;
    core::ShellFlavor shellFlavorFor(const core::Command &command) const;
    bool spillEligible(const core::Command &command) const;
    bool interpreterSpillEligible(const core::Command &command) const;
    bool moduleFilesApply(const core::Command &command) const;
    QString interpreterFor(const core::Command &command) const;
    bool isCurrentRunner(const QString &commandId, const ProcessRunner *runner) const;
    // `done(line, envEmbedded)`: com envEmbedded o arquivo do Kai já define as variáveis do ambiente, e o
    // applyTerminalProfile não as repete na linha.
    void resolveCommandLine(const core::Command &command, const std::function<void(const QString &, bool)> &done);
    bool envEmbedEligible(const core::Command &command) const;

signals:
    void logMessage(const QString &commandId, const QString &text, bool isError);
    // Resultado HTTP estruturado (status, headers, tempo, corpo, request) para as abas; logMessage segue para o terminal.
    void httpResultReady(const QString &commandId, const engine::HttpResult &result);
    // A requisição saiu e a resposta ainda não chegou. `timeoutSec` 0 = sem limite de tempo.
    void httpRequestStarted(const QString &commandId, int timeoutSec);
    // HTTP pulado por Execution Condition com conditionSkipBehavior == "success" (o de falha usa o fluxo de erro).
    void commandSkippedByCondition(const QString &commandId, const QString &reasonLabel);
    // Repassa HttpRunner::dynamicVarPersistRequested; o MainWindow grava em dynamic-vars.json.
    void dynamicVarPersistRequested(const QString &scopeKey, const QString &name, const QString &value);
    // Uma sessão KIP nasceu para `commandId` (a UI conecta a view a ela).
    void kipSessionStarted(const QString &commandId, kai::engine::KipSession *session);
    // O programa KIP pediu uma notificação do SO (`notify`).
    void kipNotifyRequested(const QString &commandId, const QString &title, const QString &text,
                            kai::core::KipLevel level);
    // Respostas de prompts KIP aceitas, a persistir em Command::kipLastValues.
    void kipAnswersRemembered(const QString &commandId, const QJsonObject &remembered);
    void stageStarted(const QString &commandId, PipelineStage stage);
    void pipelineFinished(const PipelineResult &result);

    // Comando is_background=true iniciou: o pipeline não espera e considera sucesso. O receptor deve chamar
    // releaseActiveProcessRunner() síncrona e imediatamente no slot, antes que o pipeline prossiga.
    void backgroundProcessStarted(const QString &commandId, ProcessRunner *runner);

private:
    // Avisa (só no Windows) quando a linha final passa do que o cmd.exe aguenta.
    void warnIfCommandLineTooLong(const QString &commandId, const QString &finalCommand);
    // O estado de UMA cadeia (comando principal + os ids dos seus hooks). Cada run() tem a sua: várias execuções
    // em paralelo no mesmo pipeline não podem se atribuir o término umas às outras (m_mainCommand é só a última).
    struct Chain {
        core::Command main;
        QSet<QString> ids;
    };
    using ChainPtr = std::shared_ptr<Chain>;
    void runQueue(QStringList queue, PipelineStage stage, std::function<void()> onAllSucceeded, ChainPtr chain = {});
    void runSingleCommand(const core::Command &command, std::function<void(bool success, const QString &errorMessage)> onDone);
    void abort(PipelineStage stage, const QString &commandId, const QString &errorMessage, ChainPtr chain = {});
    // Resultado das Condições de Execução, com o NOME (ou resumo) da condição que decidiu, para a mensagem de pulo/falha.
    struct ConditionEvalResult {
        bool passed = true;
        QString decidingConditionLabel; // só relevante quando !passed
    };
    // Avalia as condições do comando (conditionCombinator "and"/"or") via m_envManager->evaluateCondition(); lista
    // vazia sempre passa. Chamado no topo de runSingleCommand(), por onde passam principal e hooks.
    ConditionEvalResult evaluateConditions(const core::Command &command) const;

    // Captura de ambiente por hook: imprime todo o ambiente após um sentinela único, parseia KEY=VALUE e injeta cada
    // uma como variável dinâmica para o principal e hooks seguintes.
    QString wrapForEnvCapture(const QString &interpolatedCommand, core::ShellFlavor flavor) const;
    // `scopeKey` é o escopo de variáveis dinâmicas capturado quando a execução começou, nunca lido de
    // currentDynamicVarScope() aqui: roda no callback `finished`, quando o usuário pode já ter trocado a seleção.
    void ingestCapturedEnv(const QString &rawOutput, const QString &scopeKey,
                            const QVector<core::DeclaredEnvVar> &declaredVars);
    // Grava UMA variável declarada no escopo certo (e persiste, se pedido). Usado por ingestCapturedEnv e `set_env` do KIP.
    void exportDeclaredVar(const core::DeclaredEnvVar &declared, const QString &scopeKey, const QString &value);
    // Ambiente do processo: o resolvido pelo EnvironmentManager e, para um
    // comando KIP, KIP_VERSION/KIP_LOCALE por baixo (o env do usuário vence).
    // Mais, nos comandos que recebem os módulos em disco (ModuleFiles), PYTHONPATH/NODE_PATH/PHP_INI_SCAN_DIR.
    QMap<QString, QString> processEnvFor(const core::Command &command) const;
    KipSession *createKipSession(const core::Command &command, ProcessRunner *runner,
                                 const QString &captureScopeKey);
    // Aplica o template do alvo de terminal ao comando interpolado (inalterado sem alvo). Com workingDir, injeta um `cd`
    // no início: o CWD do QProcess não atravessa cmd->wsl->bash e `bash -l` volta ao $HOME.
    QString applyTerminalProfile(const core::Command &command,
                                const QString &interpolatedCommand,
                                const QString &interpolatedWorkingDir,
                                const QString &remoteRunId = QString(),
                                bool envEmbedded = false) const;
    // Linha que MATA o processo do lado remoto (WSL) com o mesmo template do alvo: o processo Linux não é filho do wsl.exe.
    QString buildRemoteKillCommandLine(const QString &targetName, const QString &remoteRunId) const;
    // Injeta o script no template do alvo (base64 ou {{command}}).
    QString wrapScriptInTarget(const core::TerminalProfile &target, const QString &script) const;

    const QMap<QString, core::Command> *m_allCommands = nullptr;
    // Guarda de idempotência do cleanup, por comando: no Stop manual a UI e o abort() disparavam os mesmos hooks (cleanup
    // rodava duas vezes). Por comando porque os de background terminam fora do fluxo do pipeline.
    QSet<QString> m_cleanupFiredFor;
    core::EnvironmentManager *m_envManager = nullptr;
    core::Command m_mainCommand;
    // Ids de todos os comandos desta cadeia (principal + seus hooks), montado no início de run(); abort() só encerra
    // runners daqui, nunca de outras execuções em paralelo.
    QSet<QString> m_currentChainCommandIds;
    // Comandos desta cadeia cujo término foi um stop pedido (ver
    // PipelineResult::stoppedByRequest); consumido em abort().
    QSet<QString> m_stoppedByRequestIds;
    int m_hookTimeoutMs = 30000;
    // Default 2000ms (o mesmo de ProcessRunner::m_killTimeoutMs).
    int m_gracefulStopTimeoutMs = 2000;
    QVector<core::TerminalProfile> m_terminalProfiles;
    core::InterpreterSettings m_interpreters;
    bool m_inheritTerminal = false;
    QMap<QString, int> m_lastExitCodes; // último código por comando (ver PipelineResult::exitCode)
    QVector<core::Folder> m_folders;

    // Captura de ambiente do hook em execução: buffer do stdout e flag de captura ao terminar.
    bool m_captureEnvActive = false;
    QString m_captureBuffer;

    // Runners por commandId; cada comando em execução mantém o seu vivo até terminar.
    std::map<QString, std::unique_ptr<ProcessRunner>> m_runners;
    // Runner "atual" (último disparado), só para activeProcessRunner() (compat).
    QString m_activeRunnerCommandId;
    std::unique_ptr<HttpRunner> m_activeHttpRunner;
    QString m_activeHttpCommandId;
    // Sessão KIP mais recente por comando (a dona é este objeto, via parent).
    QHash<QString, QPointer<KipSession>> m_kipSessions;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::PipelineResult)
Q_DECLARE_METATYPE(kai::engine::PipelineStage)
