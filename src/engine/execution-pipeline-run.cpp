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

namespace {
// Rótulo p/ log: o nome dado pelo usuário ou um resumo "left op right".
QString conditionLabel(const core::ExecutionCondition &c)
{
    if (!c.name.trimmed().isEmpty()) {
        return c.name;
    }
    return QStringLiteral("%1 %2 %3").arg(c.left, c.op, c.right).trimmed();
}
} // namespace

ExecutionPipeline::ConditionEvalResult ExecutionPipeline::evaluateConditions(const core::Command &command) const
{
    ConditionEvalResult result;
    if (command.executionConditions.isEmpty() || !m_envManager) {
        return result; // passed = true (sem guarda = sempre roda)
    }
    const bool isAnd = command.conditionCombinator != QStringLiteral("or");
    if (isAnd) {
        // E: a PRIMEIRA condição HABILITADA que falhar é a responsável;
        // desabilitadas são ignoradas.
        for (const core::ExecutionCondition &c : command.executionConditions) {
            if (!c.enabled) {
                continue;
            }
            if (!m_envManager->evaluateCondition(c.left, c.op, c.right)) {
                result.passed = false;
                result.decidingConditionLabel = conditionLabel(c);
                return result;
            }
        }
        return result; // todas as habilitadas passaram (ou nenhuma estava habilitada)
    }
    // OU: só falha se NENHUMA habilitada passar; a mensagem lista todas.
    QStringList labels;
    bool anyEnabled = false;
    for (const core::ExecutionCondition &c : command.executionConditions) {
        if (!c.enabled) {
            continue;
        }
        anyEnabled = true;
        if (m_envManager->evaluateCondition(c.left, c.op, c.right)) {
            return result; // passed = true
        }
        labels << conditionLabel(c);
    }
    if (!anyEnabled) {
        return result; // nenhuma condição habilitada = sem guarda, sempre roda
    }
    result.passed = false;
    result.decidingConditionLabel = labels.join(QStringLiteral(", "));
    return result;
}

void ExecutionPipeline::runSingleCommand(const core::Command &command, std::function<void(bool, const QString &)> onDone)
{
    // CONDIÇÃO DE EXECUÇÃO: runSingleCommand é o único pedágio do comando
    // principal, pre e post-hooks. Cleanup hooks (destacados) são checados em
    // runCleanupHooks.
    applyProjectBuiltins(command, *m_envManager);
    const ConditionEvalResult conditionResult = evaluateConditions(command);
    if (!conditionResult.passed) {
        const QString commandLabel = command.name.isEmpty() ? command.id : command.name;
        emit logMessage(command.id,
            utils::tr(QStringLiteral("execution_pipeline.condition_skipped"))
                .arg(commandLabel, conditionResult.decidingConditionLabel) + QStringLiteral("\n"),
            false);
        if (command.conditionSkipBehavior == QStringLiteral("failure")) {
            onDone(false, utils::tr(QStringLiteral("execution_pipeline.condition_failed"))
                .arg(commandLabel, conditionResult.decidingConditionLabel));
        } else {
            // "success" (padrão): p/ HTTP sinaliza o pulo, senão as abas
            // Resposta/Headers/Request ficam com dado velho.
            if (command.type == core::CommandType::Http) {
                emit commandSkippedByCondition(command.id, conditionResult.decidingConditionLabel);
            }
            onDone(true, QString());
        }
        return;
    }

    if (command.type == core::CommandType::Http) {
        if (!command.httpConfig.has_value()) {
            onDone(false, QStringLiteral("Comando HTTP '%1' sem http_config.").arg(command.id));
            return;
        }

        auto runner = std::make_unique<HttpRunner>();
        HttpRunner *rawRunner = runner.get();
        m_activeHttpRunner = std::move(runner);
        m_activeHttpCommandId = command.id;

        connect(rawRunner, &HttpRunner::logMessage, this, [this, id = command.id](const QString &text, bool isError) {
            emit logMessage(id, text, isError);
        });
        connect(rawRunner, &HttpRunner::dynamicVarPersistRequested, this,
            [this](const QString &scopeKey, const QString &name, const QString &value) {
            emit dynamicVarPersistRequested(scopeKey, name, value);
        });

        connect(rawRunner, &HttpRunner::finished, this,
            [this, onDone, id = command.id](const HttpResult &result) {
        emit httpResultReady(id, result);
            if (result.canceled) {
                m_stoppedByRequestIds.insert(id); // parada pedida: não é uma falha a notificar
            }
            // HTTP >= 400 é tratado como erro de pipeline.
            onDone(result.success, result.success ? QString() : result.errorMessage);
        });

        const core::HttpRequestSpec spec = core::resolveHttpRequest(command.httpConfig.value(),
                                                                    core::folderChain(command.folderId, m_folders));
        emit httpRequestStarted(command.id, spec.options.timeoutSec);
        rawRunner->execute(spec, *m_envManager);
        return;
    }

    // KIP precisa do app (view própria); no modo CLI local não há onde renderizar (spec 11 §15).
    if (command.kip && m_inheritTerminal) {
        onDone(false, utils::tr(QStringLiteral("kip.error.needs_app")));
        return;
    }
    // Registry por commandId: re-executar o MESMO comando encerra o runner
    // anterior; os de outros comandos são preservados.
    if (auto existing = m_runners.find(command.id); existing != m_runners.end()) {
        if (existing->second && existing->second->isRunning()) {
            utils::Logger::info(kLogTag,
                QStringLiteral("Re-execução de '%1': encerrando o runner anterior.").arg(command.id));
            existing->second->stop();
        }
        // Não destrói aqui (dtor bloqueante): o finished antigo limpa.
        m_runners.erase(existing);
    }
    auto runner = std::make_unique<ProcessRunner>();
    ProcessRunner *rawRunner = runner.get();
    rawRunner->setKillTimeoutMs(m_gracefulStopTimeoutMs);
    m_runners.insert_or_assign(command.id, std::move(runner));
    m_activeRunnerCommandId = command.id; // dono do runner atual (compat)

    // Captura de env só em hook shell NÃO-background (o processo precisa
    // terminar). Com KIP, stdin/stdout são o canal do protocolo: sem captura
    // (o `set_env` cobre), responders nem background (§15).
    const bool isKip = command.kip;
    const bool isBackground = command.isBackground && !isKip;
    // Python/Node não têm `export`: o ambiente que o dump leria nunca muda.
    const bool captureEnv = command.captureEnv && !command.isBackground && !isKip
        && command.language == core::CommandLanguage::Native;
    m_captureEnvActive = captureEnv;
    m_captureBuffer.clear();
    // Escopo de variáveis dinâmicas CAPTURADO agora: ingestCapturedEnv roda no
    // `finished`, quando o escopo do EnvironmentManager pode já ter mudado.
    const QString captureScopeKey = m_envManager ? m_envManager->currentDynamicVarScope() : QString();

    // Captura de env não usa PTY: ele ecoa o comando e converte quebras de
    // linha, quebrando o parse.
    if (captureEnv) {
        rawRunner->setUsePty(false);
    } else if (m_inheritTerminal) {
        // Modo CLI local: usa o terminal de quem chamou.
        rawRunner->setInheritTerminal(true);
    }

    // TTY por ALVO (TerminalProfile::usePty): true = ConPTY/forkpty (docker
    // -it, isatty); false = QProcess robusto (&&, aspas, pipes). Sem alvo,
    // mantém o padrão ConPTY/forkpty.
    const QString effTarget = effectiveTerminalProfileName(command);
    // Com alvo, o `cd` vai DENTRO do comando (applyTerminalProfile); o lançador não deve dar pushd.
    rawRunner->setCommandHandlesWorkingDir(!effTarget.isEmpty());
    QString remoteRunId; // id do arquivo de PID remoto (kill do lado WSL)
    // Sabor efetivo p/ wrapForEnvCapture. Sem alvo, vale o shell padrão da
    // PLATAFORMA (cmd.exe no Windows, bash no Unix; ver ProcessRunner::start).
#if defined(Q_OS_WIN)
    core::ShellFlavor captureShellFlavor = core::ShellFlavor::Cmd;
#else
    core::ShellFlavor captureShellFlavor = core::ShellFlavor::Posix;
#endif
    if (!effTarget.isEmpty()) {
        bool targetUsePty = true;
        core::ShellFlavor targetFlavor = core::ShellFlavor::Posix;
        for (const core::TerminalProfile &t : m_terminalProfiles) {
            if (t.name == effTarget) {
                targetUsePty = t.usePty;
                targetFlavor = effectiveShellFlavor(t);
                break;
            }
        }
        captureShellFlavor = targetFlavor;
        // Não religa o PTY na captura de env (desligado de propósito acima).
        if (!captureEnv) {
            rawRunner->setUsePty(targetUsePty);
        }
        // KILL REMOTO: no WSL2 os processos Linux não são filhos Windows do
        // wsl.exe (taskkill deixa fantasmas). O shell remoto grava o PID em
        // /tmp/kai-<runId>.pid e stop() mata o GRUPO por lá. Só alvo POSIX:
        // `echo $$` é sintaxe bash.
        if (targetFlavor == core::ShellFlavor::Posix) {
            remoteRunId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
            rawRunner->setRemoteKillCommandLine(buildRemoteKillCommandLine(effTarget, remoteRunId));
        }
        utils::Logger::info(kLogTag,
            QStringLiteral("Comando '%1' com alvo '%2'%3: usePty=%4.")
                .arg(command.id, effTarget,
                     command.terminalTarget.isEmpty() ? QStringLiteral(" (padrão)") : QString(),
                     targetUsePty ? QStringLiteral("true (ConPTY/tty)")
                                  : QStringLiteral("false (QProcess robusto)")));
    }

    // Auto-responsores: matcher por execução com buffer rolante que responde
    // prompts via stdin. Só cria se há responders.
    std::shared_ptr<OutputResponderMatcher> matcher;
    if (!command.responders.isEmpty() && !isKip) {
        // m_envManager habilita {{VAR}} na resposta.
        matcher = std::make_shared<OutputResponderMatcher>(command.responders, m_envManager);
    }

    // Conecta a sessão ANTES do pipeline: no `finished` o resultado dela deve
    // estar pronto quando o lambda do pipeline rodar (Qt chama os slots na
    // ordem de conexão). Em KIP a saída crua NÃO vai pro log: a sessão repassa
    // só o que é log.
    KipSession *kipSession = isKip ? createKipSession(command, rawRunner, captureScopeKey) : nullptr;

    if (!isKip) {
        connect(rawRunner, &ProcessRunner::outputReady, this,
                [this, id = command.id, captureEnv, matcher, rawRunner](const QString &text, bool isError) {
            if (captureEnv) {
                // Acumula p/ extrair o ambiente ao final; mostra só o que vem ANTES do sentinela.
                m_captureBuffer += text;
                const QString sentinel = QString::fromLatin1(kEnvSentinel);
                const int pos = m_captureBuffer.indexOf(sentinel);
                if (pos < 0) {
                    emit logMessage(id, text, isError);
                } else {
                    // Emite só o que faltava antes do sentinela (uma única vez).
                    const int alreadyShown = m_captureBuffer.length() - text.length();
                    if (alreadyShown < pos) {
                        emit logMessage(id, m_captureBuffer.mid(alreadyShown, pos - alreadyShown), isError);
                    }
                }
                return;
            }
            emit logMessage(id, text, isError);

            // Casa a saída e responde no stdin, ecoando a resposta no log.
            if (matcher && rawRunner->isRunning()) {
                const auto replies = matcher->feed(text);
                for (const auto &reply : replies) {
                    emit logMessage(id,
                        utils::tr(QStringLiteral("execution_pipeline.auto_reply")).arg(reply.text) + QStringLiteral("\n"), false);
                    rawRunner->writeToStdin(reply.text);
                }
            }
        });
    }

    if (isBackground) {
        // Background: o pipeline não espera; sucesso imediato ao iniciar (ex.: dev server).
        connect(rawRunner, &ProcessRunner::started, this, [this, id = command.id, onDone]() {
            emit backgroundProcessStarted(id, runnerFor(id));
            onDone(true, QString());
        });

        connect(rawRunner, &ProcessRunner::finished, this, [this, id = command.id, ignoreExitCode = command.ignoreExitCode](const ProcessResult &result) {
            // ignoreExitCode (ex.: explorer.exe via WSL retorna != 0 funcionando); crash continua reportado.
            const bool success = ignoreExitCode ? !result.crashed : (result.exitCode == 0 && !result.crashed);
            emit logMessage(id,
                // Só reporta o término quando FALHOU (o badge mostra o normal).
                success ? QString()
                        : utils::tr(QStringLiteral("execution_pipeline.process_ended")).arg(result.exitCode) + QStringLiteral("\n"),
                !success);
        });

        resolveCommandLine(command, [this, cmd = command, effTarget, remoteRunId, rawRunner](const QString &line, bool envEmbedded) {
            if (!isCurrentRunner(cmd.id, rawRunner)) {
                return;
            }
            const QString backgroundLine = applyTerminalProfile(
                cmd, line, effectiveWorkingDir(cmd, *m_envManager), remoteRunId, envEmbedded);
            warnIfCommandLineTooLong(cmd.id, backgroundLine);
            rawRunner->start(backgroundLine,
                             effTarget.isEmpty() ? effectiveWorkingDir(cmd, *m_envManager) : QString(),
                             processEnvFor(cmd));
        });
        return;
    }

    connect(rawRunner, &ProcessRunner::finished, this,
        [this, onDone, captureEnv, captureScopeKey, declaredEnvVars = command.declaredEnvVars,
         id = command.id, ignoreExitCode = command.ignoreExitCode,
         kip = QPointer<KipSession>(kipSession)](const ProcessResult &result) {
        if (captureEnv) {
            ingestCapturedEnv(m_captureBuffer, captureScopeKey, declaredEnvVars);
            m_captureBuffer.clear();
            m_captureEnvActive = false;
        }
        // Remoção DIFERIDA: destruir aqui seria destruir o objeto dentro do
        // seu próprio sinal (dtor bloqueante).
        QMetaObject::invokeMethod(this, [this, id]() {
            const auto it = m_runners.find(id);
            if (it != m_runners.end() && it->second && !it->second->isRunning()) {
                m_runners.erase(it);
            }
        }, Qt::QueuedConnection);
        if (kip && kip->outcome().finished) {
            // Comando KIP: a sessão decide o resultado (ver KipSession).
            const KipOutcome &outcome = kip->outcome();
            m_lastExitCodes.insert(id, outcome.success ? 0 : outcome.exitCode);
            if (!outcome.success && outcome.stoppedByRequest) {
                m_stoppedByRequestIds.insert(id);
            }
            onDone(outcome.success, outcome.success ? QString() : outcome.errorMessage);
            return;
        }
        const bool success = ignoreExitCode ? !result.crashed : (result.exitCode == 0 && !result.crashed);
        m_lastExitCodes.insert(id, success ? 0 : result.exitCode);
        if (!success && result.stoppedByRequest) {
            m_stoppedByRequestIds.insert(id);
        }
        onDone(success, success ? QString() : result.errorMessage);
    });

    resolveCommandLine(command, [this, command, captureEnv, captureShellFlavor, effTarget, remoteRunId, rawRunner,
                                 kip = QPointer<KipSession>(kipSession)](const QString &interpolatedCommand, bool envEmbedded) {
        if (!isCurrentRunner(command.id, rawRunner)) {
            return;
        }
        const QString interpolatedWorkingDir = effectiveWorkingDir(command, *m_envManager);

        // ORDEM IMPORTA: com alvo, applyTerminalProfile embrulha o comando num
        // shell ANINHADO (`bash -lc '...'`); wrapForEnvCapture roda ANTES p/
        // sentinela+dump ficarem na mesma sessão do `export`.
        const QString finalCommand = captureEnv
            ? applyTerminalProfile(command, wrapForEnvCapture(interpolatedCommand, captureShellFlavor), interpolatedWorkingDir, remoteRunId, envEmbedded)
            : applyTerminalProfile(command, interpolatedCommand, interpolatedWorkingDir, remoteRunId, envEmbedded);

        // Com alvo, o working_dir vai via `cd` DENTRO do shell; passá-lo ao
        // QProcess daria FailedToStart se o caminho for inválido p/ o lançador.
        const QString startWorkingDir = effTarget.isEmpty() ? interpolatedWorkingDir : QString();
        warnIfCommandLineTooLong(command.id, finalCommand);
        if (kip) {
            // Arma o relógio do handshake e força o modo pipe (sem PTY) — ver KipSession::start.
            kip->start(finalCommand, startWorkingDir, processEnvFor(command));
            return;
        }
        rawRunner->start(finalCommand, startWorkingDir, processEnvFor(command));
    });
}

void ExecutionPipeline::captureScriptOutput(const core::Command &command, const QString &script,
                                            core::EnvironmentManager &env, int timeoutMs,
                                            std::function<void(const ScriptCaptureResult &)> done)
{
    m_envManager = &env;
    core::Command probe = command;
    probe.command = script;
    probe.language = core::CommandLanguage::Native;
    probe.type = core::CommandType::Command;
    probe.kip = false;
    probe.captureEnv = false;
    probe.isBackground = false;
    probe.interactiveTerminal = false;
    probe.hooks = core::Hooks();

    struct State {
        QString out;
        QString err;
        bool timedOut = false;
    };
    auto state = std::make_shared<State>();
    auto *runner = new ProcessRunner(this);
    runner->setUsePty(false);
    const QString effTarget = effectiveTerminalProfileName(probe);
    runner->setCommandHandlesWorkingDir(!effTarget.isEmpty());

    connect(runner, &ProcessRunner::outputReady, this, [state](const QString &text, bool isError) {
        (isError ? state->err : state->out) += text;
    });
    connect(runner, &ProcessRunner::finished, this, [runner, state, done](const ProcessResult &result) {
        ScriptCaptureResult out;
        if (state->timedOut) {
            out.errorMessage = utils::tr(QStringLiteral("params.script.error.timeout"));
        } else if (result.crashed || result.exitCode != 0) {
            out.errorMessage = state->err.trimmed().isEmpty() ? result.errorMessage : state->err.trimmed();
            if (out.errorMessage.isEmpty()) {
                out.errorMessage = utils::tr(QStringLiteral("params.script.error.exit_code")).arg(result.exitCode);
            }
        } else {
            out.success = true;
            out.output = state->out;
        }
        runner->deleteLater();
        done(out);
    });
    QTimer::singleShot(timeoutMs, runner, [runner, state]() {
        if (runner->isRunning()) {
            state->timedOut = true;
            runner->forceStop();
        }
    });

    const QString workingDir = effectiveWorkingDir(probe, env);
    const QString finalCommand = applyTerminalProfile(probe, commandLineFor(probe, env), workingDir);
    runner->start(finalCommand, effTarget.isEmpty() ? workingDir : QString(), processEnvFor(probe));
}

void ExecutionPipeline::runCleanupHooks(const core::Command &command,
                                        const QMap<QString, core::Command> &allCommands)
{
    if (command.hooks.cleanup.isEmpty()) {
        return;
    }
    // IDEMPOTENTE por comando: ver comentário de m_cleanupFiredFor no header.
    if (m_cleanupFiredFor.contains(command.id)) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Cleanup de '%1' já foi disparado nesta execução; ignorando.")
                .arg(command.id));
        return;
    }
    m_cleanupFiredFor.insert(command.id);
    utils::Logger::info(kLogTag,
        QStringLiteral("Executando %1 hook(s) de cleanup de '%2'.")
            .arg(command.hooks.cleanup.size()).arg(command.id));

    for (const QString &hookId : command.hooks.cleanup) {
        const auto it = allCommands.constFind(hookId);
        if (it == allCommands.constEnd()) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Hook de cleanup '%1' não encontrado; ignorando.").arg(hookId));
            continue;
        }
        const core::Command &hook = it.value();
        if (hook.kip && hook.type == core::CommandType::Command) {
            emit logMessage(command.id,
                utils::tr(QStringLiteral("kip.error.hook_refused")).arg(hook.name.isEmpty() ? hookId : hook.name)
                    + QStringLiteral("\n"), true);
            continue;
        }
        if (hook.type != core::CommandType::Command) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Hook de cleanup '%1' não é shell; ignorando.").arg(hookId));
            continue;
        }
        // Cleanup roda destacado (fora de runSingleCommand): checa a condição aqui.
        const ConditionEvalResult hookConditionResult = evaluateConditions(hook);
        if (!hookConditionResult.passed) {
            emit logMessage(command.id,
                utils::tr(QStringLiteral("execution_pipeline.condition_skipped"))
                    .arg(hook.name.isEmpty() ? hookId : hook.name, hookConditionResult.decidingConditionLabel) + QStringLiteral("\n"),
                false);
            continue;
        }

        // DESTACADO: sobrevive ao fim do pipeline/app, sem bloquear a GUI nem
        // entrar no registry.
        const QString effTarget = effectiveTerminalProfileName(hook);
        const QString interpolatedHookWorkingDir = effectiveWorkingDir(hook, *m_envManager);
        const QString script = applyTerminalProfile(
            hook, commandLineFor(hook, *m_envManager),
            interpolatedHookWorkingDir, QString());

        emit logMessage(command.id,
            utils::tr(QStringLiteral("execution_pipeline.cleanup.running")).arg(hook.name.isEmpty() ? hookId : hook.name) + QStringLiteral("\n"), false);

        bool started = false;
#if defined(Q_OS_WIN)
        // VERBATIM via setNativeArguments: em LISTA o QProcess re-aplica o
        // quoting do Windows sobre as aspas simples do bash e desmonta o comando.
        {
            QProcess detached;
            detached.setProgram(QStringLiteral("cmd.exe"));
            // Mesma armadilha de UNC do ProcessRunner: sem workingDir o cwd
            // herdado do kai.exe pode ser UNC (interop do WSL); usa diretório
            // seguro + pushd. Com alvo o `cd` está no script (planWindowsLaunchDirs).
            const WindowsLaunchDirs launchDirs = planWindowsLaunchDirs(
                effTarget.isEmpty() ? interpolatedHookWorkingDir : QString(),
                QDir::currentPath(), !effTarget.isEmpty(), windowsSafeNonUncStartDir());
            detached.setWorkingDirectory(launchDirs.nativeCwd);
            detached.setNativeArguments(QStringLiteral("/c ")
                + wrapWindowsCommandForUncWorkingDir(script, launchDirs.pushdDir));
            isolateDetachedProcess(detached);
            started = detached.startDetached();
        }
#else
        {
            // Com alvo, o `cd` já está dentro do script (mesma regra do
            // comando principal); sem alvo o diretório vai no próprio processo.
            QProcess detached;
            detached.setProgram(QStringLiteral("/bin/bash"));
            detached.setArguments({QStringLiteral("-lc"), script});
            if (effTarget.isEmpty() && !interpolatedHookWorkingDir.isEmpty()) {
                detached.setWorkingDirectory(interpolatedHookWorkingDir);
            }
            started = detached.startDetached();
        }
#endif
        if (!started) {
            utils::Logger::error(kLogTag,
                QStringLiteral("Falha ao iniciar o hook de cleanup '%1'.").arg(hookId));
            emit logMessage(command.id,
                utils::tr(QStringLiteral("execution_pipeline.cleanup.start_failed")).arg(hookId) + QStringLiteral("\n"), true);
        }
    }
}

} // namespace kai::engine
