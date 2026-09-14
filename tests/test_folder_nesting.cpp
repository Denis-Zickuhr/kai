#include <QTest>

#include "core/folder-nesting.h"

using namespace kai::core;

class TestFolderNesting : public QObject {
    Q_OBJECT

    static Folder folder(const QString &id, const QString &name, const QString &parent = QString())
    {
        Folder f;
        f.id = id;
        f.name = name;
        if (!parent.isEmpty()) {
            f.parentId = parent;
        }
        return f;
    }

private slots:
    void splitTrimsAndDropsEmptySegments()
    {
        QCOMPARE(splitFolderPathName(QStringLiteral(" a / b //c ")), (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
        QVERIFY(splitFolderPathName(QStringLiteral("/ /")).isEmpty());
    }

    void onlyTwoOrMoreSegmentsCountAsNested()
    {
        QVERIFY(folderNameIsNested(QStringLiteral("a/b")));
        QVERIFY(!folderNameIsNested(QStringLiteral("a/")));
        QVERIFY(!folderNameIsNested(QStringLiteral("plain")));
    }

    void createsEveryLevelInsideTheChosenParent()
    {
        Folder leaf = folder(QStringLiteral("f_custom_a_b_c"), QStringLiteral("a/b/c"), QStringLiteral("root"));
        leaf.icon = QStringLiteral("rocket");
        const NestedFolderPlan plan = planNestedFolders(leaf, {folder(QStringLiteral("root"), QStringLiteral("Root"))});
        QCOMPARE(plan.created.size(), 3);
        QCOMPARE(plan.created[0].name, QStringLiteral("a"));
        QCOMPARE(plan.created[0].parentId, std::make_optional(QStringLiteral("root")));
        QCOMPARE(plan.created[1].name, QStringLiteral("b"));
        QCOMPARE(plan.created[1].parentId, std::make_optional(plan.created[0].id));
        QCOMPARE(plan.created[2].name, QStringLiteral("c"));
        QCOMPARE(plan.created[2].parentId, std::make_optional(plan.created[1].id));
        QCOMPARE(plan.leafId, plan.created[2].id);
        QCOMPARE(plan.created[2].icon, QStringLiteral("rocket"));
        QCOMPARE(plan.created[0].terminalTarget, QString::fromLatin1(kInheritTerminalTarget));
    }

    void topLevelNameStartsAtTheRoot()
    {
        const NestedFolderPlan plan = planNestedFolders(folder(QStringLiteral("x"), QStringLiteral("a/b")), {});
        QCOMPARE(plan.created.size(), 2);
        QVERIFY(!plan.created[0].parentId.has_value());
    }

    void reusesAnExistingIntermediateFolder()
    {
        const QVector<Folder> existing = {folder(QStringLiteral("f_a"), QStringLiteral("a"))};
        const NestedFolderPlan plan = planNestedFolders(folder(QStringLiteral("x"), QStringLiteral("a/b")), existing);
        QCOMPARE(plan.created.size(), 1);
        QCOMPARE(plan.created[0].name, QStringLiteral("b"));
        QCOMPARE(plan.created[0].parentId, std::make_optional(QStringLiteral("f_a")));
    }

    void sameNameUnderAnotherParentIsNotReused()
    {
        const QVector<Folder> existing = {folder(QStringLiteral("f_other"), QStringLiteral("a"), QStringLiteral("p"))};
        const NestedFolderPlan plan = planNestedFolders(folder(QStringLiteral("x"), QStringLiteral("a/b")), existing);
        QCOMPARE(plan.created.size(), 2);
    }

    void generatedIdsNeverCollide()
    {
        const QVector<Folder> existing = {folder(QStringLiteral("f_custom_b"), QStringLiteral("elsewhere"))};
        const NestedFolderPlan plan = planNestedFolders(folder(QStringLiteral("x"), QStringLiteral("a/b/a")), existing);
        QSet<QString> ids;
        for (const Folder &f : plan.created) {
            ids.insert(f.id);
        }
        QCOMPARE(ids.size(), plan.created.size());
        QVERIFY(!ids.contains(QStringLiteral("f_custom_b")));
    }
};

QTEST_APPLESS_MAIN(TestFolderNesting)
#include "test_folder_nesting.moc"
