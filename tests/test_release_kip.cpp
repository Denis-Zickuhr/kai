#include <QTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "engine/kip-session.h"

using namespace kai::core;
using namespace kai::engine;

// packaging/release.sh --kip: a mesma release do terminal, com a interface do Kai. O script roda
// DE VERDADE numa sandbox descartável — um repositório git próprio, um `origin` local e `docker`/`gh`
// de mentira — então nada de real é buildado, enviado ou publicado.

namespace {

constexpr int kWait = 20000;

QString repoRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFile::exists(dir.filePath(QStringLiteral("packaging/release.sh")))) return dir.absolutePath();
        if (!dir.cdUp()) break;
    }
    return QString();
}

QString run(const QString &command, const QString &cwd, const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment())
{
    QProcess p;
    p.setWorkingDirectory(cwd);
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("bash"), {QStringLiteral("-c"), command});
    p.waitForFinished(60000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void writeFile(const QString &path, const QString &text, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text.toUtf8());
    f.close();
    if (executable) f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup);
}

struct Sandbox {
    QTemporaryDir dir;
    QString repo() const { return dir.filePath(QStringLiteral("repo")); }
    QString origin() const { return dir.filePath(QStringLiteral("origin.git")); }
    QString bin() const { return dir.filePath(QStringLiteral("bin")); }

    explicit Sandbox(const QString &releaseScript)
    {
        QVERIFY(dir.isValid());
        run(QStringLiteral("git init -q --bare '%1'").arg(origin()), dir.path());
        QDir().mkpath(repo() + QStringLiteral("/packaging/windows"));
        run(QStringLiteral("git init -q -b main && git config user.email t@t && git config user.name T"), repo());
        writeFile(repo() + QStringLiteral("/CMakeLists.txt"),
                  QStringLiteral("cmake_minimum_required(VERSION 3.20)\nproject(kai VERSION 1.2.3 LANGUAGES CXX)\n"));
        writeFile(repo() + QStringLiteral("/packaging/windows/kai.nsi"),
                  QStringLiteral("!define APPVERSION \"1.2.3\"\nVIProductVersion \"1.2.3.0\"\n"));
        writeFile(repo() + QStringLiteral("/.gitignore"), QStringLiteral("dist/\n"));
        QFile::copy(releaseScript, repo() + QStringLiteral("/packaging/release.sh"));
        run(QStringLiteral("chmod +x packaging/release.sh && git add -A && git commit -qm init && "
                           "git remote add origin '%1' && git push -q origin main").arg(origin()), repo());
        // docker/gh de mentira: registram a chamada e (o docker) fabricam os artefatos.
        writeFile(bin() + QStringLiteral("/docker"), QStringLiteral(
            "#!/bin/bash\necho \"docker $*\" >> \"$SB/docker.log\"\necho \"fake docker: $*\"\n"
            "case \"$*\" in\n"
            "  *run*build-linux*) mkdir -p dist; echo AppImage > dist/Kai-x86_64.AppImage ;;\n"
            "  *run*build-windows*) mkdir -p dist/kai-windows; echo exe > dist/kai-setup.exe; echo k > dist/kai-windows/kai.exe ;;\n"
            "esac\n"
            "[ -n \"${FAKE_DOCKER_FAIL:-}\" ] && { echo \"fake docker: simulated failure\" >&2; exit 3; }\nexit 0\n"), true);
        writeFile(bin() + QStringLiteral("/gh"), QStringLiteral(
            "#!/bin/bash\necho \"gh $*\" >> \"$SB/gh.log\"\n"
            "if [ \"$1 $2\" = \"release view\" ]; then\n"
            "  if [ \"${4:-}\" = \"--json\" ]; then echo \"https://example.com/releases/$3\"; exit 0; fi\n"
            "  grep -qx \"$3\" \"$SB/gh.releases\" 2>/dev/null && exit 0 || exit 1\nfi\n"
            "if [ \"$1 $2\" = \"release create\" ]; then echo \"$3\" >> \"$SB/gh.releases\"; fi\nexit 0\n"), true);
    }

    QMap<QString, QString> env(const QMap<QString, QString> &extra = {}) const
    {
        QMap<QString, QString> e;
        e.insert(QStringLiteral("SB"), dir.path());
        e.insert(QStringLiteral("PATH"), bin() + QStringLiteral(":/usr/bin:/bin"));
        e.insert(QStringLiteral("KIP_LOCALE"), QStringLiteral("en"));
        for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) e.insert(it.key(), it.value());
        return e;
    }
    QString git(const QString &args) const { return run(QStringLiteral("git ") + args, repo()); }
    QString cmakeVersionLine() const
    {
        QFile f(repo() + QStringLiteral("/CMakeLists.txt"));
        f.open(QIODevice::ReadOnly);
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n')).value(1);
    }
};

struct Run {
    ProcessRunner runner;
    KipSession session{&runner};
    explicit Run(Sandbox &sb, const QString &args = QString(), const QMap<QString, QString> &extraEnv = {})
    {
        runner.setKillTimeoutMs(400);
        session.start(QStringLiteral("bash packaging/release.sh --kip %1").arg(args), sb.repo(), sb.env(extraEnv));
    }
    bool awaiting(const QString &id) const
    {
        const auto &o = session.screen().openScreen();
        return o && o->id == id && session.screen().awaitingInput() && !o->locked;
    }
    void waitFor(const QString &id) { QTRY_VERIFY_WITH_TIMEOUT(awaiting(id), kWait); }
    void waitFinished() { QTRY_VERIFY_WITH_TIMEOUT(session.outcome().finished, kWait); }
    const KipDone &done() const { return *session.screen().done(); }

    // Preenche o formulário "release" (valores como o Kai os mandaria).
    void answer(const QString &bump, const QString &version, const QString &build, bool commit, bool tag, bool push, bool release)
    {
        session.setFieldValue(QStringLiteral("bump"), bump);
        session.setFieldValue(QStringLiteral("version"), version);
        session.setFieldValue(QStringLiteral("build"), build);
        session.setFieldValue(QStringLiteral("steps"), QJsonObject{{"commit", commit}, {"tag", tag}, {"push", push}, {"release", release}});
    }
};

} // namespace

class TestReleaseKip : public QObject {
    Q_OBJECT

private:
    QString script;

private slots:
    void initTestCase()
    {
        qRegisterMetaType<kai::engine::KipOutcome>();
        qRegisterMetaType<kai::core::KipSessionState>();
        qRegisterMetaType<kai::core::KipLevel>();
        script = repoRoot() + QStringLiteral("/packaging/release.sh");
        if (repoRoot().isEmpty() || QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("bash")).isEmpty()) {
            QSKIP("precisa de git e bash");
        }
    }

    void kipFlowBumpsCommitsAndTagsWithoutPushing()
    {
        Sandbox sb(script);
        Run r(sb);
        r.waitFor(QStringLiteral("release"));
        QCOMPARE(r.session.screen().title(), QStringLiteral("Kai release"));
        r.answer(QStringLiteral("patch"), QString(), QStringLiteral("none"), true, true, false, false);
        QVERIFY(r.session.submit());
        r.waitFor(QStringLiteral("plan"));
        const auto &plan = *r.session.screen().openScreen();
        QVERIFY(plan.text.contains(QStringLiteral("1.2.3 -> 1.2.4")));
        QVERIFY2(!plan.danger, "sem push nem Release a confirmação não é perigosa");
        QVERIFY(r.session.confirm(true));
        r.waitFinished();
        QVERIFY(r.session.outcome().success);
        QCOMPARE(r.done().title, QStringLiteral("Release 1.2.4 done"));
        QVERIFY(sb.cmakeVersionLine().contains(QStringLiteral("1.2.4")));
        QCOMPARE(sb.git(QStringLiteral("log -1 --format=%s")), QStringLiteral("chore(release): bump version to 1.2.4"));
        QCOMPARE(sb.git(QStringLiteral("tag")), QStringLiteral("v1.2.4"));
        QVERIFY2(run(QStringLiteral("git tag"), sb.origin()).isEmpty(), "nada foi enviado ao origin");
        QVERIFY(!QFile::exists(sb.dir.filePath(QStringLiteral("docker.log")))); // sem build
    }

    void pushAndReleaseAreBehindARedConfirmation()
    {
        Sandbox sb(script);
        Run r(sb);
        r.waitFor(QStringLiteral("release"));
        r.answer(QStringLiteral("custom"), QStringLiteral("2.0.0"), QStringLiteral("both"), true, true, true, true);
        QVERIFY(r.session.submit());
        r.waitFor(QStringLiteral("plan"));
        QVERIFY2(r.session.screen().openScreen()->danger, "push/Release são ações externas: confirmação vermelha");
        QVERIFY(r.session.screen().openScreen()->back);
        QVERIFY(r.session.confirm(true));
        r.waitFinished();
        QVERIFY2(r.session.outcome().success, qPrintable(r.session.logText()));
        // build (Linux + Windows) de mentira, empacotado com a versão
        QVERIFY(QFile::exists(sb.repo() + QStringLiteral("/dist/kai-2.0.0-linux-x86_64.AppImage")));
        QVERIFY(QFile::exists(sb.repo() + QStringLiteral("/dist/kai-2.0.0-setup.exe")));
        QVERIFY(QFile::exists(sb.repo() + QStringLiteral("/dist/kai-2.0.0-windows-x86_64.zip")));
        // branch e tag foram para o origin local; a Release foi criada com os 3 artefatos
        QCOMPARE(run(QStringLiteral("git tag"), sb.origin()), QStringLiteral("v2.0.0"));
        QCOMPARE(run(QStringLiteral("git rev-parse main"), sb.origin()), sb.git(QStringLiteral("rev-parse HEAD")));
        QFile gh(sb.dir.filePath(QStringLiteral("gh.log")));
        QVERIFY(gh.open(QIODevice::ReadOnly));
        const QString ghLog = QString::fromUtf8(gh.readAll());
        QVERIFY(ghLog.contains(QStringLiteral("release create v2.0.0")));
        QVERIFY(ghLog.contains(QStringLiteral("kai-2.0.0-windows-x86_64.zip")));
        // o docker rodou sem TTY (-T), senão travaria sob o Kai
        QFile docker(sb.dir.filePath(QStringLiteral("docker.log")));
        QVERIFY(docker.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(docker.readAll()).contains(QStringLiteral("run --rm -T build-linux")));
        // o cartão final oferece abrir a Release
        bool opensRelease = false;
        for (const KipAction &a : r.done().actions) {
            if (a.type == KipActionType::OpenUrl && a.url.contains(QStringLiteral("v2.0.0"))) opensRelease = true;
        }
        QVERIFY(opensRelease);
    }

    void validationKeepsTheSameFormOpen()
    {
        Sandbox sb(script);
        sb.git(QStringLiteral("tag v1.2.4")); // a tag que o patch geraria já existe
        Run r(sb);
        r.waitFor(QStringLiteral("release"));
        r.answer(QStringLiteral("custom"), QString(), QStringLiteral("none"), true, false, false, false);
        QVERIFY(r.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(!r.session.screen().openScreen()->errors.value(QStringLiteral("version")).isEmpty(), kWait);
        r.waitFor(QStringLiteral("release")); // o MESMO prompt, destravado
        r.answer(QStringLiteral("custom"), QStringLiteral("abc"), QStringLiteral("none"), true, false, false, false);
        QVERIFY(r.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(r.session.screen().openScreen()->errors.value(QStringLiteral("version")).contains(QStringLiteral("abc")), kWait);
        r.waitFor(QStringLiteral("release"));
        // tag que já existe: erro geral do formulário
        r.answer(QStringLiteral("patch"), QString(), QStringLiteral("none"), true, true, false, false);
        QVERIFY(r.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(r.session.screen().openScreen()->message.contains(QStringLiteral("v1.2.4")), kWait);
        QCOMPARE(sb.cmakeVersionLine().contains(QStringLiteral("1.2.3")), true); // nada mudou
        r.session.cancel();
    }

    void backReopensTheFormAndDecliningChangesNothing()
    {
        Sandbox sb(script);
        const QString head = sb.git(QStringLiteral("rev-parse HEAD"));
        Run r(sb);
        r.waitFor(QStringLiteral("release"));
        r.answer(QStringLiteral("patch"), QString(), QStringLiteral("none"), true, true, false, false);
        QVERIFY(r.session.submit());
        r.waitFor(QStringLiteral("plan"));
        r.session.back();
        r.waitFor(QStringLiteral("release"));
        r.answer(QStringLiteral("patch"), QString(), QStringLiteral("none"), true, true, false, false);
        QVERIFY(r.session.submit());
        r.waitFor(QStringLiteral("plan"));
        QVERIFY(r.session.confirm(false));
        r.waitFinished();
        QVERIFY(r.session.outcome().success);
        QCOMPARE(r.done().title, QStringLiteral("Cancelled"));
        QCOMPARE(sb.git(QStringLiteral("rev-parse HEAD")), head);
        QVERIFY(sb.cmakeVersionLine().contains(QStringLiteral("1.2.3")));
        QVERIFY(sb.git(QStringLiteral("tag")).isEmpty());
    }

    void uncommittedChangesAreConfirmedFirst()
    {
        Sandbox sb(script);
        writeFile(sb.repo() + QStringLiteral("/CMakeLists.txt"),
                  QStringLiteral("cmake_minimum_required(VERSION 3.20)\nproject(kai VERSION 1.2.3 LANGUAGES CXX)\n# wip\n"));
        {
            Run r(sb);
            r.waitFor(QStringLiteral("dirty"));
            QVERIFY(r.session.screen().openScreen()->text.contains(QStringLiteral("CMakeLists.txt")));
            QVERIFY(r.session.confirm(false));
            r.waitFinished();
            QCOMPARE(r.done().title, QStringLiteral("Stopped"));
        }
        Run again(sb);
        again.waitFor(QStringLiteral("dirty"));
        QVERIFY(again.session.confirm(true));
        again.waitFor(QStringLiteral("release"));
        again.session.cancel();
        Run allowed(sb, QStringLiteral("--allow-dirty"));
        allowed.waitFor(QStringLiteral("release")); // com a flag, nem pergunta
        allowed.session.cancel();
    }

    void aFailingBuildShowsTheStepAsErrorAndTheLogTail()
    {
        Sandbox sb(script);
        Run r(sb, QString(), {{QStringLiteral("FAKE_DOCKER_FAIL"), QStringLiteral("1")}});
        r.waitFor(QStringLiteral("release"));
        r.answer(QStringLiteral("patch"), QString(), QStringLiteral("linux"), true, true, false, false);
        QVERIFY(r.session.submit());
        r.waitFor(QStringLiteral("plan"));
        QVERIFY(r.session.confirm(true));
        r.waitFinished();
        QVERIFY(!r.session.outcome().success); // saída != 0
        QCOMPARE(r.done().level, KipLevel::Error);
        QVERIFY(r.done().text.contains(QStringLiteral("simulated failure")));
        QVERIFY2(r.done().text.contains(QStringLiteral("1.2.4")), "avisa que o bump continua no working tree");
        bool buildError = false;
        for (const KipBlock &b : r.session.screen().blocks()) {
            if (const auto *steps = std::get_if<KipSteps>(&b)) {
                for (const KipStepItem &item : steps->items) {
                    if (item.id == QStringLiteral("build_linux") && item.state == KipStepState::Error) buildError = true;
                }
            }
        }
        QVERIFY(buildError);
        QVERIFY(sb.git(QStringLiteral("tag")).isEmpty()); // não chegou a taguear
    }

    // Sem --kip nada mudou: as flags e o --yes continuam fazendo tudo no terminal.
    void terminalModeIsUnchanged()
    {
        Sandbox sb(script);
        QProcess p;
        p.setWorkingDirectory(sb.repo());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QMap<QString, QString> sandboxEnv = sb.env();
        for (auto it = sandboxEnv.constBegin(); it != sandboxEnv.constEnd(); ++it) env.insert(it.key(), it.value());
        p.setProcessEnvironment(env);
        p.start(QStringLiteral("bash"), {QStringLiteral("packaging/release.sh"), QStringLiteral("--bump=patch"),
                                         QStringLiteral("--build=none"), QStringLiteral("--commit"), QStringLiteral("--tag"),
                                         QStringLiteral("--no-push"), QStringLiteral("--no-release"), QStringLiteral("-y")});
        QVERIFY(p.waitForFinished(30000));
        QCOMPARE(p.exitCode(), 0);
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        QVERIFY(!out.contains(QStringLiteral("\"kip\"")));              // nada de protocolo no terminal
        QVERIFY(out.contains(QStringLiteral("Plano:")));
        QVERIFY(sb.cmakeVersionLine().contains(QStringLiteral("1.2.4")));
        QCOMPARE(sb.git(QStringLiteral("tag")), QStringLiteral("v1.2.4"));
    }
};

QTEST_MAIN(TestReleaseKip)
#include "test_release_kip.moc"
