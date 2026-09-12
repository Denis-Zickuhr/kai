// Testes do histórico de execuções (RunHistory) — persistência em runs.json,
// ordem (mais recente primeiro), limite de registros e truncamento de saída.
// Usa QStandardPaths em modo de teste para isolar num diretório temporário.

#include <QTest>
#include <QStandardPaths>
#include <QDir>

#include "core/run-history.h"

using namespace kai::core;

class TestRunHistory : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        RunHistory rh;
        QFile::remove(rh.filePath());
    }

    void appendAndLoadKeepsMostRecentFirst()
    {
        RunHistory rh;
        rh.clear();
        RunRecord a; a.commandName = "A"; a.success = true; a.commandType = "shell";
        RunRecord b; b.commandName = "B"; b.success = false; b.commandType = "http";
        rh.append(a);
        rh.append(b);
        const auto runs = rh.load();
        QCOMPARE(runs.size(), 2);
        QCOMPARE(runs.at(0).commandName, QStringLiteral("B")); // mais recente primeiro
        QCOMPARE(runs.at(1).commandName, QStringLiteral("A"));
        QVERIFY(!runs.at(0).id.isEmpty()); // id gerado
    }

    void respectsMaxRecords()
    {
        RunHistory rh;
        rh.clear();
        for (int i = 0; i < RunHistory::kMaxRecords + 20; ++i) {
            RunRecord r; r.commandName = QStringLiteral("cmd%1").arg(i);
            rh.append(r);
        }
        QCOMPARE(rh.load().size(), RunHistory::kMaxRecords);
    }

    void truncatesLargeOutput()
    {
        RunHistory rh;
        rh.clear();
        RunRecord r;
        r.commandName = "big";
        r.output = QString(RunHistory::kMaxOutputChars + 5000, QLatin1Char('x'));
        rh.append(r);
        QVERIFY(rh.load().at(0).output.size() <= RunHistory::kMaxOutputChars);
    }

    void clearEmptiesHistory()
    {
        RunHistory rh;
        RunRecord r; r.commandName = "x";
        rh.append(r);
        rh.clear();
        QCOMPARE(rh.load().size(), 0);
    }
};

QTEST_MAIN(TestRunHistory)
#include "test_run_history.moc"
