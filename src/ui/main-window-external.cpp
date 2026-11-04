#include "core/project-format.h"
#include "core/folder-actions.h"
#include "core/working-dir.h"
#include "ui/features/docs/document-panel.h"
#include "ui/features/docs/note-dialog.h"
#include "ui/main-window.h"

#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/panel-card.h"
#include "ui/layout/layout-store.h"
#include "ui/layout/view-registry.h"
#include "ui/features/ai/ai-chat-window.h"
#include "ui/features/ai/main-window-agent-host.h"
#include "ui/features/ai/mcp-bridge.h"
#include "core/folder-nesting.h"
#include "core/project-tree-scan.h"

#include "core/http-collection-import.h"
#include "core/http-resolve.h"
#include "engine/http-cookie-store.h"
#include "utils/path-format.h"
#include "utils/duration-format.h"
#include "ui/external-run-session.h"
#include "ui/shared/notification-gate.h"
#include "ui/shared/native-window-corners.h"
#include "ui/shared/native-window-snap.h"
#include "ui/shared/expand-collapse-bar.h"
#include "ui/shared/panel-metrics.h"
#include "ui/app-stylesheet.h"
#include "ui/shared/dialog-utils.h"
#include "utils/action-shortcuts.h"
#include "ui/features/collections/export-dialog.h"
#include "ui/features/collections/import-dialog.h"
#include "ui/features/collections/import-selection-dialog.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QClipboard>
#include <QScopeGuard>
#include <QShortcut>
#include <QWidget>
#include <QCloseEvent>
#include <QShowEvent>
#include <QTimer>
#include <QSet>
#include <QScreen>
#include <QCursor>
#include <QToolTip>
#include <QGuiApplication>
#include <QMenu>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <memory>
#include "ui/features/docs/doc-links.h"
#include "ui/shared/lucide-icons.h"
#include <QAction>
#include <QMessageBox>
#include <QFileDialog>
#include <QSaveFile>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QPixmap>
#include <QInputDialog>
#include <QUuid>
#include <QCoreApplication>
#include <QWindow>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRegion>
#include <QStyle>
#include "qhotkey.h"

#include "ui/features/command-editor/parameter-form-dialog.h"
#include "ui/features/docs/doc-session-store.h"
#include "ui/features/docs/doc-viewer.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "ui/features/collections/folder-delete-dialog.h"
#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/collections/collection-editor-dialog.h"
#include "ui/shared/name-uniqueness.h"
#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/loading-overlay.h"
#include "ui/features/command-editor/command-json-editor-dialog.h"
#include "ui/features/settings/settings-dialog.h"
#include "ui/features/environments/environment-manager-dialog.h"
#include "ui/features/environments/dynamic-vars-inspector-widget.h"
#include "ui/features/history/run-history-dialog.h"
#include "ui/features/history/notification-history-dialog.h"
#include "engine/module-files.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/output/run-mirror-panel.h"
#include "ui/features/output/quick-run-popup.h"
#include "engine/script-spill.h"
#include "ui/shared/help-dialog.h"
#include "ui/shared/manifesto-dialog.h"
#include "core/openapi-parser.h"
#include "core/yaml-bridge.h"
#include "ui/features/http/quick-http-edit-dialog.h"
#include "ui/shared/welcome-screen.h"
#include "utils/asset-paths.h"
#include "utils/logger.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "utils/autostart-manager.h"

#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <functional>
#include "ui/shared/tray-menu.h"
#include "ui/shared/window-translucency.h"
#include "ui/shared/file-dialogs.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "MainWindow";
}

void MainWindow::handleKipSessionStarted(const QString &commandId, engine::KipSession *session)
{
    // Com painel próprio, a sessão é só do painel: a Saída padrão nem a recebe.
    if (!livesInPanel(commandId)) {
        m_terminalDrawer->bindKipSession(commandId, session);
    }
    const bool takeFocus = !m_kipAutoStarted.contains(commandId);
    forEachMirror(commandId, [session, takeFocus](RunMirrorPanel *panel) { panel->bindKipSession(session, takeFocus); });
    const auto it = m_commandsById.constFind(commandId);
    const bool wantsWindow = (it != m_commandsById.constEnd() && it->kipOpenInWindow)
        || m_kipOpenWindowFor.contains(commandId);
    m_kipOpenWindowFor.remove(commandId);
    // `kip_window` / `-w`: a janela própria copia o estado da view embutida, por isso o bind vem antes.
    if (wantsWindow && m_connectedTerminalCommandId == commandId && !m_terminalDrawer->hasDetachedWindow()) {
        m_terminalDrawer->showDetachedOutput();
    }

    // `kip_auto_close`: só fecha após sucesso (falha/cancelamento deixam a janela aberta, onde está o erro). A geração
    // evita fechar a janela de uma re-execução que começou durante a espera.
    const int generation = ++m_kipAutoCloseGeneration[commandId];
    if (it != m_commandsById.constEnd() && it->kipAutoCloseWindow && session) {
        const int delayMs = qBound(0, it->kipAutoCloseDelaySec, 60) * 1000;
        connect(session, &engine::KipSession::finished, this,
            [this, commandId, generation, delayMs](const engine::KipOutcome &outcome) {
                if (!outcome.success) {
                    return;
                }
                QTimer::singleShot(delayMs, this, [this, commandId, generation]() {
                    if (m_kipAutoCloseGeneration.value(commandId) == generation) {
                        m_terminalDrawer->closeDetachedWindowFor(commandId);
                    }
                });
            });
    }
}

void MainWindow::handleKipNotify(const QString &commandId, const QString &title, const QString &text,
                                 core::KipLevel level)
{
    const auto it = m_commandsById.constFind(commandId);
    const QString commandName = it != m_commandsById.constEnd() ? it->name : commandId;
    const QString shownTitle = title.isEmpty() ? commandName : title;
    QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
    if (level == core::KipLevel::Warning) icon = QSystemTrayIcon::Warning;
    if (level == core::KipLevel::Error) icon = QSystemTrayIcon::Critical;
    maybeShowNotification(NotificationEvent::KipNotify, shownTitle, text, icon, commandId,
                          QStringLiteral("kip_notify_%1").arg(core::kipLevelToString(level)));
}

void MainWindow::handleKipAnswersRemembered(const QString &commandId, const QJsonObject &remembered)
{
    // Como lastParamValues: grava no comando (fonte da verdade em m_commandsData) para pré-preencher a próxima execução.
    // Numa ação de pasta, o comando de verdade é o da ação.
    const auto actionIds = core::parseFolderActionCommandId(commandId);
    const QString persistedId = actionIds ? actionIds->commandId : commandId;
    for (core::Command &c : m_commandsData.commands) {
        if (c.id != persistedId) {
            continue;
        }
        bool changed = false;
        for (auto vit = remembered.constBegin(); vit != remembered.constEnd(); ++vit) {
            if (c.kipLastValues.value(vit.key()) != vit.value()) {
                c.kipLastValues[vit.key()] = vit.value();
                changed = true;
            }
        }
        if (changed) {
            m_configManager.saveCommands(m_commandsData);
            m_commandsById[c.id] = c;
            if (actionIds) {
                rebuildActionCommands(); // os comandos virtuais levam as respostas lembradas
            }
        }
        return;
    }
}

ExternalRunSession *MainWindow::startExternalRun(const QString &commandId,
                                                 const QMap<QString, QString> &paramValues,
                                                 QString &errorMessage, bool openOutputWindow,
                                                 const QString &invocationDir)
{
    const auto commandIt = m_commandsById.constFind(commandId);
    if (commandIt == m_commandsById.constEnd()) {
        errorMessage = utils::tr(QStringLiteral("cli.error.command_not_found")).arg(commandId);
        return nullptr;
    }
    core::Command command = commandIt.value();
    if (command.cliWorkingDir == core::CliWorkingDir::Invocation && !invocationDir.isEmpty()) {
        command.workingDirMode = core::WorkingDirMode::Custom;
        command.workingDir = invocationDir;
    }
    if (isCommandRunning(command.id)) {
        errorMessage = utils::tr(QStringLiteral("cli.stream.already_running")).arg(command.name);
        return nullptr;
    }

    auto *session = new ExternalRunSession(command.id, [this, id = command.id](const QString &text) {
        engine::ProcessRunner *runner = runnerFor(id);
        if (!runner) {
            runner = m_processManager->runnerFor(id);
        }
        if (runner && runner->isRunning()) {
            runner->writeRaw(text);
        }
    }, this);

    // KIP via `kai -g`: roda como um clique e traz o app à frente, salvo se a view abre na janela própria
    // (`-w` ou `kip_window`): é ela que o usuário vê.
    if (command.kip && command.type == core::CommandType::Command) {
        if (!openOutputWindow && !command.kipOpenInWindow) {
            showAndRaise("kai -g: KIP");
        }
        if (openOutputWindow) {
            m_kipOpenWindowFor.insert(command.id);
        }
    }

    // Os hooks logam com o próprio id.
    QSet<QString> chainIds{command.id};
    for (const QString &hookId : command.hooks.pre) {
        chainIds.insert(hookId);
    }
    for (const QString &hookId : command.hooks.post) {
        chainIds.insert(hookId);
    }
    connect(m_pipeline, &engine::ExecutionPipeline::logMessage, session,
        [session, chainIds](const QString &cid, const QString &text, bool isError) {
            if (chainIds.contains(cid)) {
                session->publishOutput(text, isError);
            }
        });
    // Precisa vir depois da conexão do construtor a handleBackgroundProcessStarted: o runner já está no ProcessManager.
    connect(m_pipeline, &engine::ExecutionPipeline::backgroundProcessStarted, session,
        [this, session, id = command.id](const QString &cid, engine::ProcessRunner *) {
            if (cid != id) {
                return;
            }
            engine::ProcessRunner *runner = m_processManager->runnerFor(id);
            session->publishBackground(runner ? runner->processId() : 0);
            session->publishFinished(0, QString());
        });
    connect(m_pipeline, &engine::ExecutionPipeline::pipelineFinished, session,
        [session, id = command.id](const engine::PipelineResult &result) {
            if (result.mainCommandId == id) {
                session->publishFinished(result.success ? 0 : qMax(1, result.exitCode), result.errorMessage);
            }
        });

    utils::Logger::info(kLogTag,
        QStringLiteral("Executando '%1' a pedido do CLI (sessão externa).").arg(command.name));
    // Adiado: quem chamou ainda vai conectar os sinais da sessão, e um pipeline que termina de forma síncrona
    // (ex: condição de execução não satisfeita) emitiria `finished` no vazio.
    QMetaObject::invokeMethod(this, [this, command, paramValues, openOutputWindow]() {
        runCommandWithParams(command, paramValues);
        // A janela copia o estado da Saída embutida, que o run acima acabou de conectar.
        if (openOutputWindow && m_connectedTerminalCommandId == command.id) {
            m_terminalDrawer->showDetachedOutput();
        }
    }, Qt::QueuedConnection);
    return session;
}

bool MainWindow::runCommandByName(const QString &name, QString &message)
{
    const QString target = name.trimmed().toLower();
    QVector<const core::Command *> matches;
    for (const core::Command &c : m_commandsData.commands) {
        if (c.name.trimmed().toLower() == target) {
            matches.append(&c);
        }
    }
    if (matches.isEmpty()) {
        message = utils::tr(QStringLiteral("cli.error.command_not_found")).arg(name);
        return false;
    }
    if (matches.size() > 1) {
        message = utils::tr(QStringLiteral("cli.error.ambiguous_command")).arg(name).arg(matches.size());
        return false;
    }
    const QString id = matches.first()->id;
    // A janela vem à frente para o usuário preencher parâmetros, exceto num KIP que abre na janela própria.
    // Um comando oculto sem parâmetros roda em segundo plano: nem traz a janela nem mexe na Saída.
    const bool quiet = matches.first()->hidden && !matches.first()->kip && matches.first()->params.isEmpty();
    if (!quiet && !(matches.first()->kip && matches.first()->kipOpenInWindow)) {
        showAndRaise("CLI: run");
    }
    if (quiet) {
        runInBackground(id);
    } else {
        handleCommandActivated(id);
    }
    message = utils::tr(QStringLiteral("cli.command.triggered")).arg(matches.first()->name);
    return true;
}

QStringList MainWindow::commandNames() const
{
    QStringList names;
    for (const core::Command &c : m_commandsData.commands) {
        names << c.name;
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool MainWindow::activateEnvironmentByName(const QString &name, QString &message)
{
    const core::SettingsData settings = m_configManager.loadSettings();
    const QString target = name.trimmed().toLower();
    for (const core::Environment &e : settings.environments) {
        if (e.name.trimmed().toLower() == target) {
            handleEnvironmentSelected(e.id);
            message = utils::tr(QStringLiteral("cli.environment.activated")).arg(e.name);
            return true;
        }
    }
    message = utils::tr(QStringLiteral("cli.error.environment_not_found")).arg(name);
    return false;
}

QStringList MainWindow::environmentNames(QString &activeName)
{
    const core::SettingsData settings = m_configManager.loadSettings();
    QStringList names;
    for (const core::Environment &e : settings.environments) {
        names << e.name;
        if (e.id == settings.activeEnvironmentId) {
            activeName = e.name;
        }
    }
    return names;
}

void MainWindow::armVisibilityGuard()
{
    m_visibilityGuardArmed = true;
}

void MainWindow::setVisible(bool visible)
{
    // App de bandeja: a janela principal só reaparece por ação explícita do usuário (showAndRaise / toggleVisibility).
    // Um show*() incidental ou evento do SO (troca de monitor, tampa do notebook) é descartado; quem já está visível
    // (minimizar/restaurar) fica de fora.
    if (visible && m_visibilityGuardArmed && m_explicitShowDepth == 0 && !isVisible()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Tentativa de mostrar a janela principal sem ação do usuário; ignorada."));
        return;
    }
    // Só uma ocultação explícita (hide()/X/atalho) arma o backstop do evento Show: minimizar e restaurar também geram
    // Show espontâneo.
    if (!visible) {
        m_hiddenExplicitly = true;
    } else if (m_visibilityGuardArmed) {
        m_hiddenExplicitly = false;
    }
    QMainWindow::setVisible(visible);
    // As janelas flutuantes dos painéis acompanham a visibilidade do Kai.
    if (m_dock) {
        m_dock->setFloatingVisible(isVisible());
    }
}

void MainWindow::showAndRaise(const char *reason)
{
    utils::Logger::info("Focus", QStringLiteral("Janela principal trazida à frente: %1").arg(QLatin1String(reason)));
    ++m_explicitShowDepth;
    show();
    --m_explicitShowDepth;
    raise();
    activateWindow();
#if defined(Q_OS_WIN)
    forceForegroundWindow(this);
#endif
}

QStringList MainWindow::runningProcessLines()
{
    QStringList lines;
    auto nameOf = [this](const QString &id) -> QString {
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == id) return c.name;
        }
        return id;
    };
    for (const QString &id : m_processManager->trackedCommandIds()) {
        const engine::ProcessStatus st = m_processManager->statusOf(id);
        QString status = (st == engine::ProcessStatus::Running) ? utils::tr(QStringLiteral("cli.status.running"))
                        : (st == engine::ProcessStatus::Success) ? utils::tr(QStringLiteral("cli.status.success"))
                        : utils::tr(QStringLiteral("cli.status.error"));
        qint64 pid = 0;
        if (auto *r = m_processManager->runnerFor(id)) {
            pid = r->processId();
        }
        lines << utils::tr(QStringLiteral("cli.ps.line")).arg(nameOf(id)).arg(pid).arg(status);
    }
    // Foreground: todos os runners vivos do registry, não só o ativo.
    const QSet<QString> already(m_processManager->trackedCommandIds().cbegin(),
                                m_processManager->trackedCommandIds().cend());
    for (const QString &id : allPipelineRunningCommandIds()) {
        if (already.contains(id)) {
            continue;
        }
        qint64 pid = 0;
        if (auto *r = runnerFor(id)) {
            pid = r->processId();
        }
        lines << utils::tr(QStringLiteral("cli.ps.line")).arg(nameOf(id)).arg(pid).arg(utils::tr(QStringLiteral("cli.status.running")));
    }
    return lines;
}

bool MainWindow::attachProcessByName(const QString &arg, QString &message)
{
    QString cid, cname, why;
    if (!resolveProcessTarget(arg, cid, cname, why)) {
        message = why;
        return false;
    }
    showAndRaise("CLI: attach");
    reconnectTerminalToCommand(cid);
    message = utils::tr(QStringLiteral("cli.attach.done")).arg(cname);
    return true;
}

bool MainWindow::killProcessByName(const QString &arg, QString &message)
{
    // Encerra sempre pelo commandId: assim o kill do grupo de processos (terminate, timeout, kill em -PGID) não deixa
    // filhos órfãos como um kill por PID cru deixaria.
    QString cid, cname, why;
    if (!resolveProcessTarget(arg, cid, cname, why)) {
        message = why;
        return false;
    }
    handleKillCommandRequested(cid);
    message = utils::tr(QStringLiteral("cli.kill.done")).arg(cname);
    return true;
}

bool MainWindow::resolveProcessTarget(const QString &arg, QString &commandId,
                                      QString &commandName, QString &error)
{
    struct Proc { QString id; QString name; qint64 pid; };
    QVector<Proc> procs;
    auto nameOf = [this](const QString &id) -> QString {
        for (const core::Command &c : m_commandsData.commands) {
            if (c.id == id) return c.name;
        }
        return id;
    };
    for (const QString &id : m_processManager->trackedCommandIds()) {
        if (m_processManager->statusOf(id) != engine::ProcessStatus::Running) {
            continue;
        }
        qint64 pid = 0;
        if (auto *r = m_processManager->runnerFor(id)) {
            pid = r->processId();
        }
        procs.append({id, nameOf(id), pid});
    }
    // Foreground: todos os runners vivos do registry, não só o ativo.
    for (const QString &id : allPipelineRunningCommandIds()) {
        bool dup = false;
        for (const Proc &p : procs) {
            if (p.id == id) { dup = true; break; }
        }
        if (dup) {
            continue;
        }
        qint64 pid = 0;
        if (auto *r = runnerFor(id)) {
            pid = r->processId();
        }
        procs.append({id, nameOf(id), pid});
    }

    if (procs.isEmpty()) {
        error = utils::tr(QStringLiteral("cli.error.no_process"));
        return false;
    }

    bool isPid = false;
    const qint64 wantedPid = arg.trimmed().toLongLong(&isPid);
    if (isPid) {
        for (const Proc &p : procs) {
            if (p.pid == wantedPid) {
                commandId = p.id; commandName = p.name; return true;
            }
        }
        error = utils::tr(QStringLiteral("cli.error.no_process_pid")).arg(wantedPid);
        return false;
    }

    const QString target = arg.trimmed().toLower();
    for (const Proc &p : procs) {
        if (p.name.trimmed().toLower() == target) {
            commandId = p.id; commandName = p.name; return true;
        }
    }
    error = utils::tr(QStringLiteral("cli.error.no_process_name")).arg(arg);
    return false;
}

void MainWindow::handleRunHistoryRequested()
{
    m_layoutController->focusView(layout::view_id::kHistory);
}

void MainWindow::handleNotificationHistoryRequested()
{
    m_layoutController->focusView(layout::view_id::kNotifications);
}

void MainWindow::handleLogsRequested()
{
    m_layoutController->focusView(layout::view_id::kLogs);
}

void MainWindow::handleHelpRequested()
{
    HelpDialog dialog(this);
    dialog.exec();
}

} // namespace kai::ui
