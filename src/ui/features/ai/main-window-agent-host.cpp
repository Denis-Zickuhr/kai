#include "ui/features/ai/main-window-agent-host.h"

#include "ui/external-run-session.h"
#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/layout-ops.h"
#include "ui/layout/view-registry.h"
#include "ui/main-window.h"
#include "utils/translation-manager.h"

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
    m_window->showAndRaise("AI: open command");
    m_window->handleCommandActivated(commandId);
    return true;
}

bool MainWindowAgentHost::saveSettings(const core::SettingsData &settings, QString &error)
{
    return m_window->applySettingsFromAgent(settings, error);
}

QStringList MainWindowAgentHost::availableThemes() const { return m_window->availableThemeNames(); }
QStringList MainWindowAgentHost::bundledThemes() const { return m_window->bundledThemeNames(); }
QJsonObject MainWindowAgentHost::themeDocument(const QString &name) const { return m_window->themeDocument(name); }
bool MainWindowAgentHost::saveTheme(const QString &name, const QJsonObject &doc, QString &error)
{
    return m_window->saveThemeFromAgent(name, doc, error);
}
QStringList MainWindowAgentHost::availableLanguages() const { return utils::TranslationManager::instance().availableLanguages(); }

QJsonObject MainWindowAgentHost::layoutState() const
{
    if (!m_window->m_dock || !m_window->m_viewRegistry) {
        return {};
    }
    const layout::LayoutModel &model = m_window->m_dock->model();
    const layout::ViewRegistry *registry = m_window->m_viewRegistry;
    QStringList ids = registry->staticIds();
    for (const QString &placed : model.placedViews()) {
        if (!ids.contains(placed)) ids << placed;
    }
    QJsonArray views;
    for (const QString &id : ids) {
        QString where = QStringLiteral("closed");
        if (const auto zone = model.zoneOf(id)) where = layout::zoneName(*zone);
        else if (model.isFloating(id)) where = QStringLiteral("floating");
        views.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("title"), registry->title(id)},
                                 {QStringLiteral("at"), where}, {QStringLiteral("closable"), registry->closable(id)}});
    }
    QJsonObject out{{QStringLiteral("layout"), model.toJson()},
                    {QStringLiteral("views"), views},
                    {QStringLiteral("presets"), QJsonArray::fromStringList(layout::layoutPresetNames())}};
    if (m_window->m_layoutController) {
        out[QStringLiteral("perspectives")] = QJsonArray::fromStringList(m_window->m_layoutController->savedPerspectiveNames());
    }
    return out;
}

QJsonObject MainWindowAgentHost::applyLayoutRequest(const QJsonObject &request, bool dryRun)
{
    auto refuse = [](const QString &why) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), why}};
    };
    if (!m_window->m_dock || !m_window->m_viewRegistry) {
        return refuse(QStringLiteral("The layout is not available."));
    }
    layout::ViewRegistry *registry = m_window->m_viewRegistry;
    const layout::LayoutModel current = m_window->m_dock->model();
    layout::LayoutModel target;
    if (request.contains(QStringLiteral("preset"))) {
        const auto preset = layout::LayoutModel::presetFromName(request.value(QStringLiteral("preset")).toString());
        if (!preset) {
            return refuse(QStringLiteral("Unknown preset. Known: %1.").arg(layout::layoutPresetNames().join(QStringLiteral(", "))));
        }
        target = layout::LayoutModel::preset(*preset);
    } else if (request.contains(QStringLiteral("perspective"))) {
        const QString name = request.value(QStringLiteral("perspective")).toString();
        if (!m_window->m_layoutController || !m_window->m_layoutController->savedPerspectives().contains(name)) {
            return refuse(QStringLiteral("No saved perspective named '%1'.").arg(name));
        }
        target = m_window->m_layoutController->savedPerspectives().value(name);
    } else {
        layout::LayoutOpsContext context;
        context.isKnown = registry->validator();
        context.closable = [registry](const QString &id) { return registry->closable(id); };
        context.home = [registry](const QString &id) { return registry->defaultHome(id); };
        const layout::LayoutOpsResult result = layout::applyLayoutOps(current, request.value(QStringLiteral("ops")).toArray(), context);
        if (!result.ok) {
            return refuse(result.error);
        }
        target = result.model;
    }
    QJsonArray changes;
    for (const QString &line : layout::describeLayoutChanges(current, target)) changes.append(line);
    if (!dryRun && target != current) {
        m_window->m_dock->applyLayout(target);
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("changes"), changes}};
}

} // namespace kai::ui
