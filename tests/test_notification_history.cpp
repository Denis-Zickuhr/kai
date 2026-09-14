// Testes do histórico de notificações (NotificationHistory) — persistência
// em notifications.json, ordem (mais recente primeiro), limite de registros,
// marcar como lida/todas como lidas e limpar. Mesmo padrão de test_run_history.

#include <QTest>
#include <QStandardPaths>

#include "core/notification-history.h"

using namespace kai::core;

class TestNotificationHistory : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        NotificationHistory nh;
        QFile::remove(nh.filePath());
    }

    void appendAndLoadKeepsMostRecentFirst()
    {
        NotificationHistory nh;
        nh.clear();
        NotificationRecord a; a.title = "A"; a.eventKey = "command_failure";
        NotificationRecord b; b.title = "B"; b.eventKey = "background_crash";
        nh.append(a);
        nh.append(b);
        const auto records = nh.load();
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.at(0).title, QStringLiteral("B"));
        QCOMPARE(records.at(1).title, QStringLiteral("A"));
        QVERIFY(!records.at(0).id.isEmpty());
        QVERIFY(records.at(0).createdAt.isValid());
    }

    void newRecordsStartUnread()
    {
        NotificationHistory nh;
        nh.clear();
        NotificationRecord r; r.title = "X";
        nh.append(r);
        QCOMPARE(nh.load().at(0).read, false);
        QCOMPARE(nh.unreadCount(), 1);
    }

    void markReadUpdatesOnlyThatRecord()
    {
        NotificationHistory nh;
        nh.clear();
        NotificationRecord a; a.title = "A";
        NotificationRecord b; b.title = "B";
        nh.append(a);
        nh.append(b);
        const QString idOfB = nh.load().at(0).id; // B é o mais recente
        nh.markRead(idOfB);

        const auto records = nh.load();
        QCOMPARE(records.at(0).read, true);  // B
        QCOMPARE(records.at(1).read, false); // A
        QCOMPARE(nh.unreadCount(), 1);
    }

    void markAllReadClearsUnreadCount()
    {
        NotificationHistory nh;
        nh.clear();
        for (int i = 0; i < 5; ++i) {
            NotificationRecord r; r.title = QStringLiteral("n%1").arg(i);
            nh.append(r);
        }
        QCOMPARE(nh.unreadCount(), 5);
        nh.markAllRead();
        QCOMPARE(nh.unreadCount(), 0);
        for (const NotificationRecord &r : nh.load()) {
            QVERIFY(r.read);
        }
    }

    void respectsMaxRecords()
    {
        NotificationHistory nh;
        nh.clear();
        for (int i = 0; i < NotificationHistory::kMaxRecords + 20; ++i) {
            NotificationRecord r; r.title = QStringLiteral("n%1").arg(i);
            nh.append(r);
        }
        QCOMPARE(nh.load().size(), NotificationHistory::kMaxRecords);
    }

    void clearEmptiesHistory()
    {
        NotificationHistory nh;
        NotificationRecord r; r.title = "x";
        nh.append(r);
        nh.clear();
        QCOMPARE(nh.load().size(), 0);
    }
};

QTEST_MAIN(TestNotificationHistory)
#include "test_notification_history.moc"
