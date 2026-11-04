#include <QTest>
#include <QRegularExpression>

// Contrato da versão: KAI_VERSION_STRING = X.Y.Z (estável) ou X.Y.Z-<pre> (beta).
// Vem do CMake (project VERSION + KAI_VERSION_PRERELEASE); o CI deriva o sufixo da tag.
class TestVersion : public QObject {
    Q_OBJECT

    static QString compose(const QString &core, const QString &pre)
    {
        return pre.isEmpty() ? core : core + QLatin1Char('-') + pre;
    }

private slots:
    void versionStringMatchesSemver()
    {
        static const QRegularExpression re(QStringLiteral(R"(^\d+\.\d+\.\d+(-[0-9A-Za-z]+(\.[0-9A-Za-z]+)*)?$)"));
        QVERIFY2(re.match(QStringLiteral(KAI_VERSION_STRING)).hasMatch(), KAI_VERSION_STRING);
    }

    void versionStringIsCorePlusOptionalPrerelease()
    {
        QCOMPARE(QStringLiteral(KAI_VERSION_STRING),
                 compose(QStringLiteral(KAI_VERSION_CORE), QStringLiteral(KAI_VERSION_PRERELEASE)));
    }

    void coreIsThreeNumbers()
    {
        static const QRegularExpression re(QStringLiteral(R"(^\d+\.\d+\.\d+$)"));
        QVERIFY(re.match(QStringLiteral(KAI_VERSION_CORE)).hasMatch());
    }

    void composeWithAndWithoutSuffix()
    {
        QCOMPARE(compose(QStringLiteral("1.0.0"), QString()), QStringLiteral("1.0.0"));
        QCOMPARE(compose(QStringLiteral("1.0.0"), QStringLiteral("beta.1")), QStringLiteral("1.0.0-beta.1"));
    }
};

QTEST_APPLESS_MAIN(TestVersion)
#include "test_version.moc"
