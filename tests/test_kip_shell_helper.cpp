#include <QTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/interpreter-settings.h"
#include "core/kip-cli-builder.h"
#include "core/kip-protocol.h"
#include "core/models.h"
#include "engine/command-language.h"
#include "engine/execution-pipeline.h"
#include "core/environment-manager.h"

using namespace kai;
using namespace kai::core;
using namespace kai::engine;

namespace {

QJsonObject corpus()
{
    QFile file(QStringLiteral("tests/data/kip-helper-cases.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &v : value.toArray()) {
        out << v.toString();
    }
    return out;
}

// A mensagem que o parser do Kai enxerga (as chaves vêm normalizadas), ou "" se não for uma mensagem válida.
QByteArray normalized(const QByteArray &line)
{
    const KipParseResult parsed = parseKipLine(QString::fromUtf8(line).trimmed());
    if (parsed.kind != KipParseResult::Kind::Message || !parsed.message) {
        return {};
    }
    return kipSerializeMessage(*parsed.message).trimmed();
}

struct RunResult {
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
};

RunResult run(const QString &program, const QStringList &args, const QProcessEnvironment &env = {},
              const QByteArray &input = {})
{
    QProcess process;
    if (!env.isEmpty()) {
        process.setProcessEnvironment(env);
    }
    process.start(program, args);
    if (!process.waitForStarted(5000)) {
        return {};
    }
    process.write(input);
    process.closeWriteChannel();
    process.waitForFinished(20000);
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

// Um diretório com um `awk` apontando para a implementação pedida (mawk, gawk, busybox...), na frente do PATH.
QProcessEnvironment envWithAwk(QTemporaryDir &dir, const QString &awkProgram)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (awkProgram.isEmpty()) {
        return env;
    }
    const QString link = dir.filePath(QStringLiteral("awk"));
    QFile::remove(link);
    if (awkProgram.endsWith(QLatin1String("busybox"))) {
        QFile script(link);
        script.open(QIODevice::WriteOnly);
        script.write(("#!/bin/sh\nexec " + awkProgram + " awk \"$@\"\n").toUtf8());
        script.close();
        script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    } else {
        QFile::link(awkProgram, link);
    }
    env.insert(QStringLiteral("PATH"), dir.path() + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
    return env;
}

} // namespace

// O helper `kip` injetado em comandos nativos com KIP num shell POSIX: o MESMO resultado do `kai kip` (a referência, em C++)
// para cada verbo, opção e erro, em todos os shells e awks disponíveis, e a linha de comando de verdade rodando.
class TestKipShellHelper : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QString m_shPrelude;   // kip.sh + kip.awk montados, como o Kai os injeta
    QStringList m_awks;    // "" = o awk padrão; senão o caminho do outro awk

    // O que o Kai monta para um comando Native com KIP num shell POSIX: o carregador do helper e, em seguida, o texto.
    static QString kipLine(const QString &code) { return kipShellLoader() + QLatin1Char('\n') + code; }

    static QString exe(const char *name) { return QStandardPaths::findExecutable(QString::fromLatin1(name)); }

    QString writeFile(const QString &name, const QString &content)
    {
        const QString path = m_tmp.filePath(name);
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        file.write(content.toUtf8());
        return path;
    }

    // Roda `kip <args>` num shell POSIX com o helper carregado; devolve o resultado.
    RunResult runSh(const QString &shell, const QStringList &args, const QProcessEnvironment &env)
    {
        const QString prelude = writeFile(QStringLiteral("prelude.sh"), m_shPrelude);
        return run(shell, QStringList{QStringLiteral("-c"), QStringLiteral(". \"$0\"; _kip_h=1; kip \"$@\""), prelude} + args, env);
    }

    void compareBuild(const QString &label, const QStringList &args, const RunResult &got)
    {
        const KipCliResult reference = runKipCli(args);
        if (reference.exitCode == 0) {
            QVERIFY2(got.exitCode == 0, qPrintable(label + QStringLiteral(" failed: ") + args.join(QLatin1Char(' '))
                                                   + QStringLiteral(" :: ") + QString::fromUtf8(got.err)));
            const QByteArray expected = normalized(reference.output);
            QVERIFY2(!expected.isEmpty(), qPrintable(label + QStringLiteral(": reference not a message")));
            QVERIFY2(normalized(got.out) == expected,
                     qPrintable(label + QStringLiteral(" differs for ") + args.join(QLatin1Char(' '))
                                + QStringLiteral("\n  reference: ") + QString::fromUtf8(expected)
                                + QStringLiteral("\n  helper:    ") + QString::fromUtf8(normalized(got.out))));
        } else {
            QVERIFY2(got.exitCode != 0, qPrintable(label + QStringLiteral(" accepted what kai kip rejects: ")
                                                   + args.join(QLatin1Char(' '))));
            QVERIFY2(got.out.trimmed().isEmpty(), qPrintable(label + QStringLiteral(": stdout must stay empty on a usage error")));
        }
    }

    void compareGet(const QString &label, const QString &json, const QString &path, const RunResult &got)
    {
        const KipCliResult reference = kipCliGet(json, path);
        const QString what = label + QStringLiteral(" get ") + path + QStringLiteral(" on ") + json.left(80);
        if (reference.exitCode == 0) {
            QVERIFY2(got.exitCode == 0, qPrintable(what + QStringLiteral(" failed :: ") + QString::fromUtf8(got.err)));
            QVERIFY2(QString::fromUtf8(got.out).replace(QStringLiteral("\r\n"), QStringLiteral("\n")) == QString::fromUtf8(reference.output),
                     qPrintable(what + QStringLiteral("\n  reference: ") + QString::fromUtf8(reference.output)
                                + QStringLiteral("\n  helper:    ") + QString::fromUtf8(got.out)));
        } else {
            QCOMPARE(got.exitCode, reference.exitCode);
            QVERIFY2(got.out.trimmed().isEmpty(), qPrintable(what));
        }
    }

private slots:
    void initTestCase()
    {
        m_shPrelude = kipModuleSource(CommandLanguage::Native);
        QVERIFY(!m_shPrelude.isEmpty());
        QVERIFY2(!corpus().isEmpty(), "tests/data/kip-helper-cases.json (run from the repository root)");
        m_awks << QString();
        for (const char *name : {"mawk", "gawk", "busybox"}) {
            if (!exe(name).isEmpty()) {
                m_awks << exe(name);
            }
        }
    }

    // O programa awk vai dentro de uma string de shell entre aspas simples: não pode ter nenhuma.
    void theAwkProgramHasNoSingleQuotesBecauseItLivesInASingleQuotedShellString()
    {
        QFile awk(QStringLiteral("src/engine/kip-modules/kip.awk"));
        QVERIFY(awk.open(QIODevice::ReadOnly));
        QVERIFY(!QString::fromUtf8(awk.readAll()).contains(QLatin1Char('\'')));
        QVERIFY(!m_shPrelude.contains(QStringLiteral("@@AWK@@")));
    }

    void everyVerbOfTheShHelperMatchesKaiKip_data()
    {
        QTest::addColumn<QString>("shell");
        QTest::addColumn<QString>("awk");
        for (const char *shell : {"bash", "dash", "sh"}) {
            if (exe(shell).isEmpty()) continue;
            for (const QString &awk : std::as_const(m_awks)) {
                QTest::newRow(qPrintable(QStringLiteral("%1 + %2").arg(QLatin1String(shell), awk.isEmpty() ? QStringLiteral("awk") : QFileInfo(awk).fileName())))
                    << exe(shell) << awk;
            }
        }
    }
    void everyVerbOfTheShHelperMatchesKaiKip()
    {
        QFETCH(QString, shell);
        QFETCH(QString, awk);
        QTemporaryDir awkDir;
        const QProcessEnvironment env = envWithAwk(awkDir, awk);
        int cases = 0;
        for (const QJsonValue &c : corpus().value(QStringLiteral("build")).toArray()) {
            compareBuild(QFileInfo(shell).fileName(), stringList(c), runSh(shell, stringList(c), env));
            if (QTest::currentTestFailed()) return;
            ++cases;
        }
        QVERIFY(cases >= 100);
    }

    void getMatchesKaiKipGetInTheShHelper_data() { everyVerbOfTheShHelperMatchesKaiKip_data(); }
    void getMatchesKaiKipGetInTheShHelper()
    {
        QFETCH(QString, shell);
        QFETCH(QString, awk);
        QTemporaryDir awkDir;
        const QProcessEnvironment env = envWithAwk(awkDir, awk);
        const QString prelude = writeFile(QStringLiteral("prelude.sh"), m_shPrelude);
        for (const QJsonValue &c : corpus().value(QStringLiteral("get")).toArray()) {
            const QString json = c.toArray().at(0).toString();
            const QString path = c.toArray().at(1).toString();
            compareGet(QFileInfo(shell).fileName(), json, path,
                       run(shell, {QStringLiteral("-c"), QStringLiteral(". \"$0\"; kip get \"$1\" \"$2\""), prelude, json, path}, env));
            if (QTest::currentTestFailed()) return;
        }
    }

    // ---- a linha de comando que o Kai executa ---------------------------------------------------------

    // ---- a linha de comando que o Kai executa ---------------------------------------------------------

    // O carregador define `kip` em qualquer shell POSIX, com env e stdin livres e o código de saída do script.
    void theLoaderLineDefinesKipInEveryPosixShell_data()
    {
        QTest::addColumn<QString>("shell");
        for (const char *shell : {"bash", "sh", "dash"}) {
            if (!exe(shell).isEmpty()) {
                QTest::newRow(shell) << QString::fromLatin1(shell);
            }
        }
    }
    void theLoaderLineDefinesKipInEveryPosixShell()
    {
        QFETCH(QString, shell);
        const QString code = QStringLiteral("read -r x\nkip hello --title T\necho \"got:$x env:$FOO\"\nexit 3\n");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("FOO"), QStringLiteral("bar"));
        const RunResult r = run(shell, {QStringLiteral("-c"), kipLine(code)}, env, "hello\n");
        QCOMPARE(r.exitCode, 3);
        QVERIFY2(r.out.contains("got:hello env:bar"), r.out.constData());
        QVERIFY(r.out.contains("\"type\":\"hello\""));
    }

    // Native + KIP num shell POSIX: o pipeline põe o carregador na frente do texto (já interpolado); sem KIP o
    // texto segue como está, e fora de um shell POSIX (cmd.exe do Windows) também.
    void nativeKipCommandsGetTheHelperAndStillInterpolateVariables()
    {
        Command cmd;
        cmd.id = QStringLiteral("c");
        cmd.name = QStringLiteral("c");
        cmd.type = CommandType::Command;
        cmd.kip = true;
        cmd.command = QStringLiteral("kip message \"{{GREETING}}\"");
        EnvironmentManager env;
        env.setFolderVars({{QStringLiteral("GREETING"), QStringLiteral("hello")}});
        ExecutionPipeline pipeline;
        const QString line = pipeline.commandLineFor(cmd, env);
#if defined(Q_OS_WIN)
        QCOMPARE(line, QStringLiteral("kip message \"hello\""));
#else
        QVERIFY(line.startsWith(kipShellLoader() + QLatin1Char('\n')));
        QVERIFY(line.endsWith(QStringLiteral("kip message \"hello\"")));
#endif
        cmd.kip = false;
        QCOMPARE(pipeline.commandLineFor(cmd, env), QStringLiteral("kip message \"hello\""));
    }

    // Um comando KIP em bash de ponta a ponta: o script usa `kip` sem o binário kai e o Kai o lê como protocolo.
    void aBashKipScriptSpeaksProtocolThroughTheInjectedHelper()
    {
        const QString code = QStringLiteral(
            "kip prompt --id name --title 'Who are you?' --field text who Name --required\n"
            "kip recv\n"
            "who=$(kip get \"$KIP_MSG\" values.who)\n"
            "kip done --title \"Hello, $who!\" --action copy:Copy:$who\n");
        const QString line = kipLine(code);
        const RunResult r = run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), line}, {},
                                "{\"kip\":1,\"type\":\"response\",\"id\":\"name\",\"values\":{\"who\":\"Ana \\\"A\\\" é\"}}\n");
        QCOMPARE(r.exitCode, 0);
        const QList<QByteArray> lines = r.out.trimmed().split('\n');
        QCOMPARE(lines.size(), 3); // hello (automático), prompt, done
        for (const QByteArray &l : lines) {
            QVERIFY2(!normalized(l).isEmpty(), l.constData());
        }
        QVERIFY(QJsonDocument::fromJson(lines.last()).object().value(QStringLiteral("title")).toString()
                == QStringLiteral("Hello, Ana \"A\" é!"));
    }

    // O Python/Node mandam o hello sozinhos; o helper também, antes da PRIMEIRA mensagem (e uma vez só).
    void theFirstMessageIsPrecededByAHelloTheScriptDidNotSend()
    {
        const QString body = QStringLiteral("kip message hi\nkip progress 5\n");
        RunResult r = run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
            kipLine(body)});
        QList<QByteArray> lines = r.out.trimmed().split('\n');
        QCOMPARE(lines.size(), 3);
        QVERIFY(lines.at(0).contains("\"type\":\"hello\""));
        QVERIFY(lines.at(1).contains("\"type\":\"message\""));
        // Com `kip hello --title` explícito primeiro, não há um segundo hello.
        r = run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
            kipLine(QStringLiteral("kip hello --title T\nkip message hi"))});
        lines = r.out.trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        QVERIFY(lines.at(0).contains("\"title\":\"T\""));
        // get/recv não são mensagens: nada de hello por causa deles.
        r = run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
            kipLine(QStringLiteral("kip get '{\"a\":1}' a"))});
        QCOMPARE(r.out.trimmed(), QByteArray("1"));
    }

    void cancelFromKaiMakesRecvExitWith130()
    {
        const QString line = kipLine(QStringLiteral("kip hello\nkip recv\necho never"));
        const RunResult r = run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), line}, {}, "{\"kip\":1,\"type\":\"cancel\"}\n");
        QCOMPARE(r.exitCode, 130);
        QVERIFY(!r.out.contains("never"));
        // stdin fechado também encerra (Kai foi embora).
        const RunResult eof = run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), line}, {}, {});
        QCOMPARE(eof.exitCode, 130);
    }

    // ---- o helper em arquivo (linha de comando curta: o Windows corta em 8191 caracteres) ----------------

    // ---- modelo e configurações ---------------------------------------------------------------------

    void languagesRoundTripThroughTheirNames()
    {
        for (const char *name : {"native", "python", "node", "php"}) {
            QCOMPARE(commandLanguageToString(commandLanguageFromString(QString::fromLatin1(name))), QString::fromLatin1(name));
        }
        // Desconhecidas e as antigas linguagens de shell (hoje o próprio Native) leem como Native.
        for (const char *name : {"zsh", "bash", "sh", "pwsh"}) {
            QCOMPARE(commandLanguageFromString(QString::fromLatin1(name)), CommandLanguage::Native);
        }
        Command c;
        c.language = CommandLanguage::Php;
        c.kip = true;
        QCOMPARE(Command::fromJson(c.toJson()).language, CommandLanguage::Php);
    }

    void interpreterDefaultsAndOverrides()
    {
        InterpreterSettings s;
        QCOMPARE(s.resolve(CommandLanguage::Php, QString()), QStringLiteral("php"));
        QCOMPARE(s.resolve(CommandLanguage::Python, QString()), QStringLiteral("python3"));
        QCOMPARE(s.resolve(CommandLanguage::Python, QString(), true), QStringLiteral("python")); // Windows sem alvo
        QCOMPARE(s.resolve(CommandLanguage::Php, QString(), true), QStringLiteral("php"));
        s.php = QStringLiteral("/usr/bin/php8.3 -d memory_limit=1G");
        QCOMPARE(s.resolve(CommandLanguage::Php, QString()), QStringLiteral("/usr/bin/php8.3 -d memory_limit=1G"));
        QCOMPARE(s.resolve(CommandLanguage::Php, QStringLiteral(" /opt/php ")), QStringLiteral("/opt/php"));
        QVERIFY(s.resolve(CommandLanguage::Native, QStringLiteral("x")).isEmpty());
        QVERIFY(languageEnvDefaults(CommandLanguage::Php).isEmpty()); // sem módulo `kai` no PHP
    }
};

QTEST_MAIN(TestKipShellHelper)
#include "test_kip_shell_helper.moc"
