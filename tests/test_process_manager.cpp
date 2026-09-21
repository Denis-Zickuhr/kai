#include <QTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QFile>
#include <csignal>

#include "engine/process-manager.h"

using namespace kai::engine;

// Cobre o crash real reportado: comando background finalizado via stop()
// (SIGTERM) causava segfault ao emitir statusChanged() de forma síncrona
// dentro do próprio stack de sinais do ProcessRunner::finished, levando a
// UI (MainWindow -> ProcessListDialog) a destruir widgets/reentrar durante
// a notificação. Corrigido com QMetaObject::invokeMethod(Qt::QueuedConnection).
class TestProcessManager : public QObject {
    Q_OBJECT

private slots:
    // Reproduz o cenário exato do log do usuário: processo background
    // iniciado, depois encerrado via stop() (SIGTERM), gerando
    // exitCode != 0 / crashed=true (comportamento esperado do Qt para
    // término via sinal). Deve emitir statusChanged sem crashar o processo
    // do Kai, mesmo com múltiplos listeners reagindo à mudança de estado.
    void backgroundProcessTerminatedViaStopDoesNotCrash()
    {
        ProcessManager manager;
        QSignalSpy statusSpy(&manager, &ProcessManager::statusChanged);

        auto runner = std::make_unique<ProcessRunner>();
        ProcessRunner *rawRunner = runner.get();

        // Simula reentrância: um listener de statusChanged que consulta o
        // ProcessManager de volta (mesmo padrão do MainWindow ->
        // ProcessListDialog::refreshProcessList).
        int reentrantCallCount = 0;
        connect(&manager, &ProcessManager::statusChanged, &manager,
                [&manager, &reentrantCallCount](const QString &commandId, ProcessStatus) {
                    ++reentrantCallCount;
                    // Consulta reentrante, como refreshProcessList faz.
                    manager.trackedCommandIds();
                    manager.statusOf(commandId);
                });

        manager.track(QStringLiteral("cmd_bg"), std::move(runner));

        rawRunner->start(QStringLiteral("sleep 10"), QString(), {});
        QVERIFY(rawRunner->isRunning());

        rawRunner->stop(); // SIGTERM -> exitStatus=CrashExit no Qt

        // Aguarda o processo finalizar E o evento assíncrono de
        // statusChanged ser processado (QueuedConnection precisa do
        // event loop rodando).
        QTest::qWait(3000);
        QCoreApplication::processEvents();

        // Se chegamos até aqui sem segfault, a correção funcionou.
        QVERIFY(statusSpy.count() >= 1);
        QVERIFY(reentrantCallCount >= 1);
        QCOMPARE(manager.statusOf(QStringLiteral("cmd_bg")), ProcessStatus::Error);
    }

    void trackEmitsRunningStatusAsynchronously()
    {
        ProcessManager manager;
        QSignalSpy statusSpy(&manager, &ProcessManager::statusChanged);

        auto runner = std::make_unique<ProcessRunner>();
        manager.track(QStringLiteral("cmd_x"), std::move(runner));

        // Emissão é assíncrona (QueuedConnection): não deve estar
        // disponível imediatamente, só após o event loop processar.
        QCOMPARE(statusSpy.count(), 0);

        QCoreApplication::processEvents();
        QCOMPARE(statusSpy.count(), 1);
        QCOMPARE(qvariant_cast<ProcessStatus>(statusSpy.at(0).at(1)), ProcessStatus::Running);
    }

    // Reprodução real do bug reportado: "não consigo mais rodar/rebootar
    // um comando" depois de pará-lo via Stop/SIGKILL. O ProcessManager
    // mantém o histórico do último processo daquele commandId até uma
    // nova execução chamar track() de novo (permitindo consultar o status
    // final Success/Error na UI); o bug real estava em
    // MainWindow::handleCommandActivated, que checava apenas isTracked()
    // sem considerar se o status real ainda era Running — bloqueando
    // permanentemente uma nova execução depois da primeira parada. Este
    // teste cobre a parte do ProcessManager: track() de um novo
    // ProcessRunner para o mesmo commandId depois do anterior ter
    // terminado deve funcionar normalmente (statusOf reflete o novo
    // processo, não o antigo).
    void retrackingSameCommandIdAfterStopWorksCorrectly()
    {
        ProcessManager manager;

        auto firstRunner = std::make_unique<ProcessRunner>();
        ProcessRunner *rawFirstRunner = firstRunner.get();
        manager.track(QStringLiteral("cmd_restart"), std::move(firstRunner));
        QCoreApplication::processEvents();

        rawFirstRunner->start(QStringLiteral("sleep 10"), QString(), {});
        QVERIFY(rawFirstRunner->isRunning());

        rawFirstRunner->stop();
        QTest::qWait(3000);
        QCoreApplication::processEvents();

        QVERIFY(manager.isTracked(QStringLiteral("cmd_restart")));
        QCOMPARE(manager.statusOf(QStringLiteral("cmd_restart")), ProcessStatus::Error);

        // "Reboot": nova execução do mesmo comando, novo ProcessRunner
        // rastreado sob o mesmo commandId. Deve substituir a entrada
        // antiga sem crashar e refletir o novo processo em execução.
        auto secondRunner = std::make_unique<ProcessRunner>();
        ProcessRunner *rawSecondRunner = secondRunner.get();
        manager.track(QStringLiteral("cmd_restart"), std::move(secondRunner));
        QCoreApplication::processEvents();

        QCOMPARE(manager.statusOf(QStringLiteral("cmd_restart")), ProcessStatus::Running);

        rawSecondRunner->start(QStringLiteral("sleep 10"), QString(), {});
        QVERIFY(rawSecondRunner->isRunning());
        rawSecondRunner->stop();
        QTest::qWait(3000);
    }

    // Reprodução real do bug reportado: "o SIGKILL tá meio fraco" — matar
    // apenas o PID do bash usado para interpretar o comando não encerrava
    // processos filhos gerados pelo script. Simula esse cenário real:
    // bash gera um processo filho (outro `sleep`, em background dentro do
    // próprio script) e verifica que stop() encerra AMBOS.
    void stopKillsChildProcessesSpawnedByScript()
    {
#if defined(Q_OS_WIN)
        QSKIP("Teste específico de grupos de processo POSIX (setsid/killpg); "
              "no Windows o encerramento usa QProcess::terminate/kill.");
#else
        ProcessRunner runner;
        // O `&` faz o `sleep 30` interno rodar como processo filho do
        // bash, no mesmo grupo de processos (comportamento padrão sem
        // `setsid`/`disown`).
        runner.start(QStringLiteral("sleep 30 & child_pid=$!; echo $child_pid > /tmp/kai_test_child_pid; wait"),
                     QString(), {});
        QVERIFY(runner.isRunning());
        QTest::qWait(300); // tempo para o script escrever o PID do filho

        QFile pidFile(QStringLiteral("/tmp/kai_test_child_pid"));
        QVERIFY(pidFile.open(QIODevice::ReadOnly));
        const qint64 childPid = pidFile.readAll().trimmed().toLongLong();
        pidFile.close();
        QVERIFY(childPid > 0);

        runner.stop();
        QTest::qWait(3000);

        // ::kill(pid, 0) retorna 0 se o processo ainda existir, -1 (com
        // ESRCH) se já tiver terminado — forma padrão de verificar
        // existência de um processo sem enviar sinal real.
        const int stillAlive = ::kill(static_cast<pid_t>(childPid), 0);
        QCOMPARE(stillAlive, -1);

        QFile::remove(QStringLiteral("/tmp/kai_test_child_pid"));
#endif
    }

    // Reprodução do bug "Parar não encerra, mas o Ctrl+C encerra": sob PTY com
    // `bash -i` (job control) o comando roda em primeiro plano num grupo PRÓPRIO.
    // Um SIGTERM/SIGKILL no grupo do shell não o alcança (e o bash interativo
    // ignora SIGTERM); o job aqui ainda ignora SIGHUP/SIGTERM, então nem o SIGHUP
    // do kernel ao morrer o líder o derruba. Só o Ctrl+C (SIGINT) ou o SIGKILL
    // na SESSÃO inteira o encerram — stop() precisa fazer isso.
    void stopKillsAForegroundJobThatIgnoresTermAndHup()
    {
#if defined(Q_OS_WIN)
        QSKIP("Teste específico de sessões/grupos de processo POSIX.");
#else
        const QString pidPath = QStringLiteral("/tmp/kai_test_job_pid_%1").arg(QCoreApplication::applicationPid());
        QFile::remove(pidPath);

        ProcessRunner runner;
        runner.setUsePty(true);
        runner.setInteractiveShell(true);
        runner.setKillTimeoutMs(1500);
        // O `;` depois do job impede o bash de fazer exec direto: o job vira
        // um filho em grupo próprio. `exec sleep` herda os sinais ignorados.
        runner.start(QStringLiteral("bash -c \"trap '' HUP TERM; echo \\$\\$ > %1; exec sleep 300\"; echo fim")
                         .arg(pidPath),
                     QString(), {});
        QVERIFY(runner.isRunning());

        qint64 jobPid = 0;
        for (int i = 0; i < 40 && jobPid <= 0; ++i) {
            QTest::qWait(100);
            QFile pidFile(pidPath);
            if (pidFile.open(QIODevice::ReadOnly)) {
                jobPid = pidFile.readAll().trimmed().toLongLong();
            }
        }
        QVERIFY2(jobPid > 0, "o job não gravou o PID");
        QCOMPARE(::kill(static_cast<pid_t>(jobPid), 0), 0); // vivo antes do stop

        runner.stop();
        QTest::qWait(4000);

        QCOMPARE(::kill(static_cast<pid_t>(jobPid), 0), -1);
        QFile::remove(pidPath);
#endif
    }

    // Background parado pelo usuário: lastRunStoppedByRequest() é verdadeiro
    // (a UI não notifica "caiu"); um exit != 0 próprio não é.
    void lastRunStoppedByRequestDistinguishesStopFromRealFailure()
    {
        ProcessManager manager;

        auto stopped = std::make_unique<ProcessRunner>();
        ProcessRunner *rawStopped = stopped.get();
        manager.track(QStringLiteral("cmd_stopped"), std::move(stopped));
        QCoreApplication::processEvents();
        rawStopped->start(QStringLiteral("sleep 30"), QString(), {});
        QVERIFY(rawStopped->isRunning());
        rawStopped->stop();
        QTRY_VERIFY_WITH_TIMEOUT(manager.statusOf(QStringLiteral("cmd_stopped")) != ProcessStatus::Running, 10000);
        QVERIFY(manager.lastRunStoppedByRequest(QStringLiteral("cmd_stopped")));

        auto failing = std::make_unique<ProcessRunner>();
        ProcessRunner *rawFailing = failing.get();
        manager.track(QStringLiteral("cmd_failing"), std::move(failing));
        QCoreApplication::processEvents();
        rawFailing->start(QStringLiteral("exit 2"), QString(), {});
        QTRY_VERIFY_WITH_TIMEOUT(manager.statusOf(QStringLiteral("cmd_failing")) != ProcessStatus::Running, 5000);
        QVERIFY(!manager.lastRunStoppedByRequest(QStringLiteral("cmd_failing")));
    }

    // lastRunCrashed() (notificações — feature nova): ProcessRunner já
    // distinguia QProcess::CrashExit de um exitCode != 0 comum
    // (ProcessResult::crashed), mas isso se perdia ao virar só
    // ProcessStatus::Error. Cobre os dois casos: saída normal com erro
    // (exit 1) NÃO é crash; término por sinal (o mesmo cenário de
    // backgroundProcessTerminatedViaStopDoesNotCrash, "SIGTERM ->
    // exitStatus=CrashExit no Qt") É crash.
    void lastRunCrashedDistinguishesCrashFromPlainExitError()
    {
        ProcessManager manager;

        // Caso 1: saída normal com código de erro — não é crash.
        auto plainErrorRunner = std::make_unique<ProcessRunner>();
        ProcessRunner *rawPlainErrorRunner = plainErrorRunner.get();
        manager.track(QStringLiteral("cmd_plain_error"), std::move(plainErrorRunner));
        QCoreApplication::processEvents();
        rawPlainErrorRunner->start(QStringLiteral("exit 1"), QString(), {});
        QTest::qWait(2000);
        QCoreApplication::processEvents();
        QCOMPARE(manager.statusOf(QStringLiteral("cmd_plain_error")), ProcessStatus::Error);
        QVERIFY(!manager.lastRunCrashed(QStringLiteral("cmd_plain_error")));

        // Caso 2: terminado por sinal (stop() -> SIGTERM) — é crash.
        auto crashedRunner = std::make_unique<ProcessRunner>();
        ProcessRunner *rawCrashedRunner = crashedRunner.get();
        manager.track(QStringLiteral("cmd_crashed"), std::move(crashedRunner));
        QCoreApplication::processEvents();
        rawCrashedRunner->start(QStringLiteral("sleep 10"), QString(), {});
        QVERIFY(rawCrashedRunner->isRunning());
        rawCrashedRunner->stop();
        QTest::qWait(3000);
        QCoreApplication::processEvents();
        QCOMPARE(manager.statusOf(QStringLiteral("cmd_crashed")), ProcessStatus::Error);
        QVERIFY(manager.lastRunCrashed(QStringLiteral("cmd_crashed")));

        // Comando nunca rastreado: fallback seguro, nunca crash.
        QVERIFY(!manager.lastRunCrashed(QStringLiteral("cmd_never_tracked")));
    }
};

QTEST_MAIN(TestProcessManager)
#include "test_process_manager.moc"
