#include <QTest>

#include "core/curl-parser.h"
#include "core/environment-manager.h"
#include "core/http-export.h"

using namespace kai::core;

namespace {

HttpEntry entry(const QString &name, const QString &value, bool file = false)
{
    HttpEntry e;
    e.name = name;
    e.value = value;
    e.isFile = file;
    return e;
}

} // namespace

// A requisição pronta (variáveis resolvidas) sai como curl ou código — e o curl gerado volta igual pelo importador.
class TestHttpExport : public QObject {
    Q_OBJECT

private:
    EnvironmentManager m_env;

    MaterializedHttpRequest prepare(const HttpConfig &config, HttpEffectiveOptions *options = nullptr)
    {
        const HttpRequestSpec spec = resolveHttpRequest(config, {});
        if (options) *options = spec.options;
        return materializeHttpRequest(spec, m_env);
    }

private slots:
    void initTestCase() { m_env.setGlobalVars({{QStringLiteral("HOST"), QStringLiteral("api.test")}, {QStringLiteral("T"), QStringLiteral("s3cret")}}); }

    void materializingResolvesVariablesAuthAndQuery()
    {
        HttpConfig cfg;
        cfg.setMethodName(QStringLiteral("POST"));
        cfg.url = QStringLiteral("https://{{HOST}}/v1/items?x=1");
        cfg.queryParams = {entry("page", "2")};
        cfg.auth.type = HttpAuthType::Bearer;
        cfg.auth.token = QStringLiteral("{{T}}");
        cfg.bodyType = HttpBodyType::Json;
        cfg.body = QStringLiteral("{\"a\": 1}");

        const MaterializedHttpRequest r = prepare(cfg);
        QCOMPARE(r.url.toString(), QStringLiteral("https://api.test/v1/items?x=1&page=2"));
        QCOMPARE(materializedHeader(r, "Authorization"), QStringLiteral("Bearer s3cret"));
        QCOMPARE(materializedHeader(r, "content-type"), QStringLiteral("application/json"));
        QCOMPARE(r.payload, QByteArray("{\"a\": 1}"));
    }

    void curlRoundTripsThroughTheImporter()
    {
        HttpConfig cfg;
        cfg.setMethodName(QStringLiteral("PUT"));
        cfg.url = QStringLiteral("https://{{HOST}}/it's");
        cfg.headers = {entry("X-A", "it's \"quoted\""), entry("Accept", "application/json")};
        cfg.bodyType = HttpBodyType::Raw;
        cfg.body = QStringLiteral("name=ana&x='y'");
        HttpEffectiveOptions options;
        const QString curl = exportHttpRequest(prepare(cfg, &options), options, HttpExportFormat::Curl);

        const CurlParseResult back = parseCurl(curl);
        QVERIFY2(back.ok, qPrintable(back.errorMessage));
        QCOMPARE(back.config.methodName(), QStringLiteral("PUT"));
        QCOMPARE(back.config.url, QStringLiteral("https://api.test/it's"));
        QCOMPARE(httpEntryValue(back.config.headers, QStringLiteral("X-A")), QStringLiteral("it's \"quoted\""));
        QCOMPARE(back.config.body, QStringLiteral("name=ana&x='y'"));
    }

    void connectionOptionsAreExported()
    {
        HttpConfig cfg;
        cfg.url = QStringLiteral("https://x");
        cfg.options.verifyTls = false;
        cfg.options.timeoutSec = 15;
        cfg.options.proxy = QStringLiteral("socks5://p:1080");
        HttpEffectiveOptions options;
        const QString curl = exportHttpRequest(prepare(cfg, &options), options, HttpExportFormat::Curl);
        QVERIFY(curl.contains(QStringLiteral("-k")));
        QVERIFY(curl.contains(QStringLiteral("-m 15")));
        QVERIFY(curl.contains(QStringLiteral("-x 'socks5://p:1080'")));
        const CurlParseResult back = parseCurl(curl);
        QVERIFY(back.ok);
        QVERIFY(back.config.options.verifyTls.has_value() && !*back.config.options.verifyTls);
        QCOMPARE(back.config.options.timeoutSec.value_or(0), 15);
    }

    void headAndMultipartUseTheirOwnFlags()
    {
        HttpConfig head;
        head.setMethodName(QStringLiteral("HEAD"));
        head.url = QStringLiteral("https://x");
        HttpEffectiveOptions options;
        QVERIFY(exportHttpRequest(prepare(head, &options), options, HttpExportFormat::Curl).contains(QStringLiteral("-I")));

        HttpConfig up;
        up.setMethodName(QStringLiteral("POST"));
        up.url = QStringLiteral("https://x");
        up.bodyType = HttpBodyType::Multipart;
        up.formFields = {entry("title", "Hi"), entry("doc", "/tmp/a.pdf", true)};
        const QString curl = exportHttpRequest(prepare(up, &options), options, HttpExportFormat::Curl);
        QVERIFY(curl.contains(QStringLiteral("-F 'title=Hi'")));
        QVERIFY(curl.contains(QStringLiteral("-F 'doc=@/tmp/a.pdf'")));
        const CurlParseResult back = parseCurl(curl);
        QCOMPARE(back.config.bodyType, HttpBodyType::Multipart);
        QCOMPARE(back.config.formFields.size(), 2);
        QVERIFY(back.config.formFields.at(1).isFile);
    }

    void pythonAndJavaScriptSnippetsCarryTheRequest()
    {
        HttpConfig cfg;
        cfg.setMethodName(QStringLiteral("POST"));
        cfg.url = QStringLiteral("https://{{HOST}}/x");
        cfg.headers = {entry("X-A", "say \"hi\"")};
        cfg.bodyType = HttpBodyType::Json;
        cfg.body = QStringLiteral("{\"a\": 1}");
        cfg.options.verifyTls = false;
        HttpEffectiveOptions options;
        const MaterializedHttpRequest r = prepare(cfg, &options);

        const QString py = exportHttpRequest(r, options, HttpExportFormat::PythonRequests);
        QVERIFY(py.contains(QStringLiteral("import requests")));
        QVERIFY(py.contains(QStringLiteral("\"POST\"")));
        QVERIFY(py.contains(QStringLiteral("\"https://api.test/x\"")));
        QVERIFY(py.contains(QStringLiteral("\"X-A\": \"say \\\"hi\\\"\"")));
        QVERIFY(py.contains(QStringLiteral("verify=False")));

        const QString js = exportHttpRequest(r, options, HttpExportFormat::JavaScriptFetch);
        QVERIFY(js.contains(QStringLiteral("await fetch(\"https://api.test/x\"")));
        QVERIFY(js.contains(QStringLiteral("method: \"POST\"")));
        QVERIFY(js.contains(QStringLiteral("body: \"{\\\"a\\\": 1}\"")));
    }

    void graphQlWithBadVariablesReportsTheProblem()
    {
        HttpConfig cfg;
        cfg.url = QStringLiteral("https://x");
        cfg.bodyType = HttpBodyType::GraphQl;
        cfg.body = QStringLiteral("{ a }");
        cfg.graphqlVariables = QStringLiteral("nope");
        QVERIFY(!prepare(cfg).errorKey.isEmpty());
    }
};

QTEST_MAIN(TestHttpExport)
#include "test_http_export.moc"
