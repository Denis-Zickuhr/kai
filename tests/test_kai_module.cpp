#include <QTest>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/ipc-endpoint.h"
#include "engine/command-language.h"
#include "ipc/ipc-server.h"
#include "kai-ipc-fixture.h"

using namespace kai;
using namespace kai::core;

// O módulo `kai` embutido (Python e Node) falando de verdade com um IpcServer:
// notify, env, commands, ps, kill, run, show e o fallback pela CLI.
class TestKaiModule : public QObject {
    Q_OBJECT

    static QString interpreter(CommandLanguage language)
    {
        return language == CommandLanguage::Python ? QStringLiteral("python3") : QStringLiteral("node");
    }

    // Roda `code` como um comando Python/Node, com o event loop livre (o servidor
    // roda NESTA thread). Devolve o stdout; `extraEnv` sobrepõe o ambiente.
    static QString runCode(CommandLanguage language, const QString &code,
                           const QProcessEnvironment &extraEnv = QProcessEnvironment(), int *exitCode = nullptr)
    {
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const auto defaults = engine::languageEnvDefaults(language);
        for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) env.insert(it.key(), it.value());
        for (const QString &key : extraEnv.keys()) env.insert(key, extraEnv.value(key));
        process.setProcessEnvironment(env);
        QSignalSpy finished(&process, &QProcess::finished);
        process.start(QStringLiteral("/bin/sh"),
                      {QStringLiteral("-c"), engine::buildInterpreterCommandLine(language, interpreter(language), code, false)});
        if (!finished.wait(20000)) {
            process.kill();
            return QStringLiteral("TIMEOUT");
        }
        if (exitCode) *exitCode = process.exitCode();
        return QString::fromUtf8(process.readAllStandardOutput()) + QString::fromUtf8(process.readAllStandardError());
    }

private slots:
    void endpointPointsAtTheSocketTheServerListensOn()
    {
        KaiIpcFixture fx;
        QVERIFY(fx.started);
        QVERIFY(QFile::exists(ipcEndpointPath()));
        QVERIFY(ipcEndpointPath().endsWith(ipcSocketName()));
    }

    void everyOperationReachesTheApp_data()
    {
        QTest::addColumn<int>("language");
        QTest::addColumn<QString>("code");
        QTest::newRow("python") << int(CommandLanguage::Python) << QStringLiteral(
            "import json, kai\n"
            "kai.notify('hello', title='T', level='warning')\n"
            "kai.show()\n"
            "out = {'commands': kai.commands(), 'envs': kai.env.list(), 'active': kai.env.active(),\n"
            "       'use': kai.env.use('Prod'), 'ps': kai.ps(), 'kill': kai.kill('api'),\n"
            "       'run': kai.run('Deploy'), 'import': kai.import_project('/p')}\n"
            "print(json.dumps(out))\n");
        QTest::newRow("node") << int(CommandLanguage::Node) << QStringLiteral(
            "const kai = require('kai');\n"
            "await kai.notify('hello', { title: 'T', level: 'warning' });\n"
            "await kai.show();\n"
            "console.log(JSON.stringify({ commands: await kai.commands(), envs: await kai.env.list(),\n"
            "  active: await kai.env.active(), use: await kai.env.use('Prod'), ps: await kai.ps(),\n"
            "  kill: await kai.kill('api'), run: await kai.run('Deploy'), import: await kai.importProject('/p') }));\n");
    }

    void everyOperationReachesTheApp()
    {
        QFETCH(int, language);
        QFETCH(QString, code);
        const auto lang = CommandLanguage(language);
        if (QStandardPaths::findExecutable(interpreter(lang)).isEmpty()) QSKIP("interpretador ausente");

        KaiIpcFixture fx;
        QVERIFY(fx.started);
        int exitCode = -1;
        const QString out = runCode(lang, code, QProcessEnvironment(), &exitCode);
        QCOMPARE(exitCode, 0);

        const QJsonObject json = QJsonDocument::fromJson(out.trimmed().toUtf8()).object();
        QVERIFY2(!json.isEmpty(), qPrintable(out));
        QCOMPARE(json.value("commands").toArray().at(0).toString(), QStringLiteral("Deploy"));
        QCOMPARE(json.value("envs").toArray().size(), 2);
        QCOMPARE(json.value("active").toString(), QStringLiteral("Dev"));
        QCOMPARE(json.value("use").toString(), QStringLiteral("active: Prod"));
        QCOMPARE(json.value("ps").toArray().at(0).toObject().value("pid").toInt(), 4242);
        QCOMPARE(json.value("kill").toString(), QStringLiteral("stopped api"));
        QCOMPARE(json.value("run").toString(), QStringLiteral("started Deploy"));
        QCOMPARE(json.value("import").toString(), QStringLiteral("imported"));

        QVERIFY(fx.calls.contains(QStringLiteral("raise|warning|T|hello")));
        QVERIFY(fx.calls.contains(QStringLiteral("show")));
        QVERIFY(fx.calls.contains(QStringLiteral("env-use|Prod")));
        QVERIFY(fx.calls.contains(QStringLiteral("kill|api")));
        QVERIFY(fx.calls.contains(QStringLiteral("run|Deploy")));
        QVERIFY(fx.calls.contains(QStringLiteral("import|/p")));
    }

    void aRefusedRequestRaisesKaiError_data()
    {
        QTest::addColumn<int>("language");
        QTest::addColumn<QString>("code");
        QTest::newRow("python") << int(CommandLanguage::Python) << QStringLiteral(
            "import kai\n"
            "try:\n"
            "    kai.env.use('Nope')\n"
            "except kai.Error as e:\n"
            "    print('ERR', e)\n");
        QTest::newRow("node") << int(CommandLanguage::Node) << QStringLiteral(
            "try { await kai.env.use('Nope'); } catch (e) { console.log('ERR', e.name, e.message); }\n");
    }

    void aRefusedRequestRaisesKaiError()
    {
        QFETCH(int, language);
        QFETCH(QString, code);
        const auto lang = CommandLanguage(language);
        if (QStandardPaths::findExecutable(interpreter(lang)).isEmpty()) QSKIP("interpretador ausente");
        KaiIpcFixture fx;
        const QString out = runCode(lang, code);
        QVERIFY2(out.contains(QStringLiteral("ERR")) && out.contains(QStringLiteral("unknown environment")), qPrintable(out));
    }

    void unreachableAppGivesAClearErrorWhenThereIsNoCli_data()
    {
        QTest::addColumn<int>("language");
        QTest::addColumn<QString>("code");
        QTest::newRow("python") << int(CommandLanguage::Python) << QStringLiteral(
            "import kai\ntry:\n    kai.notify('x')\nexcept kai.Error as e:\n    print('ERR', e)\n");
        QTest::newRow("node") << int(CommandLanguage::Node) << QStringLiteral(
            "try { await kai.notify('x'); } catch (e) { console.log('ERR', e.message); }\n");
    }

    void unreachableAppGivesAClearErrorWhenThereIsNoCli()
    {
        QFETCH(int, language);
        QFETCH(QString, code);
        const auto lang = CommandLanguage(language);
        if (QStandardPaths::findExecutable(interpreter(lang)).isEmpty()) QSKIP("interpretador ausente");
        QProcessEnvironment env;
        env.insert(QStringLiteral("KAI_IPC_SOCKET"), QStringLiteral("/tmp/kai-no-such-socket-xyz"));
        env.insert(QStringLiteral("KAI_EXE"), QString()); // sem CLI
        env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
        const QString out = runCode(lang, code, env);
        QVERIFY2(out.contains(QStringLiteral("ERR")) && out.contains(QStringLiteral("not reachable")), qPrintable(out));
    }

    // Script dentro do WSL sob um Kai do Windows: o socket não existe lá, mas a
    // CLI (kai.exe) existe — notify/show/run/env.use/kill vão por ela.
    void fallsBackToTheCliWhenTheSocketIsNotReachable_data()
    {
        QTest::addColumn<int>("language");
        QTest::addColumn<QString>("code");
        QTest::newRow("python") << int(CommandLanguage::Python) << QStringLiteral(
            "import kai\nkai.notify('via cli', title='T', level='error')\nkai.run('Deploy')\nkai.env.use('Prod')\n");
        QTest::newRow("node") << int(CommandLanguage::Node) << QStringLiteral(
            "await kai.notify('via cli', { title: 'T', level: 'error' });\nawait kai.run('Deploy');\nawait kai.env.use('Prod');\n");
    }

    void fallsBackToTheCliWhenTheSocketIsNotReachable()
    {
        QFETCH(int, language);
        QFETCH(QString, code);
        const auto lang = CommandLanguage(language);
        if (QStandardPaths::findExecutable(interpreter(lang)).isEmpty()) QSKIP("interpretador ausente");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(QStringLiteral("calls.log"));
        QFile fake(dir.filePath(QStringLiteral("kai")));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write(QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$*\" >> '%1'\necho done\n").arg(log).toUtf8());
        fake.close();
        fake.setPermissions(fake.permissions() | QFileDevice::ExeOwner);

        QProcessEnvironment env;
        env.insert(QStringLiteral("KAI_IPC_SOCKET"), QStringLiteral("/tmp/kai-no-such-socket-xyz"));
        env.insert(QStringLiteral("KAI_EXE"), fake.fileName());
        const QString out = runCode(lang, code, env);
        QFile calls(log);
        QVERIFY2(calls.open(QIODevice::ReadOnly), qPrintable(out));
        const QString text = QString::fromUtf8(calls.readAll());
        QVERIFY2(text.contains(QStringLiteral("raise --level error --title T via cli")), qPrintable(text + out));
        QVERIFY(text.contains(QStringLiteral("run Deploy")));
        QVERIFY(text.contains(QStringLiteral("env use Prod")));
    }
};

QTEST_MAIN(TestKaiModule)
#include "test_kai_module.moc"
