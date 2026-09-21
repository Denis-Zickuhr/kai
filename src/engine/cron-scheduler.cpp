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
    // Limpa todos os timers antigos (delete seguro)
    for (auto timer : m_timersByCommandId) {
        if (timer) {
            timer->stop();
            timer->deleteLater();
        }
    }
    m_timersByCommandId.clear();
    m_scheduledCommands.clear();

    // Arma novo timer para cada comando com expressão CRON válida
    for (const auto &cmd : commands) {
        if (cmd.type != kai::core::CommandType::Shell) {
            continue; // CRON só vale para Shell
        }
        if (cmd.cronExpression.isEmpty()) {
            continue; // Sem expressão = sem agendamento
        }

        // Guarda o Command para poder RE-ARMAR depois que o timer disparar
        // (ver onTimerFired) sem depender de um reschedule() externo.
        m_scheduledCommands[cmd.id] = cmd;
        armTimerFor(cmd);
    }

    utils::Logger::info(kLogTag,
        QStringLiteral("Agendados %1 comando(s) CRON").arg(m_timersByCommandId.size()));
}

void CronScheduler::armTimerFor(const kai::core::Command &command)
{
    // Parse e valida a expressão CRON
    auto cronExpr = utils::CronExpression::parse(command.cronExpression);
    if (!cronExpr.valid) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Comando '%1' tem expressão CRON inválida: %2")
                .arg(command.name, cronExpr.error));
        return;
    }

    // Calcula próxima ocorrência
    QDateTime now = QDateTime::currentDateTimeUtc();
    auto nextTime = cronExpr.nextOccurrence(now);
    if (!nextTime.has_value()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Comando '%1' não tem próxima ocorrência nos próximos 366 dias")
                .arg(command.name));
        return;
    }

    // Calcula delay até a próxima ocorrência
    qint64 delayMs = now.msecsTo(nextTime.value());
    if (delayMs < 0) {
        delayMs = 0; // Segurança: se for negativo (nunca deveria), dispara imediatamente
    }

    // Cria e arma o timer
    auto timer = new QTimer(this); // parent é this, será deletado automaticamente
    timer->setSingleShot(true);
    timer->setInterval(delayMs);

    // Conecta ao handler que vai reaplicar o timer
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

    // Emite o sinal para a UI executar o comando. O disparo em si NÃO
    // bloqueia o scheduler (a execução roda assíncrona pela pipeline
    // normal), então é seguro re-armar o timer logo em seguida.
    emit commandDue(commandId);

    // Remove o timer USADO (QTimer::SingleShot já parou sozinho, só
    // limpamos a entrada antiga do mapa).
    m_timersByCommandId.remove(commandId);

    // RE-ARMA para a PRÓXIMA ocorrência da MESMA expressão — sem isto o
    // comando disparava exatamente uma vez e nunca mais (bug real
    // reportado: "roda uma vez e já era"). Usa o Command guardado em
    // reschedule() (m_scheduledCommands), não um reschedule() externo:
    // um comando cron não deveria depender de o usuário salvar algo pra
    // continuar agendado.
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
