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

// A linha final do comando. Comandos que não cabem na linha de comando do sistema vão para um arquivo do Kai,
// gravado em segundo plano: o shell carrega o texto nativo com `. 'arquivo'` e o interpretador abre o código de
// Python/Node/PHP. Num alvo WSL o ambiente (um JWT grande basta) conta para o tamanho: se linha + `export`s não
// cabem, o arquivo leva também os `export`s e a linha vira só `. 'arquivo'`. Qualquer problema (alvo sem acesso ao
// arquivo, gravação que falha) deixa o comando como está. Os módulos em disco também são conferidos em segundo
// plano. Sem nada a gravar o `done` roda na hora.
void ExecutionPipeline::resolveCommandLine(const core::Command &command,
                                           const std::function<void(const QString &, bool)> &done)
{
    // Folga para o que applyTerminalProfile ainda soma: cd, set -m / echo $$ do kill remoto.
    constexpr int kTargetOverhead = 400;
    // Tamanho de uma linha que só carrega um arquivo (`python3 -u '/mnt/c/.../kai-<hash>.py'`).
    constexpr int kFileLineEstimate = 300;

    const QString line = commandLineFor(command, *m_envManager);
    const bool wsl = !wslDistroFor(command).isEmpty();
    const bool refreshModules = moduleFilesApply(command) && ModuleFiles::needsRefresh();

    QString script;
    QString extension;
    std::function<QString(const QString &)> lineForFile;
    if (command.language == core::CommandLanguage::Native) {
        if (spillEligible(command) && ScriptSpill::exceedsLimit(line.size(), wsl)) {
            const QString body = m_envManager->interpolate(command.command);
            script = command.kip ? kipShellPrelude() + QLatin1Char('\n') + body : body;
            extension = QStringLiteral("sh");
            lineForFile = [](const QString &path) { return ScriptSpill::sourceLine(path); };
        }
    } else if (interpreterSpillEligible(command) && ScriptSpill::exceedsLimit(line.size(), wsl)) {
        // No Windows local o cmd.exe roda a linha (aspas duplas e barras nativas); no WSL e no Unix o shell é POSIX.
#if defined(Q_OS_WIN)
        const bool posixPath = wsl;
#else
        const bool posixPath = true;
#endif
        script = buildInterpreterScript(command.language, command.command, command.kip);
        extension = interpreterScriptExtension(command.language);
        lineForFile = [language = command.language, interpreter = interpreterFor(command), posixPath](const QString &path) {
            const QString quoted = ScriptSpill::quotedPath(path, posixPath);
            return quoted.isEmpty() ? QString() : buildInterpreterFileCommandLine(language, interpreter, quoted);
        };
    }

    QString envExports;
    if (envEmbedEligible(command)) {
        envExports = posixEnvExports(processEnvFor(command));
        const int lineSize = lineForFile ? kFileLineEstimate : int(line.size());
        if (!ScriptSpill::exceedsLimit(lineSize + int(envExports.size()) + kTargetOverhead, true)) {
            envExports.clear();
        }
    }

    if (!lineForFile && envExports.isEmpty() && !refreshModules) {
        done(line, false);
        return;
    }
    struct Resolved {
        QString line;
        bool envEmbedded = false;
    };
    auto resolved = std::make_shared<Resolved>();
    resolved->line = line;
    QThread *thread = QThread::create([script, extension, lineForFile, envExports, refreshModules, resolved]() {
        if (refreshModules) {
            ModuleFiles::ensure();
        }
        if (lineForFile) {
            const QString path = ScriptSpill::write(script, QString(), extension);
            const QString fileLine = path.isEmpty() ? QString() : lineForFile(path);
            if (!fileLine.isEmpty()) {
                resolved->line = fileLine;
            }
        }
        if (!envExports.isEmpty()) {
            const QString path = ScriptSpill::write(envExports + resolved->line + QLatin1Char('\n'));
            const QString source = path.isEmpty() ? QString() : ScriptSpill::sourceLine(path);
            if (!source.isEmpty()) {
                resolved->line = source;
                resolved->envEmbedded = true;
            }
        }
    });
    connect(thread, &QThread::finished, this, [thread, resolved, done]() {
        thread->deleteLater();
        done(resolved->line, resolved->envEmbedded);
    });
    thread->start();
}

void ExecutionPipeline::setTerminalProfiles(const QVector<core::TerminalProfile> &targets)
{
    m_terminalProfiles = targets;
}

bool ExecutionPipeline::abortHttp(const QString &commandId)
{
    if (!m_activeHttpRunner || m_activeHttpCommandId != commandId) {
        return false;
    }
    m_activeHttpRunner->abort();
    return true;
}

void ExecutionPipeline::setFolders(const QVector<core::Folder> &folders)
{
    m_folders = folders;
}

QString ExecutionPipeline::wrapScriptInTarget(const core::TerminalProfile &target, const QString &script) const
{
    QString wrapped = target.commandTemplate;
    if (wrapped.contains(QStringLiteral("{{command_b64}}"))) {
        wrapped.replace(QStringLiteral("{{command_b64}}"),
                        QString::fromLatin1(script.toUtf8().toBase64()));
        return wrapped;
    }
    const int ph = wrapped.indexOf(QStringLiteral("{{command}}"));
    if (ph >= 0) {
        QString safe = script;
        safe.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        const QChar before = ph > 0 ? wrapped.at(ph - 1) : QChar();
        const int afterIdx = ph + 11;
        const QChar after = afterIdx < wrapped.size() ? wrapped.at(afterIdx) : QChar();
        const bool alreadyQuoted =
            (before == QLatin1Char('\'') && after == QLatin1Char('\'')) ||
            (before == QLatin1Char('"')  && after == QLatin1Char('"'));
        if (!alreadyQuoted) {
            safe = QStringLiteral("'") + safe + QStringLiteral("'");
        }
        wrapped.replace(QStringLiteral("{{command}}"), safe);
        return wrapped;
    }
    return QString(); // template sem placeholder: sem como injetar
}

QString ExecutionPipeline::buildRemoteKillCommandLine(const QString &targetName,
                                                      const QString &remoteRunId) const
{
    if (targetName.isEmpty() || remoteRunId.isEmpty()) {
        return QString();
    }
    for (const core::TerminalProfile &t : m_terminalProfiles) {
        if (t.name != targetName || t.commandTemplate.isEmpty()) {
            continue;
        }
        // Mata o processo remoto de forma EXAUSTIVA, em três camadas (cada uma
        // sozinha deixa fantasmas): (1) árvore de descendentes por PPID (pega
        // quem fez setsid() e escapou do grupo), (2) process group, (3) o
        // próprio PID. Em cada uma: TERM primeiro, KILL depois.
        const QString pidFile = QStringLiteral("/tmp/kai-%1.pid").arg(remoteRunId);
        const QString script = QStringLiteral(
            "P=$(cat '%1' 2>/dev/null); "
            "[ -z \"$P\" ] && exit 0; "
            "collect() { local p=$1; local kids; "
            "kids=$(ps -o pid= --ppid \"$p\" 2>/dev/null); "
            "for k in $kids; do echo \"$k\"; collect \"$k\"; done; }; "
            "TREE=$(collect \"$P\"); "
            "PG=$(ps -o pgid= -p \"$P\" 2>/dev/null | tr -d ' '); "
            // SEGURANÇA: só mata o GRUPO se o processo for o LÍDER dele (PGID ==
            // PID). `set -m` não garante isso sem terminal de controle; matar o
            // grupo do pai atingiria processos alheios (inclusive o relay do WSL).
            "[ \"$PG\" != \"$P\" ] && PG=\"\"; "
            "for t in $TREE; do kill -TERM \"$t\" 2>/dev/null; done; "
            "[ -n \"$PG\" ] && kill -TERM -\"$PG\" 2>/dev/null; "
            "kill -TERM \"$P\" 2>/dev/null; "
            "sleep 1; "
            "for t in $TREE; do kill -KILL \"$t\" 2>/dev/null; done; "
            "[ -n \"$PG\" ] && kill -KILL -\"$PG\" 2>/dev/null; "
            "kill -KILL \"$P\" 2>/dev/null; "
            "rm -f '%1' 2>/dev/null; exit 0").arg(pidFile);
        return wrapScriptInTarget(t, script);
    }
    return QString();
}

void ExecutionPipeline::applyProjectBuiltins(const core::Command &command, core::EnvironmentManager &env) const
{
    const core::ProjectRoot root = core::projectRootFor(command.folderId, m_folders);
    if (root.directory.isEmpty()) {
        env.clearBuiltinVar(QStringLiteral("PROJECT_PATH"));
        return;
    }
    QString dir = env.interpolate(root.directory);
    const QString targetName = effectiveTerminalProfileName(command);
    for (const core::TerminalProfile &target : m_terminalProfiles) {
        if (target.name == targetName && !target.commandTemplate.isEmpty()
            && effectiveShellFlavor(target) == core::ShellFlavor::Posix) {
            dir = utils::toPosixPath(dir);
            break;
        }
    }
    env.setBuiltinVar(QStringLiteral("PROJECT_PATH"), dir);
}

QString ExecutionPipeline::effectiveWorkingDir(const core::Command &command, core::EnvironmentManager &env) const
{
    applyProjectBuiltins(command, env);
    return env.interpolate(core::effectiveCommandWorkingDir(command, m_folders));
}

QString ExecutionPipeline::applyTerminalProfile(const core::Command &command,
                                               const QString &interpolatedCommand,
                                               const QString &interpolatedWorkingDir,
                                               const QString &remoteRunId, bool envEmbedded) const
{
    const QString targetName = effectiveTerminalProfileName(command);
    if (targetName.isEmpty()) {
        return interpolatedCommand;
    }
    for (const core::TerminalProfile &target : m_terminalProfiles) {
        if (target.name == targetName && !target.commandTemplate.isEmpty()) {
            QString wrapped = target.commandTemplate;
            const core::ShellFlavor flavor = effectiveShellFlavor(target);

            // Prefixo de env + working-dir na SINTAXE do sabor do alvo. O
            // prefixo de kill remoto (set -m / echo $$) só vale em Posix (WSL),
            // onde o processo Linux não é filho Windows do wsl.exe; p/ PowerShell/
            // Cmd o remoteRunId nem é gerado (ver run()).
            QString remotePrefixPosix;
            if (!remoteRunId.isEmpty() && flavor == core::ShellFlavor::Posix) {
                // `set -m` faz o shell virar LÍDER do próprio process group.
                remotePrefixPosix += QStringLiteral("set -m 2>/dev/null || true\n");
                remotePrefixPosix += QStringLiteral("echo $$ > '/tmp/kai-%1.pid' 2>/dev/null\n").arg(remoteRunId);
            }

            // Com o env já dentro do arquivo gravado por resolveCommandLine, a linha não o repete.
            const QMap<QString, QString> resolvedEnv = envEmbedded ? QMap<QString, QString>() : processEnvFor(command);
            const QString effectiveCommand = buildTargetedCommand(
                flavor, resolvedEnv, interpolatedCommand, interpolatedWorkingDir, remotePrefixPosix);

            // Injeção via base64 (o comando pode ter aspas/quebras de linha).
            if (wrapped.contains(QStringLiteral("{{command_b64}}"))) {
                const QString b64 = QString::fromLatin1(effectiveCommand.toUtf8().toBase64());
                wrapped.replace(QStringLiteral("{{command_b64}}"), b64);
            }
            // {{command}}: escapa aspas simples e ENVOLVE o comando em aspas
            // simples quando o template não o cerca; um placeholder "solto"
            // (ex.: `bash -lc {{command}}`) faria o bash tratar só a 1ª palavra
            // como script e o resto como $0,$1...
            const bool hadB64 = target.commandTemplate.contains(QStringLiteral("{{command_b64}}"));
            const int ph = wrapped.indexOf(QStringLiteral("{{command}}"));
            if (!hadB64 && ph < 0) {
                // Sem placeholder não há como injetar o comando: avisa e executa
                // LOCAL, preservando o comando do usuário.
                utils::Logger::error(kLogTag,
                    QStringLiteral("Alvo de terminal '%1' não tem {{command}} nem {{command_b64}} no template; "
                                   "executando o comando LOCALMENTE para não perdê-lo.").arg(target.name));
                return interpolatedCommand;
            }
            if (ph >= 0) {
                if (flavor == core::ShellFlavor::Posix) {
                    QString safe = effectiveCommand;
                    safe.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
                    // Já está entre aspas (simples ou duplas) no template?
                    const QChar before = ph > 0 ? wrapped.at(ph - 1) : QChar();
                    const int afterIdx = ph + 11; // len("{{command}}") == 11
                    const QChar after = afterIdx < wrapped.size() ? wrapped.at(afterIdx) : QChar();
                    const bool alreadyQuoted =
                        (before == QLatin1Char('\'') && after == QLatin1Char('\'')) ||
                        (before == QLatin1Char('"')  && after == QLatin1Char('"'));
                    if (!alreadyQuoted) {
                        safe = QStringLiteral("'") + safe + QStringLiteral("'");
                    }
                    wrapped.replace(QStringLiteral("{{command}}"), safe);
                } else {
                    // PowerShell/Cmd: sem escaping POSIX; o autor do template
                    // responde pelo quoting (o certo é {{command_b64}}).
                    wrapped.replace(QStringLiteral("{{command}}"), effectiveCommand);
                }
            }

            utils::Logger::info(kLogTag,
                QStringLiteral("Comando '%1' via alvo de terminal '%2'%3.")
                    .arg(command.id, target.name,
                         interpolatedWorkingDir.isEmpty() ? QString()
                             : QStringLiteral(" (cd %1)").arg(interpolatedWorkingDir)));
            return wrapped;
        }
    }
    // Alvo referenciado mas não encontrado: executa local, sem quebrar.
    utils::Logger::warning(kLogTag,
        QStringLiteral("Alvo de terminal '%1' do comando '%2' não encontrado; executando local.")
            .arg(command.terminalTarget, command.id));
    return interpolatedCommand;
}

} // namespace kai::engine
