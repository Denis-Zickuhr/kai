#include "engine/process-manager.h"

#include "utils/logger.h"

namespace kai::engine {

namespace {
constexpr const char *kLogTag = "ProcessManager";
}

ProcessManager::ProcessManager(QObject *parent)
    : QObject(parent)
{
}

void ProcessManager::track(const QString &commandId, std::unique_ptr<ProcessRunner> runner)
{
    ProcessRunner *rawRunner = runner.get();

    connect(rawRunner, &ProcessRunner::outputReady, this,
            [this, commandId](const QString &text, bool isError) {
                emit outputReady(commandId, text, isError);
            });

    connect(rawRunner, &ProcessRunner::finished, this,
            [this, commandId](const ProcessResult &result) {
                auto it = m_processes.find(commandId);
                if (it == m_processes.end()) {
                    return;
                }
                it->second.status = (result.exitCode == 0 && !result.crashed) ? ProcessStatus::Success
                                                                                : ProcessStatus::Error;
                it->second.lastCrashed = result.crashed;
                const ProcessStatus newStatus = it->second.status;
                utils::Logger::info(kLogTag,
                    QStringLiteral("Processo '%1' finalizado com status %2.")
                        .arg(commandId, newStatus == ProcessStatus::Success ? "Success" : "Error"));

                // Emite de forma assíncrona (QueuedConnection): estamos
                // dentro do stack de sinais do próprio QProcess::finished
                // (via ProcessRunner). Emitir statusChanged() de forma
                // síncrona aqui faz a UI (MainWindow -> ProcessListDialog)
                // destruir QListWidgetItem's e potencialmente o próprio
                // ProcessRunner ainda em uso mais acima no stack,
                // resultando em segfault. Adiar para o próximo ciclo do
                // event loop evita essa reentrância perigosa.
                QMetaObject::invokeMethod(this, [this, commandId, newStatus]() {
                    emit statusChanged(commandId, newStatus);
                }, Qt::QueuedConnection);
            });

    TrackedProcess tracked;
    tracked.runner = std::move(runner);
    tracked.status = ProcessStatus::Running;
    m_processes.insert_or_assign(commandId, std::move(tracked));

    utils::Logger::info(kLogTag, QStringLiteral("Rastreando processo background '%1'.").arg(commandId));

    // Mesma proteção contra reentrância: track() é chamado de dentro do
    // handler de ExecutionPipeline::backgroundProcessStarted, que por sua
    // vez roda dentro do stack de sinais do ProcessRunner::started. Adia a
    // notificação para não reentrar na UI enquanto ainda estamos "dentro"
    // do processo que está sendo rastreado.
    QMetaObject::invokeMethod(this, [this, commandId]() {
        emit statusChanged(commandId, ProcessStatus::Running);
    }, Qt::QueuedConnection);
}

void ProcessManager::stop(const QString &commandId)
{
    auto it = m_processes.find(commandId);
    if (it == m_processes.end()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Tentativa de parar processo não rastreado: '%1'.").arg(commandId));
        return;
    }
    it->second.runner->stop();
}

void ProcessManager::forceStop(const QString &commandId)
{
    auto it = m_processes.find(commandId);
    if (it == m_processes.end()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Tentativa de forçar parada de processo não rastreado: '%1'.").arg(commandId));
        return;
    }
    it->second.runner->forceStop();
}

void ProcessManager::remove(const QString &commandId)
{
    auto it = m_processes.find(commandId);
    if (it == m_processes.end()) {
        return;
    }
    it->second.runner->stop();
    // ERASE de fato: sem isto a entrada morta persistia em m_processes e
    // trackedCommandIds() a devolvia pra sempre.
    m_processes.erase(it);
    utils::Logger::info(kLogTag,
        QStringLiteral("Processo '%1' encerrado e removido do rastreamento.").arg(commandId));
}

void ProcessManager::stopAll()
{
    utils::Logger::info(kLogTag,
        QStringLiteral("Encerrando %1 processo(s) em background (aboutToQuit).").arg(m_processes.size()));
    for (auto &entry : m_processes) {
        entry.second.runner->stop();
    }
}

bool ProcessManager::isTracked(const QString &commandId) const
{
    return m_processes.contains(commandId);
}

ProcessStatus ProcessManager::statusOf(const QString &commandId) const
{
    const auto it = m_processes.find(commandId);
    return it != m_processes.end() ? it->second.status : ProcessStatus::Error;
}

bool ProcessManager::lastRunCrashed(const QString &commandId) const
{
    const auto it = m_processes.find(commandId);
    return it != m_processes.end() && it->second.lastCrashed;
}

ProcessRunner *ProcessManager::runnerFor(const QString &commandId) const
{
    const auto it = m_processes.find(commandId);
    return it != m_processes.end() ? it->second.runner.get() : nullptr;
}

QStringList ProcessManager::trackedCommandIds() const
{
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(m_processes.size()));
    for (const auto &entry : m_processes) {
        ids << entry.first;
    }
    return ids;
}

} // namespace kai::engine
