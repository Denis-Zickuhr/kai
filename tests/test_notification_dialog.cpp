// Testes da central de notificações (NotificationHistoryDialog): agrupamento
// por dia, filtro, "não marca nada como lido ao abrir", marcar como lida após
// selecionar, excluir e "ir para o comando".

#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QListWidget>
#include <QToolButton>
#include <QPushButton>
#include <QLineEdit>
#include <QRegularExpression>

#include "core/notification-history.h"
#include "ui/features/history/notification-history-dialog.h"
#include "utils/translation-manager.h"

using namespace kai::core;
using namespace kai::ui;

class TestNotificationDialog : public QObject {
    Q_OBJECT

private:
    // Hoje: A (não lida, com comando), B (lida); ontem: C (não lida).
    static void seed(NotificationHistory &history)
    {
        history.clear();
        NotificationRecord c; c.title = "C"; c.eventKey = "command_failure";
        c.createdAt = QDateTime::currentDateTime().addDays(-1);
        NotificationRecord b; b.title = "B"; b.eventKey = "background_success"; b.read = true;
        NotificationRecord a; a.title = "A"; a.eventKey = "command_failure"; a.commandId = "cmd_a";
        history.append(c);
        history.append(b);
        history.append(a);
        // append() grava como veio; B já nasce lida.
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void relativeTimeAndDayLabels()
    {
        const QDateTime now(QDate(2026, 10, 1), QTime(14, 0, 0));
        QCOMPARE(NotificationHistoryDialog::relativeTime(now.addSecs(-20), now),
                 kai::utils::tr(QStringLiteral("notifications.time.now")));
        QCOMPARE(NotificationHistoryDialog::relativeTime(now.addSecs(-5 * 60), now),
                 kai::utils::tr(QStringLiteral("notifications.time.minutes_ago")).arg(5));
        QCOMPARE(NotificationHistoryDialog::relativeTime(now.addSecs(-3 * 3600), now), QStringLiteral("11:00"));

        const QDate today(2026, 10, 1);
        QCOMPARE(NotificationHistoryDialog::dayLabel(today, today), kai::utils::tr(QStringLiteral("notifications.day.today")));
        QCOMPARE(NotificationHistoryDialog::dayLabel(today.addDays(-1), today), kai::utils::tr(QStringLiteral("notifications.day.yesterday")));
        QCOMPARE(NotificationHistoryDialog::dayLabel(QDate(2026, 9, 28), today), QStringLiteral("28/09/2026"));
    }

    // Lista agrupada por dia (2 cabeçalhos + 3 notificações) e abrir NÃO marca
    // nada como lido (antes a primeira era marcada sozinha ao abrir).
    void groupsByDayAndDoesNotMarkAnythingReadOnOpen()
    {
        NotificationHistory history;
        seed(history);
        QCOMPARE(history.unreadCount(), 2);

        NotificationHistoryDialog dialog(&history);
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("notificationList"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 5);
        QTest::qWait(900);
        QCOMPARE(history.unreadCount(), 2);
    }

    // Selecionar e esperar um instante marca como lida; o filtro "Não lidas"
    // esconde as lidas.
    void selectingMarksReadAndFilterHidesReadOnes()
    {
        NotificationHistory history;
        seed(history);
        NotificationHistoryDialog dialog(&history);
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("notificationList"));
        QVERIFY(list != nullptr);

        // Linha 1 = "A" (primeira do dia de hoje, depois do cabeçalho).
        list->setCurrentRow(1);
        QTRY_COMPARE_WITH_TIMEOUT(history.unreadCount(), 1, 3000);

        const auto buttons = dialog.findChildren<QToolButton *>(QStringLiteral("notificationFilterButton"));
        QCOMPARE(buttons.size(), 2);
        buttons.at(1)->click(); // "Não lidas"
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 2, 3000); // cabeçalho de ontem + C
    }

    // Excluir remove do histórico; "ir para o comando" emite o id e fecha.
    void deleteAndGoToCommand()
    {
        NotificationHistory history;
        seed(history);
        NotificationHistoryDialog dialog(&history);
        dialog.setCommandResolver([](const QString &id) {
            return id == QStringLiteral("cmd_a") ? QStringLiteral("Comando A") : QString();
        });
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("notificationList"));
        QVERIFY(list != nullptr);

        QSignalSpy requested(&dialog, &NotificationHistoryDialog::commandRequested);
        list->setCurrentRow(1); // "A", que tem comando
        auto *goTo = dialog.findChild<QPushButton *>(QStringLiteral("notificationGoTo"));
        QVERIFY(goTo != nullptr);
        QVERIFY(!goTo->isHidden());
        goTo->click();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.at(0).at(0).toString(), QStringLiteral("cmd_a"));

        NotificationHistoryDialog second(&history);
        auto *list2 = second.findChild<QListWidget *>(QStringLiteral("notificationList"));
        list2->setCurrentRow(2); // "B"
        auto *del = second.findChild<QPushButton *>(QStringLiteral("notificationDelete"));
        QVERIFY(del != nullptr);
        del->click();
        QTRY_COMPARE_WITH_TIMEOUT(history.load().size(), 2, 3000);
    }

    // Regressão: todo placeholder %N do QSS do diálogo precisa ser preenchido
    // (um %N sobrando — ou argumentos deslocados — deixava o texto do filtro
    // com cor errada).
    void styleSheetHasNoUnreplacedPlaceholders()
    {
        NotificationHistory history;
        seed(history);
        NotificationHistoryDialog dialog(&history);
        const QString qss = dialog.styleSheet();
        QVERIFY(!qss.isEmpty());
        QVERIFY2(!QRegularExpression(QStringLiteral("%\\d")).match(qss).hasMatch(), qPrintable(qss));
        // E as cores do filtro são cores de verdade (hex), não números soltos.
        QVERIFY(qss.contains(QRegularExpression(QStringLiteral("notificationFilterButton \\{[^}]*color: #[0-9a-fA-F]{6}"))));
    }

    // Busca filtra por título/corpo.
    void searchFiltersByText()
    {
        NotificationHistory history;
        seed(history);
        NotificationHistoryDialog dialog(&history);
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("notificationList"));
        auto *search = dialog.findChild<QLineEdit *>();
        QVERIFY(list != nullptr && search != nullptr);
        search->setText(QStringLiteral("B"));
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 2, 3000); // cabeçalho de hoje + B
    }
};

QTEST_MAIN(TestNotificationDialog)
#include "test_notification_dialog.moc"
