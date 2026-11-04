// Easter egg: cinco cliques em sequência rápida no olho (mostrar ocultos)
// geram a notificação "Ai! meu olho!" (texto no i18n).

#include <QTest>
#include <QSignalSpy>
#include <QPushButton>
#include <QTemporaryDir>

#include "core/config-manager.h"
#include "core/notification-history.h"
#include "ui/main-window.h"
#include "ui/shared/expand-collapse-bar.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using namespace kai::core;

class TestEasterEgg : public QObject {
    Q_OBJECT

private:
    static QPushButton *eyeButton(QWidget &parent)
    {
        for (QPushButton *button : parent.findChildren<QPushButton *>()) {
            if (button->toolTip() == kai::utils::tr(QStringLiteral("tree.show_hidden"))) {
                return button;
            }
        }
        return nullptr;
    }

private slots:
    void fiveQuickClicksTriggerIt()
    {
        ExpandCollapseBar bar;
        QPushButton *eye = eyeButton(bar);
        QVERIFY(eye != nullptr);
        QSignalSpy spy(&bar, &ExpandCollapseBar::eyeEasterEggTriggered);
        for (int i = 0; i < 4; ++i) {
            eye->click();
        }
        QCOMPARE(spy.count(), 0);   // quatro não bastam
        eye->click();
        QCOMPARE(spy.count(), 1);
        // O contador zera: mais quatro não disparam de novo, o quinto sim.
        for (int i = 0; i < 4; ++i) {
            eye->click();
        }
        QCOMPARE(spy.count(), 1);
        eye->click();
        QCOMPARE(spy.count(), 2);
    }

    void slowClicksNeverTriggerIt()
    {
        ExpandCollapseBar bar;
        QPushButton *eye = eyeButton(bar);
        QVERIFY(eye != nullptr);
        QSignalSpy spy(&bar, &ExpandCollapseBar::eyeEasterEggTriggered);
        for (int i = 0; i < 6; ++i) {
            eye->click();
            QTest::qWait(850);   // mais devagar que o intervalo permitido
        }
        QCOMPARE(spy.count(), 0);
    }

    // Na janela: o clique gera a notificação no histórico, com o texto do i18n.
    void theWindowRecordsTheNotification()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
        ConfigManager config;
        CommandsData data;
        Folder folder; folder.id = "f_a"; folder.name = "A";
        data.folders << folder;
        QVERIFY(config.saveCommands(data));

        NotificationHistory history;
        history.clear();
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QPushButton *eye = eyeButton(window);
        QVERIFY(eye != nullptr);
        for (int i = 0; i < 5; ++i) {
            eye->click();
        }
        const auto records = NotificationHistory().load();
        QVERIFY2(!records.isEmpty(), "o easter egg deveria gerar uma notificação");
        QCOMPARE(records.first().eventKey, QStringLiteral("easter_egg_eye"));
        QCOMPARE(records.first().title, kai::utils::tr(QStringLiteral("easter_egg.eye.title")));
        QVERIFY(!records.first().title.contains(QStringLiteral("easter_egg.eye.title")));   // texto traduzido, não a chave
    }
};

QTEST_MAIN(TestEasterEgg)
#include "test_easter_egg.moc"
