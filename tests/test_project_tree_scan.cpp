#include <QTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/project-tree-scan.h"

using namespace kai;

class TestProjectTreeScan : public QObject {
    Q_OBJECT

private:
    static void touchProject(const QString &dir)
    {
        QDir().mkpath(dir);
        QFile file(dir + QStringLiteral("/kai.yml"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("project_name: X\n");
    }

private slots:
    void findsEveryProjectParentsFirst()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        touchProject(root);
        touchProject(root + QStringLiteral("/a"));
        touchProject(root + QStringLiteral("/a/inner"));
        touchProject(root + QStringLiteral("/b/c/d"));
        const QStringList found = core::findProjectDirectories(root);
        QCOMPARE(found, (QStringList{root, root + QStringLiteral("/a"), root + QStringLiteral("/a/inner"),
                                     root + QStringLiteral("/b/c/d")}));
    }

    void skipsHiddenAndDependencyFolders()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        touchProject(root + QStringLiteral("/.git/x"));
        touchProject(root + QStringLiteral("/node_modules/x"));
        touchProject(root + QStringLiteral("/vendor/x"));
        touchProject(root + QStringLiteral("/real"));
        QCOMPARE(core::findProjectDirectories(root), QStringList{root + QStringLiteral("/real")});
    }

    void respectsMaxDepthAndMissingRoot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        touchProject(dir.path() + QStringLiteral("/a/b/c"));
        QVERIFY(core::findProjectDirectories(dir.path(), 2).isEmpty());
        QCOMPARE(core::findProjectDirectories(dir.path(), 3).size(), 1);
        QVERIFY(core::findProjectDirectories(dir.path() + QStringLiteral("/nope")).isEmpty());
    }
};

QTEST_MAIN(TestProjectTreeScan)
#include "test_project_tree_scan.moc"
