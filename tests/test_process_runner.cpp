#include <QDir>
#include <QFile>
#include <QTest>
#include <QSignalSpy>

#include "engine/process-runner.h"

using namespace kai::engine;

class TestProcessRunner : public QObject {
    Q_OBJECT

private slots:
    void echoCommandProducesExpectedOutputAndExitCode()
    {
        ProcessRunner runner;
        QSignalSpy outputSpy(&runner, &ProcessRunner::outputReady);
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("echo hello-kai"), QString(), {});

        QVERIFY(finishedSpy.wait(5000));
        QCOMPARE(finishedSpy.count(), 1);

        const ProcessResult result = qvariant_cast<ProcessResult>(finishedSpy.at(0).at(0));
        QCOMPARE(result.exitCode, 0);
        QVERIFY(!result.crashed);

        QVERIFY(outputSpy.count() >= 1);
        QString combinedOutput;
        for (const QList<QVariant> &call : outputSpy) {
            combinedOutput += call.at(0).toString();
        }
        QVERIFY(combinedOutput.contains(QStringLiteral("hello-kai")));
    }

    // Quem ignora Ctrl+C/SIGTERM, ou saiu da sessão com setsid, ainda é morto depois do tempo de graça, mesmo que o
    // runner seja destruído antes.
    void stopKillsStubbornDescendantsEvenIfRunnerIsDestroyed()
    {
#if defined(Q_OS_LINUX)
        const QString marker = QStringLiteral("kai-stubborn-%1").arg(QTest::currentTestFunction());
        const auto alive = [&marker]() {
            for (const QString &entry : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                QFile cmd(QStringLiteral("/proc/%1/cmdline").arg(entry));
                if (cmd.open(QIODevice::ReadOnly) && cmd.readAll().contains(marker.toUtf8())) {
                    QFile stat(QStringLiteral("/proc/%1/stat").arg(entry));
                    if (stat.open(QIODevice::ReadOnly) && !stat.readAll().contains(") Z")) {
                        return true;
                    }
                }
            }
            return false;
        };
        {
            auto *runner = new ProcessRunner;
            runner->setKillTimeoutMs(600);
            runner->start(QStringLiteral("setsid bash -c 'trap \"\" INT TERM; exec -a %1 sleep 30' & sleep 30").arg(marker),
                          QString(), {});
            QTRY_VERIFY_WITH_TIMEOUT(alive(), 3000);
            runner->stop();
            delete runner; // os timers do runner morrem aqui; a escalada tem de continuar sozinha
        }
        QTRY_VERIFY_WITH_TIMEOUT(!alive(), 5000);
#else
        QSKIP("Linux only");
#endif
    }

    void injectedEnvironmentVariableIsVisibleToProcess()
    {
        ProcessRunner runner;
        QSignalSpy outputSpy(&runner, &ProcessRunner::outputReady);
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        QMap<QString, QString> env;
        env["KAI_TEST_VAR"] = QStringLiteral("valor-injetado");

        runner.start(QStringLiteral("echo $KAI_TEST_VAR"), QString(), env);

        QVERIFY(finishedSpy.wait(5000));

        QString combinedOutput;
        for (const QList<QVariant> &call : outputSpy) {
            combinedOutput += call.at(0).toString();
        }
        QVERIFY(combinedOutput.contains(QStringLiteral("valor-injetado")));
    }

    // Diretório de trabalho que não existe: o comando roda no diretório atual e a saída não ganha nenhum aviso.
    void aMissingWorkingDirectoryRunsAnywayWithoutAWarning()
    {
        ProcessRunner runner;
        QSignalSpy outputSpy(&runner, &ProcessRunner::outputReady);
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);
        runner.start(QStringLiteral("echo still-ran"), QStringLiteral("/nao/existe/mesmo"), {});
        QVERIFY(finishedSpy.wait(5000));
        QString combinedOutput;
        for (const QList<QVariant> &call : outputSpy) {
            combinedOutput += call.at(0).toString();
        }
        QVERIFY(combinedOutput.contains(QStringLiteral("still-ran")));
        QVERIFY2(!combinedOutput.contains(QStringLiteral("kai:")) && !combinedOutput.contains(QStringLiteral("nao/existe")),
                 qPrintable(combinedOutput));
    }

    void nonZeroExitCodeIsReportedCorrectly()
    {
        ProcessRunner runner;
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("exit 7"), QString(), {});

        QVERIFY(finishedSpy.wait(5000));
        const ProcessResult result = qvariant_cast<ProcessResult>(finishedSpy.at(0).at(0));
        QCOMPARE(result.exitCode, 7);
    }

    // progressão de múltiplas linhas ao longo do tempo (loop com sleep).
    void multiStepLoopEmitsProgressiveOutput()
    {
        ProcessRunner runner;
        QSignalSpy outputSpy(&runner, &ProcessRunner::outputReady);
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("for i in 1 2 3; do echo \"Step $i\"; sleep 0.2; done"), QString(), {});

        QVERIFY(finishedSpy.wait(5000));

        QString combinedOutput;
        for (const QList<QVariant> &call : outputSpy) {
            combinedOutput += call.at(0).toString();
        }
        QVERIFY(combinedOutput.contains(QStringLiteral("Step 1")));
        QVERIFY(combinedOutput.contains(QStringLiteral("Step 2")));
        QVERIFY(combinedOutput.contains(QStringLiteral("Step 3")));
    }

    // writeToStdin alimenta o stdin de um processo em execução (interação real, não só leitura de output).
    void writeToStdinDeliversTextToRunningProcess()
    {
        ProcessRunner runner;
        QSignalSpy outputSpy(&runner, &ProcessRunner::outputReady);
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("read line; echo \"Recebido: $line\""), QString(), {});
        QVERIFY(runner.isRunning());

        runner.writeToStdin(QStringLiteral("ola-do-teste"));

        QVERIFY(finishedSpy.wait(5000));

        QString combinedOutput;
        for (const QList<QVariant> &call : outputSpy) {
            combinedOutput += call.at(0).toString();
        }
        QVERIFY(combinedOutput.contains(QStringLiteral("Recebido: ola-do-teste")));
    }

    void writeToStdinOnStoppedProcessIsNoOp()
    {
        ProcessRunner runner;
        runner.writeToStdin(QStringLiteral("nao deveria crashar"));
        QVERIFY(!runner.isRunning());
    }

    // writeRaw envia bytes crus sem acrescentar '\n'. Ctrl+D (\x04) sob PTY sinaliza EOF: um `cat` sem argumento lê
    // até o EOF e encerra; sem ele ficaria bloqueado.
    void ctrlDSendsEofAndEndsCat()
    {
        ProcessRunner runner;
        runner.setUsePty(true);
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("cat"), QString(), {});
        QVERIFY(runner.isRunning());

        runner.writeToStdin(QStringLiteral("linha-de-teste"));
        QTest::qWait(200);
        runner.writeRaw(QStringLiteral("\x04"));

        QVERIFY2(finishedSpy.wait(5000), "cat não encerrou após Ctrl+D (EOF)");
    }

    // read -p imprime o prompt só quando stdin é um tty: com PTY ligado o prompt aparece na saída.
    void readPromptAppearsUnderPty()
    {
        ProcessRunner runner;
        runner.setUsePty(true);
        QSignalSpy outputSpy(&runner, &ProcessRunner::outputReady);
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("read -p 'Confirma? ' ans; echo \"resp=$ans\""), QString(), {});
        QVERIFY(runner.isRunning());
        QTest::qWait(300);
        runner.writeToStdin(QStringLiteral("sim"));
        QVERIFY(finishedSpy.wait(5000));

        QString combined;
        for (const QList<QVariant> &call : outputSpy) {
            combined += call.at(0).toString();
        }
        QVERIFY2(combined.contains(QStringLiteral("Confirma?")),
                 qPrintable(QStringLiteral("o prompt do read não apareceu sob PTY -> [%1]").arg(combined)));
        QVERIFY(combined.contains(QStringLiteral("resp=sim")));
    }

    // Regressão: `kai <cli_path>` via kai.exe chamado do WSL falhava com "A sintaxe do comando está incorreta.": o cwd
    // vem do Qt com barras normais ("//wsl.localhost/...") e o cmd.exe rejeita esse formato no pushd, que precisa de
    // contrabarras.
    void uncWorkingDirWithForwardSlashesIsPushedWithBackslashes()
    {
        const QString wrapped = wrapWindowsCommandForUncWorkingDir(
            QStringLiteral("docker compose up -d"),
            QStringLiteral("//wsl.localhost/Ubuntu/home/corin/projects/kai"));
        QCOMPARE(wrapped,
                 QStringLiteral("pushd \"\\\\wsl.localhost\\Ubuntu\\home\\corin\\projects\\kai\" "
                                "&& (docker compose up -d) & popd"));
    }

    void uncDetectionAcceptsBothSeparators()
    {
        QVERIFY(isWindowsUncPath(QStringLiteral("//wsl.localhost/Ubuntu/home")));
        QVERIFY(isWindowsUncPath(QStringLiteral("\\\\wsl$\\Ubuntu\\home")));
        QVERIFY(!isWindowsUncPath(QStringLiteral("C:/Users/corin")));
        QVERIFY(!isWindowsUncPath(QStringLiteral("/home/corin")));
    }

    // Regressão: `kai -g oi` via WSL com alvo WSL dava pushd no cwd UNC do kai.exe, o wsl.exe nascia em Z:\home\corin e
    // falhava com "wsl: Failed to translate". Com alvo, o `cd` é do próprio comando.
    void targetHandlingCwdSkipsPushdAndAvoidsUncBirthDir()
    {
        const WindowsLaunchDirs dirs = planWindowsLaunchDirs(
            QString(), QStringLiteral("//wsl.localhost/Ubuntu/home/corin"), true, QStringLiteral("C:\\Kai"));
        QCOMPARE(dirs.pushdDir, QString());
        QCOMPARE(dirs.nativeCwd, QStringLiteral("C:\\Kai"));

        const WindowsLaunchDirs normal = planWindowsLaunchDirs(
            QString(), QStringLiteral("C:/Users/corin"), true, QStringLiteral("C:\\Kai"));
        QCOMPARE(normal.pushdDir, QString());
        QCOMPARE(normal.nativeCwd, QString()); // herda um cwd que já é seguro
    }

    void localCommandInUncCwdStillUsesPushd()
    {
        const WindowsLaunchDirs dirs = planWindowsLaunchDirs(
            QString(), QStringLiteral("//wsl.localhost/Ubuntu/home/corin"), false, QStringLiteral("C:\\Kai"));
        QCOMPARE(dirs.pushdDir, QStringLiteral("//wsl.localhost/Ubuntu/home/corin"));
        QCOMPARE(dirs.nativeCwd, QStringLiteral("C:\\Kai"));

        const WindowsLaunchDirs plain = planWindowsLaunchDirs(
            QStringLiteral("D:/proj"), QStringLiteral("//wsl.localhost/Ubuntu"), false, QStringLiteral("C:\\Kai"));
        QCOMPARE(plain.pushdDir, QString());
        QCOMPARE(plain.nativeCwd, QStringLiteral("D:/proj"));
    }

    void nonUncWorkingDirLeavesCommandUntouched()
    {
        QCOMPARE(wrapWindowsCommandForUncWorkingDir(QStringLiteral("dir"), QStringLiteral("C:/work")),
                 QStringLiteral("dir"));
        QCOMPARE(wrapWindowsCommandForUncWorkingDir(QStringLiteral("dir"), QString()),
                 QStringLiteral("dir"));
    }
};

QTEST_MAIN(TestProcessRunner)
#include "test_process_runner.moc"
