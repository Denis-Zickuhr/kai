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

// Timers de comandos com expressão CRON. Não executa nada: emite commandDue() e a UI o liga ao mesmo handler do
// autorun e dos cliques manuais.
class CronScheduler : public QObject {
    Q_OBJECT
    // Teste chama onTimerFired() direto e inspeciona m_timersByCommandId.
    friend class ::TestCronScheduler;

public:
    explicit CronScheduler(QObject *parent = nullptr);
    ~CronScheduler();

    // Recria os timers a partir dos comandos; os de comandos removidos ou com expressão vazia/inválida somem.
    void reschedule(const QVector<kai::core::Command> &commands);

signals:
    // É hora de executar o comando CRON `commandId`.
    void commandDue(const QString &commandId);

private:
    void armTimerFor(const kai::core::Command &command);

    // Emite commandDue() e re-arma o timer para a próxima ocorrência da mesma expressão.
    void onTimerFired(const QString &commandId);

    // commandId -> timer (filho deste objeto)
    QMap<QString, QTimer *> m_timersByCommandId;

    // commandId -> Command agendado, para re-armar em onTimerFired().
    QMap<QString, kai::core::Command> m_scheduledCommands;
};

} // namespace kai::engine
