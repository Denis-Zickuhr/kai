#include <QTest>

#include "core/http-resolve.h"
#include "core/models.h"

using namespace kai::core;

namespace {

HttpEntry entry(const QString &name, const QString &value, bool enabled = true)
{
    HttpEntry e;
    e.name = name;
    e.value = value;
    e.enabled = enabled;
    return e;
}

Folder folder(const QString &id, const QString &name, const QString &parent = QString())
{
    Folder f;
    f.id = id;
    f.name = name;
    if (!parent.isEmpty()) f.parentId = parent;
    return f;
}

QString valueOf(const QVector<HttpEntry> &entries, const QString &name)
{
    return httpEntryValue(entries, name);
}

} // namespace

// Headers, autenticação e opções que o comando herda das pastas: a regra de herança vive num lugar só.
class TestHttpResolve : public QObject {
    Q_OBJECT

private slots:
    void folderChainGoesFromTheRootDownAndSurvivesCycles()
    {
        QVector<Folder> folders = {folder("a", "A"), folder("b", "B", "a"), folder("c", "C", "b")};
        const QVector<Folder> chain = folderChain(QStringLiteral("c"), folders);
        QCOMPARE(chain.size(), 3);
        QCOMPARE(chain.first().id, QStringLiteral("a"));
        QCOMPARE(chain.last().id, QStringLiteral("c"));

        folders[0].parentId = QStringLiteral("c"); // ciclo a -> c -> b -> a
        QCOMPARE(folderChain(QStringLiteral("c"), folders).size(), 3);
        QVERIFY(folderChain(QStringLiteral("missing"), folders).isEmpty());
    }

    void headersAreInheritedFromEveryFolderAbove()
    {
        Folder root = folder("r", "Root");
        root.httpDefaults.headers = {entry("Accept", "application/json"), entry("X-Client", "kai")};
        Folder child = folder("c", "Child", "r");
        child.httpDefaults.headers = {entry("X-Team", "core")};

        HttpConfig cfg;
        cfg.url = QStringLiteral("https://x");
        cfg.headers = {entry("X-Own", "1")};
        const HttpRequestSpec spec = resolveHttpRequest(cfg, {root, child});
        QCOMPARE(spec.headers.size(), 4);
        QCOMPARE(valueOf(spec.headers, "Accept"), QStringLiteral("application/json"));
        QCOMPARE(valueOf(spec.headers, "X-Team"), QStringLiteral("core"));
        QCOMPARE(valueOf(spec.headers, "X-Own"), QStringLiteral("1"));
    }

    void theClosestLevelWinsAndNamesIgnoreCase()
    {
        Folder root = folder("r", "Root");
        root.httpDefaults.headers = {entry("Accept", "text/plain")};
        Folder child = folder("c", "Child", "r");
        child.httpDefaults.headers = {entry("accept", "text/html")};
        HttpConfig cfg;
        cfg.headers = {entry("ACCEPT", "application/json")};

        const HttpRequestSpec spec = resolveHttpRequest(cfg, {root, child});
        QCOMPARE(spec.headers.size(), 1);
        QCOMPARE(spec.headers.first().value, QStringLiteral("application/json"));
        QCOMPARE(resolveHttpRequest(HttpConfig{}, {root, child}).headers.first().value, QStringLiteral("text/html"));
    }

    void aDisabledRowRemovesTheInheritedHeader()
    {
        Folder root = folder("r", "Root");
        root.httpDefaults.headers = {entry("X-Trace", "on"), entry("Accept", "*/*")};
        HttpConfig cfg;
        cfg.headers = {entry("X-Trace", QString(), false)};
        const HttpRequestSpec spec = resolveHttpRequest(cfg, {root});
        QCOMPARE(spec.headers.size(), 1);
        QCOMPARE(spec.headers.first().name, QStringLiteral("Accept"));
    }

    void disabledRowsAndNamelessRowsNeverGoOut()
    {
        HttpConfig cfg;
        cfg.headers = {entry("A", "1", false), entry(QString(), "orphan"), entry("B", "2")};
        cfg.queryParams = {entry("q", "x", false), entry("p", "y")};
        cfg.formFields = {entry("f", "z", false), entry("g", "w")};
        const HttpRequestSpec spec = resolveHttpRequest(cfg, {});
        QCOMPARE(spec.headers.size(), 1);
        QCOMPARE(spec.queryParams.size(), 1);
        QCOMPARE(spec.formFields.size(), 1);
    }

    void duplicatedNamesInTheSameLevelAreKept()
    {
        HttpConfig cfg;
        cfg.headers = {entry("Accept", "a/b"), entry("Accept", "c/d")};
        QCOMPARE(resolveHttpRequest(cfg, {}).headers.size(), 2);
    }

    void authComesFromTheClosestFolderThatDecides()
    {
        Folder root = folder("r", "Root");
        root.httpDefaults.auth.type = HttpAuthType::Bearer;
        root.httpDefaults.auth.token = QStringLiteral("{{ROOT}}");
        Folder child = folder("c", "Child", "r");
        child.httpDefaults.auth.type = HttpAuthType::Basic;
        child.httpDefaults.auth.username = QStringLiteral("u");
        Folder grandchild = folder("g", "Grand", "c"); // Inherit: segue o da pasta de cima

        HttpConfig cfg; // Inherit
        QCOMPARE(resolveHttpRequest(cfg, {root, child, grandchild}).auth.type, HttpAuthType::Basic);
        QCOMPARE(resolveHttpRequest(cfg, {root}).auth.token, QStringLiteral("{{ROOT}}"));
        QCOMPARE(resolveHttpRequest(cfg, {}).auth.type, HttpAuthType::None);

        cfg.auth.type = HttpAuthType::None; // corta a herança
        QCOMPARE(resolveHttpRequest(cfg, {root, child}).auth.type, HttpAuthType::None);

        cfg.auth.type = HttpAuthType::ApiKey; // o do comando vence
        cfg.auth.keyName = QStringLiteral("X-Key");
        const HttpRequestSpec own = resolveHttpRequest(cfg, {root, child});
        QCOMPARE(own.auth.type, HttpAuthType::ApiKey);
        QCOMPARE(own.auth.keyName, QStringLiteral("X-Key"));
    }

    void inheritedAuthReportsTheFolderItComesFrom()
    {
        Folder root = folder("r", "Root");
        root.httpDefaults.auth.type = HttpAuthType::Bearer;
        QString origin;
        QCOMPARE(inheritedHttpAuth({root, folder("c", "Child", "r")}, &origin).type, HttpAuthType::Bearer);
        QCOMPARE(origin, QStringLiteral("Root"));
        QCOMPARE(inheritedHttpAuth({}, &origin).type, HttpAuthType::None);
        QVERIFY(origin.isEmpty());
    }

    void optionsAreInheritedFieldByField()
    {
        Folder root = folder("r", "Root");
        root.httpDefaults.options.timeoutSec = 30;
        root.httpDefaults.options.verifyTls = false;
        Folder child = folder("c", "Child", "r");
        child.httpDefaults.options.timeoutSec = 5;
        HttpConfig cfg;
        cfg.options.followRedirects = false;

        const HttpEffectiveOptions o = resolveHttpRequest(cfg, {root, child}).options;
        QCOMPARE(o.timeoutSec, 5);           // pasta mais próxima
        QVERIFY(!o.verifyTls);               // herdado da raiz
        QVERIFY(!o.followRedirects);         // do comando
        QCOMPARE(o.maxRedirects, 10);        // padrão
        QCOMPARE(o.proxy, QStringLiteral("system"));
        QVERIFY(o.useCookies);

        const HttpEffectiveOptions defaults = resolveHttpRequest(HttpConfig{}, {}).options;
        QVERIFY(defaults.followRedirects && defaults.verifyTls && defaults.useCookies);
        QCOMPARE(defaults.timeoutSec, 0);
    }

    void inheritedHeadersKnowTheirFolder()
    {
        Folder root = folder("r", "Root");
        root.httpDefaults.headers = {entry("Accept", "*/*"), entry("X-Off", "1")};
        Folder child = folder("c", "Child", "r");
        child.httpDefaults.headers = {entry("Accept", "text/html"), entry("X-Off", QString(), false)};

        const QVector<InheritedHttpHeader> headers = inheritedHttpHeaders({root, child});
        QCOMPARE(headers.size(), 1);
        QCOMPARE(headers.first().entry.value, QStringLiteral("text/html"));
        QCOMPARE(headers.first().folderName, QStringLiteral("Child"));
    }

    void methodNamesAcceptAnyVerb()
    {
        HttpConfig cfg;
        cfg.setMethodName(QStringLiteral("post"));
        QCOMPARE(cfg.method, HttpMethod::Post);
        cfg.setMethodName(QStringLiteral("head"));
        QCOMPARE(cfg.method, HttpMethod::Head);
        cfg.setMethodName(QStringLiteral("propfind"));
        QCOMPARE(cfg.method, HttpMethod::Custom);
        QCOMPARE(cfg.methodName(), QStringLiteral("PROPFIND"));
        QCOMPARE(resolveHttpRequest(cfg, {}).method, QStringLiteral("PROPFIND"));
        cfg.setMethodName(QString());
        QCOMPARE(cfg.methodName(), QStringLiteral("GET"));
    }

    void httpConfigSurvivesAJsonRoundTrip()
    {
        HttpConfig cfg;
        cfg.setMethodName(QStringLiteral("PURGE"));
        cfg.url = QStringLiteral("https://x/{{ID}}");
        cfg.headers = {entry("A", "1"), entry("B", "2", false)};
        cfg.queryParams = {entry("page", "3")};
        cfg.auth.type = HttpAuthType::ApiKey;
        cfg.auth.keyName = QStringLiteral("key");
        cfg.auth.keyValue = QStringLiteral("{{K}}");
        cfg.auth.keyInQuery = true;
        cfg.bodyType = HttpBodyType::Multipart;
        cfg.body = QStringLiteral("q");
        cfg.graphqlVariables = QStringLiteral("{}");
        HttpEntry file = entry("doc", "/tmp/a.pdf");
        file.isFile = true;
        cfg.formFields = {entry("n", "v"), file};
        cfg.bodyFile = QStringLiteral("/tmp/b");
        cfg.options.followRedirects = false;
        cfg.options.maxRedirects = 2;
        cfg.options.verifyTls = false;
        cfg.options.proxy = QStringLiteral("none");
        cfg.options.timeoutSec = 9;
        cfg.options.useCookies = false;
        cfg.responseFile = QStringLiteral("/tmp/out");
        ExecutionCondition assertion;
        assertion.left = QStringLiteral("{{RESPONSE_STATUS}}");
        assertion.op = QStringLiteral("eq");
        assertion.right = QStringLiteral("200");
        cfg.assertions = {assertion};

        const HttpConfig back = HttpConfig::fromJson(cfg.toJson());
        QCOMPARE(back.methodName(), QStringLiteral("PURGE"));
        QCOMPARE(back.url, cfg.url);
        QCOMPARE(back.headers, cfg.headers);
        QCOMPARE(back.queryParams, cfg.queryParams);
        QCOMPARE(back.auth.type, HttpAuthType::ApiKey);
        QVERIFY(back.auth.keyInQuery);
        QCOMPARE(back.bodyType, HttpBodyType::Multipart);
        QCOMPARE(back.formFields, cfg.formFields);
        QCOMPARE(back.bodyFile, cfg.bodyFile);
        QCOMPARE(back.options.followRedirects.value_or(true), false);
        QCOMPARE(back.options.maxRedirects.value_or(0), 2);
        QCOMPARE(back.options.proxy.value_or(QString()), QStringLiteral("none"));
        QCOMPARE(back.options.timeoutSec.value_or(0), 9);
        QCOMPARE(back.options.useCookies.value_or(true), false);
        QCOMPARE(back.responseFile, cfg.responseFile);
        QCOMPARE(back.assertions.size(), 1);
        QCOMPARE(back.assertions.first().right, QStringLiteral("200"));
    }

    void anOldMapOfHeadersStillOpens()
    {
        QJsonObject json;
        json["method"] = QStringLiteral("POST");
        json["url"] = QStringLiteral("https://x");
        json["body"] = QStringLiteral("{}");
        QJsonObject headers;
        headers["Content-Type"] = QStringLiteral("application/json");
        headers["X-A"] = QStringLiteral("1");
        json["headers"] = headers;

        const HttpConfig cfg = HttpConfig::fromJson(json);
        QCOMPARE(cfg.headers.size(), 2);
        QCOMPARE(httpEntryValue(cfg.headers, QStringLiteral("content-type")), QStringLiteral("application/json"));
        QCOMPARE(cfg.bodyType, HttpBodyType::Raw); // sem body_type: comportamento antigo
        QVERIFY(cfg.auth.isDefault());
    }

    void defaultConfigWritesNoNewKeys()
    {
        HttpConfig cfg;
        cfg.url = QStringLiteral("https://x");
        const QJsonObject json = cfg.toJson();
        QCOMPARE(json.keys().size(), 2); // method + url
    }

    void folderDefaultsSurviveAJsonRoundTrip()
    {
        Folder f = folder("r", "Root");
        f.httpDefaults.headers = {entry("Accept", "*/*"), entry("X-Off", QString(), false)};
        f.httpDefaults.auth.type = HttpAuthType::Bearer;
        f.httpDefaults.auth.token = QStringLiteral("{{T}}");
        f.httpDefaults.options.timeoutSec = 12;

        const Folder back = Folder::fromJson(f.toJson());
        QCOMPARE(back.httpDefaults.headers, f.httpDefaults.headers);
        QCOMPARE(back.httpDefaults.auth.type, HttpAuthType::Bearer);
        QCOMPARE(back.httpDefaults.auth.token, QStringLiteral("{{T}}"));
        QCOMPARE(back.httpDefaults.options.timeoutSec.value_or(0), 12);

        QVERIFY(!folder("x", "X").toJson().contains(QStringLiteral("http_defaults")));
    }
};

QTEST_MAIN(TestHttpResolve)
#include "test_http_resolve.moc"
