#include <QTest>
#include <QCoreApplication>

#include "cli/app-delegate.h"
#include "cli/terminal-input.h"
#include "ipc/ipc-server.h"
#include "ipc/stream-channel.h"

using namespace kai;

// Execução GLOBAL delegada ao app (`kai -g <path>`): o CLI pede ao app pelo
// IPC e espelha a saída/código de saída. Aqui um IpcServer real faz o papel
// do app, num socket exclusivo do teste.
class TestAppDelegate : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("KAI_IPC_SOCKET_NAME_OVERRIDE",
                QByteArray("kai-test-delegate-") + QByteArray::number(QCoreApplication::applicationPid()));
        qputenv("KAI_CLI_NO_APP_LAUNCH", "1");
    }

    void delegatesToTheRunningAppAndReturnsItsExitCode()
    {
        ipc::IpcServer server;
        QVERIFY(server.start());
        QString receivedId;
        QMap<QString, QString> receivedParams;
        connect(&server, &ipc::IpcServer::runStreamRequested, this,
            [&](const QString &commandId, const QMap<QString, QString> &params, bool, bool, bool,
                ipc::StreamChannel *channel) {
                receivedId = commandId;
                receivedParams = params;
                channel->sendOutput(QStringLiteral("rodando no app\r\n"), false);
                channel->sendFinished(3, QString());
            });

        core::Command command;
        command.id = QStringLiteral("c_dev_up");
        command.name = QStringLiteral("Subir");
        const std::optional<int> exitCode = cli::runViaApp(command, {{QStringLiteral("servico"), QStringLiteral("api")}});

        QVERIFY(exitCode.has_value());
        QCOMPARE(*exitCode, 3);
        QCOMPARE(receivedId, QStringLiteral("c_dev_up"));
        QCOMPARE(receivedParams.value(QStringLiteral("servico")), QStringLiteral("api"));
    }

    void refusalFromTheAppIsAUsageError()
    {
        ipc::IpcServer server;
        QVERIFY(server.start());
        connect(&server, &ipc::IpcServer::runStreamRequested, this,
            [](const QString &, const QMap<QString, QString> &, bool, bool, bool, ipc::StreamChannel *channel) {
                channel->sendFinished(2, QStringLiteral("já está rodando"));
            });
        core::Command command;
        command.id = QStringLiteral("c_x");
        const std::optional<int> exitCode = cli::runViaApp(command, {});
        QVERIFY(exitCode.has_value());
        QCOMPARE(*exitCode, 2);
    }

    // `kai -gd`: o pedido chega marcado como destacado e o app só confirma
    // (finished 0) — o CLI não espera o comando terminar.
    void detachedRunOnlyWaitsForTheAcceptance()
    {
        ipc::IpcServer server;
        QVERIFY(server.start());
        bool receivedDetached = false;
        bool receivedNotify = false;
        connect(&server, &ipc::IpcServer::runStreamRequested, this,
            [&](const QString &, const QMap<QString, QString> &, bool detached, bool notify, bool,
                ipc::StreamChannel *channel) {
                receivedDetached = detached;
                receivedNotify = notify;
                channel->sendFinished(0);
            });
        core::Command command;
        command.id = QStringLiteral("c_long");
        command.name = QStringLiteral("Longo");
        const std::optional<int> exitCode = cli::runViaApp(command, {}, /*detached=*/true, /*notify=*/true);
        QVERIFY(exitCode.has_value());
        QCOMPARE(*exitCode, 0);
        QVERIFY(receivedDetached);
        QVERIFY(receivedNotify); // -gdn: quem avisa no fim é o app
    }

    // `kai -gw`: o pedido chega marcado pra abrir a janela de saída; sem -w
    // não vai marcado.
    void windowFlagTravelsToTheApp()
    {
        ipc::IpcServer server;
        QVERIFY(server.start());
        QVector<bool> received;
        connect(&server, &ipc::IpcServer::runStreamRequested, this,
            [&](const QString &, const QMap<QString, QString> &, bool, bool, bool window,
                ipc::StreamChannel *channel) {
                received << window;
                channel->sendFinished(0);
            });
        core::Command command;
        command.id = QStringLiteral("c_win");
        command.name = QStringLiteral("Janela");
        QVERIFY(cli::runViaApp(command, {}, /*detached=*/false, /*notify=*/false, /*window=*/true).has_value());
        QVERIFY(cli::runViaApp(command, {}).has_value());
        QCOMPARE(received, (QVector<bool>{true, false}));
    }

    // Sem app (e proibido de subir um): quem chama cai no fallback.
    void noAppMeansNoDelegation()
    {
        core::Command command;
        command.id = QStringLiteral("c_x");
        QVERIFY(!cli::runViaApp(command, {}).has_value());
    }

    // kai.exe via WSL: o tty do Linux já ecoou o que foi digitado; o eco do
    // PTY do comando precisa sumir, senão cada resposta aparece duas vezes.
    void echoOfTypedInputIsStrippedOnce()
    {
        QString pending = QStringLiteral("n\n");
        QCOMPARE(cli::stripPendingEcho(QStringLiteral("n\r\nresposta=n\r\n"), pending),
                 QStringLiteral("resposta=n\r\n"));
        QVERIFY(pending.isEmpty());
    }

    void echoSplitAcrossChunksAndControlSequencesAreHandled()
    {
        const QString esc(QChar(0x1b));
        QString pending = QStringLiteral("sim\n");
        QString out = cli::stripPendingEcho(QStringLiteral("si"), pending);
        out += cli::stripPendingEcho(esc + QStringLiteral("[?25hm\r\nok"), pending);
        QCOMPARE(out, esc + QStringLiteral("[?25hok"));
        QVERIFY(pending.isEmpty());
    }

    void outputThatIsNotTheEchoIsNeverEaten()
    {
        QString pending = QStringLiteral("n\n");
        QCOMPARE(cli::stripPendingEcho(QStringLiteral("no such file\r\n"), pending),
                 QStringLiteral("no such file\r\n"));
        QVERIFY(pending.isEmpty());
    }
};

QTEST_MAIN(TestAppDelegate)
#include "test_app_delegate.moc"
