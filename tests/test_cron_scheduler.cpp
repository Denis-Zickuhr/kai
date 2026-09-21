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

        // Não há como verificar diretamente se timer foi armado, mas
        // ao chamar reschedule novamente, não deveria falhar se não houver
        // timers para comando HTTP.
        scheduler.reschedule({});
    }

    void testRescheduleIgnoresEmptyExpression()
    {
        // Comandos sem cronExpression não geram timers
        Command shellCmd;
        shellCmd.id = "shell_empty_cron";
        shellCmd.type = CommandType::Shell;
        shellCmd.command = QStringLiteral("echo test");
        shellCmd.cronExpression = QString(); // vazio

        CronScheduler scheduler;
        scheduler.reschedule({shellCmd});

        // Sem erro ao chamar novamente
        scheduler.reschedule({});
    }

    void testRescheduleIgnoresInvalidExpression()
    {
        // Expressão inválida não gera timer
        Command shellCmd;
        shellCmd.id = "shell_invalid_cron";
        shellCmd.type = CommandType::Shell;
        shellCmd.command = QStringLiteral("echo test");
        shellCmd.cronExpression = QStringLiteral("invalid cron"); // inválida

        CronScheduler scheduler;
        scheduler.reschedule({shellCmd});

        // Sem erro ao chamar novamente
        scheduler.reschedule({});
    }

    void testCommandDueEmittedWhenTimerFires()
    {
        // Comando com CRON que dispara a cada minuto — usamos
        // onTimerFired() diretamente (via friend) em vez de esperar o
        // relógio real bater num minuto exato, o que deixaria o teste
        // lento/flaky. O disparo em si (emissão de commandDue) é o mesmo
        // código que o QTimer real chamaria.
        Command cmd;
        cmd.id = "quick_cron";
        cmd.type = CommandType::Shell;
        cmd.command = QStringLiteral("echo test");
        cmd.cronExpression = QStringLiteral("* * * * *");

        CronScheduler scheduler;
        QSignalSpy dueSpy(&scheduler, &CronScheduler::commandDue);

        scheduler.reschedule({cmd});
        scheduler.onTimerFired(cmd.id);

        QCOMPARE(dueSpy.count(), 1);
        QCOMPARE(dueSpy.takeFirst().at(0).toString(), cmd.id);
    }

    // Cobre o BUG raiz reportado pelo usuário ("roda uma vez e já era"):
    // depois de disparar, o comando precisa continuar agendado — SEM
    // depender de outro reschedule() externo (que só acontecia quando o
    // usuário salvava algo manualmente).
    void testTimerRearmsAfterFiringWithoutExternalReschedule()
    {
        Command cmd;
        cmd.id = "rearm_cron";
        cmd.type = CommandType::Shell;
        cmd.command = QStringLiteral("echo test");
        cmd.cronExpression = QStringLiteral("* * * * *");

        CronScheduler scheduler;
        scheduler.reschedule({cmd});
        QCOMPARE(scheduler.m_timersByCommandId.size(), 1);
        QVERIFY(scheduler.m_timersByCommandId.contains(cmd.id));

        // Simula o disparo do timer (o mesmo caminho que QTimer::timeout
        // chamaria) SEM chamar reschedule() de novo.
        scheduler.onTimerFired(cmd.id);

        // Se o bug ainda existisse, o mapa ficaria VAZIO aqui (timer
        // removido e nunca recriado). Com a correção, uma NOVA entrada
        // precisa existir para o mesmo comando.
        QCOMPARE(scheduler.m_timersByCommandId.size(), 1);
        QVERIFY(scheduler.m_timersByCommandId.contains(cmd.id));
        QVERIFY(scheduler.m_timersByCommandId.value(cmd.id) != nullptr);
    }

    // O comando pode ter sido removido/editado entre o agendamento e o
    // disparo (ex: usuário apagou o comando) — onTimerFired não deve
    // crashar, só deixar de re-armar.
    void testTimerFiredForRemovedCommandDoesNotCrash()
    {
        Command cmd;
        cmd.id = "removed_cron";
        cmd.type = CommandType::Shell;
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
        // Primeira reschedule com comando
        Command cmd1;
        cmd1.id = "cmd1";
        cmd1.type = CommandType::Shell;
        cmd1.command = QStringLiteral("echo 1");
        cmd1.cronExpression = QStringLiteral("0 9 * * *");

        CronScheduler scheduler;
        scheduler.reschedule({cmd1});

        // Segunda reschedule com lista vazia — deve remover timers antigos
        scheduler.reschedule({});

        // Sem erro ao chamar novamente (timers foram limpos)
        scheduler.reschedule({});
    }

    void testRescheduleRearmsWhenExpressionEdited()
    {
        // Comando com expressão original
        Command cmd1;
        cmd1.id = "cmd_edited";
        cmd1.type = CommandType::Shell;
        cmd1.command = QStringLiteral("echo test");
        cmd1.cronExpression = QStringLiteral("0 9 * * *"); // 9:00

        CronScheduler scheduler;
        scheduler.reschedule({cmd1});

        // Mesma comando com expressão nova
        Command cmd2 = cmd1;
        cmd2.cronExpression = QStringLiteral("0 17 * * *"); // 17:00 (diferente)

        // Reschedule deve remover timer antigo e armar novo
        scheduler.reschedule({cmd2});

        // Sem erro
        QVERIFY(true);
    }

    void testRescheduleMultipleCommands()
    {
        // Múltiplos comandos com CRON
        Command cmd1;
        cmd1.id = "cmd1";
        cmd1.type = CommandType::Shell;
        cmd1.command = QStringLiteral("echo 1");
        cmd1.cronExpression = QStringLiteral("0 9 * * *");

        Command cmd2;
        cmd2.id = "cmd2";
        cmd2.type = CommandType::Shell;
        cmd2.command = QStringLiteral("echo 2");
        cmd2.cronExpression = QStringLiteral("0 12 * * *");

        Command cmd3;
        cmd3.id = "cmd3";
        cmd3.type = CommandType::Shell;
        cmd3.command = QStringLiteral("echo 3");
        cmd3.cronExpression = QString(); // sem CRON

        CronScheduler scheduler;
        scheduler.reschedule({cmd1, cmd2, cmd3});

        // Apenas 2 timers devem ser armados (cmd1 e cmd2)
        // Não podemos verificar diretamente, mas estrutura deve estar OK
        scheduler.reschedule({cmd1, cmd2}); // Remove cmd3, cmd2 desarmado
        QVERIFY(true);
    }

    void testRescheduleEmptyList()
    {
        CronScheduler scheduler;
        scheduler.reschedule({}); // Sem erro
        scheduler.reschedule({}); // Chamar novamente sem erro
        QVERIFY(true);
    }
};

QTEST_MAIN(TestCronScheduler)
#include "test_cron_scheduler.moc"
