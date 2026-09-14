#include "cli/cli-local-executor.h"
#include "cli/terminal-mode-filter.h"
#include "cli/app-delegate.h"
#include "cli/cli-help-format.h"
#include "cli/terminal-input.h"

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
    static QTextStream s(stdout);
    return s;
}
QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

// Encontra o kai.yml do diretório atual (o único formato de arquivo de projeto).
QString findLocalKaiFile(const QString &directoryPath)
{
    const QString candidate = QDir(directoryPath).filePath(QStringLiteral("kai.yml"));
    return QFile::exists(candidate) ? candidate : QString();
}

// Cadeia raiz -> pasta do comando (mesma lógica de MainWindow::
// runSelectedCommand, extraída aqui porque o modo local não passa pelo
// MainWindow) — usada tanto pro merge de env_vars quanto pro escopo de
// dinâmicas.
QVector<core::Folder> folderChainFor(const QString &folderId, const QVector<core::Folder> &allFolders)
{
    QMap<QString, core::Folder> byId;
    for (const core::Folder &f : allFolders) {
        byId.insert(f.id, f);
    }
    QVector<core::Folder> chain;
    QString current = folderId;
    QSet<QString> seen;
    while (!current.isEmpty() && byId.contains(current) && !seen.contains(current)) {
        seen.insert(current);
        const core::Folder &f = byId.value(current);
        chain.prepend(f); // raiz primeiro
        current = f.parentId.value_or(QString());
    }
    return chain;
}

// O que muda na EXECUÇÃO entre os modos (resolução de path e binding de
// parâmetros são idênticos):
//   - LOCAL: só o arquivo manda — nenhum alvo de terminal do app (exceto a
//     ponte pro WSL de origem, ver localWslBridgeProfile), nada de
//     dinâmicas persistidas.
//   - GLOBAL: roda como a GUI rodaria (bug real: `kai -g` ignorava o alvo
//     de terminal padrão e caía no cmd.exe — "'docker' não é reconhecido").
struct CliRunProfile {
    QVector<core::TerminalProfile> terminalProfiles;
    core::InterpreterSettings interpreters;
    bool useAppRuntimeState = false; // dinâmicas persistidas + graceful stop
    bool inheritTerminal = false;    // ver ExecutionPipeline::setInheritTerminal
    bool delegateToApp = false;      // GLOBAL: roda pelo app (ver runViaApp)
    // PTY + repasse do teclado (em vez de herdar o terminal): kai.exe local
    // chamado do WSL — ver runLocalCliPath.
    bool forwardInput = false;
    CliFlags flags; // -d/-n/--dry-run/--json (ver cli-local-executor.h)
};

// Enquanto o comando herda o terminal, o Ctrl+C vai pro grupo de processos
// inteiro (kai + comando). O kai precisa SOBREVIVER a ele pra esperar o
// comando terminar e devolver o código de saída dele — igual a um shell.
// Handler no-op (e não SIG_IGN/ignorar no Windows): disposição "ignorada"
// seria herdada pelo filho, que aí também não pararia com Ctrl+C.
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

// Núcleo compartilhado entre modo LOCAL (runLocalCliPath) e GLOBAL
// (runGlobalCliDiscover) — a diferença entre os dois é de ONDE
// `allFolders`/`commands` vêm (kai.yml local vs commands.json
// persistido do app) e o CliRunProfile acima.
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

// -d/--detached no modo local: relança este kai (mesmo binário, mesmos
// argumentos, sem o -d) em segundo plano. KAI_CLI_OUTPUT_FILE faz o filho
// mandar stdout/stderr pro log (ver utils::initConsoleForCli) — mais
// simples e seguro que herdar handles: no Windows, herdar handles levaria
// junto os pipes da interop do WSL e seguraria o terminal até o filho sair.
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

    // Caminho até a pasta que está sendo listada (as linhas de uso de cada
    // comando mostram o caminho completo): o maior prefixo de `pathArgs` que
    // ainda resolve pra uma pasta — o resolvedor não diz quantos tokens
    // consumiu quando para numa pasta ou não acha o próximo.
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

    // Kind::Command
    const core::Command *command = nullptr;
    for (const core::Command &c : commands) {
        if (c.id == resolution.commandId) {
            command = &c;
            break;
        }
    }
    if (!command) {
        // Não deveria acontecer (resolution.commandId sempre vem de
        // `commands`) — fallback seguro em vez de um crash por ponteiro
        // nulo mais abaixo.
        err() << utils::tr(QStringLiteral("cli_local.error.import_failed")).arg(QString()) << "\n";
        err().flush();
        outcome.exitCode = 1;
        return outcome;
    }

    // "Executar no diretório de invocação": o comando roda onde o usuário
    // digitou o `kai`, não no working dir dele. Cópia, pra não mexer no
    // `commands` carregado. No modo GLOBAL (app) o cliente manda o mesmo
    // diretório pelo IPC (ver runViaApp); aqui cobre o dry-run e o fallback.
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

    // KIP precisa do app (a view, o stdin/stdout do protocolo): o modo LOCAL — que
    // roda o comando com o terminal herdado — recusa com um caminho claro (§15).
    if (command->kip && command->type == core::CommandType::Command && !runProfile.delegateToApp) {
        err() << utils::tr(QStringLiteral("kip.error.needs_app")) << "\n";
        err().flush();
        outcome.exitCode = 1;
        return outcome;
    }

    // --- Ambiente: variáveis GLOBAIS (Configurações > Ambientes, o mesmo
    // ambiente ATIVO que a GUI usa — lido direto do settings.json persistido
    // pelo ConfigManager; não precisa de QApplication/GUI, só QCoreApplication
    // já é suficiente porque ConfigManager não depende de Qt Widgets) +
    // env_vars da cadeia de pastas (raiz -> pasta do comando, filho
    // sobrescreve pai) + valores de parâmetro ligados (default pros não
    // informados). Ainda SEM persistência de dinâmicas entre chamadas
    // separadas (ver conversa de design — limitação conhecida do v1: cada
    // invocação local é isolada nesse aspecto específico). ---
    core::ConfigManager configManager;
    const core::SettingsData settings = configManager.loadSettings();
    QMap<QString, QString> globalVars;
    for (const core::Environment &e : settings.environments) {
        if (e.id == settings.activeEnvironmentId) {
            globalVars = e.vars;
            break;
        }
    }

    const QVector<core::Folder> chain = folderChainFor(command->folderId, allFolders);
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
        // --dry-run: o que SERIA executado, sem executar nada.
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
                // O que roda é o código, sem {{VAR}} nem aspas: mostra-o como
                // está (a linha real é o código embrulhado em base64).
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

    // -w/--window: a janela de saída é do app, então só há como abri-la
    // quando o comando roda POR ele (caminhos globais, -g).
    if (runProfile.flags.window && !runProfile.delegateToApp) {
        err() << utils::tr(QStringLiteral("cli.error.window_requires_global")) << "\n";
        err().flush();
        outcome.exitCode = 2;
        return outcome;
    }

    if (runProfile.flags.detached) {
        // GLOBAL: o app aceita, registra e roda; o terminal volta na hora (e
        // o próprio app avisa no fim, com -n).
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

    // -n/--notify: avisa na bandeja quando terminar, com o tempo que levou.
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
        // Não deu pra falar com o app nem subi-lo: roda aqui mesmo (sem
        // registro), interativo, em vez de simplesmente falhar.
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
        // Mesma gravação write-through da GUI (MainWindow) pra EnvExtractor
        // marcado persist — senão um login via `kai -g` se perdia.
        QObject::connect(&pipeline, &engine::ExecutionPipeline::dynamicVarPersistRequested, &pipeline,
            [&configManager](const QString &scopeKey, const QString &name, const QString &value) {
                QMap<QString, QMap<QString, QString>> all = configManager.loadPersistedDynamicVars();
                all[scopeKey][name] = value;
                configManager.savePersistedDynamicVars(all);
            });
    }

    // Saída do comando vai pro terminal REAL de quem chamou — passa pelo
    // filtro de modos de terminal (ver TerminalModeFilter: o ConPTY do
    // Windows emite ESC[?9001h e deixava o terminal do WSL cuspindo lixo a
    // cada tecla depois que o kai saía). Um filtro por stream, porque as
    // sequências podem vir partidas entre chunks do mesmo stream.
    TerminalModeFilter outFilter;
    TerminalModeFilter errFilter;
    QString pendingEcho;
    bool stripEcho = false;
    std::optional<StdinForwarder> stdinForwarder;
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
    }
    QObject::connect(&pipeline, &engine::ExecutionPipeline::logMessage, &pipeline,
        [&outFilter, &errFilter, &pendingEcho, &stripEcho, interactive = stdinForwarder.has_value()](
            const QString &, const QString &chunk, bool isError) {
            const QString text = stripEcho ? stripPendingEcho(chunk, pendingEcho) : chunk;
            QTextStream &stream = isError ? err() : out();
            stream << (isError ? errFilter : outFilter).feed(text);
            // Chunks de saída de shell já trazem a própria quebra de linha;
            // só mensagens avulsas (ex: avisos do HttpRunner) precisam dela.
            // Com repasse de teclado, nunca: um prompt ("[y/N] ") termina
            // sem quebra de propósito.
            if (!interactive && !text.endsWith(QLatin1Char('\n'))) {
                stream << "\n";
            }
            stream.flush();
        });

    // Comando em SEGUNDO PLANO: o pipeline considera sucesso imediato ao
    // disparar (não espera terminar) e exige que quem recebe o sinal tome
    // posse do ProcessRunner SÍNCRONA e IMEDIATAMENTE (ver doc do sinal em
    // ExecutionPipeline). Aqui, deliberadamente "vazamos" o runner (nunca
    // deletado) — o processo real (QProcess/OS) continua vivo depois que
    // este processo `kai` sai, já que nunca chamamos kill()/o destruidor
    // dele. LIMITAÇÃO CONHECIDA (v1): esse processo detached NÃO aparece
    // em `kai ps` ainda — a unificação do registro local de processos é
    // trabalho futuro, não deste corte.
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
    // Nome SEM aspas: o wsl.exe lê a própria linha de comando e não remove
    // aspas — `-d "Ubuntu"` dá WSL_E_DISTRO_NOT_FOUND (testado de verdade).
    // Por isso só aceita nomes que não precisam de quoting.
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
        // `kai` solto (zero argumentos): TAMBÉM conta como tentativa de CLI
        // Path local, mas só num TERMINAL interativo de verdade — vira
        // "descoberta da raiz" (mesmo Kind::Folder com pathArgs vazio que
        // já resolvia `kai <pasta incompleta>`, ver runLocalCliPath).
        // Pedido do usuário: rodar `kai` solto dentro da pasta do projeto
        // não listava os cli_paths de lá ("kai kai" — a piada dele mesmo).
        // Sem o teto de tty, isto quebraria abrir o app pelo
        // launcher/ícone quando o cwd dele por acaso aponta pra uma pasta
        // com kai.yml.
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
    // Com flag (-g/-d/-n/--json...) a chamada já é explicitamente de CLI:
    // `kai --json | jq` lista a raiz mesmo sem terminal.
    const bool attempt = flags.any() ? hasLocalKaiFile() : looksLikeLocalCliPathAttempt(args);
    if (!attempt) {
        return outcome;
    }

    // `kai <cli_path>` é pra se comportar como um comando de terminal
    // comum: só a saída do PRÓPRIO comando do usuário na tela, sem os logs
    // internos "[Kai][...]" (ProjectSelector/ExecutionPipeline/
    // ProcessRunner/etc.) — pedido do usuário, com exemplo real de "kai
    // ping" poluído por esses logs antes do "pong". Este modo roda ANTES
    // de main.cpp chamar Logger::enableFileLogging() (ver comentário lá:
    // roda sob QCoreApplication, nunca chega no fluxo de GUI onde isso é
    // ligado) — habilita aqui também, pra suprimir do terminal sem perder
    // o registro pra depuração depois.
    utils::Logger::enableFileLogging();
    utils::Logger::setConsoleOutputEnabled(false);

    const QString cwd = QDir::currentPath();
    outcome.handled = true; // a partir daqui SEMPRE tratamos, mesmo em erro.

    CliRunProfile runProfile;
    // kai.exe chamado do WSL recebe PIPES da interop, não um terminal: herdar
    // isso deixaria o lado Linux sem tty — `read -p` nem mostra o prompt e
    // `bash -i` reclama de job control. Nesse caso o comando fica no ConPTY
    // (o Linux enxerga um tty) e o teclado é repassado. Em qualquer outro
    // lugar (Linux nativo, cmd/PowerShell), herda o terminal de verdade.
    // KAI_CLI_FORWARD_INPUT: força esse caminho fora do WSL (diagnóstico e
    // teste — reproduz os pipes da interop com um `printf ... | kai <path>`).
    const bool viaWslInterop = utils::launchedFromWslInterop()
        || qEnvironmentVariableIsSet("KAI_CLI_FORWARD_INPUT");
    runProfile.inheritTerminal = !viaWslInterop;
    runProfile.forwardInput = viaWslInterop;
    runProfile.flags = flags;
    QString projectPath; // vazio = o próprio cwd
#ifdef Q_OS_WIN
    if (const auto bridge = localWslBridgeProfile(cwd)) {
        runProfile.terminalProfiles = {*bridge};
        // Dentro da distro o projeto é /home/..., não o UNC do Windows —
        // {{PROJECT_PATH}} precisa apontar pro caminho que o bash enxerga.
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
        // Bare (mesmo raciocínio de looksLikeLocalCliPathAttempt): só
        // conta num terminal interativo de verdade, senão quebraria o
        // launcher/ícone.
        return kai::utils::stdoutIsInteractiveTerminal();
    }
    // 2+ args é sempre uma invocação de CLI explícita e intencional (um
    // launcher nunca passa argumento nenhum) — não precisa checar tty
    // aqui, mesmo padrão do modo local. Isto importa pras flags `-g`/`-d`
    // (ver stripLeadingCliFlags): depois de tirá-las, o que sobra pode ter
    // só 1 argumento, e essa invocação EXPLÍCITA não depende de tty.
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

    // Mesma supressão de logs internos do modo local (ver comentário em
    // runLocalCliPath) — idempotente, sem problema se já foi chamado antes
    // (main.cpp garante que só um dos dois modos roda por invocação).
    utils::Logger::enableFileLogging();
    utils::Logger::setConsoleOutputEnabled(false);

    core::ConfigManager configManager;
    const core::CommandsData data = configManager.loadCommands();
    const QStringList pathArgs = args.mid(1);

    // Descoberta da RAIZ (pathArgs vazio) sem NENHUM cli_path configurado
    // em lugar nenhum do app: não faz sentido "handled" aqui — mais útil
    // deixar main.cpp cair no fallback de sempre (ajuda genérica + tenta
    // falar com uma instância do Kai rodando) do que mostrar uma lista
    // vazia sem explicação nenhuma. Resolve uma vez só pra checar isso
    // ANTES de decidir — barato (só olha folders/commands em memória).
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
