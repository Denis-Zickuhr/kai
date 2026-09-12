// Testes do parser de curl -> HttpConfig.

#include <QTest>

#include "core/curl-parser.h"

using namespace kai::core;

class TestCurlParser : public QObject
{
    Q_OBJECT

private slots:
    void simpleGet()
    {
        const auto r = parseCurl(QStringLiteral("curl https://api.example.com/users"));
        QVERIFY(r.ok);
        QCOMPARE(r.config.method, HttpMethod::Get);
        QCOMPARE(r.config.url, QStringLiteral("https://api.example.com/users"));
    }

    void postWithHeadersAndData()
    {
        const QString curl =
            "curl -X POST https://api.example.com/login "
            "-H 'Content-Type: application/json' "
            "-H \"Accept: application/json\" "
            "-d '{\"user\":\"admin\",\"pass\":\"123\"}'";
        const auto r = parseCurl(curl);
        QVERIFY(r.ok);
        QCOMPARE(r.config.method, HttpMethod::Post);
        QCOMPARE(r.config.url, QStringLiteral("https://api.example.com/login"));
        QCOMPARE(r.config.headers.value("Content-Type"), QStringLiteral("application/json"));
        QCOMPARE(r.config.headers.value("Accept"), QStringLiteral("application/json"));
        QVERIFY(r.config.body.contains(QStringLiteral("\"user\":\"admin\"")));
    }

    // -d sem -X infere POST.
    void dataInfersPost()
    {
        const auto r = parseCurl(QStringLiteral("curl https://x.com -d 'a=1'"));
        QVERIFY(r.ok);
        QCOMPARE(r.config.method, HttpMethod::Post);
        QCOMPARE(r.config.body, QStringLiteral("a=1"));
    }

    // --flag=valor e continuação de linha com backslash.
    void equalsSyntaxAndLineContinuation()
    {
        const QString curl =
            "curl --request PUT \\\n"
            "  --url https://api.example.com/item/5 \\\n"
            "  --header 'X-Token: abc'";
        const auto r = parseCurl(curl);
        QVERIFY(r.ok);
        QCOMPARE(r.config.method, HttpMethod::Put);
        QCOMPARE(r.config.url, QStringLiteral("https://api.example.com/item/5"));
        QCOMPARE(r.config.headers.value("X-Token"), QStringLiteral("abc"));
    }

    // -u vira header Authorization Basic.
    void basicAuthBecomesHeader()
    {
        const auto r = parseCurl(QStringLiteral("curl -u user:pass https://x.com"));
        QVERIFY(r.ok);
        QVERIFY(r.config.headers.value("Authorization").startsWith(QStringLiteral("Basic ")));
    }

    // Flags que ignoramos não devem consumir a URL.
    void ignoredFlagsDontEatUrl()
    {
        const auto r = parseCurl(QStringLiteral("curl -sSL -k https://x.com/api"));
        QVERIFY(r.ok);
        QCOMPARE(r.config.url, QStringLiteral("https://x.com/api"));
    }

    void emptyIsError()
    {
        const auto r = parseCurl(QStringLiteral("curl -X GET"));
        QVERIFY(!r.ok); // sem URL
    }
};

QTEST_MAIN(TestCurlParser)
#include "test_curl_parser.moc"
