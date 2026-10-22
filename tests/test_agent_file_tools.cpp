// Ferramentas de arquivo do agente: leitura/escrita só dentro das pastas-projeto, com tentativas de fuga
// (`..`, caminho absoluto, link simbólico, link quebrado, .git) e a aprovação de escrita.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "ai-fake-host.h"
#include "engine/ai/agent-toolbox.h"
#include "utils/translation-manager.h"

using namespace kai::core;
using namespace kai::engine;

namespace {

QString slurp(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

void spit(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
}

QString S(const char *s) { return QString::fromUtf8(s); }

} // namespace

class TestAgentFileTools : public QObject {
    Q_OBJECT

    QTemporaryDir tmp;
    QString project;
    QString outside;
    FakeAgentHost host;
    std::unique_ptr<AgentToolbox> box;

    ToolOutcome run(const QString &name, const QJsonObject &args)
    {
        ToolOutcome out;
        bool called = false;
        box->execute({S("id1"), name, args}, [&](const ToolOutcome &o) { out = o; called = true; });
        QTest::qWaitFor([&]() { return called; }, 5000);
        return out;
    }
    QJsonObject json(const ToolOutcome &o) { return QJsonDocument::fromJson(o.content.toUtf8()).object(); }
    ToolOutcome read(const QString &path) { return run(S("read_project_file"), {{S("path"), path}}); }
    ToolOutcome write(const QString &path, const QString &content, bool append = false)
    {
        return run(S("write_project_file"), {{S("path"), path}, {S("content"), content}, {S("append"), append}});
    }

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void init()
    {
        QVERIFY(tmp.isValid());
        QDir(tmp.path()).removeRecursively();
        project = tmp.path() + S("/proj");
        outside = tmp.path() + S("/outside");
        spit(project + S("/src/a.txt"), S("hello"));
        spit(outside + S("/secret.txt"), S("top secret"));
        host = FakeAgentHost();
        Folder &f = host.addFolder(S("p1"), S("Proj"));
        f.isProject = true;
        f.workingDir = project;
        box = std::make_unique<AgentToolbox>(&host);
    }

    void readsInsideProject()
    {
        const auto o = read(S("src/a.txt"));
        QVERIFY(!o.isError);
        QCOMPARE(json(o).value(S("content")).toString(), S("hello"));
        QVERIFY(!read(project + S("/src/a.txt")).isError);
    }

    void listsFolder()
    {
        const auto o = run(S("list_project_files"), {});
        QVERIFY(!o.isError);
        QVERIFY(o.content.contains(S("src/")));
    }

    void writeNeedsApprovalAndCreatesFolders()
    {
        const AiToolCall c{S("i"), S("write_project_file"), {{S("path"), S("new/dir/b.txt")}, {S("content"), S("data")}}};
        const ToolPlan plan = box->plan(c);
        QVERIFY(plan.valid);
        QCOMPARE(plan.risk, ToolRisk::Write);
        QVERIFY(plan.detail.contains(S("data")));
        QVERIFY(!QFileInfo::exists(project + S("/new/dir/b.txt"))); // planejar não escreve
        QVERIFY(!run(c.name, c.arguments).isError);
        QCOMPARE(slurp(project + S("/new/dir/b.txt")), S("data"));
    }

    void appendAndOverwrite()
    {
        QVERIFY(!write(S("src/a.txt"), S("+x"), true).isError);
        QCOMPARE(slurp(project + S("/src/a.txt")), S("hello+x"));
        QVERIFY(!write(S("src/a.txt"), S("z")).isError);
        QCOMPARE(slurp(project + S("/src/a.txt")), S("z"));
    }

    void dotDotEscapeIsRefused()
    {
        QVERIFY(!box->plan({S("i"), S("read_project_file"), {{S("path"), S("../outside/secret.txt")}}}).valid);
        const AiToolCall w{S("i"), S("write_project_file"), {{S("path"), S("src/../../outside/pwn.txt")}, {S("content"), S("x")}}};
        QVERIFY(!box->plan(w).valid);
        QVERIFY(run(w.name, w.arguments).isError); // mesmo sem passar pelo plano
        QVERIFY(!QFileInfo::exists(outside + S("/pwn.txt")));
    }

    void absolutePathOutsideIsRefused()
    {
        QVERIFY(read(outside + S("/secret.txt")).isError);
        QVERIFY(write(S("/etc/kai-test-pwn"), S("x")).isError);
        // prefixo parecido não conta ("proj-evil" não está em "proj")
        QVERIFY(read(project + S("-evil/x")).isError);
    }

#ifndef Q_OS_WIN
    void symlinkEscapeIsRefused()
    {
        QVERIFY(QFile::link(outside, project + S("/link")));
        QVERIFY(QFile::link(outside + S("/secret.txt"), project + S("/file-link")));
        QVERIFY(read(S("link/secret.txt")).isError);
        QVERIFY(read(S("file-link")).isError);
        QVERIFY(write(S("link/new.txt"), S("x")).isError);
        QVERIFY(write(S("file-link"), S("x")).isError);
        QCOMPARE(slurp(outside + S("/secret.txt")), S("top secret"));
        QVERIFY(!QFileInfo::exists(outside + S("/new.txt")));
    }

    void brokenSymlinkIsRefused()
    {
        QVERIFY(QFile::link(outside + S("/not-there.txt"), project + S("/dangling")));
        QVERIFY(write(S("dangling"), S("x")).isError);
        QVERIFY(!QFileInfo::exists(outside + S("/not-there.txt")));
    }

    void symlinkInsideProjectIsFine()
    {
        QVERIFY(QFile::link(project + S("/src"), project + S("/alias")));
        QVERIFY(!read(S("alias/a.txt")).isError);
    }
#endif

    void gitFolderIsNotWritable() { QVERIFY(write(S(".git/hooks/pre-commit"), S("x")).isError); }

    void noProjectMeansNoAccess()
    {
        host.data.folders.first().isProject = false;
        QVERIFY(!box->plan({S("i"), S("read_project_file"), {{S("path"), S("src/a.txt")}}}).valid);
        QVERIFY(read(S("src/a.txt")).isError);
    }

    void severalProjectsNeedAnId()
    {
        const QString other = tmp.path() + S("/other");
        spit(other + S("/o.txt"), S("other"));
        Folder &f = host.addFolder(S("p2"), S("Other"));
        f.isProject = true;
        f.workingDir = other;
        QVERIFY(read(S("o.txt")).isError);
        QVERIFY(!run(S("read_project_file"), {{S("path"), S("o.txt")}, {S("project_id"), S("p2")}}).isError);
    }

    void binaryAndOversizeAreRefused()
    {
        spit(project + S("/bin.dat"), QString(QChar(0)) + S("abc"));
        QVERIFY(read(S("bin.dat")).isError);
        const QString big(1024 * 1024 + 1, QLatin1Char('a'));
        QVERIFY(!box->plan({S("i"), S("write_project_file"), {{S("path"), S("big.txt")}, {S("content"), big}}}).valid);
    }
};

QTEST_GUILESS_MAIN(TestAgentFileTools)
#include "test_agent_file_tools.moc"
