#include "cli/cli-local-executor.h"

#include "core/cli-path-resolver.h"
#include "core/cli-param-binder.h"
#include "core/cli-trust-store.h"
#include "core/cli-reserved-verbs.h"
#include "core/config-manager.h"
#include "core/environment-manager.h"
#include "engine/execution-pipeline.h"
#include "ui/project-selector.h"
#include "utils/translation-manager.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>

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

} // namespace

bool looksLikeLocalCliPathAttempt(const QStringList &args)
{
    if (args.size() < 2) {
        return false;
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

    const QString cwd = QDir::currentPath();
    const QString kaiFilePath = findLocalKaiFile(cwd);
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

    core::CliPathResolver resolver(allFolders, imported.commands);
    const QStringList pathArgs = args.mid(1);
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
    for (const core::Command &c : imported.commands) {
        if (c.id == resolution.commandId) {
            command = &c;
            break;
        }
    }
    if (!command) {
        // Não deveria acontecer (resolution.commandId sempre vem de
        // imported.commands) — fallback seguro em vez de um crash por
        // ponteiro nulo mais abaixo.
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

    // --- Confiança (ver core::CliTrustStore) ---
    QFile file(kaiFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        err() << utils::tr(QStringLiteral("cli_local.error.import_failed")).arg(kaiFilePath) << "\n";
        err().flush();
        outcome.exitCode = 1;
        return outcome;
    }
    const QString rawFileText = QString::fromUtf8(file.readAll());
    file.close();
    const QString contentHash = core::CliTrustStore::hashContent(rawFileText);
    core::CliTrustStore trustStore;
    if (!trustStore.isTrusted(cwd, contentHash)) {
        out() << utils::tr(QStringLiteral("cli_local.trust.prompt")).arg(kaiFilePath) << " [y/N] ";
        out().flush();
        QTextStream in(stdin);
        const QString answer = in.readLine().trimmed().toLower();
        if (answer != QStringLiteral("y") && answer != QStringLiteral("yes")) {
            out() << utils::tr(QStringLiteral("cli_local.trust.declined")) << "\n";
            out().flush();
            outcome.exitCode = 1;
            return outcome;
        }
        trustStore.trust(cwd, contentHash);
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
    for (const core::Command &c : imported.commands) {
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

} // namespace kai::cli
