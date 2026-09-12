#include <QTest>
#include <QSignalSpy>
#include <QTimer>

#include "engine/process-runner.h"

using namespace kai::engine;

// Cobre a correção do bug reportado: "se eu fizer um cmd que read, não
// consigo ver o output do read na pergunta". Causa raiz: `read -p` do bash
// só imprime o prompt quando stdin é um terminal (tty); sob o pipe do
// QProcess o prompt some. A correção roda o comando shell dentro de um
// pseudo-terminal (forkpty), fazendo o processo enxergar um tty — e o
// prompt aparece na saída.
class TestReadPromptPty : public QObject {
    Q_OBJECT

private slots:
    void readPromptIsVisibleUnderPty()
    {
#if defined(Q_OS_WIN)
        QSKIP("PTY (forkpty) é específico de Unix; no Windows o ProcessRunner "
              "usa QProcess sem pseudo-terminal.");
#else
        ProcessRunner runner; // PTY ligado por padrão
        QString out;
        connect(&runner, &ProcessRunner::outputReady, [&](const QString &t, bool) { out += t; });

        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("read -p 'Digite algo: ' x; echo \"recebido: $x\""), QString(), {});

        // Responde após o processo bloquear no read.
        QTimer::singleShot(400, [&]() { runner.writeToStdin(QStringLiteral("KaiResposta")); });

        QVERIFY(QTest::qWaitFor([&]() { return finishedSpy.count() > 0; }, 5000));

        // O PROMPT do read deve aparecer na saída (era isto que sumia).
        QVERIFY2(out.contains(QStringLiteral("Digite algo:")),
                 "O prompt do 'read -p' deve aparecer na saída sob PTY.");
        // E a resposta deve ter chegado ao comando.
        QVERIFY(out.contains(QStringLiteral("recebido: KaiResposta")));
#endif
    }

    void nonInteractiveCommandStillWorksUnderPty()
    {
#if defined(Q_OS_WIN)
        QSKIP("PTY é específico de Unix.");
#else
        // Comando simples (não interativo) também funciona sob PTY e
        // reporta exitCode corretamente.
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
        // Com PTY desligado, o motor usa o caminho QProcess (bash -c) e
        // ainda executa comandos normais — garante o fallback seguro.
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
