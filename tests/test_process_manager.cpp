#include <QTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QFile>
#include <csignal>

#include "engine/process-manager.h"

using namespace kai::engine;

// Regressão: comando background finalizado via stop() (SIGTERM) dava segfault ao emitir statusChanged() de forma
// síncrona na pilha de ProcessRunner::finished, e a UI (MainWindow -> ProcessListDialog) reentrava durante a
// notificação. Corrigido com QMetaObject::invokeMethod(Qt::QueuedConnection).
class TestProcessManager : public QObject {
    Q_OBJECT

private slots:
    // Background iniciado e encerrado via stop() (SIGTERM) gera exitCode != 0 / crashed=true; deve emitir
    // statusChanged sem crashar, mesmo com vários listeners reagindo.
    void backgroundProcessTerminatedViaStopDoesNotCrash()
    {
        ProcessManager manager;
        QSignalSpy statusSpy(&manager, &ProcessManager::statusChanged);

        auto runner = std::make_unique<ProcessRunner>();
        ProcessRunner *rawRunner = runner.get();

        // Simula reentrância: um listener de statusChanged consulta o ProcessManager de volta (como
        // ProcessListDialog::refreshProcessList).
        int reentrantCallCount = 0;
        connect(&manager, &ProcessManager::statusChanged, &manager,
                [&manager, &reentrantCallCount](const QString &commandId, ProcessStatus) {
                    ++reentrantCallCount;
                    manager.trackedCommandIds();
                    manager.statusOf(commandId);
                });

        manager.track(QStringLiteral("cmd_bg"), std::move(runner));

        rawRunner->start(QStringLiteral("sleep 10"), QString(), {});
        QVERIFY(rawRunner->isRunning());

        rawRunner->stop();

        // Espera o processo finalizar e o evento assíncrono de statusChanged (QueuedConnection precisa do event loop).
        QTest::qWait(3000);
        QCoreApplication::processEvents();

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

        // A emissão é assíncrona (QueuedConnection): só chega após o event loop processar.
        QCOMPARE(statusSpy.count(), 0);

        QCoreApplication::processEvents();
        QCOMPARE(statusSpy.count(), 1);
        QCOMPARE(qvariant_cast<ProcessStatus>(statusSpy.at(0).at(1)), ProcessStatus::Running);
    }

    // Regressão: não dava para rodar/rebootar um comando depois de pará-lo. O ProcessManager guarda o último processo
    // do commandId até um novo track(); o bug estava em MainWindow::handleCommandActivated, que checava só isTracked()
    // sem ver se o status ainda era Running. Aqui: track() de um novo ProcessRunner para o mesmo commandId, depois do
    // anterior ter terminado, reflete o novo processo em statusOf.
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

        // "Reboot": novo ProcessRunner sob o mesmo commandId substitui a entrada antiga.
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

    // Regressão: matar só o PID do bash não encerrava os filhos gerados pelo script. O bash gera um `sleep` em
    // background e stop() deve encerrar ambos.
    void stopKillsChildProcessesSpawnedByScript()
    {
#if defined(Q_OS_WIN)
        QSKIP("Teste específico de grupos de processo POSIX (setsid/killpg); "
              "no Windows o encerramento usa QProcess::terminate/kill.");
#else
        ProcessRunner runner;
        // O `&` faz o `sleep 30` rodar como filho do bash, no mesmo grupo de processos (sem `setsid`/`disown`).
        runner.start(QStringLiteral("sleep 30 & child_pid=$!; echo $child_pid > /tmp/kai_test_child_pid; wait"),
                     QString(), {});
        QVERIFY(runner.isRunning());
        QTest::qWait(300);

        QFile pidFile(QStringLiteral("/tmp/kai_test_child_pid"));
        QVERIFY(pidFile.open(QIODevice::ReadOnly));
        const qint64 childPid = pidFile.readAll().trimmed().toLongLong();
        pidFile.close();
        QVERIFY(childPid > 0);

        runner.stop();
        QTest::qWait(3000);

        // ::kill(pid, 0) retorna 0 se o processo existe, -1 (ESRCH) se terminou, sem enviar sinal.
        const int stillAlive = ::kill(static_cast<pid_t>(childPid), 0);
        QCOMPARE(stillAlive, -1);

        QFile::remove(QStringLiteral("/tmp/kai_test_child_pid"));
#endif
    }

    // Regressão: "Parar não encerra, mas Ctrl+C encerra". Sob PTY com `bash -i` (job control) o comando roda num grupo
    // PRÓPRIO; SIGTERM/SIGKILL no grupo do shell não o alcança, e o job aqui ignora SIGHUP/SIGTERM. Só Ctrl+C (SIGINT)
    // ou SIGKILL na sessão inteira o encerram: stop() precisa fazer isso.
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
        // O `;` depois do job impede o bash de fazer exec direto: o job vira filho em grupo próprio. `exec sleep` herda os
        // sinais ignorados.
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
        QCOMPARE(::kill(static_cast<pid_t>(jobPid), 0), 0);

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

    // lastRunCrashed(): ProcessRunner distingue QProcess::CrashExit de exitCode != 0 (ProcessResult::crashed), o que
    // se perdia ao virar ProcessStatus::Error. Saída normal com erro (exit 1) não é crash; término por sinal é.
    void lastRunCrashedDistinguishesCrashFromPlainExitError()
    {
        ProcessManager manager;

        auto plainErrorRunner = std::make_unique<ProcessRunner>();
        ProcessRunner *rawPlainErrorRunner = plainErrorRunner.get();
        manager.track(QStringLiteral("cmd_plain_error"), std::move(plainErrorRunner));
        QCoreApplication::processEvents();
        rawPlainErrorRunner->start(QStringLiteral("exit 1"), QString(), {});
        QTest::qWait(2000);
        QCoreApplication::processEvents();
        QCOMPARE(manager.statusOf(QStringLiteral("cmd_plain_error")), ProcessStatus::Error);
        QVERIFY(!manager.lastRunCrashed(QStringLiteral("cmd_plain_error")));

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

        QVERIFY(!manager.lastRunCrashed(QStringLiteral("cmd_never_tracked")));
    }
};

QTEST_MAIN(TestProcessManager)
#include "test_process_manager.moc"
