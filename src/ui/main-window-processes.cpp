
#include "core/folder-actions.h"

#include "ui/main-window-internal.h"

#include "ui/layout/layout-controller.h"

#include "engine/http-cookie-store.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include "ui/shared/lucide-icons.h"

#include "ui/features/output/run-mirror-panel.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include "ui/shared/tray-menu.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

void MainWindow::rebuildActionCommands()
{
    // Os comandos virtuais de uma ação em andamento ficam: o painel e os indicadores os consultam até ela terminar.
    for (auto it = m_commandsById.begin(); it != m_commandsById.end();) {
        if (core::isFolderActionCommandId(it.key()) && m_actionRuns.find(it.key()) == m_actionRuns.end()) {
            it = m_commandsById.erase(it);
        } else {
            ++it;
        }
    }
    QMap<QString, core::Command> real;
    for (auto it = m_commandsById.cbegin(); it != m_commandsById.cend(); ++it) {
        if (!core::isFolderActionCommandId(it.key())) {
            real.insert(it.key(), it.value());
        }
    }
    for (const core::Folder &folder : std::as_const(m_commandsData.folders)) {
        if (!folder.parentId.has_value()) {
            continue; // pasta de primeiro nível é aba: sem linha, sem ações
        }
        for (const core::FolderAction &action : core::actionsForFolder(folder, m_globalActions, real)) {
            const core::Command virtualCommand = core::makeFolderActionCommand(real.value(action.commandId), folder);
            m_commandsById[virtualCommand.id] = virtualCommand;
        }
    }
}

void MainWindow::handleFolderActionActivated(const QString &commandId, const QString &folderId)
{
    const QString actionId = core::folderActionCommandId(commandId, folderId);
    if (!m_commandsById.contains(actionId)) {
        return;
    }
    // Rodando, ou já rodou e não está carregado: carrega a saída guardada sem executar. Nunca rodou, ou já está
    // carregado (focado): executa.
    const bool running = isCommandRunning(actionId);
    const bool loaded = m_commandTree->focusedAction() == actionId;
    if (running || (!loaded && m_actionsRan.contains(actionId))) {
        reconnectTerminalToCommand(actionId);
        m_commandTree->setFocusedAction(actionId);
        return;
    }
    handleCommandActivated(actionId);
}

void MainWindow::showFolderActionMenu(const QString &commandId, const QString &folderId, const QPoint &globalPos)
{
    const QString actionId = core::folderActionCommandId(commandId, folderId);
    if (!m_commandsById.contains(actionId)) {
        return;
    }
    const bool running = isCommandRunning(actionId);
    const bool hasOutput = running || !m_commandLogs.value(actionId).isEmpty();
    const QColor accent(utils::tokens::accent());
    QMenu menu(this);
    QAction *runAction = menu.addAction(LucideIcons::icon(QStringLiteral("play"), QColor(utils::tokens::successFg()), 16),
                                        utils::tr(QStringLiteral("tree.run")));
    runAction->setEnabled(!running);
    QAction *showAction = menu.addAction(LucideIcons::icon(QStringLiteral("square-terminal"), accent, 16),
                                         utils::tr(QStringLiteral("tree.action.show_output")));
    showAction->setEnabled(hasOutput);
    QAction *stopAction = menu.addAction(LucideIcons::icon(QStringLiteral("square"), QColor(utils::tokens::warningFg()), 16),
                                         utils::tr(QStringLiteral("tree.stop")));
    stopAction->setEnabled(running);
    QAction *forceAction = menu.addAction(LucideIcons::icon(QStringLiteral("octagon-x"), QColor(utils::tokens::errorFg()), 16),
                                          utils::tr(QStringLiteral("sidebar.force_stop")));
    forceAction->setEnabled(running);
    menu.addSeparator();
    QAction *editAction = menu.addAction(LucideIcons::icon(QStringLiteral("pencil"), accent, 16),
                                         utils::tr(QStringLiteral("ctx.command.edit")));
    QAction *chosen = menu.exec(globalPos);
    if (!chosen) {
        return;
    }
    if (chosen == runAction) {
        handleCommandActivated(actionId);
    } else if (chosen == showAction) {
        reconnectTerminalToCommand(actionId);
        m_commandTree->setFocusedAction(actionId);
    } else if (chosen == stopAction) {
        handleKillCommandRequested(actionId, false);
    } else if (chosen == forceAction) {
        handleKillCommandRequested(actionId, true);
    } else if (chosen == editAction) {
        handleEditRequested(commandId, false);
    }
}

void MainWindow::startFolderAction(const core::Command &command, const QMap<QString, QString> &paramValues)
{
    const QString actionId = command.id;
    const bool isKip = command.kip && command.type == core::CommandType::Command;
    if (m_actionRuns.find(actionId) != m_actionRuns.end()) {
        reconnectTerminalToCommand(actionId);
        m_commandTree->setFocusedAction(actionId);
        return;
    }

    auto run = std::make_shared<ActionRun>();
    run->command = command;
    run->allCommands = m_commandsById;
    run->env = std::make_unique<core::EnvironmentManager>(m_envManager);
    configureEnvForRun(*run->env, command, paramValues);
    run->startedAt = QDateTime::currentDateTime();

    // Re-execução de uma ação KIP: a sessão (e o pipeline que a guarda) da vez anterior sai de cena.
    if (const auto kept = m_keptKipPipelines.find(actionId); kept != m_keptKipPipelines.end()) {
        if (kept->second) {
            kept->second->deleteLater();
        }
        m_keptKipPipelines.erase(kept);
    }
    auto *pipeline = new engine::ExecutionPipeline(this);
    run->pipeline = pipeline;
    const core::SettingsData settings = m_configManager.loadSettings();
    pipeline->setTerminalProfiles(settings.terminalProfiles);
    pipeline->setInterpreters(settings.interpreters);
    pipeline->setFolders(m_commandsData.folders);
    pipeline->setGracefulStopTimeoutMs(settings.gracefulStopTimeoutSec * 1000);

    connect(pipeline, &engine::ExecutionPipeline::logMessage, this, &MainWindow::handlePipelineLog);
    connect(pipeline, &engine::ExecutionPipeline::pipelineFinished, this,
            [this, actionId](const engine::PipelineResult &result) { handleActionFinished(actionId, result); });
    connect(pipeline, &engine::ExecutionPipeline::stageStarted, this,
            [this](const QString &, engine::PipelineStage) { updateRunningCommandStatus(); });
    // Em segundo plano o processo passa ao gerenciador, que o mantém vivo.
    connect(pipeline, &engine::ExecutionPipeline::backgroundProcessStarted, this,
            [this, pipeline](const QString &commandId, engine::ProcessRunner *) {
        std::unique_ptr<engine::ProcessRunner> owned = pipeline->releaseRunnerFor(commandId);
        if (!owned) {
            return;
        }
        m_processManager->track(commandId, std::move(owned));
        m_processListDialog->refreshProcessList();
        updateRunningCommandStatus();
        if (m_connectedTerminalCommandId == commandId) {
            m_terminalDrawer->setExecutionStatus(ExecutionStatus::Background);
        }
    });
    connectPipelineAuxSignals(pipeline);
    if (isKip) {
        // As respostas lembradas vão para o comando de verdade.
        connect(pipeline, &engine::ExecutionPipeline::kipSessionStarted, this, &MainWindow::handleKipSessionStarted);
        connect(pipeline, &engine::ExecutionPipeline::kipNotifyRequested, this, &MainWindow::handleKipNotify);
        connect(pipeline, &engine::ExecutionPipeline::kipAnswersRemembered, this, &MainWindow::handleKipAnswersRemembered);
    }

    m_actionRuns[actionId] = run;
    m_actionsRan.insert(actionId);
    m_commandTree->setActionsWithOutput(m_actionsRan);
    m_pipelineRunningIds.insert(actionId);
    m_failedCommandIds.remove(actionId);
    m_commandLogs[actionId].clear();
    m_openedLastLinkForRun.remove(actionId);
    if (livesInPanel(actionId)) {
        forEachMirror(actionId, [](RunMirrorPanel *panel) { panel->beginRun(); });
        revealCommandPanel(actionId);
    } else {
        m_connectedTerminalCommandId = actionId;
        prepareDrawerForRun(command);
    }
    m_commandTree->setFocusedAction(actionId);
    updateRunningCommandStatus();

    utils::Logger::info(kLogTag,
        QStringLiteral("Executando ação '%1' na pasta '%2'.").arg(command.name, command.folderId));
    pipeline->run(run->command, run->allCommands, *run->env);
    QTimer::singleShot(0, this, [this]() { updateRunningCommandStatus(); });
    QTimer::singleShot(150, this, [this]() { updateRunningCommandStatus(); });
}

void MainWindow::handleActionFinished(const QString &actionId, const engine::PipelineResult &result)
{
    const auto runIt = m_actionRuns.find(actionId);
    std::shared_ptr<ActionRun> run = runIt != m_actionRuns.end() ? runIt->second : nullptr;
    const auto cmdIt = m_commandsById.constFind(actionId);
    const core::Command command = run ? run->command : (cmdIt != m_commandsById.constEnd() ? cmdIt.value() : core::Command());
    const QDateTime started = run ? run->startedAt : QDateTime::currentDateTime();

    if (run) {
        // O pipeline ainda está na pilha (é ele quem emite este sinal): o lambda vazio segura o `run` (ambiente e
        // comandos que o pipeline aponta) até o próximo ciclo.
        if (command.kip && command.type == core::CommandType::Command) {
            m_keptKipPipelines[actionId] = run->pipeline; // mantém a sessão KIP e o estado final da view
        } else {
            run->pipeline->deleteLater();
        }
        m_actionRuns.erase(runIt);
        QTimer::singleShot(0, this, [run]() {});
    }
    m_pipelineRunningIds.remove(actionId);

    const bool failed = !result.success && !result.stoppedByRequest;
    if (failed) {
        m_failedCommandIds.insert(actionId);
    } else {
        m_failedCommandIds.remove(actionId);
    }
    if (m_statusLine) {
        m_statusLine->setLastResult(command.name, result.success, QDateTime::currentDateTime());
    }
    if (m_terminalDrawer->hasDetachedWindow() && m_terminalDrawer->detachedCommandId() == actionId) {
        m_terminalDrawer->setDetachedStatus(result.success ? ExecutionStatus::Success
            : result.stoppedByRequest ? ExecutionStatus::Idle : ExecutionStatus::Failed);
    }

    // O erro também vai para o log guardado: rever a saída depois mostra o motivo.
    const QString error = result.errorMessage.trimmed();
    if (!result.success && !error.isEmpty()) {
        appendToCommandLog(actionId, error + QStringLiteral("\n"));
    }
    forEachMirror(actionId, [&result, &error](RunMirrorPanel *panel) {
        if (!result.success && !error.isEmpty()) {
            panel->appendLog(error + QStringLiteral("\n"), true);
        }
    });
    setMirrorStatus(actionId, result.success ? ExecutionStatus::Success
        : result.stoppedByRequest ? ExecutionStatus::Idle : ExecutionStatus::Failed);
    if (m_connectedTerminalCommandId == actionId) {
        if (!result.success && !error.isEmpty()) {
            m_terminalDrawer->appendRawText(error + QStringLiteral("\n"), true);
        }
        m_terminalDrawer->setExecutionStatus(result.success ? ExecutionStatus::Success
            : result.stoppedByRequest ? ExecutionStatus::Idle : ExecutionStatus::Failed);
        m_terminalDrawer->setInputEnabled(false);
        m_terminalDrawer->setInteractiveAcceptingInput(false);
    }

    const auto ids = core::parseFolderActionCommandId(actionId);
    core::RunRecord rec;
    rec.commandId = ids ? ids->commandId : actionId;
    rec.commandName = command.name;
    rec.commandType = core::commandTypeToString(command.type);
    rec.startedAt = started;
    rec.durationMs = started.msecsTo(QDateTime::currentDateTime());
    rec.success = result.success;
    rec.output = m_commandLogs.value(actionId);
    m_runHistory.append(rec);

    if (failed) {
        const QString folderName = ids ? [this, &ids]() {
            for (const core::Folder &f : m_commandsData.folders) {
                if (f.id == ids->folderId) return f.name;
            }
            return ids->folderId;
        }() : QString();
        const QString label = folderName.isEmpty() ? command.name : QStringLiteral("%1 — %2").arg(command.name, folderName);
        maybeShowNotification(NotificationEvent::CommandFailure,
            utils::tr(QStringLiteral("notification.command_failure.title")),
            error.isEmpty() ? utils::tr(QStringLiteral("notification.command_failure.body_no_detail")).arg(label)
                            : utils::tr(QStringLiteral("notification.command_failure.body")).arg(label, error),
            QSystemTrayIcon::Warning, actionId);
    }
    updateRunningCommandStatus();
}

void MainWindow::handleBackgroundProcessStarted(const QString &commandId, engine::ProcessRunner *runner)
{
    Q_UNUSED(runner);

    // O ProcessManager passa a ser dono do runner, para o processo sobreviver ao pipeline. Precisa rodar
    // sincronamente no slot de backgroundProcessStarted, antes de o pipeline seguir.
    std::unique_ptr<engine::ProcessRunner> ownedRunner = m_pipeline->releaseActiveProcessRunner();
    if (!ownedRunner) {
        return;
    }

    m_processManager->track(commandId, std::move(ownedRunner));
    m_processListDialog->refreshProcessList();
    updateRunningCommandStatus();

    m_terminalDrawer->appendRawText(
        // Sem anúncio de "processo iniciado": o badge e a lista de Processos já o comunicam.
        QString(), false);

    if (m_connectedTerminalCommandId == commandId) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Background);
    }
}

void MainWindow::handleBackgroundProcessOutput(const QString &commandId, const QString &text, bool isError)
{
    m_processListDialog->appendLogFor(commandId, text, isError);
    appendToCommandLog(commandId, text);

    if (m_terminalDrawer->hasDetachedWindow()
        && m_terminalDrawer->detachedCommandId() == commandId) {
        m_terminalDrawer->appendToDetached(text, isError);
    }
    forEachMirror(commandId, [&text, isError](RunMirrorPanel *panel) { panel->appendLog(text, isError); });

    if (m_connectedTerminalCommandId == commandId) {
        m_terminalDrawer->appendRawText(text, isError);
        // Como em handlePipelineLog: só alimenta o vterm se o comando for interativo.
        const auto it = m_commandsById.constFind(commandId);
        if (it != m_commandsById.constEnd() && it->type == core::CommandType::Command
            && it->interactiveTerminal) {
            m_terminalDrawer->feedInteractive(text);
        }
    }
}

void MainWindow::handleBackgroundProcessStatusChanged(const QString &commandId, engine::ProcessStatus status)
{
    m_processListDialog->refreshProcessList();

    // Cleanup hooks de comandos em background: o pipeline retorna cedo para is_background e não acompanha o término,
    // então é aqui, com o processo já morto, que o cleanup roda.
    if (status != engine::ProcessStatus::Running) {
        const auto it = m_commandsById.constFind(commandId);
        if (it != m_commandsById.constEnd() && !it.value().hooks.cleanup.isEmpty()) {
            m_pipeline->runCleanupHooks(it.value(), m_commandsById);
        }
    }

    // Parado por pedido (Parar/Forçar/kill/reset): nem falha nem sucesso, sem marca de erro e sem notificação.
    const bool stoppedByRequest = status != engine::ProcessStatus::Running
        && m_processManager->lastRunStoppedByRequest(commandId);
    if (stoppedByRequest) {
        m_failedCommandIds.remove(commandId);
    } else if (status == engine::ProcessStatus::Error) {
        m_failedCommandIds.insert(commandId);
    } else if (status == engine::ProcessStatus::Success) {
        m_failedCommandIds.remove(commandId);
    }

    // Processo em background que caiu sozinho (ou, opcionalmente, terminou bem) sem ninguém olhando.
    if (!stoppedByRequest
        && (status == engine::ProcessStatus::Error || status == engine::ProcessStatus::Success)) {
        const auto it = m_commandsById.constFind(commandId);
        const QString name = it != m_commandsById.constEnd() ? it->name : commandId;
        if (status == engine::ProcessStatus::Error) {
            const bool crashed = m_processManager->lastRunCrashed(commandId);
            maybeShowNotification(NotificationEvent::BackgroundProcessCrash,
                utils::tr(QStringLiteral("notification.background_crash.title")),
                utils::tr(crashed ? QStringLiteral("notification.background_crash.body_crashed")
                                   : QStringLiteral("notification.background_crash.body_error")).arg(name),
                QSystemTrayIcon::Warning, commandId);
        } else {
            maybeShowNotification(NotificationEvent::BackgroundProcessSuccess,
                utils::tr(QStringLiteral("notification.background_success.title")),
                utils::tr(QStringLiteral("notification.background_success.body")).arg(name),
                QSystemTrayIcon::Information, commandId);
        }
    }

    updateRunningCommandStatus();
    setMirrorStatus(commandId, status == engine::ProcessStatus::Running ? ExecutionStatus::Background
        : status == engine::ProcessStatus::Success ? ExecutionStatus::Success : ExecutionStatus::Failed);

    if (m_connectedTerminalCommandId == commandId) {
        switch (status) {
        case engine::ProcessStatus::Running:
            m_terminalDrawer->setExecutionStatus(ExecutionStatus::Background);
            break;
        case engine::ProcessStatus::Success:
            m_terminalDrawer->setExecutionStatus(ExecutionStatus::Success);
            m_terminalDrawer->setInteractiveAcceptingInput(false);
            break;
        case engine::ProcessStatus::Error:
            m_terminalDrawer->setExecutionStatus(ExecutionStatus::Failed);
            m_terminalDrawer->setInteractiveAcceptingInput(false);
            break;
        }
    }
}

void MainWindow::handleShowProcessListRequested()
{
    // O painel pode exibir um estado antigo até o próximo sinal chegar.
    m_processListDialog->refreshProcessList();
    m_layoutController->focusView(layout::view_id::kProcesses);
}

void MainWindow::handleKillCommandRequested(const QString &commandId, bool force)
{
    // Os cleanup hooks não rodam aqui de propósito: rodariam antes de o processo morrer e de novo no abort() do
    // pipeline. A fonte única é o término real do processo (ver runCleanupHooks()).

    // Gracioso: SIGTERM, espera o tempo de Configurações -> Geral, SIGKILL. Forçado: SIGKILL direto.
    if (force) {
        m_processManager->forceStop(commandId);
    } else {
        m_processManager->stop(commandId);
    }

    // O runner é o deste comando (registry por id), que pode ser o do comando principal ou o de um hook em curso.
    if (engine::ProcessRunner *runner = runnerFor(commandId)) {
        if (force) {
            runner->forceStop();
        } else {
            runner->stop();
        }
    }
    // Uma requisição HTTP não tem processo: o Parar cancela a transferência em curso.
    {
        const auto action = m_actionRuns.find(commandId);
        engine::ExecutionPipeline *owner = action != m_actionRuns.end() ? action->second->pipeline : m_pipeline;
        owner->abortHttp(commandId);
    }
    m_pipelineRunningIds.remove(commandId);
    if (commandId == m_activePipelineCommandId) {
        m_activePipelineCommandId.clear();
    }
    m_failedCommandIds.remove(commandId);
    updateRunningCommandStatus();

    // A Saída já produzida é preservada: o log só é limpo no início da próxima execução (ver runCommandWithParams).
    if (m_connectedTerminalCommandId == commandId) {
        m_terminalDrawer->setExecutionStatus(ExecutionStatus::Idle);
        m_terminalDrawer->setInputEnabled(false);
        m_terminalDrawer->setInteractiveMode(false);
    }
}

void MainWindow::handleResetCommandRequested(const QString &commandId)
{
    // Reset é "tentar de novo com os mesmos dados": encerra o processo em background, se houver, e reexecuta sem
    // reabrir o formulário de parâmetros.
    const auto it = m_commandsById.constFind(commandId);
    if (it == m_commandsById.constEnd()) {
        utils::Logger::warning(kLogTag, QStringLiteral("Comando '%1' não encontrado para reset.").arg(commandId));
        return;
    }

    if (m_processManager->isTracked(commandId)
        && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running) {
        m_processManager->stop(commandId);
    }

    m_failedCommandIds.remove(commandId);
    updateRunningCommandStatus();

    // Sem cleanup aqui: o término do processo antigo (poll abaixo) já o dispara pela fonte única.

    // Relê o comando de m_commandsData (a fonte da verdade): m_commandsById pode estar defasado e perder os parâmetros.
    core::Command fresh = it.value();
    for (const core::Command &c : m_commandsData.commands) {
        if (c.id == commandId) { fresh = c; break; }
    }
    // Sem valores salvos, reabre o formulário: executar com o mapa vazio deixaria {{param}} literal no comando.
    if (!fresh.params.isEmpty() && fresh.lastParamValues.isEmpty()) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Reset de '%1': sem parâmetros salvos, reabrindo o formulário.").arg(fresh.name));
        handleCommandActivated(commandId);
        return;
    }

    // Espera o processo antigo morrer antes de reexecutar: duas instâncias coexistindo dão "address already in use".
    const bool stillAlive = (m_processManager->isTracked(commandId)
            && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running)
        || runnerFor(commandId) != nullptr;
    if (stillAlive) {
        auto *waitTimer = new QTimer(this);
        waitTimer->setInterval(150);
        connect(waitTimer, &QTimer::timeout, this, [this, commandId, fresh, waitTimer, tries = 0]() mutable {
            const bool alive = (m_processManager->isTracked(commandId)
                    && m_processManager->statusOf(commandId) == engine::ProcessStatus::Running)
                || runnerFor(commandId) != nullptr;
            if (!alive || ++tries > 40) { // ~6s de teto
                waitTimer->stop();
                waitTimer->deleteLater();
                runCommandWithParams(fresh, fresh.lastParamValues);
            }
        });
        waitTimer->start();
        return;
    }
    runCommandWithParams(fresh, fresh.lastParamValues);
}

} // namespace kai::ui
