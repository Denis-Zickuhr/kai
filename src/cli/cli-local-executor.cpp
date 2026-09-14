#include "cli/cli-local-executor.h"

#include "core/cli-path-resolver.h"
#include "core/cli-param-binder.h"
#include "core/cli-reserved-verbs.h"
#include "core/config-manager.h"
#include "core/environment-manager.h"
#include "engine/execution-pipeline.h"
#include "ui/features/collections/project-selector.h"
#include "utils/translation-manager.h"
#include "utils/logger.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace kai::cli {

namespace {

// Mesmo helper de ipc/cli-client.cpp, duplicado de propósito (kai-cli não
// depende de kai-ipc, e vice-versa — são dois módulos de CLI conceitualmente
// separados; 5 linhas não justificam acoplar os dois só por isto). true só
// quando a saída padrão é um TERMINAL interativo de verdade — é o que
// distingue "rodei `kai` solto no terminal dentro da pasta do projeto" de
// "o launcher/ícone abriu o app com o cwd apontando pra essa pasta por
// acaso" (esse segundo caso TEM que continuar abrindo a GUI normalmente).
bool stdoutIsInteractiveTerminal()
{
    // KAI_FORCE_GUI: ver o mesmo comentário/bug em ipc/cli-client.cpp —
    // relançada automática do docker/watch.sh (via entr) roda `./bin/kai`
    // sob um pty de verdade, então precisa desta válvula de escape pra não
    // cair aqui também.
    if (qEnvironmentVariableIsSet("KAI_FORCE_GUI")) {
        return false;
    }
#ifdef Q_OS_WIN
    // Ver o mesmo comentário detalhado em ipc/cli-client.cpp: main.cpp
    // sempre TENTA AttachConsole (mesmo na invocação solta); sem console
    // pai de verdade a tentativa falha e fd fica < 0 — nunca trata como
    // terminal interativo nesse caso. Com console real atrás (inclusive
    // via WSL interop chamando o kai.exe do Windows), o fd é válido.
    const int fd = _fileno(stdout);
    if (fd < 0) {
        return false;
    }
    return _isatty(fd) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

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

// Encontra um kai.json/kai.yml/kai.yaml no diretório atual — mesma ordem
// de prioridade do ProjectSelector::importFromDirectory (kai.json primeiro).
QString findLocalKaiFile(const QString &directoryPath)
{
    for (const char *name : {"kai.json", "kai.yml", "kai.yaml"}) {
        const QString candidate = QDir(directoryPath).filePath(QString::fromLatin1(name));
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

void printChildren(const QVector<core::CliPathChildEntry> &children)
{
    for (const core::CliPathChildEntry &c : children) {
        out() << "  " << c.cliPath;
        if (!c.label.isEmpty()) {
            out() << "\t" << c.label;
        }
        out() << "\n";
        if (!c.description.isEmpty()) {
            out() << "      " << c.description << "\n";
        }
    }
    out().flush();
}

void printCommandHelp(const core::Command &command, const QStringList &resolvedPath)
{
    QStringList requiredNames, optionalUsage;
    for (const core::Parameter &p : command.params) {
        if (p.optional) {
            optionalUsage << QStringLiteral("[--%1=<value>]").arg(p.name);
        } else {
            requiredNames << QStringLiteral("<%1>").arg(p.name);
        }
    }
    out() << "kai " << resolvedPath.join(QLatin1Char(' '));
    if (!requiredNames.isEmpty()) {
        out() << ' ' << requiredNames.join(QLatin1Char(' '));
    }
    if (!optionalUsage.isEmpty()) {
        out() << ' ' << optionalUsage.join(QLatin1Char(' '));
    }
    out() << "\n\n";
    if (!command.description.isEmpty()) {
        out() << command.description << "\n\n";
    }
    for (const core::Parameter &p : command.params) {
        out() << "  " << (p.optional ? QStringLiteral("--%1").arg(p.name) : p.name)
              << "\t" << (p.optional
                    ? utils::tr(QStringLiteral("cli_local.help.optional"))
                    : utils::tr(QStringLiteral("cli_local.help.required")))
              << "\t" << core::parameterTypeToString(p.type) << "\n";
        if (!p.description.isEmpty()) {
            out() << "      " << p.description << "\n";
        }
    }
    out().flush();
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

// Núcleo compartilhado entre modo LOCAL (runLocalCliPath) e GLOBAL
// (runGlobalCliDiscover) — a ÚNICA diferença entre os dois é de ONDE
// `allFolders`/`commands` vêm (kai.json/kai.yml local vs commands.json
// persistido do app); resolução de path, binding de parâmetros e execução
// do pipeline são idênticos dali em diante.
LocalExecutionOutcome runCliPathAgainst(const QVector<core::Folder> &allFolders,
                                          const QVector<core::Command> &commands,
                                          const QStringList &pathArgs)
{
    LocalExecutionOutcome outcome;
    outcome.handled = true;

    core::CliPathResolver resolver(allFolders, commands);
    const core::CliPathResolution resolution = resolver.resolve(pathArgs);

    if (resolution.kind == core::CliPathResolution::Kind::NotFound) {
        err() << utils::tr(QStringLiteral("cli_local.error.path_not_found")).arg(pathArgs.join(QLatin1Char(' '))) << "\n";
        err().flush();
        printChildren(resolution.children);
        outcome.exitCode = 2;
        return outcome;
    }
    if (resolution.kind == core::CliPathResolution::Kind::Folder) {
        // Caminho incompleto (ou vazio) — descoberta automática, não é erro.
        printChildren(resolution.children);
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

    const core::CliParamBindingResult bound = core::bindCliParams(command->params, resolution.remainingArgs);
    const QStringList resolvedPathTokens = pathArgs.mid(0, pathArgs.size() - resolution.remainingArgs.size());
    if (bound.helpRequested) {
        printCommandHelp(*command, resolvedPathTokens);
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

    QMap<QString, core::Command> allCommandsById;
    for (const core::Command &c : commands) {
        allCommandsById.insert(c.id, c);
    }

    engine::ExecutionPipeline pipeline;
    pipeline.setFolders(allFolders);

    QObject::connect(&pipeline, &engine::ExecutionPipeline::logMessage, &pipeline,
        [](const QString &, const QString &text, bool isError) {
            (isError ? err() : out()) << text << "\n";
            (isError ? err() : out()).flush();
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

    pipeline.run(*command, allCommandsById, envManager);
    loop.exec();

    outcome.exitCode = result.success ? 0 : 1;
    return outcome;
}

} // namespace

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
        // com kai.json/kai.yml.
        return stdoutIsInteractiveTerminal() && !findLocalKaiFile(QDir::currentPath()).isEmpty();
    }
    if (core::reservedCliVerbs().contains(args.at(1))) {
        return false;
    }
    return !findLocalKaiFile(QDir::currentPath()).isEmpty();
}

LocalExecutionOutcome runLocalCliPath(const QStringList &args)
{
    LocalExecutionOutcome outcome;
    if (!looksLikeLocalCliPathAttempt(args)) {
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

    ui::ProjectSelector selector;
    const ui::ProjectImportResult imported = selector.importFromDirectory(cwd);
    if (!imported.success) {
        err() << utils::tr(QStringLiteral("cli_local.error.import_failed")).arg(imported.errorMessage) << "\n";
        err().flush();
        outcome.exitCode = 1;
        return outcome;
    }

    QVector<core::Folder> allFolders = imported.subFolders;
    allFolders.prepend(imported.folder);

    return runCliPathAgainst(allFolders, imported.commands, args.mid(1));
}

bool looksLikeGlobalCliPathAttempt(const QStringList &args)
{
    if (args.size() < 2) {
        // Bare (mesmo raciocínio de looksLikeLocalCliPathAttempt): só
        // conta num terminal interativo de verdade, senão quebraria o
        // launcher/ícone.
        return stdoutIsInteractiveTerminal();
    }
    // 2+ args é sempre uma invocação de CLI explícita e intencional (um
    // launcher nunca passa argumento nenhum) — não precisa checar tty
    // aqui, mesmo padrão do modo local. Isto importa pro prefixo
    // `kai global <path>` (ver main.cpp): depois de tirar o "global", o
    // que sobra pode ter só 1 argumento (ex: "testglobal"), e essa
    // invocação EXPLÍCITA não deveria depender de tty.
    if (core::reservedCliVerbs().contains(args.at(1))) {
        return false;
    }
    return true;
}

LocalExecutionOutcome runGlobalCliDiscover(const QStringList &args)
{
    LocalExecutionOutcome outcome;
    if (!looksLikeGlobalCliPathAttempt(args)) {
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

    return runCliPathAgainst(data.folders, data.commands, pathArgs);
}

} // namespace kai::cli
