#include <QTest>
#include <QApplication>
#include <QAbstractItemView>
#include <QVariantList>

#include "ui/shared/folder-picker-widget.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

namespace {
constexpr int kDepthRole = Qt::UserRole + 2;
constexpr int kGuidesRole = Qt::UserRole + 3;

Folder makeFolder(const QString &id, const QString &name, const QString &parentId = QString())
{
    Folder f;
    f.id = id;
    f.name = name;
    f.parentId = parentId.isEmpty() ? std::nullopt : std::make_optional(parentId);
    return f;
}

QStringList idsOf(const FolderPickerWidget &picker)
{
    QStringList ids;
    for (int i = 0; i < picker.count(); ++i) {
        ids << picker.itemData(i, Qt::UserRole).toString();
    }
    return ids;
}
}

class TestFolderPickerWidget : public QObject {
    Q_OBJECT

private slots:
    void itemsFollowTreeOrderWithDepth()
    {
        FolderPickerWidget picker;
        // Filho antes do pai e irmãos intercalados na entrada.
        picker.setFolders({makeFolder("b1", "B1", "b"), makeFolder("a", "A"), makeFolder("b", "B"),
                           makeFolder("a1", "A1", "a"), makeFolder("b2", "B2", "b"),
                           makeFolder("a11", "A11", "a1")});
        const QStringList ids = idsOf(picker);
        QCOMPARE(ids.indexOf("a") < ids.indexOf("a1"), true);
        QCOMPARE(ids.indexOf("a1") + 1, ids.indexOf("a11"));
        QCOMPARE(ids.indexOf("b") < ids.indexOf("b1"), true);
        QCOMPARE(ids.indexOf("b1") < ids.indexOf("b2"), true);

        QCOMPARE(picker.itemData(ids.indexOf("a"), kDepthRole).toInt(), 0);
        QCOMPARE(picker.itemData(ids.indexOf("a1"), kDepthRole).toInt(), 1);
        QCOMPARE(picker.itemData(ids.indexOf("a11"), kDepthRole).toInt(), 2);
        // b1 tem irmão depois (b2); b2 é o último.
        QCOMPARE(picker.itemData(ids.indexOf("b1"), kGuidesRole).toList().last().toBool(), true);
        QCOMPARE(picker.itemData(ids.indexOf("b2"), kGuidesRole).toList().last().toBool(), false);
    }

    void selectionRoundTripsWithTheNoneOption()
    {
        FolderPickerWidget picker;
        picker.setFolders({makeFolder("a", "A"), makeFolder("a1", "A1", "a")});
        picker.enableNoneOption(QStringLiteral("(none)"));
        QCOMPARE(picker.selectedFolderId(), QString());
        picker.setSelectedFolderId("a1");
        QCOMPARE(picker.selectedFolderId(), QStringLiteral("a1"));
        QCOMPARE(picker.count(), 3);
    }

    void cyclesAndOrphansStillListEveryFolder()
    {
        FolderPickerWidget picker;
        picker.setFolders({makeFolder("x", "X", "y"), makeFolder("y", "Y", "x"), makeFolder("o", "O", "gone")});
        QStringList ids = idsOf(picker);
        ids.sort();
        QCOMPARE(ids, (QStringList{"o", "x", "y"}));
    }

    void renderPopupWhenAsked()
    {
        const QByteArray dir = qgetenv("KAI_TEST_SHOTS");
        if (dir.isEmpty()) {
            QSKIP("set KAI_TEST_SHOTS to render");
        }
        FolderPickerWidget picker;
        picker.setFolders({makeFolder("d", "Demos"), makeFolder("t", "Kai API Tester", "d"),
                           makeFolder("n", "Kai Docs & Notes", "t"), makeFolder("x", "Kai Stop Test", "d"),
                           makeFolder("w", "Kai Web Shortcuts", "d"), makeFolder("o", "Other")});
        picker.enableNoneOption(QStringLiteral("(none - root)"));
        picker.setSelectedFolderId("n");
        picker.resize(320, 34);
        picker.show();
        picker.showPopup();
        QTest::qWait(120);
        QWidget *popup = picker.view()->window();
        popup->grab().save(QString::fromUtf8(dir) + "/folder-picker.png");
        picker.hidePopup();
    }
};

QTEST_MAIN(TestFolderPickerWidget)
#include "test_folder_picker_widget.moc"
