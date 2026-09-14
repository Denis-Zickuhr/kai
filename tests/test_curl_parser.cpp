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
        QCOMPARE(httpEntryValue(r.config.headers, QStringLiteral("Content-Type")), QStringLiteral("application/json"));
        QCOMPARE(httpEntryValue(r.config.headers, QStringLiteral("Accept")), QStringLiteral("application/json"));
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
        QCOMPARE(httpEntryValue(r.config.headers, QStringLiteral("X-Token")), QStringLiteral("abc"));
    }

    // -u vira autenticação Basic (e não um header com a senha em base64).
    void basicAuthBecomesAuth()
    {
        const auto r = parseCurl(QStringLiteral("curl -u user:pass https://x.com"));
        QVERIFY(r.ok);
        QCOMPARE(r.config.auth.type, HttpAuthType::Basic);
        QCOMPARE(r.config.auth.username, QStringLiteral("user"));
        QCOMPARE(r.config.auth.password, QStringLiteral("pass"));
        QVERIFY(httpEntryValue(r.config.headers, QStringLiteral("Authorization")).isEmpty());
    }

    void formFlagsBecomeMultipartFields()
    {
        const auto r = parseCurl(QStringLiteral("curl -F name=Ana -F avatar=@/tmp/a.png https://x.com/up"));
        QVERIFY(r.ok);
        QCOMPARE(r.config.bodyType, HttpBodyType::Multipart);
        QCOMPARE(r.config.method, HttpMethod::Post);
        QCOMPARE(r.config.formFields.size(), 2);
        QCOMPARE(r.config.formFields.at(0).name, QStringLiteral("name"));
        QVERIFY(!r.config.formFields.at(0).isFile);
        QVERIFY(r.config.formFields.at(1).isFile);
        QCOMPARE(r.config.formFields.at(1).value, QStringLiteral("/tmp/a.png"));
    }

    void connectionFlagsFillTheOptions()
    {
        const auto r = parseCurl(QStringLiteral(
            "curl -k -x socks5://p:1080 -m 30 -I -o out.bin --connect-timeout 5 https://x.com"));
        QVERIFY(r.ok);
        QCOMPARE(r.config.url, QStringLiteral("https://x.com"));
        QCOMPARE(r.config.method, HttpMethod::Head);
        QVERIFY(r.config.options.verifyTls.has_value() && !*r.config.options.verifyTls);
        QCOMPARE(r.config.options.proxy.value_or(QString()), QStringLiteral("socks5://p:1080"));
        QCOMPARE(r.config.options.timeoutSec.value_or(0), 30);
    }

    void unknownVerbsKeepTheirName()
    {
        const auto r = parseCurl(QStringLiteral("curl -X PROPFIND https://x.com"));
        QVERIFY(r.ok);
        QCOMPARE(r.config.method, HttpMethod::Custom);
        QCOMPARE(r.config.methodName(), QStringLiteral("PROPFIND"));
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
