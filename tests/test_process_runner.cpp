#include <QTest>
#include <QSignalSpy>

#include "engine/process-runner.h"

using namespace kai::engine;

// Execução de Comando Shell Assíncrono com Log em Tempo Real.
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

    // Valida injeção de ambiente via QProcessEnvironment.
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

    // Terminal interativo: writeToStdin deve alimentar o stdin de
    // um processo em execução, permitindo interação real, não apenas
    // leitura de output.
    void writeToStdinDeliversTextToRunningProcess()
    {
        ProcessRunner runner;
        QSignalSpy outputSpy(&runner, &ProcessRunner::outputReady);
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        // 'read' shell builtin lê uma linha de stdin e a repete via echo.
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
        // Nunca chamou start(): processo não está rodando.
        runner.writeToStdin(QStringLiteral("nao deveria crashar"));
        QVERIFY(!runner.isRunning());
    }

    // (2) writeRaw envia bytes CRUS sem acrescentar '\n'. Ctrl+D (\x04) sob
    // PTY sinaliza EOF: um `cat` sem argumento lê stdin até o EOF e então
    // encerra. Sem o EOF, `cat` ficaria bloqueado para sempre.
    void ctrlDSendsEofAndEndsCat()
    {
        ProcessRunner runner;
        runner.setUsePty(true);
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("cat"), QString(), {});
        QVERIFY(runner.isRunning());

        // Manda uma linha e depois EOF (Ctrl+D).
        runner.writeToStdin(QStringLiteral("linha-de-teste"));
        QTest::qWait(200);
        runner.writeRaw(QStringLiteral("\x04"));

        // cat deve encerrar após o EOF (sem travar).
        QVERIFY2(finishedSpy.wait(5000), "cat não encerrou após Ctrl+D (EOF)");
    }

    // read -p imprime o prompt SÓ quando stdin é um tty. Com PTY ligado, o
    // prompt aparece na saída (regressão do terminal interativo).
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
};

QTEST_MAIN(TestProcessRunner)
#include "test_process_runner.moc"
