#include <QTest>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "cli/app-delegate.h"
#include "cli/cli-app-verbs.h"
#include "ipc/ipc-server.h"

using namespace kai;

// `kai raise` e `kai import`: argumentos, resolução do arquivo e o pedido
// chegando ao app (um IpcServer real num socket exclusivo faz o papel dele).
class TestCliAppVerbs : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("KAI_IPC_SOCKET_NAME_OVERRIDE",
                QByteArray("kai-test-verbs-") + QByteArray::number(QCoreApplication::applicationPid()));
        qputenv("KAI_CLI_NO_APP_LAUNCH", "1");
    }

    void raiseAcceptsSpacedAndEqualsForms()
    {
        const cli::RaiseRequest a = cli::parseRaiseArgs({QStringLiteral("--level"), QStringLiteral("error"),
                                                         QStringLiteral("deu"), QStringLiteral("pau")});
        QVERIFY(a.error.isEmpty());
        QCOMPARE(a.level, QStringLiteral("error"));
        QCOMPARE(a.message, QStringLiteral("deu pau"));

        const cli::RaiseRequest b = cli::parseRaiseArgs({QStringLiteral("--level=WARNING"),
                                                         QStringLiteral("--title=Deploy"), QStringLiteral("lento")});
        QVERIFY(b.error.isEmpty());
        QCOMPARE(b.level, QStringLiteral("warning"));
        QCOMPARE(b.title, QStringLiteral("Deploy"));

        const cli::RaiseRequest c = cli::parseRaiseArgs({QStringLiteral("so a mensagem")});
        QCOMPARE(c.level, QStringLiteral("info"));
        QVERIFY(c.error.isEmpty());
    }

    void raiseRejectsUnknownLevelOrEmptyMessage()
    {
        QVERIFY(!cli::parseRaiseArgs({QStringLiteral("--level"), QStringLiteral("fatal"), QStringLiteral("x")}).error.isEmpty());
        QVERIFY(!cli::parseRaiseArgs({QStringLiteral("--level"), QStringLiteral("error")}).error.isEmpty());
    }

    void importFindsTheProjectFileInAFolderOrUsesTheGivenFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile yml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(yml.open(QIODevice::WriteOnly));
        yml.write("project_name: X\n");
        yml.close();

        // Sem argumento = pasta atual.
        QCOMPARE(cli::resolveImportTarget(QString(), dir.path()), QFileInfo(yml).absoluteFilePath());
        QCOMPARE(cli::resolveImportTarget(QStringLiteral("kai.yml"), dir.path()), QFileInfo(yml).absoluteFilePath());
        QTemporaryDir empty;
        QVERIFY(cli::resolveImportTarget(QString(), empty.path()).isEmpty());
    }

    void importAcceptsTheFileAndAnOnlyList()
    {
        const cli::ImportRequest plain = cli::parseImportArgs({});
        QVERIFY(plain.error.isEmpty() && plain.argument.isEmpty() && plain.only.isEmpty());

        const cli::ImportRequest a = cli::parseImportArgs({QStringLiteral("pack.yml"), QStringLiteral("--only"), QStringLiteral("commands, actions")});
        QVERIFY(a.error.isEmpty());
        QCOMPARE(a.argument, QStringLiteral("pack.yml"));
        QCOMPARE(a.only, (QStringList{QStringLiteral("commands"), QStringLiteral("actions")}));

        const cli::ImportRequest b = cli::parseImportArgs({QStringLiteral("--only=collections"), QStringLiteral("dir")});
        QVERIFY(b.error.isEmpty());
        QCOMPARE(b.argument, QStringLiteral("dir"));
        QCOMPARE(b.only, QStringList{QStringLiteral("collections")});

        QVERIFY(!cli::parseImportArgs({QStringLiteral("--only")}).error.isEmpty());
        QVERIFY(!cli::parseImportArgs({QStringLiteral("--only=")}).error.isEmpty());
        QVERIFY(!cli::parseImportArgs({QStringLiteral("--wat")}).error.isEmpty());
        QVERIFY(!cli::parseImportArgs({QStringLiteral("a"), QStringLiteral("b")}).error.isEmpty());
    }

    void raiseAndImportReachTheApp()
    {
        ipc::IpcServer server;
        QVERIFY(server.start());
        QString gotLevel, gotMessage, gotPath;
        QStringList gotOnly;
        connect(&server, &ipc::IpcServer::raiseRequested, this,
            [&](const QString &level, const QString &, const QString &message, bool &ok, QString &) {
                gotLevel = level;
                gotMessage = message;
                ok = true;
            });
        connect(&server, &ipc::IpcServer::importRequested, this,
            [&](const QString &path, const QStringList &only, bool &ok, QString &reply) {
                gotPath = path;
                gotOnly = only;
                ok = true;
                reply = QStringLiteral("importado");
            });

        QJsonObject raise;
        raise[QStringLiteral("cmd")] = QStringLiteral("raise");
        raise[QStringLiteral("level")] = QStringLiteral("error");
        raise[QStringLiteral("message")] = QStringLiteral("deu pau");
        const std::optional<QJsonObject> raiseReply = cli::requestApp(raise, 5000);
        QVERIFY(raiseReply.has_value());
        QVERIFY(raiseReply->value(QStringLiteral("ok")).toBool());
        QCOMPARE(gotLevel, QStringLiteral("error"));
        QCOMPARE(gotMessage, QStringLiteral("deu pau"));

        QJsonObject import;
        import[QStringLiteral("cmd")] = QStringLiteral("import");
        import[QStringLiteral("path")] = QStringLiteral("/tmp/proj/kai.yml");
        const std::optional<QJsonObject> importReply = cli::requestApp(import, 5000);
        QVERIFY(importReply.has_value());
        QCOMPARE(importReply->value(QStringLiteral("message")).toString(), QStringLiteral("importado"));
        QCOMPARE(gotPath, QStringLiteral("/tmp/proj/kai.yml"));
        QVERIFY(gotOnly.isEmpty());

        import[QStringLiteral("only")] = QJsonArray{QStringLiteral("commands"), QStringLiteral("actions")};
        QVERIFY(cli::requestApp(import, 5000).has_value());
        QCOMPARE(gotOnly, (QStringList{QStringLiteral("commands"), QStringLiteral("actions")}));
    }
};

QTEST_MAIN(TestCliAppVerbs)
#include "test_cli_app_verbs.moc"
