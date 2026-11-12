#include <QTest>

#include "core/palette-usage.h"

using kai::core::PaletteUsage;

class TestPaletteUsage : public QObject {
    Q_OBJECT

    static constexpr qint64 kDay = 24LL * 60 * 60 * 1000;
    static constexpr qint64 kNow = 1'800'000'000'000LL;

private slots:
    void neverUsedScoresZero()
    {
        PaletteUsage usage;
        QCOMPARE(usage.score(QStringLiteral("0:a"), kNow), 0.0);
    }

    void moreUsesScoreHigher()
    {
        PaletteUsage usage;
        usage.record(QStringLiteral("0:a"), kNow);
        usage.record(QStringLiteral("0:b"), kNow);
        usage.record(QStringLiteral("0:b"), kNow);
        QVERIFY(usage.score(QStringLiteral("0:b"), kNow) > usage.score(QStringLiteral("0:a"), kNow));
    }

    void oldUseLosesHalfItsWeightEveryTwoWeeks()
    {
        PaletteUsage usage;
        usage.record(QStringLiteral("0:a"), kNow);
        QVERIFY(qAbs(usage.score(QStringLiteral("0:a"), kNow + 14 * kDay) - 0.5) < 1e-9);
    }

    void recentSingleUseBeatsOldHeavyUse()
    {
        PaletteUsage usage;
        for (int i = 0; i < 4; ++i) {
            usage.record(QStringLiteral("0:old"), kNow - 90 * kDay);
        }
        usage.record(QStringLiteral("0:new"), kNow);
        QVERIFY(usage.score(QStringLiteral("0:new"), kNow) > usage.score(QStringLiteral("0:old"), kNow));
    }

    void roundTripsThroughJson()
    {
        PaletteUsage usage;
        usage.record(QStringLiteral("1:x"), kNow);
        usage.record(QStringLiteral("1:x"), kNow);
        const PaletteUsage back = PaletteUsage::fromJson(usage.toJson());
        QCOMPARE(back.score(QStringLiteral("1:x"), kNow), usage.score(QStringLiteral("1:x"), kNow));
    }

    void corruptedJsonIsIgnored()
    {
        QJsonObject root;
        root["usage"] = QJsonObject{{"0:a", QJsonObject{{"count", -3}, {"last_ms", 0}}}, {"0:b", 7}};
        QVERIFY(PaletteUsage::fromJson(root).isEmpty());
        QVERIFY(PaletteUsage::fromJson(QJsonObject{{"usage", 5}}).isEmpty());
    }

    void memoryIsCappedAndKeepsTheBest()
    {
        PaletteUsage usage;
        for (int i = 0; i < PaletteUsage::kMaxEntries + 50; ++i) {
            usage.record(QStringLiteral("0:%1").arg(i), kNow - i);
        }
        usage.record(QStringLiteral("0:favorite"), kNow);
        usage.record(QStringLiteral("0:favorite"), kNow);
        QCOMPARE(usage.size(), PaletteUsage::kMaxEntries);
        QVERIFY(usage.score(QStringLiteral("0:favorite"), kNow) > 1.5);
    }
};

QTEST_MAIN(TestPaletteUsage)
#include "test_palette_usage.moc"
