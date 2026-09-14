#include "engine/cron-scheduler.h"
#include "core/models.h"
#include "utils/cron-expression.h"
#include "utils/logger.h"

#include <QDateTime>

namespace kai::engine {

static constexpr auto kLogTag = "CronScheduler";

CronScheduler::CronScheduler(QObject *parent)
    : QObject(parent)
{
}

CronScheduler::~CronScheduler() = default;

void CronScheduler::reschedule(const QVector<kai::core::Command> &commands)
{
    for (const auto &timer : m_timersByCommandId) {
        if (timer) {
            timer->stop();
            timer->deleteLater();
        }
    }
    m_timersByCommandId.clear();
    m_scheduledCommands.clear();

    for (const auto &cmd : commands) {
        if (cmd.type != kai::core::CommandType::Command) {
            continue; // CRON só vale para Shell
        }
        if (cmd.cronExpression.isEmpty()) {
            continue;
        }
        if (cmd.kip) {
            // Sessão KIP precisa de alguém olhando a tela.
            utils::Logger::info(kLogTag,
                QStringLiteral("Comando '%1' é KIP; agendamento CRON ignorado.").arg(cmd.id));
            continue;
        }

        // Guardado para re-armar em onTimerFired.
        m_scheduledCommands[cmd.id] = cmd;
        armTimerFor(cmd);
    }

    utils::Logger::info(kLogTag,
        QStringLiteral("Agendados %1 comando(s) CRON").arg(m_timersByCommandId.size()));
}

void CronScheduler::armTimerFor(const kai::core::Command &command)
{
    auto cronExpr = utils::CronExpression::parse(command.cronExpression);
    if (!cronExpr.valid) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Comando '%1' tem expressão CRON inválida: %2")
                .arg(command.name, cronExpr.error));
        return;
    }

    QDateTime now = QDateTime::currentDateTimeUtc();
    auto nextTime = cronExpr.nextOccurrence(now);
    if (!nextTime.has_value()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Comando '%1' não tem próxima ocorrência nos próximos 366 dias")
                .arg(command.name));
        return;
    }

    qint64 delayMs = now.msecsTo(nextTime.value());
    if (delayMs < 0) {
        delayMs = 0;
    }

    auto timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delayMs);

    connect(timer, &QTimer::timeout, this, [this, commandId = command.id]() {
        onTimerFired(commandId);
    });

    timer->start();

    m_timersByCommandId[command.id] = timer;

    utils::Logger::info(kLogTag,
        QStringLiteral("Agendado '%1' para %2 UTC (%3 ms)")
            .arg(command.name, nextTime->toString(Qt::ISODate), QString::number(delayMs)));
}

void CronScheduler::onTimerFired(const QString &commandId)
{
    utils::Logger::info(kLogTag,
        QStringLiteral("Timer do comando '%1' dispara, emitindo commandDue()")
            .arg(commandId));

    emit commandDue(commandId);

    m_timersByCommandId.remove(commandId);

    // Re-arma a partir do Command guardado em reschedule(): o cron não pode depender de um reschedule() externo.
    const auto it = m_scheduledCommands.constFind(commandId);
    if (it != m_scheduledCommands.constEnd()) {
        armTimerFor(it.value());
    } else {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Comando '%1' disparou mas não foi encontrado para re-agendar "
                           "(removido ou editado entre o agendamento e o disparo).")
                .arg(commandId));
    }
}

} // namespace kai::engine
