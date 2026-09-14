#include <QTest>
#include <QApplication>
#include <QTabWidget>
#include <QTabBar>
#include <QTreeWidget>

#include "ui/features/command-editor/command-tree-widget.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

namespace {
QTreeWidget *treeForRoot(CommandTreeWidget &widget, const QString &rootId)
{
    auto *tabWidget = widget.findChild<QTabWidget *>();
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->tabBar()->tabData(i).toString() == rootId) {
            return qobject_cast<QTreeWidget *>(tabWidget->widget(i));
        }
    }
    return nullptr;
}
} // namespace

// Formato do tempo de execução ao lado do ícone de run: só mostra o próximo token quando chega nele
// (nunca "0m" antes de 1 minuto nem "0h" antes de 1 hora).
class TestElapsedTimer : public QObject {
    Q_OBJECT
private slots:
    void showsOnlySecondsBeforeOneMinute()
    {
        QCOMPARE(CommandTreeWidget::formatElapsed(0),  QStringLiteral("0s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(1),  QStringLiteral("1s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(59), QStringLiteral("59s"));
        QVERIFY(!CommandTreeWidget::formatElapsed(59).contains(QLatin1Char('m')));
    }

    void addsMinutesTokenExactlyAtOneMinute()
    {
        QCOMPARE(CommandTreeWidget::formatElapsed(60),   QStringLiteral("1m 0s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(61),   QStringLiteral("1m 1s"));
        QCOMPARE(CommandTreeWidget::formatElapsed(3599), QStringLiteral("59m 59s"));
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

    // setRunningCommandIds() sozinho só reinicia o cronômetro de um id AUSENTE do conjunto anterior: um re-disparo rápido
    // (double click/enter) com o MESMO id ainda "rodando" não resetava. resetRunTimer() (chamado no instante síncrono em que a
    // execução começa) reinicia INCONDICIONALMENTE.
    void resetRunTimerRestartsElapsedTimeEvenWhileStillRunning()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Raiz");

        Command c;
        c.id = QStringLiteral("c_1");
        c.folderId = QStringLiteral("f_root");
        c.name = QStringLiteral("Comando");
        c.command = QStringLiteral("sleep 5");

        CommandTreeWidget widget;
        widget.setData({root}, {c});
        QTreeWidget *tree = treeForRoot(widget, QStringLiteral("f_root"));
        QVERIFY(tree != nullptr);
        QTreeWidgetItem *item = tree->topLevelItem(0);
        QVERIFY(item != nullptr);

        widget.setRunningCommandIds({c.id});
        QCOMPARE(item->text(1), QStringLiteral("0s"));

        QTest::qWait(2200);
        QVERIFY(item->text(1) != QStringLiteral("0s")); // avançou (2s/3s...)

        // Re-disparo rápido: com o id JÁ presente no conjunto anterior, setRunningCommandIds() sozinho NÃO reiniciaria.
        widget.resetRunTimer(c.id);
        QCOMPARE(item->text(1), QStringLiteral("0s"));
    }
};

QTEST_MAIN(TestElapsedTimer)
#include "test_elapsed_timer.moc"
