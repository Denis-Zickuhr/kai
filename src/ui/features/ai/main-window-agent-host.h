#pragma once

#include <QObject>
#include <QVector>

#include "engine/ai/agent-tool-host.h"

namespace kai::ui {

class MainWindow;

// Liga o motor de IA ao app de verdade: as listas vivas, a persistência, a execução pelo pipeline normal
// (histórico, processos e painel de saída como num clique) e o buffer dos últimos logs. É a única classe
// que enxerga os dois lados; o resto do motor só conhece engine::AgentToolHost.
class MainWindowAgentHost : public QObject, public engine::AgentToolHost {
    Q_OBJECT

public:
    explicit MainWindowAgentHost(MainWindow *window);

    core::CommandsData &commandsData() override;
    QVector<core::Collection> &collections() override;
    QVector<core::Note> &notes() override;
    core::SettingsData settings() const override;
    void commandsChanged() override;
    void notesChanged() override;
    bool saveEnvironments(const QVector<core::Environment> &environments, QString &error) override;
    QVector<core::RunRecord> runHistory() const override;
    QVector<core::NotificationRecord> notifications() const override;
    QVector<utils::LogEntry> recentLogs() const override { return m_logs; }
    QJsonArray processes() override;
    QString storedOutput(const QString &commandId) const override;
    bool isRunning(const QString &commandId) const override;
    void runCommand(const QString &commandId, const QMap<QString, QString> &params,
                    std::function<void(const RunOutcome &)> done) override;
    bool killCommand(const QString &commandId, bool force, QString &error) override;
    bool openCommandInApp(const QString &commandId, QString &error) override;
    bool saveSettings(const core::SettingsData &settings, QString &error) override;
    QStringList availableThemes() const override;
    QStringList availableLanguages() const override;
    QJsonObject layoutState() const override;
    QJsonObject applyLayoutRequest(const QJsonObject &request, bool dryRun) override;

private:
    MainWindow *m_window;
    QVector<utils::LogEntry> m_logs;
};

} // namespace kai::ui
