#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>

#include "core/interpreter-settings.h"
#include "core/kai-file-validator.h"
#include "core/models.h"
#include "engine/command-language.h"
#include "engine/execution-pipeline.h"

using namespace kai::core;
using namespace kai::engine;

// Comandos com linguagem (Python/Node): o código chega ao interpretador sem
// passar por aspas de shell nem por {{VAR}}, o stdin fica livre e o módulo
// `kip` embutido fala o protocolo.
// Macro (e não função): QSKIP só encerra a função em que está, e o teste seguiria.
#define REQUIRE_INTERPRETER(language)                                                          \
    do {                                                                                       \
        const QString program_ = (language) == CommandLanguage::Python ? QStringLiteral("python3") \
                                                                       : QStringLiteral("node");   \
        if (QStandardPaths::findExecutable(program_).isEmpty()) {                              \
            QSKIP(qPrintable(program_ + QStringLiteral(" não está instalado neste ambiente")));   \
        }                                                                                      \
    } while (false)

class TestCommandLanguage : public QObject {
    Q_OBJECT

    static QString interpreterFor(CommandLanguage language)
    {
        return language == CommandLanguage::Python ? QStringLiteral("python3") : QStringLiteral("node");
    }

    // Roda a linha como o Kai roda (um comando de shell) e devolve stdout.
    static QString runLine(const QString &line, const QByteArray &stdinData, int *exitCode = nullptr)
    {
        QProcess process;
        process.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), line});
        process.write(stdinData);
        process.closeWriteChannel();
        process.waitForFinished(15000);
        if (exitCode) {
            *exitCode = process.exitCode();
        }
        return QString::fromUtf8(process.readAllStandardOutput());
    }

    // ---- alvo WSL padrão (Kai do Windows, comandos no WSL) ----
    // O template padrão do WSL embrulha a linha em OUTRO base64 e o Windows a executa
    // por `cmd.exe /c`, que corta em 8191 caracteres.
    static inline QStringList s_launchLines; // linhas passadas ao ProcessRunner
    static void captureLaunchLines(QtMsgType, const QMessageLogContext &, const QString &message)
    {
        const int at = message.indexOf(QStringLiteral("omando iniciado sob PTY"));
        const int plain = message.indexOf(QStringLiteral("Iniciando comando: "));
        if (plain >= 0) s_launchLines << message.mid(plain + 19);
        else if (at >= 0) s_launchLines << message.mid(message.indexOf(QStringLiteral("): ")) + 3);
    }

    struct WslRun {
        QString output;
        qsizetype lineLength = 0;
        bool success = false;
    };

    static WslRun runThroughDefaultWslProfile(Command cmd, const QString &fakeBinDir)
    {
        QFile fake(fakeBinDir + QStringLiteral("/wsl.exe"));
        fake.open(QIODevice::WriteOnly);
        // Um `wsl.exe` de mentira: descarta as opções até `--` e executa o resto.
        fake.write("#!/bin/sh\nwhile [ \"$1\" != \"--\" ] && [ $# -gt 0 ]; do shift; done\nshift\nexec \"$@\"\n");
        fake.close();
        fake.setPermissions(fake.permissions() | QFileDevice::ExeOwner);

        TerminalProfile target;
        target.name = QStringLiteral("WSL");
        target.usePty = false;
        target.shell = ShellFlavor::Posix;
        target.commandTemplate = fakeBinDir + QStringLiteral("/wsl.exe -- bash -lic 'eval \"$(echo \"$1\" | base64 -d)\"' kai {{command_b64}}");
        cmd.terminalTarget = target.name;
        cmd.id = QStringLiteral("wsl_cmd");
        QMap<QString, Command> all{{cmd.id, cmd}};

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        pipeline.setTerminalProfiles({target});
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        s_launchLines.clear();
        const QtMessageHandler previous = qInstallMessageHandler(&TestCommandLanguage::captureLaunchLines);
        pipeline.run(cmd, all, env);
        const bool finished = finishedSpy.wait(20000);
        qInstallMessageHandler(previous);
        WslRun run;
        if (!finished) return run;
        for (const QList<QVariant> &call : logSpy) run.output += call.at(1).toString();
        run.success = qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success;
        if (!s_launchLines.isEmpty()) {
            // Tira o prefixo do fake: o que o Windows passaria ao cmd.exe é o template com o base64.
            run.lineLength = s_launchLines.last().size() - fakeBinDir.size() + QStringLiteral("wsl.exe").size();
        }
        return run;
    }

private slots:
    // ---- modelo ----
    void commandTypeIsWrittenAsCommandAndShellStillLoads()
    {
        Command c;
        c.id = "a";
        c.name = "A";
        c.command = "ls";
        QCOMPARE(c.toJson().value("type").toString(), QStringLiteral("command"));
        QVERIFY(!c.toJson().contains("language")); // nativo é o padrão: chave omitida

        const Command legacy = Command::fromJson({{"id", "old"}, {"type", "shell"}, {"command", "ls"}});
        QCOMPARE(legacy.type, CommandType::Command);
        QCOMPARE(legacy.language, CommandLanguage::Native);
        QCOMPARE(Command::fromJson({{"type", "command"}}).type, CommandType::Command);
        QCOMPARE(Command::fromJson({{"type", "http"}}).type, CommandType::Http);
    }

    void languageAndInterpreterRoundTrip()
    {
        Command c;
        c.id = "py";
        c.language = CommandLanguage::Python;
        c.interpreter = QStringLiteral("uv run python");
        const QJsonObject json = c.toJson();
        QCOMPARE(json.value("language").toString(), QStringLiteral("python"));
        QCOMPARE(json.value("interpreter").toString(), QStringLiteral("uv run python"));

        const Command back = Command::fromJson(json);
        QCOMPARE(back.language, CommandLanguage::Python);
        QCOMPARE(back.interpreter, QStringLiteral("uv run python"));
        QCOMPARE(commandLanguageFromString(QStringLiteral("NODE")), CommandLanguage::Node);
        QCOMPARE(commandLanguageFromString(QStringLiteral("cobol")), CommandLanguage::Native);
    }

    void kipAutoCloseRoundTripsOmitsDefaultsAndClampsTheDelay()
    {
        Command c;
        c.id = "k";
        c.kip = true;
        QVERIFY(!c.toJson().contains("kip_auto_close"));            // padrão: chaves omitidas
        QVERIFY(!c.toJson().contains("kip_auto_close_delay_sec"));
        c.kipAutoCloseWindow = true;
        c.kipAutoCloseDelaySec = 0;
        const Command back = Command::fromJson(c.toJson());
        QVERIFY(back.kipAutoCloseWindow);
        QCOMPARE(back.kipAutoCloseDelaySec, 0);
        QCOMPARE(Command::fromJson({{"kip_auto_close_delay_sec", 9999}}).kipAutoCloseDelaySec, 60);
        QCOMPARE(Command::fromJson({{"kip_auto_close_delay_sec", -5}}).kipAutoCloseDelaySec, 0);
        QCOMPARE(Command::fromJson({{"id", "x"}}).kipAutoCloseDelaySec, 2);
    }

    void interpreterSettingsResolve()
    {
        InterpreterSettings s;
        QCOMPARE(s.resolve(CommandLanguage::Native, QStringLiteral("x")), QString());
        QCOMPARE(s.resolve(CommandLanguage::Python, QString()), QStringLiteral("python3"));
        QCOMPARE(s.resolve(CommandLanguage::Node, QString()), QStringLiteral("node"));
        QCOMPARE(s.resolve(CommandLanguage::Python, QStringLiteral("  /opt/venv/bin/python ")),
                 QStringLiteral("/opt/venv/bin/python")); // override do comando
        s.python = QStringLiteral("~/.venv/bin/python");
        QCOMPARE(s.resolve(CommandLanguage::Python, QString()), QStringLiteral("~/.venv/bin/python"));
        s.python = QStringLiteral("   ");
        QCOMPARE(s.resolve(CommandLanguage::Python, QString()), QStringLiteral("python3")); // vazio = padrão
        // No Windows sem alvo, "python3" é o atalho da Store: o padrão vira "python"...
        QCOMPARE(s.resolve(CommandLanguage::Python, QString(), true), QStringLiteral("python"));
        // ...mas um valor escolhido pelo usuário nunca é trocado.
        s.python = QStringLiteral("py -3");
        QCOMPARE(s.resolve(CommandLanguage::Python, QString(), true), QStringLiteral("py -3"));
    }

    // ---- linha de comando ----
    void nativeHasNoInterpreterLine()
    {
        QVERIFY(buildInterpreterCommandLine(CommandLanguage::Native, QString(), QStringLiteral("ls"), false).isEmpty());
        QVERIFY(languageEnvDefaults(CommandLanguage::Native).isEmpty());
    }

    void pythonDefaultsAreUnbufferedUtf8()
    {
        const auto env = languageEnvDefaults(CommandLanguage::Python);
        QCOMPARE(env.value("PYTHONUNBUFFERED"), QStringLiteral("1"));
        QCOMPARE(env.value("PYTHONIOENCODING"), QStringLiteral("utf-8"));
        QVERIFY(!languageEnvDefaults(CommandLanguage::Node).contains("PYTHONUNBUFFERED"));
        // O módulo `kai` acha o app por KAI_IPC_SOCKET, em qualquer linguagem de código.
        for (CommandLanguage language : {CommandLanguage::Python, CommandLanguage::Node}) {
            QVERIFY(languageEnvDefaults(language).value("KAI_IPC_SOCKET").endsWith("kai-ipc-v1"));
        }
    }

    void theCodeNeverAppearsRawInTheShellLine()
    {
        const QString code = QStringLiteral("print(\"it's $HOME `x` \\\\ !\")");
        for (CommandLanguage language : {CommandLanguage::Python, CommandLanguage::Node}) {
            const QString line = buildInterpreterCommandLine(language, QStringLiteral("interp"), code, false);
            QVERIFY(line.startsWith(QStringLiteral("interp ")));
            QVERIFY2(!line.contains(QStringLiteral("$HOME")), qPrintable(line));
            // Dentro das aspas duplas só pode haver o que o shell não interpreta.
            const int open = line.indexOf(QLatin1Char('"'));
            const QString body = line.mid(open + 1, line.size() - open - 2);
            for (const QChar bad : {QLatin1Char('"'), QLatin1Char('$'), QLatin1Char('`'), QLatin1Char('!')}) {
                QVERIFY2(!body.contains(bad), qPrintable(QString(bad) + QStringLiteral(" em ") + line));
            }
            QVERIFY(!body.contains(QLatin1Char('\n')));
        }
    }

    void pythonRunsAwkwardCodeAndKeepsStdinFree()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Python);
        const QString code = QStringLiteral(
            "import sys\n"
            "name = 'x'\n"
            "print(f\"{{name}} {name}\", '{{VAR}}', \"aspas ' \\\"\", '$HOME', '`ls`', 'ü ✓')\n"
            "print('lido:', sys.stdin.readline().strip())\n");
        int exitCode = -1;
        const QString out = runLine(buildInterpreterCommandLine(CommandLanguage::Python, QStringLiteral("python3"), code, false),
                                    "ola\n", &exitCode);
        QCOMPARE(exitCode, 0);
        QVERIFY2(out.contains(QStringLiteral("{name} x {{VAR}} aspas ' \" $HOME `ls` ü ✓")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("lido: ola")), qPrintable(out));
    }

    void pythonFailureHasANonZeroExitCodeAndAnErrorOnStderr()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Python);
        int exitCode = 0;
        runLine(buildInterpreterCommandLine(CommandLanguage::Python, QStringLiteral("python3"),
                                            QStringLiteral("raise SystemExit(7)"), false), QByteArray(), &exitCode);
        QCOMPARE(exitCode, 7);
        runLine(buildInterpreterCommandLine(CommandLanguage::Python, QStringLiteral("python3"),
                                            QStringLiteral("raise ValueError('x')"), false), QByteArray(), &exitCode);
        QCOMPARE(exitCode, 1);
    }

    void pythonInterpreterWithArgumentsIsUsedAsTyped()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Python);
        // "env python3" ~ "uv run python": o interpretador é uma linha de shell.
        const QString out = runLine(buildInterpreterCommandLine(CommandLanguage::Python,
            QStringLiteral("env FOO=bar python3"), QStringLiteral("import os; print(os.environ['FOO'])"), false), QByteArray());
        QCOMPARE(out.trimmed(), QStringLiteral("bar"));
    }

    void nodeRunsAwkwardCodeWithTopLevelAwait()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Node);
        const QString code = QStringLiteral(
            "const name = 'x';\n"
            "console.log(`${name}`, '{{VAR}}', \"aspas ' \\\"\", '$HOME', 'ü ✓');\n"
            "const sleep = (ms) => new Promise((r) => setTimeout(r, ms));\n"
            "await sleep(10);\n"
            "const input = await new Promise((r) => process.stdin.once('data', (d) => r(String(d).trim())));\n"
            "console.log('lido:', input); // comentário na última linha");
        int exitCode = -1;
        const QString out = runLine(buildInterpreterCommandLine(CommandLanguage::Node, QStringLiteral("node"), code, false),
                                    "ola\n", &exitCode);
        QCOMPARE(exitCode, 0);
        QVERIFY2(out.contains(QStringLiteral("x {{VAR}} aspas ' \" $HOME ü ✓")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("lido: ola")), qPrintable(out));
    }

    void nodeFailureHasANonZeroExitCode()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Node);
        int exitCode = 0;
        runLine(buildInterpreterCommandLine(CommandLanguage::Node, QStringLiteral("node"),
                                            QStringLiteral("throw new Error('x')"), false), QByteArray(), &exitCode);
        QVERIFY(exitCode != 0);
        runLine(buildInterpreterCommandLine(CommandLanguage::Node, QStringLiteral("node"),
                                            QStringLiteral("process.exitCode = 4"), false), QByteArray(), &exitCode);
        QCOMPARE(exitCode, 4);
    }

    // `import kip` num comando SEM KIP ligado não é um ModuleNotFoundError misterioso:
    // o módulo existe e, ao ser usado, diz o que falta ligar.
    void kipModuleInANonKipCommandExplainsWhatToTurnOn_data()
    {
        QTest::addColumn<int>("language");
        QTest::addColumn<QString>("code");
        QTest::newRow("python") << int(CommandLanguage::Python) << QStringLiteral(
            "import kip\ntry:\n    kip.prompt([])\nexcept RuntimeError as e:\n    print('MSG', e)\n");
        QTest::newRow("node") << int(CommandLanguage::Node) << QStringLiteral(
            "const k = require('kip');\ntry { k.prompt([]); } catch (e) { console.log('MSG', e.message); }\n");
    }

    void kipModuleInANonKipCommandExplainsWhatToTurnOn()
    {
        QFETCH(int, language);
        QFETCH(QString, code);
        const auto lang = CommandLanguage(language);
        REQUIRE_INTERPRETER(lang);
        int exitCode = -1;
        const QString out = runLine(buildInterpreterCommandLine(lang, interpreterFor(lang), code, false), QByteArray(), &exitCode);
        QCOMPARE(exitCode, 0);
        QVERIFY2(out.contains(QStringLiteral("MSG")) && out.contains(QStringLiteral("KIP interface")), qPrintable(out));
        QVERIFY2(!out.contains(QStringLiteral("ModuleNotFoundError")), qPrintable(out));
    }

    // ---- módulo kip ----
    void kipModulesAreEmbedded()
    {
        QVERIFY(kipModuleSource(CommandLanguage::Python).contains(QStringLiteral("def prompt(")));
        QVERIFY(kipModuleSource(CommandLanguage::Node).contains(QStringLiteral("async function prompt(")));
        QVERIFY(kipModuleSource(CommandLanguage::Native).isEmpty());
        QVERIFY(kaiModuleSource(CommandLanguage::Python).contains(QStringLiteral("def notify(")));
        QVERIFY(kaiModuleSource(CommandLanguage::Node).contains(QStringLiteral("async function notify(")));
        QVERIFY(kaiModuleSource(CommandLanguage::Native).isEmpty());
    }

    // Conversa com um programa KIP escrito com o módulo: lê cada mensagem,
    // responde e devolve tudo que o programa imprimiu.
    void kipModuleSpeaksTheProtocol_data()
    {
        QTest::addColumn<int>("language");
        QTest::addColumn<QString>("code");
        QTest::newRow("python") << int(CommandLanguage::Python) << QStringLiteral(
            "import kip\n"
            "values = kip.prompt([{'name': 'who', 'type': 'text'}], id='ask', title='Who?')\n"
            "kip.progress(50, 'Working')\n"
            "kip.done(title='Hello ' + values['who'])\n");
        QTest::newRow("node") << int(CommandLanguage::Node) << QStringLiteral(
            "const kip = require('kip');\n"
            "const values = await kip.prompt([{ name: 'who', type: 'text' }], { id: 'ask', title: 'Who?' });\n"
            "kip.progress(50, 'Working');\n"
            "kip.done({ title: 'Hello ' + values.who });\n");
    }

    void kipModuleSpeaksTheProtocol()
    {
        QFETCH(int, language);
        QFETCH(QString, code);
        const auto lang = CommandLanguage(language);
        REQUIRE_INTERPRETER(lang);

        QProcess process;
        process.start(QStringLiteral("/bin/sh"),
                      {QStringLiteral("-c"), buildInterpreterCommandLine(lang, interpreterFor(lang), code, true)});
        QVERIFY(process.waitForStarted());

        auto nextMessage = [&process]() {
            while (!process.canReadLine()) {
                if (!process.waitForReadyRead(10000)) {
                    return QJsonObject();
                }
            }
            return QJsonDocument::fromJson(process.readLine()).object();
        };

        const QJsonObject hello = nextMessage();
        QCOMPARE(hello.value("type").toString(), QStringLiteral("hello")); // enviado antes da 1ª mensagem
        QCOMPARE(hello.value("kip").toInt(), 1);
        const QJsonObject prompt = nextMessage();
        QCOMPARE(prompt.value("type").toString(), QStringLiteral("prompt"));
        QCOMPARE(prompt.value("id").toString(), QStringLiteral("ask"));
        QCOMPARE(prompt.value("title").toString(), QStringLiteral("Who?"));
        QCOMPARE(prompt.value("fields").toArray().at(0).toObject().value("name").toString(), QStringLiteral("who"));

        process.write("{\"kip\":1,\"type\":\"response\",\"id\":\"ask\",\"values\":{\"who\":\"Kai\"}}\n");
        const QJsonObject progress = nextMessage();
        QCOMPARE(progress.value("type").toString(), QStringLiteral("progress"));
        QCOMPARE(progress.value("value").toInt(), 50);
        QCOMPARE(progress.value("label").toString(), QStringLiteral("Working"));
        const QJsonObject done = nextMessage();
        QCOMPARE(done.value("type").toString(), QStringLiteral("done"));
        QCOMPARE(done.value("title").toString(), QStringLiteral("Hello Kai"));

        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0); // nada segura o processo depois do done
    }

    void kipModuleExitsWith130OnCancel_data()
    {
        QTest::addColumn<int>("language");
        QTest::addColumn<QString>("code");
        QTest::newRow("python") << int(CommandLanguage::Python)
            << QStringLiteral("import kip\nkip.confirm('Sure?', id='c')\nkip.done(title='nunca')\n");
        QTest::newRow("node") << int(CommandLanguage::Node)
            << QStringLiteral("await kip.confirm('Sure?', { id: 'c' });\nkip.done({ title: 'nunca' });\n");
    }

    void kipModuleExitsWith130OnCancel()
    {
        QFETCH(int, language);
        QFETCH(QString, code);
        const auto lang = CommandLanguage(language);
        REQUIRE_INTERPRETER(lang);

        QProcess process;
        process.start(QStringLiteral("/bin/sh"),
                      {QStringLiteral("-c"), buildInterpreterCommandLine(lang, interpreterFor(lang), code, true)});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForReadyRead(10000));
        process.write("{\"kip\":1,\"type\":\"cancel\"}\n");
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 130);
        QVERIFY(!process.readAllStandardOutput().contains("nunca"));
    }

    void kipModuleAnswersChangeWithAPatchEchoingSeq()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Python);
        const QString code = QStringLiteral(
            "import kip\n"
            "def changed(field, values):\n"
            "    return [{'name': 'b', 'type': 'text', 'default': values['a'].upper()}]\n"
            "kip.prompt([{'name': 'a', 'type': 'text', 'watch': True}], id='p', on_change=changed)\n");
        QProcess process;
        process.start(QStringLiteral("/bin/sh"),
                      {QStringLiteral("-c"), buildInterpreterCommandLine(CommandLanguage::Python, QStringLiteral("python3"), code, true)});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForReadyRead(10000));
        process.write("{\"kip\":1,\"type\":\"change\",\"id\":\"p\",\"seq\":7,\"field\":\"a\",\"values\":{\"a\":\"xy\"}}\n");
        process.write("{\"kip\":1,\"type\":\"response\",\"id\":\"p\",\"values\":{\"a\":\"xy\"}}\n");
        QVERIFY(process.waitForFinished(10000));
        const QString out = QString::fromUtf8(process.readAllStandardOutput());
        QVERIFY2(out.contains(QStringLiteral("\"type\":\"patch\"")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("\"seq\":7")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("\"default\":\"XY\"")), qPrintable(out));
    }

    // ---- pipeline ----
    void pipelineKeepsBracesAndDeliversVariablesThroughTheEnvironment()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Python);
        Command cmd;
        cmd.id = "py_env";
        cmd.type = CommandType::Command;
        cmd.language = CommandLanguage::Python;
        cmd.command = QStringLiteral("import os\nprint('NAME=' + os.environ['NAME'], '{{NAME}}', f'{{os.environ[\"NAME\"]}}')");
        QMap<QString, Command> all{{cmd.id, cmd}};

        EnvironmentManager env;
        env.setParamVars({{QStringLiteral("NAME"), QStringLiteral("kai")}});
        ExecutionPipeline pipeline;
        QSignalSpy logSpy(&pipeline, &ExecutionPipeline::logMessage);
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QVERIFY(finishedSpy.wait(10000));
        QVERIFY(qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success);

        QString output;
        for (const QList<QVariant> &call : logSpy) {
            output += call.at(1).toString();
        }
        QVERIFY2(output.contains(QStringLiteral("NAME=kai {{NAME}} {os.environ[\"NAME\"]}")), qPrintable(output));
    }

    void pipelineUsesTheConfiguredInterpreterAndThePerCommandOverride()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Python);
        Command cmd;
        cmd.id = "py_interp";
        cmd.type = CommandType::Command;
        cmd.language = CommandLanguage::Python;
        cmd.command = QStringLiteral("print('ok')");

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        InterpreterSettings settings;
        settings.python = QStringLiteral("kai-no-such-python");
        pipeline.setInterpreters(settings);
        QVERIFY(pipeline.commandLineFor(cmd, env).startsWith(QStringLiteral("kai-no-such-python -u -c ")));
        cmd.interpreter = QStringLiteral("python3");
        QVERIFY(pipeline.commandLineFor(cmd, env).startsWith(QStringLiteral("python3 -u -c ")));

        // Nativo continua interpolando.
        env.setParamVars({{QStringLiteral("X"), QStringLiteral("1")}});
        Command native;
        native.command = QStringLiteral("echo {{X}}");
        QCOMPARE(pipeline.commandLineFor(native, env), QStringLiteral("echo 1"));
    }

    // ---- samples ----
    static QString repoFile(const QString &relative)
    {
        QDir dir(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 6; ++i) {
            if (QFile::exists(dir.filePath(relative))) return dir.filePath(relative);
            if (!dir.cdUp()) break;
        }
        return QString();
    }

    static QJsonArray sampleCommands(const QString &relative)
    {
        QFile file(repoFile(relative));
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object().value("commands").toArray();
    }

    void sampleProjectsValidateWithoutIssues()
    {
        for (const char *sample : {"sample/languages/kai.json", "sample/kip/kai.json"}) {
            QFile file(repoFile(QLatin1String(sample)));
            QVERIFY2(file.open(QIODevice::ReadOnly), sample);
            const ValidationResult result = validateKaiFileText(QString::fromUtf8(file.readAll()));
            QVERIFY2(!result.hasErrors() && result.warningCount() == 0, sample);
        }
    }

    // Cada comando de sample/languages roda de verdade e termina com sucesso.
    void everyLanguagesSampleRuns()
    {
        const QJsonArray commands = sampleCommands(QStringLiteral("sample/languages/kai.json"));
        QVERIFY(commands.size() >= 6);
        for (const QJsonValue &value : commands) {
            Command cmd = Command::fromJson(value.toObject());
            cmd.id = QStringLiteral("sample_") + QString::number(qHash(cmd.name));
            if (cmd.language == CommandLanguage::Python) REQUIRE_INTERPRETER(CommandLanguage::Python);
            if (cmd.language == CommandLanguage::Node) REQUIRE_INTERPRETER(CommandLanguage::Node);
            if (cmd.name.contains(QStringLiteral("stdin"))) continue; // espera digitação (PTY)
            if (cmd.kip) continue; // sessão interativa: ver test_showcase

            EnvironmentManager env;
            env.setGlobalVars({{QStringLiteral("GREETING"), QStringLiteral("hello")}, {QStringLiteral("PORT"), QStringLiteral("8080")}});
            QMap<QString, QString> params;
            for (const Parameter &p : cmd.params) params.insert(p.name, p.defaultValue);
            env.setParamVars(params);
            QMap<QString, Command> all{{cmd.id, cmd}};
            ExecutionPipeline pipeline;
            QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
            pipeline.run(cmd, all, env);
            QVERIFY2(finishedSpy.wait(15000), qPrintable(cmd.name));
            QVERIFY2(qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success, qPrintable(cmd.name));
        }
    }

    void pythonKipSampleRunsEndToEnd()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Python);
        Command cmd;
        for (const QJsonValue &value : sampleCommands(QStringLiteral("sample/kip/kai.json"))) {
            if (value.toObject().value("language").toString() == QStringLiteral("python")) {
                cmd = Command::fromJson(value.toObject());
            }
        }
        QVERIFY(cmd.kip);
        cmd.id = QStringLiteral("py_kip_sample");
        QMap<QString, Command> all{{cmd.id, cmd}};
        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QTRY_VERIFY_WITH_TIMEOUT(pipeline.kipSessionFor(cmd.id)
                                     && pipeline.kipSessionFor(cmd.id)->screen().awaitingInput(), 10000);
        KipSession *session = pipeline.kipSessionFor(cmd.id);
        session->setFieldValue(QStringLiteral("env"), QStringLiteral("dev"));
        QVERIFY(session->submit());
        QVERIFY(finishedSpy.wait(10000));
        QVERIFY(qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success);
        QCOMPARE(session->screen().done()->title, QStringLiteral("Deployed to dev"));
    }

    void theWindowsLineLimitIsDetected()
    {
        QVERIFY(!kai::engine::exceedsWindowsCommandLine(QString(kai::engine::kWindowsCommandLineSoftLimit, QLatin1Char('x'))));
        QVERIFY(kai::engine::exceedsWindowsCommandLine(QString(kai::engine::kWindowsCommandLineSoftLimit + 1, QLatin1Char('x'))));
    }

    void pythonAndNodeModulesAreVisibleThroughTheWslProfile_data()
    {
        QTest::addColumn<int>("language");
        QTest::addColumn<bool>("kip");
        QTest::addColumn<QString>("code");
        QTest::newRow("python") << int(CommandLanguage::Python) << false
            << QStringLiteral("import kai\nprint('MODS', kai.__name__, callable(kai.notify))");
        QTest::newRow("python+kip") << int(CommandLanguage::Python) << true
            << QStringLiteral("import kai, kip\nprint('MODS', kai.__name__, kip.__name__, callable(kip.prompt))");
        QTest::newRow("node") << int(CommandLanguage::Node) << false
            << QStringLiteral("console.log('MODS', typeof kai.notify, typeof require('kai').notify)");
        QTest::newRow("node+kip") << int(CommandLanguage::Node) << true
            << QStringLiteral("console.log('MODS', typeof kai.notify, typeof kip.prompt, typeof require('kip').prompt)");
    }

    void pythonAndNodeModulesAreVisibleThroughTheWslProfile()
    {
        QFETCH(int, language);
        QFETCH(bool, kip);
        QFETCH(QString, code);
        const auto lang = CommandLanguage(language);
        REQUIRE_INTERPRETER(lang);
        QTemporaryDir bin;
        QVERIFY(bin.isValid());
        Command cmd;
        cmd.type = CommandType::Command;
        cmd.language = lang;
        cmd.kip = kip;
        cmd.command = code;
        const WslRun run = runThroughDefaultWslProfile(cmd, bin.path());
        qInfo() << "WSLRUN" << QTest::currentDataTag() << "line length" << run.lineLength << "output:" << run.output.left(200);
        // A linha que o Windows passa ao cmd.exe (limite 8191) fica com folga, mesmo com
        // os dois módulos injetados e o segundo base64 do template do WSL.
        QVERIFY(run.lineLength > 0);
        QVERIFY2(run.lineLength < kai::engine::kWindowsCommandLineSoftLimit,
                 qPrintable(QStringLiteral("linha com %1 caracteres").arg(run.lineLength)));
        if (kip) {
            // Num comando KIP a saída é protocolo, não texto: aqui só interessa que o programa subiu.
            return;
        }
        QVERIFY2(run.output.contains(QStringLiteral("MODS")), qPrintable(run.output));
        QVERIFY2(!run.output.contains(QStringLiteral("Error")), qPrintable(run.output));
    }

    void pythonKipCommandRunsAsASession()
    {
        REQUIRE_INTERPRETER(CommandLanguage::Python);
        Command cmd;
        cmd.id = "py_kip";
        cmd.name = "py_kip";
        cmd.type = CommandType::Command;
        cmd.language = CommandLanguage::Python;
        cmd.kip = true;
        cmd.command = QStringLiteral(
            "import kip\n"
            "ok = kip.confirm('Go?', id='go')\n"
            "kip.done(title='ok' if ok else 'no')\n");
        QMap<QString, Command> all{{cmd.id, cmd}};

        EnvironmentManager env;
        ExecutionPipeline pipeline;
        QSignalSpy finishedSpy(&pipeline, &ExecutionPipeline::pipelineFinished);
        pipeline.run(cmd, all, env);
        QTRY_VERIFY_WITH_TIMEOUT(pipeline.kipSessionFor(cmd.id)
                                     && pipeline.kipSessionFor(cmd.id)->screen().awaitingInput(), 10000);
        pipeline.kipSessionFor(cmd.id)->confirm(true);
        QVERIFY(finishedSpy.wait(10000));
        QVERIFY(qvariant_cast<PipelineResult>(finishedSpy.at(0).at(0)).success);
        const auto &done = pipeline.kipSessionFor(cmd.id)->screen().done();
        QVERIFY(done.has_value());
        QCOMPARE(done->title, QStringLiteral("ok"));
    }
};

QTEST_MAIN(TestCommandLanguage)
#include "test_command_language.moc"
