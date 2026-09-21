#include <QtTest>
#include "utils/cron-expression.h"

using namespace kai::utils;

class TestCronExpression : public QObject {
    Q_OBJECT

private slots:
    // Parser: casos válidos
    void testParseSimpleWildcard()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("* * * * *"));
        QVERIFY(e.valid);
        QCOMPARE(e.minutes.size(), 60);      // 0-59
        QCOMPARE(e.hours.size(), 24);        // 0-23
        QCOMPARE(e.daysOfMonth.size(), 31);  // 1-31
        QCOMPARE(e.months.size(), 12);       // 1-12
        QCOMPARE(e.daysOfWeek.size(), 7);    // 0-6
    }

    void testParseSimpleValues()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("30 9 15 3 1"));
        QVERIFY(e.valid);
        QCOMPARE(e.minutes.at(0), 30);
        QCOMPARE(e.hours.at(0), 9);
        QCOMPARE(e.daysOfMonth.at(0), 15);
        QCOMPARE(e.months.at(0), 3);
        QCOMPARE(e.daysOfWeek.at(0), 1);
    }

    void testParseRange()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 * * 1-5"));
        QVERIFY(e.valid);
        QCOMPARE(e.daysOfWeek.size(), 5); // 1,2,3,4,5
        QVERIFY(std::find(e.daysOfWeek.begin(), e.daysOfWeek.end(), 1) != e.daysOfWeek.end());
        QVERIFY(std::find(e.daysOfWeek.begin(), e.daysOfWeek.end(), 5) != e.daysOfWeek.end());
    }

    void testParseList()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("0,15,30,45 * * * *"));
        QVERIFY(e.valid);
        QCOMPARE(e.minutes.size(), 4);
        QVERIFY(std::find(e.minutes.begin(), e.minutes.end(), 0) != e.minutes.end());
        QVERIFY(std::find(e.minutes.begin(), e.minutes.end(), 15) != e.minutes.end());
        QVERIFY(std::find(e.minutes.begin(), e.minutes.end(), 30) != e.minutes.end());
        QVERIFY(std::find(e.minutes.begin(), e.minutes.end(), 45) != e.minutes.end());
    }

    void testParseStep()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("*/15 * * * *"));
        QVERIFY(e.valid);
        QCOMPARE(e.minutes.size(), 4); // 0, 15, 30, 45
        QVERIFY(std::find(e.minutes.begin(), e.minutes.end(), 0) != e.minutes.end());
        QVERIFY(std::find(e.minutes.begin(), e.minutes.end(), 15) != e.minutes.end());
    }

    void testParseListWithRange()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 * * 1-3,5"));
        QVERIFY(e.valid);
        QCOMPARE(e.daysOfWeek.size(), 4); // 1,2,3,5
        QVERIFY(std::find(e.daysOfWeek.begin(), e.daysOfWeek.end(), 1) != e.daysOfWeek.end());
        QVERIFY(std::find(e.daysOfWeek.begin(), e.daysOfWeek.end(), 5) != e.daysOfWeek.end());
    }

    // Parser: casos inválidos
    void testParseTooFewFields()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 * *"));
        QVERIFY(!e.valid);
        QVERIFY(e.error.contains(QStringLiteral("5 campos")));
    }

    void testParseTooManyFields()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 * * * extra"));
        QVERIFY(!e.valid);
    }

    void testParseInvalidNumber()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("abc 9 * * *"));
        QVERIFY(!e.valid);
    }

    void testParseOutOfRange()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("0 25 * * *")); // hora 25 (inválida)
        QVERIFY(!e.valid);
    }

    void testParseInvalidRange()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 * * 5-1")); // range inverso
        QVERIFY(!e.valid);
    }

    // nextOccurrence: casos básicos
    void testNextOccurrenceSimple()
    {
        // "0 9 * * *" = 9:00 diariamente
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 * * *"));
        QVERIFY(e.valid);

        QDateTime from(QDate(2025, 3, 21), QTime(8, 30), Qt::UTC);
        auto next = e.nextOccurrence(from);

        QVERIFY(next.has_value());
        QCOMPARE(next->time().hour(), 9);
        QCOMPARE(next->time().minute(), 0);
        QCOMPARE(next->date().day(), 21);
    }

    void testNextOccurrenceNextDay()
    {
        // "0 9 * * *" = 9:00 diariamente
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 * * *"));
        QVERIFY(e.valid);

        QDateTime from(QDate(2025, 3, 21), QTime(10, 0), Qt::UTC); // já passou 9:00
        auto next = e.nextOccurrence(from);

        QVERIFY(next.has_value());
        QCOMPARE(next->time().hour(), 9);
        QCOMPARE(next->time().minute(), 0);
        QCOMPARE(next->date().day(), 22); // próximo dia
    }

    void testNextOccurrenceWeekdays()
    {
        // "0 9 * * 1-5" = 9:00 seg-sex
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 * * 1-5"));
        QVERIFY(e.valid);

        // Sexta, 21 de março 2025 às 8:30
        QDateTime from(QDate(2025, 3, 21), QTime(8, 30), Qt::UTC);
        auto next = e.nextOccurrence(from);

        QVERIFY(next.has_value());
        QCOMPARE(next->time().hour(), 9);
        QCOMPARE(next->date().day(), 21);
        QCOMPARE(next->date().dayOfWeek(), 5); // sexta
    }

    void testNextOccurrenceSkipsWeekend()
    {
        // "0 9 * * 1-5" = 9:00 seg-sex
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 * * 1-5"));
        QVERIFY(e.valid);

        // Sexta, 21 de março 2025 às 9:00
        QDateTime from(QDate(2025, 3, 21), QTime(9, 0), Qt::UTC);
        auto next = e.nextOccurrence(from);

        QVERIFY(next.has_value());
        // Próxima ocorrência deve pular o sábado 22 e domingo 23, chegando em segunda 24
        QCOMPARE(next->date().dayOfWeek(), 1); // segunda
        QCOMPARE(next->date().day(), 24);
    }

    void testNextOccurrenceMonthly()
    {
        // "0 9 15 * *" = 9:00 dia 15 de qualquer mês
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 15 * *"));
        QVERIFY(e.valid);

        QDateTime from(QDate(2025, 3, 14), QTime(8, 0), Qt::UTC);
        auto next = e.nextOccurrence(from);

        QVERIFY(next.has_value());
        QCOMPARE(next->date().day(), 15);
        QCOMPARE(next->date().month(), 3);
    }

    void testNextOccurrenceMonthlyNextMonth()
    {
        // "0 9 15 * *" = 9:00 dia 15
        CronExpression e = CronExpression::parse(QStringLiteral("0 9 15 * *"));
        QVERIFY(e.valid);

        QDateTime from(QDate(2025, 3, 16), QTime(8, 0), Qt::UTC); // já passou 15
        auto next = e.nextOccurrence(from);

        QVERIFY(next.has_value());
        QCOMPARE(next->date().day(), 15);
        QCOMPARE(next->date().month(), 4);
    }

    void testNextOccurrenceInvalidExpression()
    {
        CronExpression e = CronExpression::parse(QStringLiteral("invalid"));
        QVERIFY(!e.valid);

        QDateTime from = QDateTime::currentDateTimeUtc();
        auto next = e.nextOccurrence(from);

        QVERIFY(!next.has_value());
    }

    // nextOccurrence: múltiplas ocorrências
    void testNextOccurrenceMultipleHours()
    {
        // "0 9,12,15 * * *" = 9:00, 12:00, 15:00 diariamente
        CronExpression e = CronExpression::parse(QStringLiteral("0 9,12,15 * * *"));
        QVERIFY(e.valid);

        QDateTime from(QDate(2025, 3, 21), QTime(8, 0), Qt::UTC);
        auto next1 = e.nextOccurrence(from);

        QVERIFY(next1.has_value());
        QCOMPARE(next1->time().hour(), 9);

        auto next2 = e.nextOccurrence(next1.value().addSecs(1));
        QVERIFY(next2.has_value());
        QCOMPARE(next2->time().hour(), 12);

        auto next3 = e.nextOccurrence(next2.value().addSecs(1));
        QVERIFY(next3.has_value());
        QCOMPARE(next3->time().hour(), 15);
    }
};

QTEST_MAIN(TestCronExpression)
#include "test_cron_expression.moc"
