#include "engine/execution-pipeline.h"
#include "core/terminal-target.h"
#include "engine/command-language.h"
#include "engine/module-files.h"

#include "utils/logger.h"
#include "utils/translation-manager.h"

#include "utils/path-format.h"
#include "engine/execution-pipeline-helpers.h"

namespace kai::engine {

using namespace detail;

namespace {
constexpr const char *kLogTag = "ExecutionPipeline";
}

QString ExecutionPipeline::wrapForEnvCapture(const QString &interpolatedCommand, core::ShellFlavor flavor) const
{
    // Roda o comando e, no mesmo shell, emite o sentinela e o ambiente completo
    // (uma variável por linha) na SINTAXE do sabor que vai executar: `env`/aspas
    // simples POSIX não funcionam em cmd.exe nem PowerShell.
    const QString sentinel = QString::fromLatin1(kEnvSentinel);
    if (flavor == core::ShellFlavor::PowerShell) {
        // Um objeto por variável, formatado como NAME=VALUE (o que ingestCapturedEnv espera).
        return QStringLiteral(
            "%1\nWrite-Output '%2'\nGet-ChildItem Env: | ForEach-Object { \"$($_.Name)=$($_.Value)\" }")
            .arg(interpolatedCommand, sentinel);
    }
    if (flavor == core::ShellFlavor::Cmd) {
        // `set` sem argumento já dumpa NAME=VALUE; sentinela sem aspas (literais no cmd).
        return QStringLiteral("%1\necho %2\nset").arg(interpolatedCommand, sentinel);
    }
    return QStringLiteral("%1\necho '%2'\nenv").arg(interpolatedCommand, sentinel);
}

void ExecutionPipeline::ingestCapturedEnv(const QString &rawOutput, const QString &scopeKey,
                                           const QVector<core::DeclaredEnvVar> &declaredVars)
{
    if (!m_envManager) {
        return;
    }
    // LISTA BRANCA (segurança): só os nomes DECLARADOS no comando
    // (Command::declaredEnvVars) são considerados; capturar tudo que difere do
    // ambiente herdado vazava variáveis de sistema/distro/WSL. Sem nomes
    // declarados não há o que procurar.
    if (declaredVars.isEmpty()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Captura de env: 'Exportar variáveis' ligado mas nenhuma variável declarada; nada capturado."));
        return;
    }

    const int sentinelPos = rawOutput.indexOf(QString::fromLatin1(kEnvSentinel));
    if (sentinelPos < 0) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Captura de env: sentinela não encontrado na saída do hook; nada capturado."));
        return;
    }

    const QString envDump = rawOutput.mid(sentinelPos + static_cast<int>(qstrlen(kEnvSentinel)));
    // Uma variável por linha (KEY=VALUE); remove \r de terminais que convertem quebras.
    const QStringList lines = envDump.split(QChar(u'\n'), Qt::SkipEmptyParts);

    QMap<QString, QString> foundValues; // nome -> valor, só os que realmente apareceram no dump
    for (const QString &rawLine : lines) {
        QString line = rawLine;
        if (line.endsWith(QChar(u'\r'))) {
            line.chop(1);
        }
        const int eq = line.indexOf(QChar(u'='));
        if (eq <= 0) {
            continue;
        }
        foundValues.insert(line.left(eq), line.mid(eq + 1));
    }

    int captured = 0;
    for (const core::DeclaredEnvVar &declared : declaredVars) {
        const QString name = declared.name.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        // Declarada mas não setada: vira variável dinâmica VAZIA, p/ o inspetor
        // mostrar que era esperada.
        exportDeclaredVar(declared, scopeKey, foundValues.value(name));
        ++captured;
    }

    utils::Logger::info(kLogTag,
        QStringLiteral("Captura de env do hook: %1 variável(is) declarada(s) processada(s).").arg(captured));
}

void ExecutionPipeline::exportDeclaredVar(const core::DeclaredEnvVar &declared, const QString &scopeKey,
                                          const QString &value)
{
    // ESCOPO por variável declarada (como EnvExtractor::scope): "global" força
    // Global; "project" usa o escopo capturado no INÍCIO da execução (trocar de
    // projeto durante a execução jogaria a variável no projeto ERRADO).
    const QString name = declared.name.trimmed();
    const QString targetScope = declared.scope == QStringLiteral("global") ? QString() : scopeKey;
    m_envManager->setDynamicVarInScope(targetScope, name, value);
    if (declared.persist) {
        emit dynamicVarPersistRequested(targetScope, name, value);
    }
}

QMap<QString, QString> ExecutionPipeline::processEnvFor(const core::Command &command) const
{
    QMap<QString, QString> env = languageEnvDefaults(command.language);
    if (command.kip) {
        env.insert(QStringLiteral("KIP_VERSION"), QString::number(core::kKipProtocolVersion));
        env.insert(QStringLiteral("KIP_LOCALE"), utils::TranslationManager::instance().currentLanguage());
    }
    const QMap<QString, QString> resolved = m_envManager->resolvedEnv();
    for (auto it = resolved.constBegin(); it != resolved.constEnd(); ++it) {
        env.insert(it.key(), it.value()); // o env do usuário sobrepõe os padrões do KIP
    }
    if (moduleFilesApply(command)) {
        // A pasta do Kai entra NA FRENTE do que o usuário (ou o ambiente do Kai) já tem nessas variáveis.
        QMap<QString, QString> current;
        for (const QString &name : {QStringLiteral("PYTHONPATH"), QStringLiteral("NODE_PATH"), QStringLiteral("PHP_INI_SCAN_DIR")}) {
            current.insert(name, resolved.contains(name) ? resolved.value(name) : qEnvironmentVariable(name.toUtf8().constData()));
        }
        const QMap<QString, QString> modules = ModuleFiles::environment(command.kip, current);
        for (auto it = modules.constBegin(); it != modules.constEnd(); ++it) {
            env.insert(it.key(), it.value());
        }
    }
    return env;
}

KipSession *ExecutionPipeline::kipSessionFor(const QString &commandId) const
{
    return m_kipSessions.value(commandId).data();
}

KipSession *ExecutionPipeline::createKipSession(const core::Command &command, ProcessRunner *runner,
                                                const QString &captureScopeKey)
{
    // Re-execução: a sessão antiga sai de cena; deleteLater porque a view pode
    // estar no meio de um sinal dela.
    if (KipSession *old = m_kipSessions.value(command.id).data()) {
        old->disconnect(this);
        old->deleteLater();
    }
    auto *session = new KipSession(runner, this);
    const core::KipSettings &prefs = core::kipSettings();
    session->setHandshakeTimeoutMs(prefs.handshakeTimeoutSec * 1000);
    session->setPatchTimeoutMs(prefs.changeTimeoutSec * 1000);
    session->setCancelGraceMs(prefs.cancelGraceSec * 1000);
    session->setDeclaredEnvVars(command.declaredEnvVars);
    session->setRememberedValues(prefs.rememberAnswers ? command.kipLastValues : QJsonObject());
    session->setIgnoreExitCode(command.ignoreExitCode);
    session->setWslDistro(wslDistroFor(command));
    m_kipSessions.insert(command.id, session);

    const QString id = command.id;
    // `set_env` só chega p/ nomes declarados (a sessão filtra); o escopo é o
    // capturado no início da execução, nunca relido.
    connect(session, &KipSession::setEnvRequested, this,
        [this, declared = command.declaredEnvVars, captureScopeKey](const QString &name, const QString &value) {
            for (const core::DeclaredEnvVar &d : declared) {
                if (d.name.trimmed() == name) {
                    exportDeclaredVar(d, captureScopeKey, value);
                    return;
                }
            }
        });
    connect(session, &KipSession::logLine, this,
        [this, id](const QString &text, bool isError) { emit logMessage(id, text, isError); });
    connect(session, &KipSession::notifyRequested, this,
        [this, id](const QString &title, const QString &text, core::KipLevel level) {
            emit kipNotifyRequested(id, title, text, level);
        });
    connect(session, &KipSession::answersRemembered, this, [this, id](const QJsonObject &remembered) {
        if (core::kipSettings().rememberAnswers) {
            emit kipAnswersRemembered(id, remembered);
        }
    });
    emit kipSessionStarted(id, session);
    return session;
}

QString ExecutionPipeline::wslDistroFor(const core::Command &command) const
{
    const QString name = effectiveTerminalProfileName(command);
    if (name.isEmpty()) {
        return QString();
    }
    for (const core::TerminalProfile &t : m_terminalProfiles) {
        if (t.name == name) {
            return utils::wslDistroFromTemplate(t.commandTemplate, utils::defaultWslDistro());
        }
    }
    return QString();
}

QString ExecutionPipeline::effectiveTerminalProfileName(const core::Command &command) const
{
    return core::resolveTerminalProfileName(command.terminalTarget, command.folderId, m_folders, m_terminalProfiles);
}

} // namespace kai::engine
