#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTest>
#include <QToolButton>

#include "ui/features/output/logs-panel.h"
#include "utils/logger.h"

using namespace kai;
using kai::ui::LogsPanel;
using kai::utils::LogEntry;
using kai::utils::LogLevel;

namespace {

LogEntry entry(LogLevel level, const QString &tag, const QString &message)
{
    return LogEntry{QDateTime::currentDateTime(), level, tag, message};
}

} // namespace

class TestLogsPanel : public QObject {
    Q_OBJECT

private slots:
    void parsesALogFileLineAndRejectsOthers()
    {
        const auto parsed = LogsPanel::parseFileLine(QStringLiteral("2026-10-21T10:11:12 [WARN] [Runner] disk almost full"));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->level, LogLevel::Warning);
        QCOMPARE(parsed->tag, QStringLiteral("Runner"));
        QCOMPARE(parsed->message, QStringLiteral("disk almost full"));
        QCOMPARE(parsed->timestamp.time().second(), 12);
        QVERIFY(!LogsPanel::parseFileLine(QStringLiteral("    at some continuation line")).has_value());
        QVERIFY(!LogsPanel::parseFileLine(QStringLiteral("2026-10-21T10:11:12 [LOUD] [x] y")).has_value());
    }

    void filtersByLevelAndSearch()
    {
        LogsPanel panel;
        panel.clear(); // sem o aviso de "arquivo indisponível" que o painel mostra fora do app
        panel.appendEntry(entry(LogLevel::Debug, QStringLiteral("Net"), QStringLiteral("connecting")));
        panel.appendEntry(entry(LogLevel::Info, QStringLiteral("Net"), QStringLiteral("connected")));
        panel.appendEntry(entry(LogLevel::Warning, QStringLiteral("Disk"), QStringLiteral("almost full")));
        panel.appendEntry(entry(LogLevel::Error, QStringLiteral("Disk"), QStringLiteral("write failed")));
        QCOMPARE(panel.entryCount(), 4);
        QCOMPARE(panel.visibleCount(), 4);

        panel.setMinLevel(LogLevel::Warning);
        QCOMPARE(panel.visibleCount(), 2);
        QCOMPARE(panel.view()->document()->blockCount(), 2);
        QVERIFY(panel.visibleText().contains(QStringLiteral("write failed")));
        QVERIFY(!panel.visibleText().contains(QStringLiteral("connected")));

        panel.setMinLevel(LogLevel::Debug);
        panel.setSearch(QStringLiteral("DISK")); // sem diferença de caixa, também pela etiqueta
        QCOMPARE(panel.visibleCount(), 2);
        panel.setSearch(QStringLiteral("connect"));
        QCOMPARE(panel.visibleCount(), 2);
        panel.setSearch(QString());
        QCOMPARE(panel.visibleCount(), 4);

        // Uma entrada nova respeita o filtro de agora.
        panel.setMinLevel(LogLevel::Error);
        panel.appendEntry(entry(LogLevel::Info, QStringLiteral("Net"), QStringLiteral("ignored")));
        QCOMPARE(panel.visibleCount(), 1);
        QCOMPARE(panel.entryCount(), 5);
    }

    void pauseHoldsTheViewAndResumeCatchesUp()
    {
        LogsPanel panel;
        panel.clear();
        panel.appendEntry(entry(LogLevel::Info, QStringLiteral("A"), QStringLiteral("one")));
        panel.setPaused(true);
        panel.appendEntry(entry(LogLevel::Info, QStringLiteral("A"), QStringLiteral("two")));
        panel.appendEntry(entry(LogLevel::Info, QStringLiteral("A"), QStringLiteral("three")));
        QVERIFY(panel.paused());
        QCOMPARE(panel.view()->document()->blockCount(), 1);
        QCOMPARE(panel.entryCount(), 3);
        panel.setPaused(false);
        QCOMPARE(panel.visibleCount(), 3);
        QCOMPARE(panel.view()->document()->blockCount(), 3);
    }

    void clearEmptiesEverythingAndCopyTakesTheVisibleLines()
    {
        LogsPanel panel;
        panel.clear();
        panel.appendEntry(entry(LogLevel::Error, QStringLiteral("T"), QStringLiteral("boom\nsecond line")));
        panel.appendEntry(entry(LogLevel::Info, QStringLiteral("T"), QStringLiteral("fine")));
        panel.setMinLevel(LogLevel::Error);
        auto *copy = panel.findChild<QToolButton *>(QStringLiteral("logsCopy"));
        QVERIFY(copy);
        copy->click();
        const QString copied = QApplication::clipboard()->text();
        QVERIFY(copied.contains(QStringLiteral("boom")));
        QVERIFY(!copied.contains(QStringLiteral("fine")));
        QCOMPARE(copied.count(QLatin1Char('\n')), 0); // uma entrada, uma linha

        panel.findChild<QToolButton *>(QStringLiteral("logsClear"))->click();
        QCOMPARE(panel.entryCount(), 0);
        QCOMPARE(panel.visibleCount(), 0);
        QVERIFY(panel.view()->toPlainText().isEmpty());
    }

    void historyFromTheFileComesBeforeLiveEntries()
    {
        LogsPanel panel;
        panel.clear();
        panel.appendEntry(entry(LogLevel::Info, QStringLiteral("Live"), QStringLiteral("now")));
        LogEntry old = entry(LogLevel::Info, QStringLiteral("File"), QStringLiteral("before"));
        old.timestamp = QDateTime::currentDateTime().addDays(-1);
        LogEntry dup = entry(LogLevel::Info, QStringLiteral("File"), QStringLiteral("same second as live"));
        panel.addHistory({old, dup});
        QCOMPARE(panel.entryCount(), 2); // a que coincide com o ao vivo já está nele
        QVERIFY(panel.view()->toPlainText().indexOf(QStringLiteral("before")) < panel.view()->toPlainText().indexOf(QStringLiteral("now")));
    }

    void renders()
    {
        LogsPanel panel;
        panel.resize(520, 260);
        for (LogLevel level : {LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error}) {
            panel.appendEntry(entry(level, QStringLiteral("Tag"), QStringLiteral("a message")));
        }
        QVERIFY(!panel.grab().isNull());
    }
};

QTEST_MAIN(TestLogsPanel)
#include "test_logs_panel.moc"
