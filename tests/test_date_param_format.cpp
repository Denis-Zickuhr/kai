#include <QTest>

#include "core/date-param-format.h"

using namespace kai::core;

// Cobre a formatação do parâmetro tipo Date (pedido do usuário: "formatador
// de paste no CMD, select com formatos e opção custom, que permite
// escolher o formato via template").
class TestDateParamFormat : public QObject {
    Q_OBJECT

private slots:
    void isoDateFormatsCorrectly()
    {
        const QDateTime dt(QDate(2024, 1, 15), QTime(10, 30, 0));
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("iso_date"), QString()), QStringLiteral("2024-01-15"));
    }

    void isoDateTimeFormatsCorrectly()
    {
        const QDateTime dt(QDate(2024, 1, 15), QTime(10, 30, 5));
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("iso_datetime"), QString()),
            QStringLiteral("2024-01-15T10:30:05"));
    }

    void brAndUsDateFormatsDiffer()
    {
        const QDateTime dt(QDate(2024, 1, 15), QTime(0, 0, 0));
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("br_date"), QString()), QStringLiteral("15/01/2024"));
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("us_date"), QString()), QStringLiteral("01/15/2024"));
    }

    void time24hFormatsCorrectly()
    {
        const QDateTime dt(QDate(2024, 1, 15), QTime(23, 5, 9));
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("time_24h"), QString()), QStringLiteral("23:05:09"));
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("time_24h_short"), QString()), QStringLiteral("23:05"));
    }

    void unixSecondsAndMillisAreComputedNotFormatted()
    {
        QDateTime dt(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("unix_seconds"), QString()),
            QString::number(dt.toSecsSinceEpoch()));
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("unix_millis"), QString()),
            QString::number(dt.toMSecsSinceEpoch()));
    }

    void customTemplateUsesGivenTokens()
    {
        const QDateTime dt(QDate(2024, 1, 15), QTime(10, 30, 0));
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("custom"), QStringLiteral("dd.MM.yy")),
            QStringLiteral("15.01.24"));
    }

    // Chave desconhecida (typo de config) cai no preset ISO — nunca quebra
    // silenciosamente com string vazia.
    void unknownPresetFallsBackToIsoDate()
    {
        const QDateTime dt(QDate(2024, 1, 15), QTime(0, 0, 0));
        QCOMPARE(formatDateParamValue(dt, QStringLiteral("bogus"), QString()), QStringLiteral("2024-01-15"));
    }

    void presetKeysListIsStableAndEndsWithCustom()
    {
        const QStringList keys = dateFormatPresetKeys();
        QVERIFY(keys.contains(QStringLiteral("iso_date")));
        QVERIFY(keys.contains(QStringLiteral("unix_millis")));
        QCOMPARE(keys.last(), QStringLiteral("custom"));
    }

    void everyPresetKeyHasANonEmptyLabel()
    {
        for (const QString &key : dateFormatPresetKeys()) {
            QVERIFY2(!dateFormatPresetLabel(key).isEmpty(), qPrintable(key));
        }
    }
};

QTEST_MAIN(TestDateParamFormat)
#include "test_date_param_format.moc"
