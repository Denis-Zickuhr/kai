#include <QTest>

#include <QTemporaryDir>

#include "ui/main-window.h"

using namespace kai::ui;

// O Kai é um app de bandeja: depois do boot, a janela principal só reaparece por ação explícita do usuário
// (já voltou sozinha como "fantasma" na barra de tarefas após ocultar o Kai e fechar a tampa do notebook).
class TestMainWindowVisibilityGuard : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;

private slots:
    void init()
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    }

    void incidentalShowIsIgnoredOnceArmedAndHidden()
    {
        MainWindow window;
        window.show();
        QVERIFY(window.isVisible());
        window.armVisibilityGuard();
        window.hide();

        window.show();
        QVERIFY(!window.isVisible());
        window.showMaximized();
        QVERIFY(!window.isVisible());
        window.showFullScreen();
        QVERIFY(!window.isVisible());
        window.setVisible(true);
        QVERIFY(!window.isVisible());
    }

    void explicitShowStillWorksWhenArmed()
    {
        MainWindow window;
        window.armVisibilityGuard();
        QVERIFY(!window.isVisible());

        window.showAndRaise();
        QVERIFY(window.isVisible());

        window.hide();
        QVERIFY(!window.isVisible());
        window.showAndRaise();
        QVERIFY(window.isVisible());
    }

    void unarmedWindowShowsNormally()
    {
        MainWindow window;
        window.show();
        QVERIFY(window.isVisible());
    }

    void visibleWindowKeepsWorkingWhenArmed()
    {
        MainWindow window;
        window.show();
        window.armVisibilityGuard();
        window.showMinimized();
        window.showNormal();
        QTest::qWait(50); // o backstop do evento Show age num timer de 0ms
        QVERIFY(window.isVisible());
    }
};

QTEST_MAIN(TestMainWindowVisibilityGuard)
#include "test_main_window_visibility_guard.moc"
