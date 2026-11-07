#include "ui/main-window.h"

#include "engine/pipeline-step-executor.h"
#include "ui/features/output/run-mirror-panel.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

bool MainWindow::isRunbookRunning(const QString &commandId) const
{
    const auto it = m_runbooks.find(commandId);
    return it != m_runbooks.end() && it->second && !it->second->isFinished();
}

engine::KipSession *MainWindow::runbookSessionFor(const QString &commandId) const
{
    const auto it = m_runbooks.find(commandId);
    if (it == m_runbooks.end() || !it->second) {
        return nullptr;
    }
    // Com um passo KIP em andamento, quem tem a tela é o filho: religar a view (foco, troca de comando, reabrir) na
    // sessão da pipeline escondia o formulário e o passo ficava esperando uma resposta sem ninguém ver.
    if (engine::KipSession *child = it->second->childSession()) {
        return child;
    }
    return it->second->session();
}

// Uma pipeline (spec 13) roda no RunbookRunner e se mostra como uma sessão KIP: o runner dirige uma KipSession sem
// processo, e a view da KIP (na Saída ou no painel do comando) a renderiza. Cada passo roda pelo ExecutionPipeline
// normal dentro de um PipelineStepExecutor, sem painel de saída próprio.
void MainWindow::startPipelineRun(const core::Command &command, const QMap<QString, QString> &paramValues)
{
    if (isRunbookRunning(command.id)) {
        m_runbooks[command.id]->stop(); // re-execução: o anterior termina antes (como o Parar)
    }
    m_failedCommandIds.remove(command.id);
    m_commandLogs[command.id].clear();
    m_runStartedAt = QDateTime::currentDateTime();

    const core::SettingsData settings = m_configManager.loadSettings();
    auto factory = [this, settings](QObject *parent) -> engine::StepExecutor * {
        return new engine::PipelineStepExecutor(&m_commandsById, [this, settings](engine::ExecutionPipeline &pipeline) {
            pipeline.setTerminalProfiles(settings.terminalProfiles);
            pipeline.setInterpreters(settings.interpreters);
            pipeline.setFolders(m_commandsData.folders);
            pipeline.setGracefulStopTimeoutMs(settings.gracefulStopTimeoutSec * 1000);
        }, parent);
    };
    auto owned = std::make_unique<engine::RunbookRunner>(factory, this);
    engine::RunbookRunner *runner = owned.get();
    if (const auto old = m_runbooks.find(command.id); old != m_runbooks.end() && old->second) {
        old->second.release()->deleteLater();
    }
    m_runbooks[command.id] = std::move(owned);

    const QString commandId = command.id;
    connect(runner, &engine::RunbookRunner::logLine, this,
            [this, commandId](const QString &text, bool) { appendToCommandLog(commandId, text); });
    connect(runner, &engine::RunbookRunner::finished, this,
            [this, commandId](const engine::RunbookResult &result) { handleRunbookFinished(commandId, result); });

    const bool inPanel = livesInPanel(commandId);
    if (inPanel) {
        forEachMirror(commandId, [](RunMirrorPanel *panel) { panel->beginRun(); });
        revealCommandPanel(commandId);
    } else {
        m_connectedTerminalCommandId = commandId;
        prepareDrawerForRun(command);
    }
    m_pipelineRunningIds.insert(commandId);
    m_commandTree->resetRunTimer(commandId);
    updateRunningCommandStatus();

    utils::Logger::info(kLogTag, QStringLiteral("Executando pipeline '%1'.").arg(command.name));
    core::EnvironmentManager env = m_envManager;
    configureEnvForRun(env, command, paramValues);
    runner->start(command, m_commandsById, m_commandsData.folders, env, paramValues,
                  m_pendingRunbookOptions.take(command.id));
    // A sessão nasce no construtor do runner (só uma "rodar daqui" posterior cria outra e a anuncia).
    handleKipSessionStarted(commandId, runner->session());
    connect(runner, &engine::RunbookRunner::sessionStarted, this,
            [this, commandId](engine::KipSession *session) { handleKipSessionStarted(commandId, session); });
    // Um passo KIP: a view deixa o checklist e mostra a tela do filho (prompts, confirmações) até ele terminar; aí
    // volta para a sessão da pipeline. Sem isso o passo ficava "rodando" esperando uma resposta que ninguém via.
    connect(runner, &engine::RunbookRunner::childSessionStarted, this,
            [this, commandId](const QString &, engine::KipSession *child) { bindPipelineView(commandId, child, true); });
    connect(runner, &engine::RunbookRunner::childSessionEnded, this,
            [this, commandId, runner](const QString &) { bindPipelineView(commandId, runner->session(), false); });
}

void MainWindow::bindPipelineView(const QString &commandId, engine::KipSession *session, bool takeFocus)
{
    if (!livesInPanel(commandId)) {
        m_terminalDrawer->bindKipSession(commandId, session);
    }
    forEachMirror(commandId, [session, takeFocus](RunMirrorPanel *panel) { panel->bindKipSession(session, takeFocus); });
}

void MainWindow::handleRunbookFinished(const QString &commandId, const engine::RunbookResult &result)
{
    using Outcome = engine::RunbookResult::Outcome;
    const bool success = result.outcome == Outcome::Success;
    const bool cancelled = result.outcome == Outcome::Cancelled;
    const ExecutionStatus status = success ? ExecutionStatus::Success
        : cancelled ? ExecutionStatus::Idle : ExecutionStatus::Failed;

    m_pipelineRunningIds.remove(commandId);
    if (success || cancelled) {
        m_failedCommandIds.remove(commandId);
    } else {
        m_failedCommandIds.insert(commandId);
    }
    if (m_terminalDrawer->hasDetachedWindow() && m_terminalDrawer->detachedCommandId() == commandId) {
        m_terminalDrawer->setDetachedStatus(status);
    }
    if (m_connectedTerminalCommandId == commandId) {
        m_terminalDrawer->setExecutionStatus(status);
    }
    setMirrorStatus(commandId, status);

    const auto it = m_commandsById.constFind(commandId);
    const QString name = it != m_commandsById.constEnd() ? it->name : commandId;
    if (m_statusLine && !cancelled) {
        m_statusLine->setLastResult(name, success, QDateTime::currentDateTime());
    }
    if (it != m_commandsById.constEnd()) {
        core::RunRecord rec;
        rec.commandId = it->id;
        rec.commandName = it->name;
        rec.commandType = core::commandTypeToString(it->type);
        rec.startedAt = m_runStartedAt.isValid() ? m_runStartedAt : QDateTime::currentDateTime();
        rec.durationMs = rec.startedAt.msecsTo(QDateTime::currentDateTime());
        rec.success = success;
        rec.output = m_commandLogs.value(commandId);
        m_runHistory.append(rec);
    }
    if (!success && !cancelled && !isKipViewVisible(commandId)) {
        const QString detail = result.message.trimmed();
        maybeShowNotification(NotificationEvent::CommandFailure,
            utils::tr(QStringLiteral("notification.command_failure.title")),
            detail.isEmpty()
                ? utils::tr(QStringLiteral("notification.command_failure.body_no_detail")).arg(name)
                : utils::tr(QStringLiteral("notification.command_failure.body")).arg(name, detail),
            QSystemTrayIcon::Warning, commandId);
    }
    updateRunningCommandStatus();
}

} // namespace kai::ui
