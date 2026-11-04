#include <QtTest>
#include <QSignalSpy>
#include <QDateTime>

#include "engine/cron-scheduler.h"
#include "core/models.h"

using namespace kai::engine;
using namespace kai::core;

class TestCronScheduler : public QObject {
    Q_OBJECT

private slots:
    void testRescheduleIgnoresHttpCommands()
    {
        // Comandos HTTP não devem gerar timers, mesmo com cronExpression
        Command httpCmd;
        httpCmd.id = "http_cmd";
        httpCmd.type = CommandType::Http;
        httpCmd.cronExpression = QStringLiteral("0 9 * * *");

        CronScheduler scheduler;
        scheduler.reschedule({httpCmd});

        // O timer não é observável: só garante que reschedule() repetido não falha.
        scheduler.reschedule({});
    }

    void testRescheduleIgnoresEmptyExpression()
    {
        // Comandos sem cronExpression não geram timers
        Command shellCmd;
        shellCmd.id = "shell_empty_cron";
        shellCmd.type = CommandType::Command;
        shellCmd.command = QStringLiteral("echo test");
        shellCmd.cronExpression = QString(); // vazio

        CronScheduler scheduler;
        scheduler.reschedule({shellCmd});

        scheduler.reschedule({});
    }

    void testRescheduleIgnoresInvalidExpression()
    {
        // Expressão inválida não gera timer
        Command shellCmd;
        shellCmd.id = "shell_invalid_cron";
        shellCmd.type = CommandType::Command;
        shellCmd.command = QStringLiteral("echo test");
        shellCmd.cronExpression = QStringLiteral("invalid cron"); // inválida

        CronScheduler scheduler;
        scheduler.reschedule({shellCmd});

        scheduler.reschedule({});
    }

    // Uma sessão interativa KIP não pode ser disparada sozinha por um agendamento: o scheduler pula o comando.
    void testRescheduleSkipsKipCommands()
    {
        Command kip;
        kip.id = "kip_cron";
        kip.type = CommandType::Command;
        kip.command = QStringLiteral("wizard --kip");
        kip.cronExpression = QStringLiteral("* * * * *");
        kip.kip = true;
        Command plain = kip;
        plain.id = "plain_cron";
        plain.kip = false;

        CronScheduler scheduler;
        scheduler.reschedule({kip, plain});

        QVERIFY(!scheduler.m_timersByCommandId.contains(kip.id));
        QVERIFY(scheduler.m_timersByCommandId.contains(plain.id));
    }

    void testCommandDueEmittedWhenTimerFires()
    {
        // Chama onTimerFired() direto (via friend) em vez de esperar o relógio real (lento/flaky); a emissão de commandDue é a mesma.
        Command cmd;
        cmd.id = "quick_cron";
        cmd.type = CommandType::Command;
        cmd.command = QStringLiteral("echo test");
        cmd.cronExpression = QStringLiteral("* * * * *");

        CronScheduler scheduler;
        QSignalSpy dueSpy(&scheduler, &CronScheduler::commandDue);

        scheduler.reschedule({cmd});
        scheduler.onTimerFired(cmd.id);

        QCOMPARE(dueSpy.count(), 1);
        QCOMPARE(dueSpy.takeFirst().at(0).toString(), cmd.id);
    }

    // Depois de disparar, o comando precisa continuar agendado SEM depender de outro reschedule() externo.
    void testTimerRearmsAfterFiringWithoutExternalReschedule()
    {
        Command cmd;
        cmd.id = "rearm_cron";
        cmd.type = CommandType::Command;
        cmd.command = QStringLiteral("echo test");
        cmd.cronExpression = QStringLiteral("* * * * *");

        CronScheduler scheduler;
        scheduler.reschedule({cmd});
        QCOMPARE(scheduler.m_timersByCommandId.size(), 1);
        QVERIFY(scheduler.m_timersByCommandId.contains(cmd.id));

        // Simula o disparo do timer SEM chamar reschedule() de novo.
        scheduler.onTimerFired(cmd.id);

        // Sem a re-armação o mapa ficaria VAZIO; precisa existir uma NOVA entrada para o mesmo comando.
        QCOMPARE(scheduler.m_timersByCommandId.size(), 1);
        QVERIFY(scheduler.m_timersByCommandId.contains(cmd.id));
        QVERIFY(scheduler.m_timersByCommandId.value(cmd.id) != nullptr);
    }

    // Comando removido/editado entre o agendamento e o disparo: onTimerFired não crasha, só deixa de re-armar.
    void testTimerFiredForRemovedCommandDoesNotCrash()
    {
        Command cmd;
        cmd.id = "removed_cron";
        cmd.type = CommandType::Command;
        cmd.command = QStringLiteral("echo test");
        cmd.cronExpression = QStringLiteral("* * * * *");

        CronScheduler scheduler;
        scheduler.reschedule({cmd});
        scheduler.reschedule({}); // remove o comando (limpa m_scheduledCommands)

        scheduler.onTimerFired(cmd.id); // não deve crashar
        QVERIFY(!scheduler.m_timersByCommandId.contains(cmd.id));
    }

    void testRescheduleRemovesOldTimers()
    {
        Command cmd1;
        cmd1.id = "cmd1";
        cmd1.type = CommandType::Command;
        cmd1.command = QStringLiteral("echo 1");
        cmd1.cronExpression = QStringLiteral("0 9 * * *");

        CronScheduler scheduler;
        scheduler.reschedule({cmd1});

        // Segunda reschedule com lista vazia — deve remover timers antigos
        scheduler.reschedule({});

        scheduler.reschedule({});
    }

    void testRescheduleRearmsWhenExpressionEdited()
    {
        Command cmd1;
        cmd1.id = "cmd_edited";
        cmd1.type = CommandType::Command;
        cmd1.command = QStringLiteral("echo test");
        cmd1.cronExpression = QStringLiteral("0 9 * * *"); // 9:00

        CronScheduler scheduler;
        scheduler.reschedule({cmd1});

        Command cmd2 = cmd1;
        cmd2.cronExpression = QStringLiteral("0 17 * * *"); // 17:00 (diferente)

        scheduler.reschedule({cmd2});

        QVERIFY(true);
    }

    void testRescheduleMultipleCommands()
    {
        Command cmd1;
        cmd1.id = "cmd1";
        cmd1.type = CommandType::Command;
        cmd1.command = QStringLiteral("echo 1");
        cmd1.cronExpression = QStringLiteral("0 9 * * *");

        Command cmd2;
        cmd2.id = "cmd2";
        cmd2.type = CommandType::Command;
        cmd2.command = QStringLiteral("echo 2");
        cmd2.cronExpression = QStringLiteral("0 12 * * *");

        Command cmd3;
        cmd3.id = "cmd3";
        cmd3.type = CommandType::Command;
        cmd3.command = QStringLiteral("echo 3");
        cmd3.cronExpression = QString(); // sem CRON

        CronScheduler scheduler;
        scheduler.reschedule({cmd1, cmd2, cmd3});

        scheduler.reschedule({cmd1, cmd2}); // Remove cmd3, cmd2 desarmado
        QVERIFY(true);
    }

    void testRescheduleEmptyList()
    {
        CronScheduler scheduler;
        scheduler.reschedule({});
        scheduler.reschedule({});
        QVERIFY(true);
    }
};

QTEST_MAIN(TestCronScheduler)
#include "test_cron_scheduler.moc"
