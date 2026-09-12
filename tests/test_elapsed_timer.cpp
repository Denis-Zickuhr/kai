#include <QTest>
#include <QApplication>

#include "ui/command-tree-widget.h"

using namespace kai::ui;

// Formato do tempo de execução exibido ao lado do ícone de run.
// Regra do usuário: "0s -> 0m 0s -> 0h 0m 0s, onde só exibe o próximo valor do
// token quando chega nele" — ou seja, nunca mostrar "0m" antes de 1 minuto nem
// "0h" antes de 1 hora.
class TestElapsedTimer : public QObject {
    Q_OBJECT
private slots:
    void showsOnlySecondsBeforeOneMinute()
    {
        QCOMPARE(CommandTreeWidget::formatElapsed(0),  QStringLiteral("0s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(1),  QStringLiteral("1s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(59), QStringLiteral("59s"));
        // Não deve haver token de minuto ainda.
        QVERIFY(!CommandTreeWidget::formatElapsed(59).contains(QLatin1Char('m')));
    }

    void addsMinutesTokenExactlyAtOneMinute()
    {
        QCOMPARE(CommandTreeWidget::formatElapsed(60),   QStringLiteral("1m 0s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(61),   QStringLiteral("1m 1s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(3599), QStringLiteral("59m 59s"));
        // Ainda sem token de hora.
        QVERIFY(!CommandTreeWidget::formatElapsed(3599).contains(QLatin1Char('h')));
    }

    void addsHoursTokenExactlyAtOneHour()
    {
        QCOMPARE(CommandTreeWidget::formatElapsed(3600), QStringLiteral("1h 0m 0s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(3661), QStringLiteral("1h 1m 1s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(7325), QStringLiteral("2h 2m 5s"));
        // 25h nao vira "1d": o formato para em horas, como especificado.
        QCOMPARE(CommandTreeWidget::formatElapsed(90000), QStringLiteral("25h 0m 0s"));
    }

    void negativeIsClampedToZero()
    {
        QCOMPARE(CommandTreeWidget::formatElapsed(-5), QStringLiteral("0s"));
    }
};

QTEST_MAIN(TestElapsedTimer)
#include "test_elapsed_timer.moc"
