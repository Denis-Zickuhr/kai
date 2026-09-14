#include <QTest>
#include <QSignalSpy>
#include <QTimer>

#include "engine/process-runner.h"

using namespace kai::engine;

// Regressão: a saída do `read -p` não aparecia. O bash só imprime o prompt quando stdin é um tty; o comando shell
// roda num pseudo-terminal (forkpty) para o processo enxergar um tty.
class TestReadPromptPty : public QObject {
    Q_OBJECT

private slots:
    void readPromptIsVisibleUnderPty()
    {
#if defined(Q_OS_WIN)
        QSKIP("PTY (forkpty) é específico de Unix; no Windows o ProcessRunner "
              "usa QProcess sem pseudo-terminal.");
#else
        ProcessRunner runner;
        QString out;
        connect(&runner, &ProcessRunner::outputReady, [&](const QString &t, bool) { out += t; });

        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("read -p 'Digite algo: ' x; echo \"recebido: $x\""), QString(), {});

        QTimer::singleShot(400, [&]() { runner.writeToStdin(QStringLiteral("KaiResposta")); });

        QVERIFY(QTest::qWaitFor([&]() { return finishedSpy.count() > 0; }, 5000));

        // O prompt do read aparece na saída (era isto que sumia).
        QVERIFY2(out.contains(QStringLiteral("Digite algo:")),
                 "O prompt do 'read -p' deve aparecer na saída sob PTY.");
        QVERIFY(out.contains(QStringLiteral("recebido: KaiResposta")));
#endif
    }

    void nonInteractiveCommandStillWorksUnderPty()
    {
#if defined(Q_OS_WIN)
        QSKIP("PTY é específico de Unix.");
#else
        // Comando simples também funciona sob PTY e reporta o exitCode.
        ProcessRunner runner;
        QString out;
        connect(&runner, &ProcessRunner::outputReady, [&](const QString &t, bool) { out += t; });
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("echo hello-kai"), QString(), {});
        QVERIFY(QTest::qWaitFor([&]() { return finishedSpy.count() > 0; }, 3000));

        const ProcessResult result = finishedSpy.first().at(0).value<ProcessResult>();
        QCOMPARE(result.exitCode, 0);
        QVERIFY(out.contains(QStringLiteral("hello-kai")));
#endif
    }

    void ptyCanBeDisabledFallingBackToQProcess()
    {
#if defined(Q_OS_WIN)
        QSKIP("Fallback testado com bash -c; no Windows o shell é cmd /c.");
#else
        // Com PTY desligado o motor usa o caminho QProcess (bash -c): fallback seguro.
        ProcessRunner runner;
        runner.setUsePty(false);
        QString out;
        connect(&runner, &ProcessRunner::outputReady, [&](const QString &t, bool) { out += t; });
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("echo fallback-ok"), QString(), {});
        QVERIFY(QTest::qWaitFor([&]() { return finishedSpy.count() > 0; }, 3000));

        const ProcessResult result = finishedSpy.first().at(0).value<ProcessResult>();
        QCOMPARE(result.exitCode, 0);
        QVERIFY(out.contains(QStringLiteral("fallback-ok")));
#endif
    }
};

QTEST_MAIN(TestReadPromptPty)
#include "test_read_prompt_pty.moc"
