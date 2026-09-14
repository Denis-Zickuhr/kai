#include "cli/cli-local-executor.h"
#include "cli/terminal-mode-filter.h"
#include "cli/app-delegate.h"
#include "cli/cli-help-format.h"
#include "cli/terminal-input.h"

#include "core/http-resolve.h"
#include "core/cli-path-resolver.h"
#include "core/cli-param-binder.h"
#include "core/cli-reserved-verbs.h"
#include "core/config-manager.h"
#include "core/environment-manager.h"
#include "engine/execution-pipeline.h"
#include "ui/features/collections/project-selector.h"
#include "utils/console-context.h"
#include "utils/duration-format.h"
#include "utils/translation-manager.h"
#include "utils/logger.h"
#include "utils/path-format.h"

#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#endif

namespace kai::cli {

namespace {

QTextStream &out()
{
    return kai::utils::cliOut();
}
QTextStream &err()
{
    return kai::utils::cliErr();
}

QString findLocalKaiFile(const QString &directoryPath)
{
    const QString candidate = QDir(directoryPath).filePath(QStringLiteral("kai.yml"));
    return QFile::exists(candidate) ? candidate : QString();
}

// Diferenças de EXECUÇÃO entre os modos (resolução e binding são idênticos):
//  - LOCAL: só o arquivo manda; sem alvo de terminal do app (exceto a ponte pro WSL de origem) nem
//    dinâmicas persistidas.
//  - GLOBAL: roda como a GUI rodaria (alvo de terminal padrão incluso).
struct CliRunProfile {
    QVector<core::TerminalProfile> terminalProfiles;
    core::InterpreterSettings interpreters;
    bool useAppRuntimeState = false; // dinâmicas persistidas + graceful stop
    bool inheritTerminal = false;    // ver ExecutionPipeline::setInheritTerminal
    bool delegateToApp = false;      // GLOBAL: roda pelo app (ver runViaApp)
    // PTY + repasse do teclado em vez de herdar o terminal (kai.exe local chamado do WSL).
    bool forwardInput = false;
    CliFlags flags; // -d/-n/--dry-run/--json (ver cli-local-executor.h)
};

// Enquanto o comando herda o terminal, o Ctrl+C vai pro grupo de processos inteiro. O kai precisa
// sobreviver a ele pra devolver o código de saída do comando. Handler no-op, não SIG_IGN: a disposição
// "ignorada" seria herdada pelo filho, que também não pararia com Ctrl+C.
class InterruptPassthroughGuard {
public:
    InterruptPassthroughGuard()
    {
#ifdef Q_OS_WIN
        ::SetConsoleCtrlHandler(&InterruptPassthroughGuard::onConsoleCtrl, TRUE);
#else
        struct sigaction action {};
        action.sa_handler = [](int) {};
        sigemptyset(&action.sa_mask);
        ::sigaction(SIGINT, &action, &m_previous);
#endif
    }
    ~InterruptPassthroughGuard()
    {
#ifdef Q_OS_WIN
        ::SetConsoleCtrlHandler(&InterruptPassthroughGuard::onConsoleCtrl, FALSE);
#else
        ::sigaction(SIGINT, &m_previous, nullptr);
#endif
    }
    InterruptPassthroughGuard(const InterruptPassthroughGuard &) = delete;
    InterruptPassthroughGuard &operator=(const InterruptPassthroughGuard &) = delete;

private:
#ifdef Q_OS_WIN
    static BOOL WINAPI onConsoleCtrl(DWORD type)
    {
        return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT;
    }
#else
    struct sigaction m_previous {};
#endif
};

// -n/--notify: mesma notificação do `kai raise`, pelo app.
void notifyRunFinished(const QString &commandName, int exitCode, qint64 elapsedMs)
{
    QJsonObject request;
    request[QStringLiteral("cmd")] = QStringLiteral("raise");
    request[QStringLiteral("level")] = exitCode == 0 ? QStringLiteral("info") : QStringLiteral("error");
    request[QStringLiteral("message")] = exitCode == 0
        ? utils::tr(QStringLiteral("cli.notify.done")).arg(commandName, utils::formatShortDuration(elapsedMs))
        : utils::tr(QStringLiteral("cli.notify.failed")).arg(commandName).arg(exitCode).arg(utils::formatShortDuration(elapsedMs));
    requestApp(request, 15000);
}

// -d/--detached no modo local: relança este kai (sem o -d) em segundo plano. KAI_CLI_OUTPUT_FILE faz o
// filho mandar stdout/stderr pro log; herdar handles no Windows levaria os pipes da interop do WSL e
// seguraria o terminal até o filho sair.
int spawnDetachedSelf(const core::Command &command, const QStringList &pathArgs, bool global, bool notify)
{
    QString slug = command.cliPath.isEmpty() ? command.id : command.cliPath;
    slug.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    const QString logPath = QDir(QDir::tempPath()).filePath(QStringLiteral("kai-%1-%2.log")
        .arg(slug, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));

    QProcess child;
    child.setProgram(QCoreApplication::applicationFilePath());
    QStringList childArgs;
    if (global) {
        childArgs << QStringLiteral("--global");
    }
    if (notify) {
        childArgs << QStringLiteral("--notify"); // o filho avisa quando terminar
    }
    childArgs << pathArgs;
    child.setArguments(childArgs);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("KAI_CLI_OUTPUT_FILE"), logPath);
    child.setProcessEnvironment(env);
    child.setWorkingDirectory(QDir::currentPath());
#ifdef Q_OS_WIN
    child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NO_WINDOW;
        a->inheritHandles = false;
    });
#endif
    qint64 pid = 0;
    if (!child.startDetached(&pid)) {
        err() << utils::tr(QStringLiteral("cli.detached.spawn_failed")).arg(command.name) << "\n";
        err().flush();
        return 1;
    }
    // kai.exe chamado do WSL: mostra o log no caminho que o shell do Linux abre.
    const QString shownLog = utils::launchedFromWslInterop() ? utils::toPosixPath(logPath)
                                                             : QDir::toNativeSeparators(logPath);
    out() << utils::tr(QStringLiteral("cli.detached.local_started")).arg(command.name).arg(pid).arg(shownLog) << "\n";
    out().flush();
    return 0;
}

LocalExecutionOutcome runCliPathAgainst(const QVector<core::Folder> &allFolders,
                                          const QVector<core::Command> &commands,
                                          const QStringList &pathArgs,
                                          const CliRunProfile &runProfile)
{
    LocalExecutionOutcome outcome;
    outcome.handled = true;

    core::CliPathResolver resolver(allFolders, commands);
    const core::CliPathResolution resolution = resolver.resolve(pathArgs);

    // Pasta listada: o maior prefixo de `pathArgs` que ainda resolve pra uma pasta (o resolvedor não
    // diz quantos tokens consumiu).
    auto listedFolderTokens = [&]() {
        for (int n = pathArgs.size(); n > 0; --n) {
            if (resolver.resolve(pathArgs.mid(0, n)).kind == core::CliPathResolution::Kind::Folder) {
                return pathArgs.mid(0, n);
            }
        }
        return QStringList();
    };

    if (resolution.kind == core::CliPathResolution::Kind::NotFound) {
        err() << utils::tr(QStringLiteral("cli_local.error.path_not_found")).arg(pathArgs.join(QLatin1Char(' '))) << "\n";
        err().flush();
        out() << (runProfile.flags.json ? formatCliListingJson(resolution.children, commands, listedFolderTokens())
                                         : formatCliListing(resolution.children, commands, listedFolderTokens()));
        out().flush();
        outcome.exitCode = 2;
        return outcome;
    }
    if (resolution.kind == core::CliPathResolution::Kind::Folder) {
        // Caminho incompleto (ou vazio) — descoberta automática, não é erro.
        out() << (runProfile.flags.json ? formatCliListingJson(resolution.children, commands, listedFolderTokens())
                                         : formatCliListing(resolution.children, commands, listedFolderTokens()));
        out().flush();
        outcome.exitCode = 0;
        return outcome;
    }

    const core::Command *command = nullptr;
    for (const core::Command &c : commands) {
        if (c.id == resolution.commandId) {
            command = &c;
            break;
        }
    }
    if (!command) {
        // Fallback seguro (commandId sempre vem de `commands`) em vez de ponteiro nulo adiante.
        err() << utils::tr(QStringLiteral("cli_local.error.import_failed")).arg(QString()) << "\n";
        err().flush();
        outcome.exitCode = 1;
        return outcome;
    }

    // "Executar no diretório de invocação": cópia, pra não mexer no `commands` carregado. No modo
    // GLOBAL o cliente manda o mesmo diretório pelo IPC (ver runViaApp); aqui cobre dry-run e fallback.
    std::optional<core::Command> invocationDirCommand;
    if (command->cliWorkingDir == core::CliWorkingDir::Invocation) {
        invocationDirCommand = *command;
        invocationDirCommand->workingDirMode = core::WorkingDirMode::Custom;
        invocationDirCommand->workingDir = QDir::currentPath();
        command = &*invocationDirCommand;
    }

    const core::CliParamBindingResult bound = core::bindCliParams(command->params, resolution.remainingArgs);
    const QStringList resolvedPathTokens = pathArgs.mid(0, pathArgs.size() - resolution.remainingArgs.size());
    if (bound.helpRequested) {
        out() << formatCommandHelp(*command, resolvedPathTokens);
        out().flush();
        outcome.exitCode = 0;
        return outcome;
    }
    if (!bound.ok()) {
        for (const core::CliParamBindingIssue &issue : bound.issues) {
            err() << issue.message << "\n";
        }
        err().flush();
        outcome.exitCode = 2;
        return outcome;
    }

    // KIP precisa do app (view, stdin/stdout do protocolo): o modo LOCAL, com terminal herdado, recusa.
    if (command->kip && command->type == core::CommandType::Command && !runProfile.delegateToApp) {
        err() << utils::tr(QStringLiteral("kip.error.needs_app")) << "\n";
        err().flush();
        outcome.exitCode = 1;
        return outcome;
    }

    // Ambiente: variáveis GLOBAIS do ambiente ATIVO (lidas do settings.json pelo ConfigManager, que
    // não depende de Qt Widgets) + env_vars da cadeia de pastas (filho sobrescreve pai) + valores de
    // parâmetro ligados. Sem persistência de dinâmicas entre invocações.
    core::ConfigManager configManager;
    const core::SettingsData settings = configManager.loadSettings();
    QMap<QString, QString> globalVars;
    for (const core::Environment &e : settings.environments) {
        if (e.id == settings.activeEnvironmentId) {
            globalVars = e.vars;
            break;
        }
    }

    const QVector<core::Folder> chain = core::folderChain(command->folderId, allFolders);
    QMap<QString, QString> mergedFolderVars;
    for (const core::Folder &f : chain) {
        for (auto it = f.envVars.constBegin(); it != f.envVars.constEnd(); ++it) {
            mergedFolderVars[it.key()] = it.value();
        }
    }
    QString dynamicScope;
    for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
        if (it->isProject) {
            dynamicScope = it->id;
            break;
        }
    }

    core::EnvironmentManager envManager;
    envManager.setGlobalVars(globalVars);
    envManager.setFolderVars(mergedFolderVars);
    envManager.setDynamicVarScope(dynamicScope);
    QMap<QString, QString> paramVars;
    for (const core::Parameter &p : command->params) {
        paramVars.insert(p.name, bound.values.value(p.name, p.defaultValue));
    }
    envManager.setParamVars(paramVars);

    if (runProfile.flags.dryRun) {
        engine::ExecutionPipeline previewPipeline;
        previewPipeline.setFolders(allFolders);
        previewPipeline.setTerminalProfiles(runProfile.terminalProfiles);
        previewPipeline.setInterpreters(runProfile.interpreters);
        DryRunPreview preview;
        preview.name = command->name;
        preview.pathTokens = resolvedPathTokens;
        preview.type = core::commandTypeToString(command->type);
        preview.target = previewPipeline.effectiveTerminalProfileName(*command);
        preview.workingDir = previewPipeline.effectiveWorkingDir(*command, envManager);
        if (command->type == core::CommandType::Http && command->httpConfig.has_value()) {
            preview.httpMethod = core::httpMethodToString(command->httpConfig->method);
            preview.httpUrl = envManager.interpolate(command->httpConfig->url);
            preview.httpBody = envManager.interpolate(command->httpConfig->body);
        } else {
            if (command->language == core::CommandLanguage::Native) {
                preview.command = envManager.interpolate(command->command);
            } else {
                // O que roda é o código, sem {{VAR}} nem aspas (a linha real é o código em base64).
                preview.command = command->command;
                preview.language = core::commandLanguageToString(command->language);
                preview.interpreter = runProfile.interpreters.resolve(command->language, command->interpreter);
            }
        }
        auto hookNames = [&commands](const QStringList &ids) {
            QStringList names;
            for (const QString &id : ids) {
                const auto it = std::find_if(commands.cbegin(), commands.cend(),
                    [&id](const core::Command &c) { return c.id == id; });
                names << (it != commands.cend() ? it->name : id);
            }
            return names;
        };
        preview.preHooks = hookNames(command->hooks.pre);
        preview.postHooks = hookNames(command->hooks.post);
        out() << (runProfile.flags.json ? formatDryRunJson(preview) : formatDryRun(preview));
        out().flush();
        outcome.exitCode = 0;
        return outcome;
    }

    // -w/--window: a janela é do app, então só vale quando o comando roda POR ele (-g).
    if (runProfile.flags.window && !runProfile.delegateToApp) {
        err() << utils::tr(QStringLiteral("cli.error.window_requires_global")) << "\n";
        err().flush();
        outcome.exitCode = 2;
        return outcome;
    }

    if (runProfile.flags.detached) {
        // GLOBAL: o app aceita, registra e roda; o terminal volta na hora.
        if (runProfile.delegateToApp) {
            if (const std::optional<int> accepted =
                    runViaApp(*command, paramVars, /*detached=*/true, runProfile.flags.notify,
                              runProfile.flags.window)) {
                outcome.exitCode = *accepted;
                return outcome;
            }
        }
        // LOCAL (ou app inalcançável): este mesmo kai, relançado em segundo
        // plano sem o -d, roda tudo (hooks inclusive) com a saída num log.
        outcome.exitCode = spawnDetachedSelf(*command, pathArgs, runProfile.delegateToApp,
                                             runProfile.flags.notify);
        return outcome;
    }

    QElapsedTimer runTimer;
    runTimer.start();
    auto finishWith = [&](int exitCode) {
        if (runProfile.flags.notify) {
            notifyRunFinished(command->name, exitCode, runTimer.elapsed());
        }
        outcome.exitCode = exitCode;
        return outcome;
    };

    bool inheritTerminal = runProfile.inheritTerminal;
    if (runProfile.delegateToApp) {
        if (const std::optional<int> delegatedExit = runViaApp(*command, paramVars, /*detached=*/false, /*notify=*/false,
                          runProfile.flags.window)) {
            return finishWith(*delegatedExit);
        }
        // App inalcançável: roda aqui mesmo (sem registro), interativo, em vez de falhar.
        err() << utils::tr(QStringLiteral("cli.stream.launch_failed")) << "\n";
        err().flush();
        inheritTerminal = true;
    }
    if (runProfile.useAppRuntimeState) {
        envManager.seedPersistedDynamicVars(configManager.loadPersistedDynamicVars());
    }

    QMap<QString, core::Command> allCommandsById;
    for (const core::Command &c : commands) {
        allCommandsById.insert(c.id, c);
    }

    engine::ExecutionPipeline pipeline;
    pipeline.setFolders(allFolders);
    pipeline.setTerminalProfiles(runProfile.terminalProfiles);
    pipeline.setInterpreters(runProfile.interpreters);
    pipeline.setInheritTerminal(inheritTerminal);
    if (runProfile.useAppRuntimeState) {
        pipeline.setGracefulStopTimeoutMs(settings.gracefulStopTimeoutSec * 1000);
        // Mesma gravação write-through da GUI pra EnvExtractor persist (senão um login via `kai -g` se perdia).
        QObject::connect(&pipeline, &engine::ExecutionPipeline::dynamicVarPersistRequested, &pipeline,
            [&configManager](const QString &scopeKey, const QString &name, const QString &value) {
                QMap<QString, QMap<QString, QString>> all = configManager.loadPersistedDynamicVars();
                all[scopeKey][name] = value;
                configManager.savePersistedDynamicVars(all);
            });
    }

    // Saída vai pro terminal REAL de quem chamou, filtrada por TerminalModeFilter (o ConPTY emite
    // ESC[?9001h e estragava o terminal do WSL). Um filtro por stream: sequências podem vir partidas.
    TerminalModeFilter outFilter;
    TerminalModeFilter errFilter;
    QString pendingEcho;
    bool stripEcho = false;
    std::optional<StdinForwarder> stdinForwarder;
    std::optional<InterruptForwarder> interruptForwarder;
    if (runProfile.forwardInput && !inheritTerminal) {
        stdinForwarder.emplace([&pipeline, &pendingEcho, &stripEcho](const QString &text) {
            engine::ProcessRunner *runner = pipeline.activeProcessRunner();
            if (runner && runner->isRunning()) {
                runner->writeRaw(text);
                if (stripEcho) {
                    pendingEcho += text;
                }
            }
        });
        stripEcho = !stdinForwarder->isTty();
        // Com o stdin num pipe (WSL) o Ctrl+C chega como sinal, não como tecla: vira ^C no terminal do comando.
        interruptForwarder.emplace([&pipeline, &pendingEcho, &stripEcho]() {
            engine::ProcessRunner *runner = pipeline.activeProcessRunner();
            if (runner && runner->isRunning()) {
                runner->writeRaw(QStringLiteral("\x03"));
                if (stripEcho) {
                    pendingEcho += QStringLiteral("^C"); // o tty do Linux já mostrou o ^C
                }
            }
        });
    }
    QObject::connect(&pipeline, &engine::ExecutionPipeline::logMessage, &pipeline,
        [&outFilter, &errFilter, &pendingEcho, &stripEcho, interactive = stdinForwarder.has_value()](
            const QString &, const QString &chunk, bool isError) {
            const QString text = stripEcho ? stripPendingEcho(chunk, pendingEcho) : chunk;
            QTextStream &stream = isError ? err() : out();
            stream << (isError ? errFilter : outFilter).feed(text);
            // Chunks de shell já trazem a quebra; só mensagens avulsas (ex: HttpRunner) precisam dela.
            // Nunca com repasse de teclado: um prompt ("[y/N] ") termina sem quebra de propósito.
            if (!interactive && !text.endsWith(QLatin1Char('\n'))) {
                stream << "\n";
            }
            stream.flush();
        });

    // Comando em SEGUNDO PLANO: o pipeline exige que quem recebe o sinal tome posse do ProcessRunner
    // de forma SÍNCRONA. Vazamos o runner de propósito (sem kill()/destruidor) para o processo
    // continuar vivo depois que este `kai` sai. Limitação: não aparece em `kai ps`.
    QObject::connect(&pipeline, &engine::ExecutionPipeline::backgroundProcessStarted, &pipeline,
        [&pipeline](const QString &commandId, engine::ProcessRunner *) {
            std::unique_ptr<engine::ProcessRunner> runner = pipeline.releaseRunnerFor(commandId);
            runner.release(); // vazamento intencional — ver comentário acima
        });

    QEventLoop loop;
    engine::PipelineResult result;
    QObject::connect(&pipeline, &engine::ExecutionPipeline::pipelineFinished, &pipeline,
        [&loop, &result](const engine::PipelineResult &r) {
            result = r;
            loop.quit();
        });

    {
        std::optional<InterruptPassthroughGuard> interruptGuard;
        if (inheritTerminal) {
            interruptGuard.emplace();
        }
        pipeline.run(*command, allCommandsById, envManager);
        loop.exec();
    }
    out() << outFilter.flush();
    out().flush();
    err() << errFilter.flush();
    err().flush();

    return finishWith(result.success ? 0 : qMax(1, result.exitCode));
}

} // namespace

std::optional<core::TerminalProfile> localWslBridgeProfile(const QString &cwd)
{
    static const QRegularExpression wslUnc(
        QStringLiteral(R"(^(?:\\\\|//)wsl(?:\.localhost|\$)[\\/]([^\\/]+)(?:[\\/]|$))"));
    const QRegularExpressionMatch m = wslUnc.match(cwd.trimmed());
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    // Nome SEM aspas: o wsl.exe não remove aspas (`-d "Ubuntu"` dá WSL_E_DISTRO_NOT_FOUND), então só
    // aceita nomes que não precisam de quoting.
    static const QRegularExpression safeDistro(QStringLiteral(R"(^[A-Za-z0-9._-]+$)"));
    if (!safeDistro.match(m.captured(1)).hasMatch()) {
        return std::nullopt;
    }
    core::TerminalProfile bridge;
    bridge.name = QStringLiteral("WSL (%1)").arg(m.captured(1));
    // Mesmo formato do alvo WSL padrão sugerido pelo app: base64 à prova de
    // aspas, -lic pra carregar ~/.bashrc (PATH/aliases do usuário).
    bridge.commandTemplate = QStringLiteral(
        "wsl.exe -d %1 -- bash -lic 'eval \"$(echo \"$1\" | base64 -d)\"' kai {{command_b64}}")
        .arg(m.captured(1));
    bridge.shell = core::ShellFlavor::Posix;
    bridge.usePty = true;
    bridge.isDefault = true;
    return bridge;
}

bool hasLocalKaiFile()
{
    return !findLocalKaiFile(QDir::currentPath()).isEmpty();
}

bool looksLikeLocalCliPathAttempt(const QStringList &args)
{
    if (args.size() < 2) {
        // `kai` solto: só conta num TERMINAL interativo (vira "descoberta da raiz"); sem o teto de tty,
        // abrir o app pelo launcher/ícone com cwd numa pasta com kai.yml quebraria.
        return kai::utils::stdoutIsInteractiveTerminal() && !findLocalKaiFile(QDir::currentPath()).isEmpty();
    }
    if (core::reservedCliVerbs().contains(args.at(1))) {
        return false;
    }
    return !findLocalKaiFile(QDir::currentPath()).isEmpty();
}

bool loadLocalCliTree(const QString &directoryPath, QVector<core::Folder> &folders,
                      QVector<core::Command> &commands)
{
    if (findLocalKaiFile(directoryPath).isEmpty()) {
        return false;
    }
    ui::ProjectSelector selector;
    const ui::ProjectImportResult imported = selector.importFromDirectory(directoryPath);
    if (!imported.success) {
        return false;
    }
    folders = imported.subFolders;
    folders.prepend(imported.folder);
    commands = imported.commands;
    return true;
}

LocalExecutionOutcome runLocalCliPath(const QStringList &args, const CliFlags &flags)
{
    LocalExecutionOutcome outcome;
    // Com flag (-g/-d/-n/--json...) a chamada é explícita: `kai --json | jq` lista a raiz sem terminal.
    const bool attempt = flags.any() ? hasLocalKaiFile() : looksLikeLocalCliPathAttempt(args);
    if (!attempt) {
        return outcome;
    }

    // `kai <cli_path>` comporta-se como comando de terminal comum: sem os logs internos "[Kai][...]"
    // na tela. Este modo roda antes de main.cpp chamar Logger::enableFileLogging(), então habilita aqui
    // para suprimir do terminal sem perder o registro.
    utils::Logger::enableFileLogging();
    utils::Logger::setConsoleOutputEnabled(false);

    const QString cwd = QDir::currentPath();
    outcome.handled = true;

    CliRunProfile runProfile;
    // kai.exe chamado do WSL recebe PIPES da interop, não terminal: herdá-los deixaria o lado Linux sem
    // tty (`read -p` não mostra o prompt, `bash -i` reclama de job control). Nesse caso o comando fica
    // no ConPTY e o teclado é repassado. KAI_CLI_FORWARD_INPUT força esse caminho fora do WSL (teste:
    // `printf ... | kai <path>`).
    const bool viaWslInterop = utils::launchedFromWslInterop()
        || qEnvironmentVariableIsSet("KAI_CLI_FORWARD_INPUT");
    runProfile.inheritTerminal = !viaWslInterop;
    runProfile.forwardInput = viaWslInterop;
    runProfile.flags = flags;
    QString projectPath; // vazio = o próprio cwd
#ifdef Q_OS_WIN
    if (const auto bridge = localWslBridgeProfile(cwd)) {
        runProfile.terminalProfiles = {*bridge};
        // Dentro da distro o projeto é /home/..., não o UNC: {{PROJECT_PATH}} precisa do caminho que o bash enxerga.
        projectPath = utils::toPosixPath(cwd);
    }
#endif

    ui::ProjectSelector selector;
    const ui::ProjectImportResult imported = selector.importFromDirectory(cwd, projectPath);
    if (!imported.success) {
        err() << utils::tr(QStringLiteral("cli_local.error.import_failed")).arg(imported.errorMessage) << "\n";
        err().flush();
        outcome.exitCode = 1;
        return outcome;
    }

    QVector<core::Folder> allFolders = imported.subFolders;
    allFolders.prepend(imported.folder);

    return runCliPathAgainst(allFolders, imported.commands, args.mid(1), runProfile);
}

QStringList stripLeadingCliFlags(const QStringList &args, CliFlags &flags)
{
    static const QRegularExpression shortCluster(QStringLiteral("^-[gdnw]+$"));
    QStringList rest;
    if (args.isEmpty()) {
        return rest;
    }
    rest << args.first();
    int i = 1;
    for (; i < args.size(); ++i) {
        const QString &token = args.at(i);
        if (token == QStringLiteral("--global")) {
            flags.global = true;
        } else if (token == QStringLiteral("--detached")) {
            flags.detached = true;
        } else if (token == QStringLiteral("--notify")) {
            flags.notify = true;
        } else if (token == QStringLiteral("--window")) {
            flags.window = true;
        } else if (token == QStringLiteral("--dry-run")) {
            flags.dryRun = true;
        } else if (token == QStringLiteral("--json")) {
            flags.json = true;
        } else if (shortCluster.match(token).hasMatch()) {
            flags.global = flags.global || token.contains(QLatin1Char('g'));
            flags.detached = flags.detached || token.contains(QLatin1Char('d'));
            flags.notify = flags.notify || token.contains(QLatin1Char('n'));
            flags.window = flags.window || token.contains(QLatin1Char('w'));
        } else {
            break;
        }
    }
    rest << args.mid(i);
    return rest;
}

bool looksLikeGlobalCliPathAttempt(const QStringList &args)
{
    if (args.size() < 2) {
        // Só num terminal interativo, senão quebraria o launcher/ícone.
        return kai::utils::stdoutIsInteractiveTerminal();
    }
    // 2+ args é sempre invocação explícita (um launcher não passa argumento), sem checar tty; importa
    // pras flags -g/-d, após cujo strip pode sobrar 1 argumento.
    if (core::reservedCliVerbs().contains(args.at(1))) {
        return false;
    }
    return true;
}

LocalExecutionOutcome runGlobalCliDiscover(const QStringList &args, const CliFlags &flags)
{
    LocalExecutionOutcome outcome;
    const bool reservedFirst = args.size() >= 2 && core::reservedCliVerbs().contains(args.at(1));
    const bool attempt = flags.any() ? !reservedFirst : looksLikeGlobalCliPathAttempt(args);
    if (!attempt) {
        return outcome;
    }

    // Mesma supressão de logs do modo local; idempotente.
    utils::Logger::enableFileLogging();
    utils::Logger::setConsoleOutputEnabled(false);

    core::ConfigManager configManager;
    const core::CommandsData data = configManager.loadCommands();
    const QStringList pathArgs = args.mid(1);

    // Raiz sem NENHUM cli_path no app: não é "handled" — deixa main.cpp cair no fallback (ajuda
    // genérica + instância do Kai rodando) em vez de listar vazio.
    if (pathArgs.isEmpty()) {
        core::CliPathResolver rootResolver(data.folders, data.commands);
        if (rootResolver.resolve(pathArgs).children.isEmpty()) {
            return outcome; // handled=false — main.cpp cai no fallback antigo.
        }
    }

    CliRunProfile runProfile;
    const core::SettingsData appSettings = configManager.loadSettings();
    runProfile.terminalProfiles = appSettings.terminalProfiles;
    runProfile.interpreters = appSettings.interpreters;
    runProfile.useAppRuntimeState = true;
    runProfile.delegateToApp = true;
    runProfile.flags = flags;
    return runCliPathAgainst(data.folders, data.commands, pathArgs, runProfile);
}

} // namespace kai::cli
