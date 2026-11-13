#include <QTest>

#include <QComboBox>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QTemporaryDir>

#include "cli/cli-store.h"
#include "ui/features/settings/tabs/store-tab.h"
#include "ui/features/store/store-preview-dialog.h"
#include "ui/features/store/store-provider-dialog.h"
#include "ui/features/store/store-window.h"

using namespace kai;
using namespace kai::ui;

class TestStoreUi : public QObject {
    Q_OBJECT

private:
    static core::StoreProviderConfig provider(const QString &name, const QString &location, core::StoreProviderKind kind)
    {
        core::StoreProviderConfig p;
        p.id = name;
        p.name = name;
        p.location = location;
        p.kind = kind;
        return p;
    }

    static int visibleCards(StoreWindow &w)
    {
        int n = 0;
        for (QFrame *f : w.findChildren<QFrame *>(QStringLiteral("storeCard"))) {
            if (!f->isHidden()) ++n;
        }
        return n;
    }

private slots:
    void packageSpecSplitsTheVersion()
    {
        QCOMPARE(cli::parseStorePackageSpec(QStringLiteral("acme/x@1.2.0")).version, QStringLiteral("1.2.0"));
        QCOMPARE(cli::parseStorePackageSpec(QStringLiteral("acme/x")).id, QStringLiteral("acme/x"));
        QVERIFY(cli::parseStorePackageSpec(QStringLiteral("acme/x")).version.isEmpty());
    }

    void theProvidersTableListsAndRoundTrips()
    {
        StoreTab tab({provider(QStringLiteral("A"), QStringLiteral("/a.yml"), core::StoreProviderKind::File),
                      provider(QStringLiteral("B"), QStringLiteral("https://github.com/o/r"), core::StoreProviderKind::Repo)});
        tab.resize(700, 400);
        auto *table = tab.findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(tab.providers().size(), 2);
        QVERIFY(!tab.grab().isNull());
    }

    void clickingAnywhereOnAPreviewRowTogglesItsCommand()
    {
        engine::StoreStagedPackage staged;
        staged.manifest.name = QStringLiteral("demo");
        staged.manifest.version = QStringLiteral("1.0.0");
        core::StorePreviewCommand command;
        command.name = QStringLiteral("Disk report");
        command.type = QStringLiteral("command");
        staged.preview.commands.append(command);
        StorePreviewDialog dialog(staged, QStringLiteral("P"), QStringLiteral("Community"), QStringLiteral("unsigned"), {});
        dialog.show();
        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->item(0)->checkState(), Qt::Checked);
        const QPoint onText = list->visualItemRect(list->item(0)).center();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, onText);
        QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, onText);
        QCOMPARE(list->item(0)->checkState(), Qt::Checked);
    }

    void theDialogSuggestsTheKindFromTheLocationUntilTheUserPicksOne()
    {
        StoreProviderDialog dialog(core::StoreProviderConfig{});
        dialog.locationField()->setText(QStringLiteral("https://github.com/o/r"));
        QTest::keyClick(dialog.locationField(), Qt::Key_End);
        QTest::keyClicks(dialog.locationField(), QStringLiteral("x"));
        QTest::keyClick(dialog.locationField(), Qt::Key_Backspace);
        QCOMPARE(dialog.kindField()->currentData().toString(), QStringLiteral("repo"));
        QVERIFY(!dialog.provider().id.isEmpty());
    }

    void theWindowShowsTheSampleCatalogAndGreysOutTheIncompatibleCard()
    {
        QTemporaryDir config;
        StoreWindow::Host host;
        host.configDir = config.path();
        host.install = [](const core::StoreInstallRequest &, QString &) { return true; };
        host.uninstall = [](const QString &, QString &) { return true; };
        host.commands = []() { return core::CommandsData(); };
        StoreWindow window({provider(QStringLiteral("Sample"), QStringLiteral("sample/store/catalog.yml"), core::StoreProviderKind::File)}, host);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(visibleCards(window), 7, 15000);
        bool greyed = false;
        for (QFrame *f : window.findChildren<QFrame *>(QStringLiteral("storeCard"))) {
            if (!f->toolTip().isEmpty()) greyed = true;
        }
        QVERIFY(greyed);

        auto *search = window.findChild<QLineEdit *>();
        QVERIFY(search);
        search->setText(QStringLiteral("hello"));
        QTRY_COMPARE_WITH_TIMEOUT(visibleCards(window), 1, 5000);
        QVERIFY(!window.grab().isNull());
    }
};

QTEST_MAIN(TestStoreUi)
#include "test_store_ui.moc"
