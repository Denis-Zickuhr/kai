// Testes do parser de OpenAPI/Swagger (JSON) -> endpoints HTTP.

#include <QTest>
#include <QString>

#include "core/openapi-parser.h"

using namespace kai::core;

namespace {
const char *kOpenApi3Spec =
    "{"
    "  \"openapi\": \"3.0.0\","
    "  \"info\": { \"title\": \"Loja API\" },"
    "  \"servers\": [ { \"url\": \"https://api.loja.com/v1\" } ],"
    "  \"paths\": {"
    "    \"/users\": {"
    "      \"get\": { \"summary\": \"Lista usuarios\" },"
    "      \"post\": {"
    "        \"summary\": \"Cria usuario\","
    "        \"requestBody\": { \"content\": { \"application/json\": {"
    "          \"schema\": { \"type\": \"object\", \"properties\": {"
    "            \"name\": { \"type\": \"string\" },"
    "            \"age\": { \"type\": \"integer\" } } } } } }"
    "      }"
    "    },"
    "    \"/users/{id}\": { \"delete\": { \"operationId\": \"deleteUser\" } }"
    "  }"
    "}";

const char *kSwagger2Spec =
    "{"
    "  \"swagger\": \"2.0\","
    "  \"info\": { \"title\": \"Legacy\" },"
    "  \"host\": \"old.api.com\","
    "  \"basePath\": \"/api\","
    "  \"schemes\": [\"https\"],"
    "  \"paths\": { \"/ping\": { \"get\": { \"summary\": \"ping\" } } }"
    "}";
}

class TestOpenApiParser : public QObject
{
    Q_OBJECT

private slots:
    void parsesOpenApi3PathsAndBaseUrl()
    {
        const auto r = parseOpenApi(QString::fromUtf8(kOpenApi3Spec));
        QVERIFY2(r.ok, qPrintable(r.errorMessage));
        QCOMPARE(r.apiTitle, QStringLiteral("Loja API"));
        QCOMPARE(r.endpoints.size(), 3);

        bool foundPost = false;
        for (const auto &ep : r.endpoints) {
            if (ep.config.method == HttpMethod::Post) {
                foundPost = true;
                QCOMPARE(ep.config.url, QStringLiteral("https://api.loja.com/v1/users"));
                QVERIFY(ep.config.body.contains(QStringLiteral("name")));
                QVERIFY(ep.config.body.contains(QStringLiteral("age")));
                QCOMPARE(ep.config.headers.value("Content-Type"), QStringLiteral("application/json"));
            }
        }
        QVERIFY(foundPost);
    }

    void parsesSwagger2HostBasePath()
    {
        const auto r = parseOpenApi(QString::fromUtf8(kSwagger2Spec));
        QVERIFY2(r.ok, qPrintable(r.errorMessage));
        QCOMPARE(r.endpoints.size(), 1);
        QCOMPARE(r.endpoints.first().config.url, QStringLiteral("https://old.api.com/api/ping"));
    }

    void rejectsNonOpenApi()
    {
        const auto r = parseOpenApi(QStringLiteral("{\"foo\": 1}"));
        QVERIFY(!r.ok);
    }

    void rejectsInvalidJson()
    {
        const auto r = parseOpenApi(QStringLiteral("nao eh json"));
        QVERIFY(!r.ok);
    }
};

QTEST_MAIN(TestOpenApiParser)
#include "test_openapi_parser.moc"
