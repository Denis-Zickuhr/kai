#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>

#include "ui/shared/tray-menu.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

using namespace kai::ui;

class TestTrayMenu : public QObject {
    Q_OBJECT

private:
    static QAction *find(const QMenu &menu, const QString &key)
    {
        const QString text = kai::utils::tr(key);
        for (QAction *a : menu.actions()) {
            if (a->text() == text) {
                return a;
            }
        }
        return nullptr;
    }

private slots:
    void stopAllIsDisabledWhenNothingRuns()
    {
        TrayMenu menu;
        menu.rebuild({});
        QAction *stop = find(menu, QStringLiteral("tray.stop_all"));
        QVERIFY(stop);
        QVERIFY(!stop->isEnabled());
    }

    void stopAllEmitsWhenSomethingRuns()
    {
        TrayMenu menu;
        TrayMenu::State state;
        state.runningCount = 2;
        menu.rebuild(state);
        QSignalSpy spy(&menu, &TrayMenu::stopAllRequested);
        QAction *stop = find(menu, QStringLiteral("tray.stop_all"));
        QVERIFY(stop && stop->isEnabled());
        stop->trigger();
        QCOMPARE(spy.count(), 1);
    }

    void environmentAndThemeSubmenusNeedAChoice()
    {
        TrayMenu menu;
        menu.rebuild({});
        QVERIFY(!find(menu, QStringLiteral("tray.environment")));
        QVERIFY(!find(menu, QStringLiteral("tray.theme")));

        TrayMenu::State state;
        state.environmentIds = {QStringLiteral("a"), QStringLiteral("b")};
        state.environmentNames = {QStringLiteral("A"), QStringLiteral("B")};
        state.activeEnvironmentId = QStringLiteral("a");
        state.themes = {QStringLiteral("dracula"), QStringLiteral("kai-light")};
        state.activeTheme = QStringLiteral("dracula");
        menu.rebuild(state);

        QAction *env = find(menu, QStringLiteral("tray.environment"));
        QVERIFY(env && env->menu());
        QSignalSpy spy(&menu, &TrayMenu::environmentChosen);
        QCOMPARE(env->menu()->actions().size(), 2);
        QVERIFY(env->menu()->actions().at(0)->isChecked());
        env->menu()->actions().at(1)->trigger();
        QCOMPARE(spy.value(0).value(0).toString(), QStringLiteral("b"));

        QAction *theme = find(menu, QStringLiteral("tray.theme"));
        QVERIFY(theme && theme->menu());
        QSignalSpy themeSpy(&menu, &TrayMenu::themeChosen);
        theme->menu()->actions().at(1)->trigger();
        QCOMPARE(themeSpy.value(0).value(0).toString(), QStringLiteral("kai-light"));
    }

    void styleUsesRadiusTokens()
    {
        const QString qss = TrayMenu::menuStyleSheet();
        QVERIFY(qss.contains(QStringLiteral("border-radius: %1px").arg(kai::utils::tokens::radiusLg())));
        QVERIFY(qss.contains(QStringLiteral("border-radius: %1px").arg(kai::utils::tokens::radiusSm())));
    }

    void rebuildTwiceDoesNotDuplicateEntries()
    {
        TrayMenu menu;
        menu.rebuild({});
        const int n = menu.actions().size();
        menu.rebuild({});
        QCOMPARE(menu.actions().size(), n);
    }
};

QTEST_MAIN(TestTrayMenu)
#include "test_tray_menu.moc"
