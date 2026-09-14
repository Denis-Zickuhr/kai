#include <QTest>

#include "ui/shared/fuzzy-search.h"

using namespace kai::ui;

// Testa o algoritmo puro de fuzzy match, sem depender de display.
class TestFuzzySearch : public QObject {
    Q_OBJECT

private slots:
    void exactSubsequenceMatchScoresNonNegative()
    {
        QVERIFY(FuzzyMatcher::score(QStringLiteral("dev"), QStringLiteral("Dev Server")) >= 0);
    }

    void nonSubsequenceReturnsNegativeScore()
    {
        QCOMPARE(FuzzyMatcher::score(QStringLiteral("xyz"), QStringLiteral("Dev Server")), -1);
    }

    void caseInsensitiveMatch()
    {
        QVERIFY(FuzzyMatcher::score(QStringLiteral("DEV"), QStringLiteral("dev server")) >= 0);
    }

    void emptyQueryMatchesEverythingWithZeroScore()
    {
        QCOMPARE(FuzzyMatcher::score(QString(), QStringLiteral("anything")), 0);
    }

    void emptyTargetWithNonEmptyQueryDoesNotMatch()
    {
        QCOMPARE(FuzzyMatcher::score(QStringLiteral("a"), QString()), -1);
    }

    // Prefixo/início-de-palavra deve pontuar mais alto que um match no meio
    // de uma palavra (sem separador antes), mesmo em strings de tamanho
    // parecido — valida o bônus de word-boundary do FuzzyMatcher.
    void prefixMatchScoresHigherThanMidWordMatch()
    {
        const int prefixScore = FuzzyMatcher::score(QStringLiteral("mig"), QStringLiteral("Migration"));
        const int midWordScore = FuzzyMatcher::score(QStringLiteral("mig"), QStringLiteral("XXmigYY"));

        QVERIFY(prefixScore > 0);
        QVERIFY(midWordScore > 0);
        QVERIFY(prefixScore > midWordScore);
    }

    // Match contíguo (substring) deve pontuar mais alto que caracteres
    // espalhados sem serem vizinhos.
    void contiguousMatchScoresHigherThanSpreadMatch()
    {
        const int contiguousScore = FuzzyMatcher::score(QStringLiteral("run"), QStringLiteral("Run Tests"));
        const int spreadScore = FuzzyMatcher::score(QStringLiteral("run"), QStringLiteral("Rebuild Utils Now"));

        QVERIFY(contiguousScore > spreadScore);
    }

    void searchFiltersAndSortsCandidatesByScoreDescending()
    {
        const QStringList candidates = {
            QStringLiteral("Build Release"),
            QStringLiteral("Run Migration"),
            QStringLiteral("Dev Server"),
            QStringLiteral("Something Unrelated"),
        };

        const QVector<FuzzyMatchResult> results = FuzzyMatcher::search(QStringLiteral("run"), candidates);

        // "Run Migration" começa com "run" (prefixo) e deve vir primeiro.
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().text, QStringLiteral("Run Migration"));

        // "Something Unrelated" não é subsequência de "run" -> não deve aparecer.
        for (const FuzzyMatchResult &r : results) {
            QVERIFY(r.text != QStringLiteral("Something Unrelated"));
        }

        for (int i = 1; i < results.size(); ++i) {
            QVERIFY(results.at(i - 1).score >= results.at(i).score);
        }
    }

    void searchWithEmptyQueryReturnsAllCandidatesInOriginalOrder()
    {
        const QStringList candidates = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
        const QVector<FuzzyMatchResult> results = FuzzyMatcher::search(QString(), candidates);

        QCOMPARE(results.size(), 3);
        QCOMPARE(results.at(0).text, QStringLiteral("A"));
        QCOMPARE(results.at(1).text, QStringLiteral("B"));
        QCOMPARE(results.at(2).text, QStringLiteral("C"));
    }
};

QTEST_MAIN(TestFuzzySearch)
#include "test_fuzzy_search.moc"
