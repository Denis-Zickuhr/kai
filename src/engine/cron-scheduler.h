#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QTimer>
#include <QVector>
#include <memory>

#include "core/models.h"

class TestCronScheduler;

namespace kai::engine {

// Orquestrador de agendamentos CRON. Gerencia timers para comandos com
// expressões CRON ativas e emite sinais quando chegou a hora de executar.
// Segue o mesmo padrão de autorun: a UI conecta commandDue() a
// runCommandWithParams, nunca o scheduler executa diretamente.
class CronScheduler : public QObject {
    Q_OBJECT
    // Permite ao teste chamar onTimerFired() diretamente (bypass do QTimer
    // real) e inspecionar m_timersByCommandId — validar o RE-ARM sem
    // precisar esperar o relógio de verdade bater num minuto exato.
    friend class ::TestCronScheduler;

public:
    explicit CronScheduler(QObject *parent = nullptr);
    ~CronScheduler();

    // Recalcula timers a partir do estado atual de comandos.
    // Chamado ao carregar commands.json, ao salvar, e quando cronExpression
    // de um comando é editada. Cancela timers de comandos removidos ou com
    // expressão vazia/inválida.
    void reschedule(const QVector<kai::core::Command> &commands);

signals:
    // Emitido quando é hora de executar um comando CRON.
    // A UI conecta isto a runCommandWithParams, mesmo handler do autorun
    // e cliques manuais — o scheduler NUNCA chama ExecutionPipeline
    // diretamente.
    void commandDue(const QString &commandId);

private:
    // Arma timer para um comando individual.
    void armTimerFor(const kai::core::Command &command);

    // Slot interno para disparo de timer: emite commandDue() e RE-ARMA o
    // timer para a PRÓXIMA ocorrência da mesma expressão — sem isto, um
    // comando cron disparava exatamente uma vez e nunca mais (bug real
    // reportado: "roda uma vez e já era"), porque o timer antigo era só
    // removido, esperando um reschedule() externo (acionado por salvar
    // comandos) que não tem relação nenhuma com o próximo horário agendado.
    void onTimerFired(const QString &commandId);

    // commandId -> timer (ownership do QObject, será deletado automaticamente)
    QMap<QString, QTimer *> m_timersByCommandId;

    // commandId -> último Command conhecido com cronExpression válida —
    // necessário para RE-ARMAR o timer em onTimerFired() sem depender de um
    // reschedule() externo. Atualizado a cada chamada de reschedule().
    QMap<QString, kai::core::Command> m_scheduledCommands;
};

} // namespace kai::engine
