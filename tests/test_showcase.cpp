#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/models.h"
#include "engine/command-language.h"
#include "kai-ipc-fixture.h"

using namespace kai::core;

// Conduz sample/languages/showcase.py (o programa Python longo do demo) de ponta a
// ponta, como o Kai faria: o programa fala KIP pelo stdout/stdin e usa o módulo
// `kai` contra um servidor IPC de verdade. Cobre menu, wizard com Back, campos
// dependentes (change/patch), chips, validação (invalid), falha + retry,
// arquivos escritos de verdade e as notificações enviadas ao app.
class TestShowcase : public QObject {
    Q_OBJECT

    static QString repoRoot()
    {
        QDir dir(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 6; ++i) {
            if (QFile::exists(dir.filePath(QStringLiteral("sample/languages/showcase.py")))) return dir.absolutePath();
            if (!dir.cdUp()) break;
        }
        return QString();
    }

    // O texto do comando "8. Showcase" do kai.json (o que o Kai executaria).
    static Command showcaseCommand()
    {
        QFile file(repoRoot() + QStringLiteral("/sample/languages/kai.json"));
        file.open(QIODevice::ReadOnly);
        for (const QJsonValue &v : QJsonDocument::fromJson(file.readAll()).object().value("commands").toArray()) {
            if (v.toObject().value("name").toString().startsWith(QStringLiteral("8."))) {
                return Command::fromJson(v.toObject());
            }
        }
        return Command();
    }

    struct Conversation {
        QProcess process;
        QByteArray buffer;
        QVector<QJsonObject> seen; // tudo que o programa mandou, em ordem

        QJsonObject next(int timeoutMs = 20000)
        {
            for (;;) {
                const int nl = buffer.indexOf('\n');
                if (nl >= 0) {
                    const QByteArray line = buffer.left(nl);
                    buffer.remove(0, nl + 1);
                    const QJsonObject message = QJsonDocument::fromJson(line).object();
                    if (message.value("kip").toInt() == 1) {
                        seen << message;
                        return message;
                    }
                    continue; // não é protocolo (log, banner)
                }
                QSignalSpy ready(&process, &QProcess::readyReadStandardOutput);
                if (!ready.wait(timeoutMs)) return QJsonObject();
                buffer += process.readAllStandardOutput();
            }
        }
        // A próxima mensagem que espera resposta (prompt/confirm) ou o fim (done).
        QJsonObject nextScreen()
        {
            for (;;) {
                const QJsonObject m = next();
                const QString type = m.value("type").toString();
                if (m.isEmpty() || type == "prompt" || type == "confirm" || type == "done") return m;
            }
        }
        void send(const QJsonObject &message)
        {
            QJsonObject out = message;
            out["kip"] = 1;
            process.write(QJsonDocument(out).toJson(QJsonDocument::Compact) + "\n");
        }
        void respond(const QString &id, const QJsonObject &values)
        {
            send({{"type", "response"}, {"id", id}, {"values", values}});
        }
        void confirm(const QString &id, bool yes) { respond(id, {{"confirmed", yes}}); }
        QJsonObject firstOf(const QString &type, int from = 0) const
        {
            for (int i = from; i < seen.size(); ++i) {
                if (seen.at(i).value("type").toString() == type) return seen.at(i);
            }
            return QJsonObject();
        }
        int count(const QString &type) const
        {
            int n = 0;
            for (const QJsonObject &m : seen) n += m.value("type").toString() == type;
            return n;
        }
    };

    static QJsonArray fieldNames(const QJsonObject &prompt)
    {
        QJsonArray names;
        for (const QJsonValue &f : prompt.value("fields").toArray()) names << f.toObject().value("name");
        return names;
    }

private slots:
    void theWholeShowcaseRunsEndToEnd()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) QSKIP("python3 ausente");
        const QString root = repoRoot();
        QVERIFY(!root.isEmpty());
        const Command cmd = showcaseCommand();
        QVERIFY(cmd.kip);
        QCOMPARE(cmd.language, CommandLanguage::Python);

        QTemporaryDir work;
        QVERIFY(work.isValid());
        KaiIpcFixture app;
        QVERIFY(app.started);

        Conversation c;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const auto defaults = kai::engine::languageEnvDefaults(cmd.language);
        for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) env.insert(it.key(), it.value());
        env.insert(QStringLiteral("TMPDIR"), work.path());
        c.process.setProcessEnvironment(env);
        c.process.setWorkingDirectory(root + QStringLiteral("/sample/languages"));
        c.process.start(QStringLiteral("/bin/sh"),
                        {QStringLiteral("-c"), kai::engine::buildInterpreterCommandLine(
                                                   cmd.language, QStringLiteral("python3"), cmd.command, true)});
        QVERIFY(c.process.waitForStarted());

        // ---- menu -> scaffolder ----
        QJsonObject screen = c.nextScreen();
        QCOMPARE(c.firstOf("hello").value("type").toString(), QStringLiteral("hello"));
        QCOMPARE(screen.value("id").toString(), QStringLiteral("menu"));
        c.respond("menu", {{"tool", "scaffold"}});

        // ---- 1/4 basics: dependent field, chip, validation ----
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("sc-basics"));
        QVERIFY(fieldNames(screen).contains(QStringLiteral("framework")));
        QVERIFY(screen.value("chips").toArray().size() >= 3);

        c.send({{"type", "change"}, {"id", "sc-basics"}, {"seq", 1}, {"field", "language"},
                {"values", QJsonObject{{"language", "node"}, {"name", ""}}}});
        QJsonObject patch;
        do { patch = c.next(); } while (!patch.isEmpty() && patch.value("type").toString() != "patch");
        QCOMPARE(patch.value("seq").toInt(), 1);
        const QJsonObject framework = patch.value("fields").toArray().at(0).toObject();
        QCOMPARE(framework.value("name").toString(), QStringLiteral("framework"));
        QVERIFY(framework.value("options").toArray().contains(QJsonValue(QStringLiteral("express"))));

        c.send({{"type", "chip"}, {"id", "sc-basics"}, {"chip", "check"}, {"values", QJsonObject{{"name", "demo-app"}}}});
        QJsonObject chipResult;
        do { chipResult = c.next(); } while (!chipResult.isEmpty() && chipResult.value("type").toString() != "chip_result");
        QCOMPARE(chipResult.value("chip").toString(), QStringLiteral("check"));
        QCOMPARE(chipResult.value("state").toString(), QStringLiteral("success"));

        const QJsonObject features{{"git", true}, {"tests", true}, {"docker", false}, {"ci", false},
                                   {"license", true}, {"fail", true}}; // fail: simula falha nos testes
        auto basics = [&](const QString &name) {
            return QJsonObject{{"name", name}, {"template", "cli"}, {"language", "node"}, {"framework", "express"},
                               {"target", "x"}, {"features", features}, {"tags", QJsonArray{"cli"}}};
        };
        c.respond("sc-basics", basics("A")); // curto demais -> invalid
        QJsonObject invalid;
        do { invalid = c.next(); } while (!invalid.isEmpty() && invalid.value("type").toString() != "invalid");
        QVERIFY(invalid.value("errors").toObject().contains("name"));
        c.respond("sc-basics", basics("demo-app"));

        // ---- 2/4 details: Back volta pré-preenchido ----
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("sc-details"));
        c.send({{"type", "back"}, {"id", "sc-details"}});
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("sc-basics")); // a volta
        QCOMPARE(screen.value("fields").toArray().at(0).toObject().value("default").toString(), QStringLiteral("demo-app"));
        c.respond("sc-basics", basics("demo-app"));

        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("sc-details"));
        auto details = [&](const QString &version) {
            return QJsonObject{{"version", version}, {"description", "demo"}, {"license", "MIT"}, {"budget", 12},
                               {"token", ""}, {"output", work.path()}, {"due", QJsonValue()}};
        };
        c.respond("sc-details", details("1.x")); // semver inválido
        do { invalid = c.next(); } while (!invalid.isEmpty() && invalid.value("type").toString() != "invalid");
        QVERIFY(invalid.value("errors").toObject().contains("version"));
        c.respond("sc-details", details("1.2.3"));

        // ---- 3/4 review ----
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("sc-confirm"));
        QCOMPARE(c.firstOf("table").value("id").toString(), QStringLiteral("sc-summary"));
        QVERIFY(!screen.value("danger").toBool()); // pasta nova: não é destrutivo
        c.confirm("sc-confirm", true);

        // ---- 4/4 build: falha simulada -> retry ----
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("sc-retry"));
        bool testsFailed = false;
        for (const QJsonObject &m : c.seen) {
            testsFailed |= m.value("type").toString() == "step" && m.value("id").toString() == "tests"
                           && m.value("state").toString() == "error";
        }
        QVERIFY2(testsFailed, "o passo dos testes deveria ter falhado");
        c.confirm("sc-retry", true);

        // sucesso na segunda volta -> volta ao menu
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("menu"));
        QCOMPARE(c.count("steps"), 2); // o checklist foi montado de novo no retry
        QCOMPARE(c.firstOf("set_env").value("name").toString(), QStringLiteral("SHOWCASE_LAST_PROJECT"));
        const QString project = work.filePath(QStringLiteral("demo-app"));
        QVERIFY(QFile::exists(project + QStringLiteral("/MANIFEST.json")));
        QVERIFY(QFile::exists(project + QStringLiteral("/src/main.js")));
        QVERIFY(QFile::exists(project + QStringLiteral("/README.md")));

        // ---- relatório, depois sair ----
        c.respond("menu", {{"tool", "report"}});
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("menu"));
        QVERIFY(c.count("markdown") >= 1);
        c.respond("menu", {{"tool", "quit"}});
        screen = c.nextScreen();
        QCOMPARE(screen.value("type").toString(), QStringLiteral("done"));
        QVERIFY(screen.value("text").toString().contains(QStringLiteral("1 project(s)")));
        QVERIFY(c.process.waitForFinished(10000));
        QCOMPARE(c.process.exitCode(), 0);

        // O app recebeu as notificações do módulo kai.
        const auto notified = [&app](const QString &fragment) {
            for (const QString &call : app.calls) {
                if (call.startsWith(QStringLiteral("raise|")) && call.contains(fragment)) return true;
            }
            return false;
        };
        QVERIFY(notified(QStringLiteral("Showcase started")));
        QVERIFY(notified(QStringLiteral("Project demo-app created")));
        QVERIFY(notified(QStringLiteral("Showcase finished: 1 project(s)")));
    }

    void toolsTalkToTheAppAndBackReturnsToTheMenu()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) QSKIP("python3 ausente");
        const QString root = repoRoot();
        const Command cmd = showcaseCommand();
        QTemporaryDir work;
        KaiIpcFixture app;
        QVERIFY(app.started);

        Conversation c;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const auto defaults = kai::engine::languageEnvDefaults(cmd.language);
        for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) env.insert(it.key(), it.value());
        env.insert(QStringLiteral("TMPDIR"), work.path());
        c.process.setProcessEnvironment(env);
        c.process.setWorkingDirectory(root + QStringLiteral("/sample/languages"));
        c.process.start(QStringLiteral("/bin/sh"),
                        {QStringLiteral("-c"), kai::engine::buildInterpreterCommandLine(
                                                   cmd.language, QStringLiteral("python3"), cmd.command, true)});
        QVERIFY(c.process.waitForStarted());

        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("menu"));

        // Ambientes: troca para Prod, "terminei" e restaura o Dev.
        c.respond("menu", {{"tool", "envs"}});
        QJsonObject screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("envs-pick"));
        c.respond("envs-pick", {{"env", "Prod"}, {"options", QJsonObject{{"restore", true}}}});
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("envs-again"));
        c.confirm("envs-again", false);
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("menu"));
        QVERIFY(app.calls.contains(QStringLiteral("env-use|Prod")));
        QVERIFY(app.calls.contains(QStringLiteral("env-use|Dev"))); // restaurou o ambiente original

        // Monitor de processos: lista o "api" do app e mata, com confirmação.
        c.respond("menu", {{"tool", "procs"}});
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("procs"));
        QVERIFY(fieldNames(screen).contains(QStringLiteral("targets")));
        c.respond("procs", {{"action", "kill"}, {"targets", QJsonArray{"None"}}});
        // O id do item é str(p.get("id")) ("None": o fixture não manda id) e o pid 4242 não é desta sessão.
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("procs-kill"));
        QVERIFY(screen.value("danger").toBool());
        c.confirm("procs-kill", true);
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("procs")); // o laço volta à lista
        QVERIFY(app.calls.contains(QStringLiteral("kill|4242")));
        c.send({{"type", "back"}, {"id", "procs"}}); // Back na lista = volta ao menu
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("menu"));

        // Runner: lista os comandos do app e roda um.
        c.respond("menu", {{"tool", "runner"}});
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("runner-pick"));
        c.respond("runner-pick", {{"command", "Deploy"}, {"options", QJsonObject{{"notify", true}}}});
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("runner-confirm"));
        c.confirm("runner-confirm", true);
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("menu"));
        QVERIFY(app.calls.contains(QStringLiteral("run|Deploy")));

        // Laboratório de notificações: 2 envios.
        c.respond("menu", {{"tool", "notify"}});
        screen = c.nextScreen();
        QCOMPARE(screen.value("id").toString(), QStringLiteral("notify-lab"));
        c.respond("notify-lab", {{"title", "T"}, {"message", "ping"}, {"level", "warning"}, {"count", 2}, {"delay", 0}});
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("menu"));
        int pings = 0;
        for (const QString &call : app.calls) pings += call.startsWith(QStringLiteral("raise|warning|T|ping"));
        QCOMPARE(pings, 2);

        // Back no primeiro passo do scaffolder volta ao menu, sem derrubar nada.
        c.respond("menu", {{"tool", "scaffold"}});
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("sc-basics"));
        c.send({{"type", "back"}, {"id", "sc-basics"}});
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("menu"));

        c.respond("menu", {{"tool", "quit"}});
        QCOMPARE(c.nextScreen().value("type").toString(), QStringLiteral("done"));
        QVERIFY(c.process.waitForFinished(10000));
        QCOMPARE(c.process.exitCode(), 0);
    }

    void withoutTheAppTheKaiCallsDegradeToWarnings()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) QSKIP("python3 ausente");
        const QString root = repoRoot();
        const Command cmd = showcaseCommand();
        QTemporaryDir work;

        Conversation c;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("KAI_IPC_SOCKET"), work.filePath(QStringLiteral("no-socket")));
        env.insert(QStringLiteral("KAI_EXE"), QString());
        env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
        env.insert(QStringLiteral("TMPDIR"), work.path());
        c.process.setProcessEnvironment(env);
        c.process.setWorkingDirectory(root + QStringLiteral("/sample/languages"));
        c.process.start(QStringLiteral("/bin/sh"),
                        {QStringLiteral("-c"), kai::engine::buildInterpreterCommandLine(
                                                   cmd.language, QStringLiteral("python3"), cmd.command, true)});
        QVERIFY(c.process.waitForStarted());
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("menu")); // o início não depende do app
        QVERIFY2(c.firstOf("message").value("level").toString() == "warning", "esperava um aviso de app inalcançável");
        c.respond("menu", {{"tool", "envs"}});
        // sem app o ambiente nem lista: volta direto ao menu
        QCOMPARE(c.nextScreen().value("id").toString(), QStringLiteral("menu"));
        c.respond("menu", {{"tool", "quit"}});
        QCOMPARE(c.nextScreen().value("type").toString(), QStringLiteral("done"));
        QVERIFY(c.process.waitForFinished(10000));
        QCOMPARE(c.process.exitCode(), 0);
    }
};

QTEST_MAIN(TestShowcase)
#include "test_showcase.moc"
