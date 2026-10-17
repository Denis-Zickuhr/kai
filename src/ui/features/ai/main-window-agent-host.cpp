#include "ui/features/ai/main-window-agent-host.h"

#include "ui/external-run-session.h"
#include "ui/main-window.h"

#include <memory>

namespace kai::ui {

namespace {
constexpr int kMaxLogEntries = 500;
constexpr int kMaxCapturedOutput = 200 * 1024;
} // namespace

MainWindowAgentHost::MainWindowAgentHost(MainWindow *window)
    : QObject(window)
    , m_window(window)
{
    qRegisterMetaType<kai::utils::LogEntry>();
    connect(&utils::LoggerSink::instance(), &utils::LoggerSink::logEmitted, this, [this](const utils::LogEntry &entry) {
        m_logs.append(entry);
        if (m_logs.size() > kMaxLogEntries) {
            m_logs.remove(0, m_logs.size() - kMaxLogEntries);
        }
    });
}

core::CommandsData &MainWindowAgentHost::commandsData() { return m_window->m_commandsData; }
QVector<core::Collection> &MainWindowAgentHost::collections() { return m_window->m_collections; }
QVector<core::Note> &MainWindowAgentHost::notes() { return m_window->m_notes; }
core::SettingsData MainWindowAgentHost::settings() const { return m_window->m_configManager.loadSettings(); }

void MainWindowAgentHost::commandsChanged()
{
    m_window->persistCommands();
}

void MainWindowAgentHost::notesChanged()
{
    m_window->persistNotes();
}

bool MainWindowAgentHost::saveEnvironments(const QVector<core::Environment> &environments, QString &error)
{
    core::SettingsData s = m_window->m_configManager.loadSettings();
    s.environments = environments;
    if (!m_window->m_configManager.saveSettings(s)) {
        error = QStringLiteral("The settings could not be saved.");
        return false;
    }
    m_window->applyActiveEnvironment();
    return true;
}

QVector<core::RunRecord> MainWindowAgentHost::runHistory() const { return m_window->m_runHistory.load(); }
QVector<core::NotificationRecord> MainWindowAgentHost::notifications() const { return m_window->m_notificationHistory.load(); }
QJsonArray MainWindowAgentHost::processes() { return m_window->runningProcessItems(); }
QString MainWindowAgentHost::storedOutput(const QString &commandId) const { return m_window->storedOutputFor(commandId); }
bool MainWindowAgentHost::isRunning(const QString &commandId) const { return m_window->isCommandRunning(commandId); }

void MainWindowAgentHost::runCommand(const QString &commandId, const QMap<QString, QString> &params,
                                     std::function<void(const RunOutcome &)> done)
{
    QString error;
    ExternalRunSession *session = m_window->startExternalRun(commandId, params, error);
    if (!session) {
        RunOutcome o;
        o.error = error;
        done(o);
        return;
    }
    auto output = std::make_shared<QString>();
    auto background = std::make_shared<qint64>(0);
    connect(session, &ExternalRunSession::output, this, [output](const QString &text, bool) {
        if (output->size() < kMaxCapturedOutput) output->append(text);
    });
    connect(session, &ExternalRunSession::background, this, [background](qint64 pid) { *background = pid; });
    connect(session, &ExternalRunSession::finished, this, [output, background, done](int exitCode, const QString &message) {
        RunOutcome o;
        o.started = true;
        o.finished = true;
        o.exitCode = exitCode;
        o.success = exitCode == 0;
        o.output = *output;
        if (*background > 0) {
            o.output += QStringLiteral("\n[running in the background, pid %1]").arg(*background);
        }
        o.error = message;
        done(o);
    });
}

bool MainWindowAgentHost::killCommand(const QString &commandId, bool force, QString &error)
{
    if (!m_window->isCommandRunning(commandId)) {
        error = QStringLiteral("That command is not running.");
        return false;
    }
    m_window->handleKillCommandRequested(commandId, force);
    return true;
}

bool MainWindowAgentHost::openCommandInApp(const QString &commandId, QString &error)
{
    if (!m_window->m_commandsById.contains(commandId)) {
        error = QStringLiteral("That command does not exist.");
        return false;
    }
    m_window->showAndRaise();
    m_window->handleCommandActivated(commandId);
    return true;
}

} // namespace kai::ui
