#pragma once

// AgentToolHost com dados em memória, para testar as ferramentas do agente sem a janela principal.

#include "engine/ai/agent-tool-host.h"

class FakeAgentHost : public kai::engine::AgentToolHost {
public:
    kai::core::CommandsData data;
    QVector<kai::core::Collection> collectionList;
    QVector<kai::core::Note> noteList;
    kai::core::SettingsData settingsData;
    QVector<kai::core::RunRecord> runs;
    QVector<kai::core::NotificationRecord> notifs;
    QVector<kai::utils::LogEntry> logs;
    QJsonArray processList;
    QMap<QString, QString> outputs;
    QSet<QString> running;

    int commandsChangedCount = 0;
    int notesChangedCount = 0;
    int environmentsSavedCount = 0;
    QStringList runRequests;
    QMap<QString, QString> lastRunParams;
    // run_command termina quando o teste chamar finishRun().
    bool deferRun = false;
    std::function<void(const RunOutcome &)> pendingDone;
    RunOutcome nextOutcome;
    QStringList killed;

    FakeAgentHost()
    {
        nextOutcome.started = true;
        nextOutcome.finished = true;
        nextOutcome.success = true;
        nextOutcome.output = QStringLiteral("hello\n");
    }

    kai::core::CommandsData &commandsData() override { return data; }
    QVector<kai::core::Collection> &collections() override { return collectionList; }
    QVector<kai::core::Note> &notes() override { return noteList; }
    kai::core::SettingsData settings() const override { return settingsData; }
    void commandsChanged() override { ++commandsChangedCount; }
    void notesChanged() override { ++notesChangedCount; }
    bool saveEnvironments(const QVector<kai::core::Environment> &environments, QString &) override
    {
        settingsData.environments = environments;
        ++environmentsSavedCount;
        return true;
    }
    QVector<kai::core::RunRecord> runHistory() const override { return runs; }
    QVector<kai::core::NotificationRecord> notifications() const override { return notifs; }
    QVector<kai::utils::LogEntry> recentLogs() const override { return logs; }
    QJsonArray processes() override { return processList; }
    QString storedOutput(const QString &id) const override { return outputs.value(id); }
    bool isRunning(const QString &id) const override { return running.contains(id); }
    void runCommand(const QString &id, const QMap<QString, QString> &params,
                    std::function<void(const RunOutcome &)> done) override
    {
        runRequests << id;
        lastRunParams = params;
        if (deferRun) {
            pendingDone = std::move(done);
            return;
        }
        done(nextOutcome);
    }
    bool killCommand(const QString &id, bool, QString &) override
    {
        killed << id;
        running.remove(id);
        return true;
    }

    QStringList openedInApp;
    bool openCommandInApp(const QString &id, QString &) override
    {
        openedInApp << id;
        return true;
    }

    // Conveniências de montagem.
    kai::core::Folder &addFolder(const QString &id, const QString &name, const QString &parent = {})
    {
        kai::core::Folder f;
        f.id = id;
        f.name = name;
        if (!parent.isEmpty()) f.parentId = parent;
        data.folders << f;
        return data.folders.last();
    }
    kai::core::Command &addCommand(const QString &id, const QString &folderId, const QString &name, const QString &text = QStringLiteral("echo x"))
    {
        kai::core::Command c;
        c.id = id;
        c.folderId = folderId;
        c.name = name;
        c.command = text;
        data.commands << c;
        return data.commands.last();
    }
    const kai::core::Command *commandById(const QString &id) const
    {
        for (const auto &c : data.commands) if (c.id == id) return &c;
        return nullptr;
    }
};
