#include "engine/execution-pipeline.h"
#include "core/terminal-target.h"
#include "engine/command-language.h"
#include "engine/module-files.h"
#include "engine/script-spill.h"
#include "engine/output-responder-matcher.h"
#include <QDir>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QProcessEnvironment>
#include <QSet>
#include <QRegularExpression>
#include <QUuid>
#include "utils/logger.h"
#include "utils/translation-manager.h"
#include "core/kip-settings.h"
#include "core/working-dir.h"
#include "utils/path-format.h"
#include "engine/execution-pipeline-helpers.h"

namespace kai::engine {

using namespace detail;

namespace {
constexpr const char *kLogTag = "ExecutionPipeline";
}

ExecutionPipeline::ExecutionPipeline(QObject *parent)
    : QObject(parent)
{
}

ExecutionPipeline::~ExecutionPipeline()
{
    for (auto &entry : m_runners) {
        if (entry.second) {
            entry.second->disconnect(this);
        }
    }
}

void ExecutionPipeline::setHookTimeoutMs(int ms)
{
    m_hookTimeoutMs = ms;
}

void ExecutionPipeline::setGracefulStopTimeoutMs(int ms)
{
    m_gracefulStopTimeoutMs = ms;
}

void ExecutionPipeline::setInheritTerminal(bool enabled)
{
    m_inheritTerminal = enabled;
}

// No Windows a linha vai por `cmd.exe /c`, que corta em 8191 caracteres: o comando
// (e, num alvo WSL, o base64 de cima dele) sumiria pela metade, sem erro nenhum.
// Avisa na saída ANTES de rodar, dizendo o que fazer.
void ExecutionPipeline::warnIfCommandLineTooLong(const QString &commandId, const QString &finalCommand)
{
#if defined(Q_OS_WIN)
    if (exceedsWindowsCommandLine(finalCommand)) {
        emit logMessage(commandId,
            utils::tr(QStringLiteral("execution_pipeline.line_too_long")).arg(finalCommand.size()) + QStringLiteral("\n"),
            true);
    }
#else
    Q_UNUSED(commandId);
    Q_UNUSED(finalCommand);
#endif
}

void ExecutionPipeline::setInterpreters(const core::InterpreterSettings &interpreters)
{
    m_interpreters = interpreters;
}

// Sabor do shell que vai rodar o comando: o do alvo de terminal (declarado ou detectado pelo template) ou, sem
// alvo, o shell padrão da plataforma (cmd.exe no Windows, bash/sh no Unix).
core::ShellFlavor ExecutionPipeline::shellFlavorFor(const core::Command &command) const
{
    const QString name = effectiveTerminalProfileName(command);
    if (name.isEmpty()) {
#if defined(Q_OS_WIN)
        return core::ShellFlavor::Cmd;
#else
        return core::ShellFlavor::Posix;
#endif
    }
    for (const core::TerminalProfile &t : m_terminalProfiles) {
        if (t.name == name) {
            return effectiveShellFlavor(t);
        }
    }
    return core::ShellFlavor::Posix;
}

QString ExecutionPipeline::commandLineFor(const core::Command &command, const core::EnvironmentManager &env) const
{
    if (command.language == core::CommandLanguage::Native) {
        const QString line = env.interpolate(command.command);
        // KIP num shell POSIX: a função `kip` é definida no próprio shell que roda o comando. Em PowerShell/cmd
        // (Windows sem alvo POSIX) não há helper: o programa fala o protocolo por conta própria.
        if (!command.kip || command.type != core::CommandType::Command
            || shellFlavorFor(command) != core::ShellFlavor::Posix) {
            return line;
        }
        return kipShellLoader() + QLatin1Char('\n') + line;
    }
    return buildInterpreterCommandLine(command.language, interpreterFor(command), command.command, command.kip);
}

QString ExecutionPipeline::interpreterFor(const core::Command &command) const
{
    bool nativeWindowsHost = false;
#if defined(Q_OS_WIN)
    nativeWindowsHost = effectiveTerminalProfileName(command).isEmpty();
#endif
    return m_interpreters.resolve(command.language, command.interpreter, nativeWindowsHost);
}

bool ExecutionPipeline::isCurrentRunner(const QString &commandId, const ProcessRunner *runner) const
{
    const auto it = m_runners.find(commandId);
    return it != m_runners.end() && it->second.get() == runner;
}

// O texto cabe no arquivo do Kai (ScriptSpill) quando o shell que roda o comando é POSIX e enxerga o disco do Kai:
// shell local no Unix ou uma distro WSL no Windows. Em docker/ssh o arquivo não existe do outro lado.
bool ExecutionPipeline::spillEligible(const core::Command &command) const
{
    if (command.language != core::CommandLanguage::Native || command.type != core::CommandType::Command
        || shellFlavorFor(command) != core::ShellFlavor::Posix) {
        return false;
    }
#if defined(Q_OS_WIN)
    return !wslDistroFor(command).isEmpty();
#else
    return effectiveTerminalProfileName(command).isEmpty();
#endif
}

// O código Python/Node/PHP vai num arquivo do Kai nos mesmos alvos em que o interpretador enxerga o disco do Kai:
// o processo local (Unix ou Windows) ou uma distro WSL. Docker/ssh seguem com o código na linha.
bool ExecutionPipeline::interpreterSpillEligible(const core::Command &command) const
{
    if (command.language == core::CommandLanguage::Native || command.type != core::CommandType::Command) {
        return false;
    }
    return effectiveTerminalProfileName(command).isEmpty()
#if defined(Q_OS_WIN)
        || !wslDistroFor(command).isEmpty()
#endif
        ;
}

// Os módulos `kai`/`kip` em disco (ModuleFiles) valem para o processo que o Kai inicia direto: com um alvo de
// terminal (WSL, docker, ssh) as variáveis não atravessam a fronteira, e o bootstrap segue injetando os módulos.
// Um comando nativo só os recebe com KIP ligado: PYTHONPATH/NODE_PATH em todo shell poderiam sombrear um pacote
// `kai` ou `kip` do próprio projeto.
bool ExecutionPipeline::moduleFilesApply(const core::Command &command) const
{
    if (command.type != core::CommandType::Command || !effectiveTerminalProfileName(command).isEmpty()) {
        return false;
    }
    return command.language != core::CommandLanguage::Native || command.kip;
}

// Num alvo POSIX que enxerga o disco do Kai (uma distro WSL) as variáveis do ambiente viajam dentro do TEXTO do
// comando (`export K='V'` de toda a hierarquia, ver buildTargetedCommand), não como variáveis do processo.
bool ExecutionPipeline::envEmbedEligible(const core::Command &command) const
{
    return command.type == core::CommandType::Command && !wslDistroFor(command).isEmpty()
        && shellFlavorFor(command) == core::ShellFlavor::Posix;
}

ProcessRunner *ExecutionPipeline::activeProcessRunner() const
{
    return runnerFor(m_activeRunnerCommandId);
}

ProcessRunner *ExecutionPipeline::runnerFor(const QString &commandId) const
{
    if (commandId.isEmpty()) {
        return nullptr;
    }
    const auto it = m_runners.find(commandId);
    if (it == m_runners.end() || !it->second) {
        return nullptr;
    }
    return it->second->isRunning() ? it->second.get() : nullptr;
}

QStringList ExecutionPipeline::runningCommandIds() const
{
    QStringList ids;
    for (const auto &entry : m_runners) {
        if (entry.second && entry.second->isRunning()) {
            ids << entry.first;
        }
    }
    return ids;
}

std::unique_ptr<ProcessRunner> ExecutionPipeline::releaseRunnerFor(const QString &commandId)
{
    const auto it = m_runners.find(commandId);
    if (it == m_runners.end()) {
        return nullptr;
    }
    std::unique_ptr<ProcessRunner> released = std::move(it->second);
    m_runners.erase(it);
    return released;
}

std::unique_ptr<ProcessRunner> ExecutionPipeline::releaseActiveProcessRunner()
{
    return releaseRunnerFor(m_activeRunnerCommandId);
}

void ExecutionPipeline::run(const core::Command &command,
                             const QMap<QString, core::Command> &allCommands,
                             core::EnvironmentManager &envManager)
{
    m_cleanupFiredFor.remove(command.id);

    m_allCommands = &allCommands;
    m_envManager = &envManager;
    m_mainCommand = command;
    // Snapshot dos ids desta cadeia (abort() não encerra runners de OUTRAS execuções).
    m_currentChainCommandIds.clear();
    m_stoppedByRequestIds.clear();
    m_currentChainCommandIds.insert(command.id);
    for (const QString &hookId : command.hooks.pre) {
        m_currentChainCommandIds.insert(hookId);
    }
    for (const QString &hookId : command.hooks.post) {
        m_currentChainCommandIds.insert(hookId);
    }

    utils::Logger::info(kLogTag, QStringLiteral("Iniciando pipeline para comando '%1'.").arg(command.id));
    utils::Logger::info(kLogTag,
        QStringLiteral("Hooks do comando '%1': pre=[%2] post=[%3]")
            .arg(command.id, command.hooks.pre.join(QStringLiteral(", ")),
                 command.hooks.post.join(QStringLiteral(", "))));
    emit stageStarted(command.id, PipelineStage::PreHooks);

    // A cadeia DESTA execução: os lambdas abaixo a carregam, em vez de ler m_mainCommand (que a próxima run()
    // sobrescreve): com dois comandos rodando juntos, o término do primeiro era atribuído ao segundo.
    auto chain = std::make_shared<Chain>();
    chain->main = command;
    chain->ids = m_currentChainCommandIds;

    runQueue(command.hooks.pre, PipelineStage::PreHooks, [this, chain]() {
        emit stageStarted(chain->main.id, PipelineStage::MainCommand);

        runSingleCommand(chain->main, [this, chain](bool success, const QString &errorMessage) {
            if (!success) {
                abort(PipelineStage::MainCommand, chain->main.id, errorMessage, chain);
                return;
            }

            emit stageStarted(chain->main.id, PipelineStage::PostHooks);
            runQueue(chain->main.hooks.post, PipelineStage::PostHooks, [this, chain]() {
                PipelineResult result;
                result.success = true;
                utils::Logger::info(kLogTag,
                    QStringLiteral("Pipeline concluído com sucesso para '%1'.").arg(chain->main.id));
                // CLEANUP sempre roda ao terminar, inclusive no sucesso.
                if (m_allCommands) {
                    runCleanupHooks(chain->main, *m_allCommands);
                }
                result.mainCommandId = chain->main.id;
                emit pipelineFinished(result);
            }, chain);
        });
    }, chain);
}

void ExecutionPipeline::runQueue(QStringList queue, PipelineStage stage, std::function<void()> onAllSucceeded, ChainPtr chain)
{
    if (queue.isEmpty()) {
        onAllSucceeded();
        return;
    }

    const QString commandId = queue.takeFirst();
    const auto it = m_allCommands->constFind(commandId);

    if (it == m_allCommands->constEnd()) {
        abort(stage, commandId, QStringLiteral("Hook referencia um comando inexistente: '%1'.").arg(commandId), chain);
        return;
    }

    const core::Command hookCommand = it.value();
    if (hookCommand.kip && hookCommand.type == core::CommandType::Command) {
        // KIP é sessão interativa com view própria: não serve como hook (spec 11 §15).
        abort(stage, commandId, utils::tr(QStringLiteral("kip.error.hook_refused"))
            .arg(hookCommand.name.isEmpty() ? commandId : hookCommand.name), chain);
        return;
    }

    runSingleCommand(hookCommand, [this, queue, stage, onAllSucceeded, commandId, chain](bool success, const QString &errorMessage) mutable {
        if (!success) {
            abort(stage, commandId, errorMessage, chain);
            return;
        }
        runQueue(queue, stage, onAllSucceeded, chain);
    });
}

void ExecutionPipeline::abort(PipelineStage stage, const QString &commandId, const QString &errorMessage, ChainPtr chain)
{
    // A cadeia que aborta (a da execução em curso quando não vem uma): comando principal e ids a encerrar.
    const core::Command &mainCommand = chain ? chain->main : m_mainCommand;
    const QSet<QString> &chainIds = chain ? chain->ids : m_currentChainCommandIds;
    // Encerra runners AINDA VIVOS desta CADEIA (comando principal + seus
    // pre/post hooks), NUNCA todo o registry: senão abortar um comando que
    // falhou matava processos de outras execuções (ex.: terminal interativo).
    // O stop() marca m_stopRequested; o texto associado é neutro ("parada
    // solicitada"), pois quem para é o pipeline, não o usuário.
    for (auto &entry : m_runners) {
        if (!chainIds.contains(entry.first)) {
            continue;
        }
        if (entry.second && entry.second->isRunning()) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Abort do pipeline: encerrando runner de '%1'.").arg(entry.first));
            entry.second->stop();
        }
    }
    PipelineResult result;
    result.success = false;
    result.failedStage = stage;
    result.failedCommandId = commandId;
    result.stoppedByRequest = m_stoppedByRequestIds.contains(commandId);
    result.errorMessage = errorMessage;
    // Código real do passo que falhou (shell); sem código (HTTP, falha ao
    // iniciar, condição): 1.
    const int failedCode = m_lastExitCodes.value(commandId, 1);
    result.exitCode = failedCode > 0 ? failedCode : 1;
    // Exit code != 0 sem texto de erro: evita "Pipeline abortado: " vazio.
    if (result.errorMessage.trimmed().isEmpty() && m_lastExitCodes.contains(commandId)) {
        result.errorMessage = utils::tr(QStringLiteral("execution_pipeline.process_ended")).arg(result.exitCode);
    }

    const QString stageName = stage == PipelineStage::PreHooks ? QStringLiteral("PreHooks") :
                               stage == PipelineStage::MainCommand ? QStringLiteral("MainCommand") :
                               QStringLiteral("PostHooks");

    utils::Logger::error(kLogTag,
        QStringLiteral("Pipeline abortado no estágio '%1' pelo comando '%2': %3")
            .arg(stageName, commandId, result.errorMessage));

    emit logMessage(commandId, utils::tr(QStringLiteral("execution_pipeline.aborted")).arg(result.errorMessage) + QStringLiteral("\n"), true);
    // CLEANUP também no abort: é quando mais importa desmontar o que ficou de pé.
    if (m_allCommands) {
        runCleanupHooks(mainCommand, *m_allCommands);
    }
    result.mainCommandId = mainCommand.id;
    emit pipelineFinished(result);
}

} // namespace kai::engine
