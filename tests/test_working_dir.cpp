#include <QTest>

#include "core/working-dir.h"

using namespace kai::core;

// Diretório de trabalho com herança: Inherit sobe a cadeia, Custom decide
// (relativo cola no que vem de cima), None corta. O caso que motivou o
// modelo: "o pai tem wd, mas o filho não quer ter".
class TestWorkingDir : public QObject {
    Q_OBJECT

private:
    static Folder folder(const QString &id, const QString &parent = QString())
    {
        Folder f;
        f.id = id;
        f.name = id;
        if (!parent.isEmpty()) {
            f.parentId = parent;
        }
        return f;
    }
    static void setDir(Folder &f, WorkingDirMode mode, const QString &path = QString())
    {
        f.workingDirMode = mode;
        f.workingDir = path;
    }

private slots:
    void inheritsFromTheNearestFolderThatDecides()
    {
        Folder root = folder("root");
        setDir(root, WorkingDirMode::Custom, "/work/app");
        Folder child = folder("child", "root");
        Folder grandchild = folder("grandchild", "child");
        const QVector<Folder> all{root, child, grandchild};
        QCOMPARE(effectiveFolderWorkingDir("grandchild", all), QStringLiteral("/work/app"));
        QCOMPARE(effectiveFolderWorkingDir("root", all), QStringLiteral("/work/app"));
    }

    void noDecisionAnywhereMeansEmpty()
    {
        const QVector<Folder> all{folder("a"), folder("b", "a")};
        QCOMPARE(effectiveFolderWorkingDir("b", all), QString());
        QCOMPARE(effectiveFolderWorkingDir("missing", all), QString());
    }

    void noneCutsTheInheritance()
    {
        Folder root = folder("root");
        setDir(root, WorkingDirMode::Custom, "/work/app");
        Folder child = folder("child", "root");
        setDir(child, WorkingDirMode::None);
        Folder grandchild = folder("grandchild", "child");
        const QVector<Folder> all{root, child, grandchild};
        QCOMPARE(effectiveFolderWorkingDir("child", all), QString());
        QCOMPARE(effectiveFolderWorkingDir("grandchild", all), QString());
        QCOMPARE(effectiveFolderWorkingDir("root", all), QStringLiteral("/work/app"));
    }

    void relativePathIsResolvedOverTheInheritedOne()
    {
        Folder root = folder("root");
        setDir(root, WorkingDirMode::Custom, "/work/app/");
        Folder child = folder("child", "root");
        setDir(child, WorkingDirMode::Custom, "services/api");
        const QVector<Folder> all{root, child};
        QCOMPARE(effectiveFolderWorkingDir("child", all), QStringLiteral("/work/app/services/api"));

        // Base no estilo Windows continua nele.
        Folder win = folder("win");
        setDir(win, WorkingDirMode::Custom, "C:\\proj");
        Folder winChild = folder("winChild", "win");
        setDir(winChild, WorkingDirMode::Custom, "src");
        QCOMPARE(effectiveFolderWorkingDir("winChild", {win, winChild}), QStringLiteral("C:\\proj\\src"));
    }

    void relativePathWithNothingAboveStaysAsIs()
    {
        Folder f = folder("f");
        setDir(f, WorkingDirMode::Custom, "rel/dir");
        QCOMPARE(effectiveFolderWorkingDir("f", {f}), QStringLiteral("rel/dir"));
    }

    void absoluteAndTemplatedPathsIgnoreTheParent()
    {
        for (const QString &abs : {QStringLiteral("/etc"), QStringLiteral("C:\\x"), QStringLiteral("D:/x"),
                                   QStringLiteral("\\\\wsl$\\Ubuntu\\home"), QStringLiteral("~/code"),
                                   QStringLiteral("{{HOME}}/code")}) {
            QVERIFY2(isAbsoluteWorkingDirPath(abs), qPrintable(abs));
            Folder root = folder("root");
            setDir(root, WorkingDirMode::Custom, "/work/app");
            Folder child = folder("child", "root");
            setDir(child, WorkingDirMode::Custom, abs);
            QCOMPARE(effectiveFolderWorkingDir("child", {root, child}), abs);
        }
        QVERIFY(!isAbsoluteWorkingDirPath(QStringLiteral("src")));
        QVERIFY(!isAbsoluteWorkingDirPath(QStringLiteral("./src")));
        QVERIFY(!isAbsoluteWorkingDirPath(QStringLiteral("../src")));
    }

    void commandInheritsFromItsFolderUnlessItDecides()
    {
        Folder root = folder("root");
        setDir(root, WorkingDirMode::Custom, "/work/app");
        const QVector<Folder> all{root};

        Command inherit;
        inherit.folderId = "root";
        QCOMPARE(effectiveCommandWorkingDir(inherit, all), QStringLiteral("/work/app"));

        Command custom = inherit;
        custom.workingDirMode = WorkingDirMode::Custom;
        custom.workingDir = "tools";
        QCOMPARE(effectiveCommandWorkingDir(custom, all), QStringLiteral("/work/app/tools"));
        custom.workingDir = "/tmp";
        QCOMPARE(effectiveCommandWorkingDir(custom, all), QStringLiteral("/tmp"));

        Command none = inherit;
        none.workingDirMode = WorkingDirMode::None;
        QCOMPARE(effectiveCommandWorkingDir(none, all), QString());

        Command orphan; // sem pasta
        QCOMPARE(effectiveCommandWorkingDir(orphan, all), QString());
    }

    void projectRootIsTheNearestProjectFolder()
    {
        Folder outer = folder("outer");
        outer.isProject = true;
        setDir(outer, WorkingDirMode::Custom, "/work/outer");
        Folder inner = folder("inner", "outer");
        inner.isProject = true;
        setDir(inner, WorkingDirMode::Custom, "pkg");
        Folder leaf = folder("leaf", "inner");
        Folder plain = folder("plain");
        const QVector<Folder> all{outer, inner, leaf, plain};

        const ProjectRoot fromLeaf = projectRootFor("leaf", all);
        QCOMPARE(fromLeaf.projectFolderId, QStringLiteral("inner"));
        QCOMPARE(fromLeaf.directory, QStringLiteral("/work/outer/pkg"));
        QCOMPARE(projectRootFor("outer", all).projectFolderId, QStringLiteral("outer"));
        QVERIFY(projectRootFor("plain", all).projectFolderId.isEmpty());
    }

    void cyclicParentsNeverHang()
    {
        Folder a = folder("a", "b");
        Folder b = folder("b", "a");
        QCOMPARE(effectiveFolderWorkingDir("a", {a, b}), QString());
        QVERIFY(projectRootFor("a", {a, b}).projectFolderId.isEmpty());
    }
};

QTEST_MAIN(TestWorkingDir)
#include "test_working_dir.moc"
