#include <QTest>

#include "ui/shared/notification-gate.h"

using namespace kai::ui;

// Lógica pura de shouldShowNotification (ver notification-gate.h) —
// decide se um evento vira toast da bandeja, dados os toggles relevantes.
class TestNotificationGate : public QObject {
    Q_OBJECT

private slots:
    void allConditionsMetShowsNotification()
    {
        QVERIFY(shouldShowNotification(/*trayAvailable=*/true, /*notificationsEnabled=*/true,
                                        /*eventToggleOn=*/true, /*notifyEvenWhenFocused=*/false,
                                        /*windowIsActive=*/false));
    }

    void noTrayNeverNotifies()
    {
        QVERIFY(!shouldShowNotification(false, true, true, true, false));
    }

    void masterSwitchOffNeverNotifies()
    {
        QVERIFY(!shouldShowNotification(true, /*notificationsEnabled=*/false, true, true, false));
    }

    void specificEventToggleOffNeverNotifies()
    {
        // Master ligado, mas ESTE evento está desligado (ex: "processo concluído com sucesso" desligado, "falha de comando" ligado).
        QVERIFY(!shouldShowNotification(true, true, /*eventToggleOn=*/false, true, false));
    }

    void windowFocusedWithoutOverrideSuppressesNotification()
    {
        // Janela em foco + notifyEvenWhenFocused desligado (default) = o
        // badge vermelho na árvore já avisa, o toast seria redundante.
        QVERIFY(!shouldShowNotification(true, true, true, /*notifyEvenWhenFocused=*/false,
                                         /*windowIsActive=*/true));
    }

    void windowFocusedWithOverrideStillNotifies()
    {
        // Mesmo cenário, com notifyEvenWhenFocused ligado: notifica mesmo em foco.
        QVERIFY(shouldShowNotification(true, true, true, /*notifyEvenWhenFocused=*/true,
                                        /*windowIsActive=*/true));
    }

    void windowNotActiveAlwaysNotifiesRegardlessOfOverride()
    {
        QVERIFY(shouldShowNotification(true, true, true, false, /*windowIsActive=*/false));
        QVERIFY(shouldShowNotification(true, true, true, true, /*windowIsActive=*/false));
    }
};

QTEST_MAIN(TestNotificationGate)
#include "test_notification_gate.moc"
