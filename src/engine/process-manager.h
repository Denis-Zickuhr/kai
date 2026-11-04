#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <map>
#include <memory>

#include "engine/process-runner.h"

namespace kai::engine {

enum class ProcessStatus {
    Running,
    Success,
    Error
};

// Rastreia processos em background (is_background: true) mantidos vivos em memória, para consultar status e interrompê-los.
class ProcessManager : public QObject {
    Q_OBJECT

public:
    explicit ProcessManager(QObject *parent = nullptr);

    // Registra um processo background já iniciado para `commandId`; o ProcessManager assume a posse do ProcessRunner.
    void track(const QString &commandId, std::unique_ptr<ProcessRunner> runner);

    // Encerra o processo de `commandId` (terminate -> timeout -> kill, delegado ao ProcessRunner).
    void stop(const QString &commandId);

    // Encerramento IMEDIATO ("Forçar parada"): ProcessRunner::forceStop(), sem esperar terminate.
    void forceStop(const QString &commandId);

    // Encerra E ESQUECE o processo: remove a entrada do rastreamento (stop() a mantém, com status, para consulta).
    void remove(const QString &commandId);

    // Encerra todos os processos rastreados; ligar ao aboutToQuit do QCoreApplication.
    void stopAll();

    bool isTracked(const QString &commandId) const;
    ProcessStatus statusOf(const QString &commandId) const;
    QStringList trackedCommandIds() const;
    // A ÚLTIMA finalização foi um crash (QProcess::CrashExit), não só exitCode != 0; escolhe o texto da notificação.
    // Comando não rastreado ou que nunca finalizou: false.
    bool lastRunCrashed(const QString &commandId) const;

    // A última finalização foi um encerramento PEDIDO (Parar/Forçar/kill/reset): não é erro nem falha do comando.
    bool lastRunStoppedByRequest(const QString &commandId) const;

    // ProcessRunner rastreado para `commandId`, ou nullptr; o Terminal Drawer o usa para stdin.
    ProcessRunner *runnerFor(const QString &commandId) const;

signals:
    void statusChanged(const QString &commandId, ProcessStatus status);
    void outputReady(const QString &commandId, const QString &text, bool isError);

private:
    struct TrackedProcess {
        std::unique_ptr<ProcessRunner> runner;
        ProcessStatus status = ProcessStatus::Running;
        bool lastCrashed = false;
        bool lastStoppedByRequest = false;
    };

    std::map<QString, TrackedProcess> m_processes;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::ProcessStatus)
