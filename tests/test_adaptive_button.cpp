#include <QApplication>
#include <QHBoxLayout>
#include <QTest>
#include <QToolButton>

#include "ui/shared/adaptive-button.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/search-line-edit.h"

using namespace kai::ui;

class TestAdaptiveButton : public QObject {
    Q_OBJECT

private slots:
    void collapsesToIconWhenNarrow()
    {
        QWidget host;
        auto *layout = new QHBoxLayout(&host);
        auto *button = new AdaptiveButton(QStringLiteral("Copy output"), &host);
        button->setIcon(LucideIcons::icon(QStringLiteral("copy"), Qt::white, 16));
        layout->addWidget(button);
        host.resize(600, 40);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QVERIFY(!button->isCollapsed());

        host.resize(button->minimumSizeHint().width() + 20, 40);
        QTRY_VERIFY(button->isCollapsed());
        QCOMPARE(button->toolTip(), QStringLiteral("Copy output"));

        host.resize(600, 40);
        QTRY_VERIFY(!button->isCollapsed());
        QVERIFY(button->toolTip().isEmpty());
    }

    void searchFieldClearButtonFollowsText()
    {
        SearchLineEdit edit;
        edit.resize(200, 28);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        auto *clear = edit.findChild<QToolButton *>(QStringLiteral("searchClear"));
        QVERIFY(clear);
        QVERIFY(clear->isHidden());
        edit.setText(QStringLiteral("p"));
        QVERIFY(!clear->isHidden());
        QVERIFY(clear->geometry().center().y() == edit.height() / 2
                || qAbs(clear->geometry().center().y() - edit.height() / 2) <= 1);
        clear->click();
        QVERIFY(edit.text().isEmpty());
    }
};

QTEST_MAIN(TestAdaptiveButton)
#include "test_adaptive_button.moc"
