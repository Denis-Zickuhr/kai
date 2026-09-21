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

// Rastreia processos em background (is_background: true) mantidos vivos
// em memória, permitindo consultar status e interrompê-los sob demanda
//.
class ProcessManager : public QObject {
    Q_OBJECT

public:
    explicit ProcessManager(QObject *parent = nullptr);

    // Registra um novo processo background já iniciado, associado ao
    // `commandId` do Command que o originou. ProcessManager assume a posse
    // (ownership) do ProcessRunner.
    void track(const QString &commandId, std::unique_ptr<ProcessRunner> runner);

    // Encerra o processo associado a `commandId` (terminate -> timeout -> kill,
    // delegado ao próprio ProcessRunner).
    void stop(const QString &commandId);

    // Encerramento IMEDIATO (botão "Forçar parada"): SIGKILL direto, sem
    // esperar terminate — delegado a ProcessRunner::forceStop().
    void forceStop(const QString &commandId);

    // Encerra E ESQUECE o processo: para o runner e REMOVE a entrada do
    // rastreamento. Diferente de stop(), que mantém a entrada (com status
    // Success/Error) para consulta posterior. Usado quando a sessão é
    // descartada de vez.
    void remove(const QString &commandId);

    // Encerra todos os processos rastreados. Deve ser conectado ao sinal
    // aboutToQuit do QCoreApplication ("processos zumbis").
    void stopAll();

    bool isTracked(const QString &commandId) const;
    ProcessStatus statusOf(const QString &commandId) const;
    QStringList trackedCommandIds() const;
    // true se a ÚLTIMA finalização deste processo foi um crash de verdade
    // (QProcess::CrashExit), não só um exitCode != 0 comum — distinção que
    // já existe em ProcessResult::crashed mas se perdia ao virar
    // ProcessStatus::Error (os dois colapsavam juntos). Usado só pra
    // escolher o texto certo em notificações ("crashou" vs. "terminou com
    // erro"); não afeta nenhuma lógica de badge/UI existente. Comando não
    // rastreado ou que nunca finalizou: false (fallback seguro).
    bool lastRunCrashed(const QString &commandId) const;

    // Retorna o ProcessRunner rastreado para `commandId`, ou nullptr se não
    // rastreado. Usado pelo Terminal Drawer para permitir interação via
    // stdin em processos background reconectados (terminal
    // por-comando).
    ProcessRunner *runnerFor(const QString &commandId) const;

signals:
    void statusChanged(const QString &commandId, ProcessStatus status);
    void outputReady(const QString &commandId, const QString &text, bool isError);

private:
    struct TrackedProcess {
        std::unique_ptr<ProcessRunner> runner;
        ProcessStatus status = ProcessStatus::Running;
        // Ver lastRunCrashed(): reflete ProcessResult::crashed da última
        // finalização, senão a informação se perde ao virar só Success/Error.
        bool lastCrashed = false;
    };

    std::map<QString, TrackedProcess> m_processes;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::ProcessStatus)
