#include <QTest>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "core/environment-manager.h"
#include "core/http-resolve.h"
#include "engine/http-cookie-store.h"
#include "engine/http-runner.h"

using namespace kai;
using namespace kai::core;
using kai::engine::HttpResult;
using kai::engine::HttpRunner;

namespace {

HttpEntry entry(const QString &name, const QString &value)
{
    HttpEntry e;
    e.name = name;
    e.value = value;
    return e;
}

// Servidor HTTP mínimo, na mesma thread do teste: responde conforme o caminho e devolve o que recebeu.
class TestServer : public QTcpServer {
public:
    explicit TestServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handle(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost, 0);
    }

    QString url(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path);
    }

private:
    void handle(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
        QJsonObject headers;
        qint64 length = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const int colon = lines.at(i).indexOf(':');
            if (colon <= 0) continue;
            const QString name = QString::fromLatin1(lines.at(i).left(colon).trimmed()).toLower();
            const QString value = QString::fromLatin1(lines.at(i).mid(colon + 1).trimmed());
            headers[name] = value;
            if (name == QStringLiteral("content-length")) length = value.toLongLong();
        }
        const QByteArray body = buffer.mid(headerEnd + 4);
        if (body.size() < length) {
            return; // o corpo ainda está chegando
        }
        m_buffers.remove(socket);

        const QString method = QString::fromLatin1(requestLine.value(0));
        const QString target = QString::fromLatin1(requestLine.value(1));
        const QString path = target.section('?', 0, 0);
        const QString query = target.contains('?') ? target.section('?', 1) : QString();

        if (path == QStringLiteral("/hang")) {
            return; // recebe a requisição e nunca responde
        }

        QByteArray status = "200 OK";
        QByteArray contentType = "application/json";
        QByteArray extra;
        QByteArray payload;
        if (path == QStringLiteral("/redirect")) {
            status = "302 Found";
            extra = "Location: /echo\r\n";
        } else if (path == QStringLiteral("/set-cookie")) {
            extra = "Set-Cookie: sid=abc; Path=/\r\n";
            payload = "{}";
        } else if (path == QStringLiteral("/sse")) {
            contentType = "text/event-stream";
            payload = "data: one\n\ndata: two\n\n";
        } else if (path == QStringLiteral("/bin")) {
            contentType = "application/octet-stream";
            payload = QByteArray(20000, '\x01');
        } else if (path == QStringLiteral("/xml")) {
            contentType = "application/xml";
            payload = "<a><b>1</b></a>";
        } else if (path == QStringLiteral("/missing")) {
            status = "404 Not Found";
            contentType = "text/plain";
            payload = "nope";
        } else {
            QJsonObject echo;
            echo["method"] = method;
            echo["path"] = path;
            echo["query"] = query;
            echo["headers"] = headers;
            echo["body"] = QString::fromLatin1(body);
            payload = QJsonDocument(echo).toJson(QJsonDocument::Compact);
        }
        QByteArray response = "HTTP/1.1 " + status + "\r\nContent-Type: " + contentType + "\r\n" + extra
                              + "Content-Length: " + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n";
        if (method != QStringLiteral("HEAD")) {
            response += payload;
        }
        socket->write(response);
        socket->disconnectFromHost();
    }

    QHash<QTcpSocket *, QByteArray> m_buffers;
};

} // namespace

class TestHttpRunner : public QObject {
    Q_OBJECT

private:
    TestServer m_server;
    EnvironmentManager m_env;

    struct Outcome {
        HttpResult result;
        QStringList logs;
        QJsonObject echo; // o que o servidor viu (quando a rota é /echo)
    };

    Outcome run(const HttpConfig &config, const QVector<Folder> &chain = {})
    {
        return run(resolveHttpRequest(config, chain));
    }

    Outcome run(const HttpRequestSpec &spec)
    {
        Outcome out;
        HttpRunner runner;
        bool done = false;
        connect(&runner, &HttpRunner::logMessage, this, [&out](const QString &text, bool) { out.logs << text; });
        connect(&runner, &HttpRunner::finished, this, [&](const HttpResult &r) {
            out.result = r;
            done = true;
        });
        runner.execute(spec, m_env);
        [&]() { QTRY_VERIFY_WITH_TIMEOUT(done, 8000); }();
        out.echo = QJsonDocument::fromJson(out.result.body).object();
        return out;
    }

    HttpConfig get(const QString &path, const QString &method = QStringLiteral("GET"))
    {
        HttpConfig cfg;
        cfg.setMethodName(method);
        cfg.url = m_server.url(path);
        return cfg;
    }

    static QString header(const Outcome &o, const QString &name)
    {
        return o.echo.value(QStringLiteral("headers")).toObject().value(name.toLower()).toString();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_server.isListening());
        m_env.setGlobalVars({{QStringLiteral("TOKEN"), QStringLiteral("tk-123")}, {QStringLiteral("ID"), QStringLiteral("7")}});
    }

    void init() { engine::HttpCookieStore::clear(); }

    void foldersFeedHeadersAndBearerAuthToTheRequest()
    {
        Folder root;
        root.id = QStringLiteral("r");
        root.httpDefaults.headers = {entry("Accept", "application/json"), entry("X-Client", "kai-{{ID}}")};
        root.httpDefaults.auth.type = HttpAuthType::Bearer;
        root.httpDefaults.auth.token = QStringLiteral("{{TOKEN}}");
        HttpConfig cfg = get("/echo");
        cfg.headers = {entry("X-Own", "1")};
        cfg.queryParams = {entry("page", "2"), entry("q", "a b&c")};

        const Outcome o = run(cfg, {root});
        QVERIFY2(o.result.success, qPrintable(o.result.errorMessage));
        QCOMPARE(header(o, "Authorization"), QStringLiteral("Bearer tk-123"));
        QCOMPARE(header(o, "Accept"), QStringLiteral("application/json"));
        QCOMPARE(header(o, "X-Client"), QStringLiteral("kai-7"));
        QCOMPARE(header(o, "X-Own"), QStringLiteral("1"));
        QCOMPARE(o.echo.value("query").toString(), QStringLiteral("page=2&q=a%20b%26c"));
        bool sawAuthInRequestTab = false;
        for (const auto &h : o.result.requestHeaders) {
            sawAuthInRequestTab |= h.first == QStringLiteral("Authorization");
        }
        QVERIFY(sawAuthInRequestTab);
    }

    void aDisabledHeaderIsNotSent()
    {
        Folder root;
        root.id = QStringLiteral("r");
        root.httpDefaults.headers = {entry("X-Trace", "on")};
        HttpConfig cfg = get("/echo");
        HttpEntry off = entry("X-Trace", QString());
        off.enabled = false;
        cfg.headers = {off};
        QVERIFY(header(run(cfg, {root}), "X-Trace").isEmpty());
    }

    void basicAuthSendsBase64Credentials()
    {
        HttpConfig cfg = get("/echo");
        cfg.auth.type = HttpAuthType::Basic;
        cfg.auth.username = QStringLiteral("ana");
        cfg.auth.password = QStringLiteral("{{TOKEN}}");
        const Outcome o = run(cfg);
        QCOMPARE(header(o, "Authorization"), QStringLiteral("Basic ") + QString::fromLatin1(QByteArray("ana:tk-123").toBase64()));
    }

    void apiKeyGoesInTheHeaderOrInTheQuery()
    {
        HttpConfig cfg = get("/echo");
        cfg.auth.type = HttpAuthType::ApiKey;
        cfg.auth.keyName = QStringLiteral("X-API-Key");
        cfg.auth.keyValue = QStringLiteral("{{TOKEN}}");
        QCOMPARE(header(run(cfg), "X-API-Key"), QStringLiteral("tk-123"));

        cfg.auth.keyInQuery = true;
        cfg.auth.keyName = QStringLiteral("key");
        const Outcome o = run(cfg);
        QCOMPARE(o.echo.value("query").toString(), QStringLiteral("key=tk-123"));
        QVERIFY(header(o, "X-API-Key").isEmpty());
    }

    void aHandWrittenAuthorizationHeaderBeatsTheAuthBlock()
    {
        HttpConfig cfg = get("/echo");
        cfg.headers = {entry("Authorization", "Token mine")};
        cfg.auth.type = HttpAuthType::Bearer;
        cfg.auth.token = QStringLiteral("other");
        QCOMPARE(header(run(cfg), "Authorization"), QStringLiteral("Token mine"));
    }

    void jsonBodySetsTheContentType()
    {
        HttpConfig cfg = get("/echo", "POST");
        cfg.bodyType = HttpBodyType::Json;
        cfg.body = QStringLiteral("{\"id\": \"{{ID}}\"}");
        const Outcome o = run(cfg);
        QCOMPARE(header(o, "Content-Type"), QStringLiteral("application/json"));
        QCOMPARE(o.echo.value("body").toString(), QStringLiteral("{\"id\": \"7\"}"));
        QCOMPARE(o.echo.value("method").toString(), QStringLiteral("POST"));
    }

    void anExplicitContentTypeIsKept()
    {
        HttpConfig cfg = get("/echo", "POST");
        cfg.bodyType = HttpBodyType::Json;
        cfg.body = QStringLiteral("{}");
        cfg.headers = {entry("Content-Type", "application/vnd.api+json")};
        QCOMPARE(header(run(cfg), "Content-Type"), QStringLiteral("application/vnd.api+json"));
    }

    void formBodyIsUrlEncoded()
    {
        HttpConfig cfg = get("/echo", "POST");
        cfg.bodyType = HttpBodyType::Form;
        cfg.formFields = {entry("a", "1"), entry("b", "x y&z")};
        const Outcome o = run(cfg);
        QCOMPARE(header(o, "Content-Type"), QStringLiteral("application/x-www-form-urlencoded"));
        QCOMPARE(o.echo.value("body").toString(), QStringLiteral("a=1&b=x%20y%26z"));
    }

    void multipartBodyCarriesTextAndFiles()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("note.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("file-content-42");
        file.close();

        HttpConfig cfg = get("/echo", "POST");
        cfg.bodyType = HttpBodyType::Multipart;
        HttpEntry upload = entry("doc", path);
        upload.isFile = true;
        cfg.formFields = {entry("title", "Hello"), upload};
        const Outcome o = run(cfg);
        QVERIFY2(o.result.success, qPrintable(o.result.errorMessage));
        QVERIFY(header(o, "Content-Type").startsWith(QStringLiteral("multipart/form-data; boundary=")));
        const QString body = o.echo.value("body").toString();
        QVERIFY(body.contains(QStringLiteral("name=\"title\"")));
        QVERIFY(body.contains(QStringLiteral("Hello")));
        QVERIFY(body.contains(QStringLiteral("name=\"doc\"; filename=\"note.txt\"")));
        QVERIFY(body.contains(QStringLiteral("file-content-42")));
    }

    void aMissingUploadFailsBeforeSending()
    {
        HttpConfig cfg = get("/echo", "POST");
        cfg.bodyType = HttpBodyType::Multipart;
        HttpEntry upload = entry("doc", QStringLiteral("/nonexistent/file.bin"));
        upload.isFile = true;
        cfg.formFields = {upload};
        const Outcome o = run(cfg);
        QVERIFY(!o.result.success);
        QVERIFY(o.result.errorMessage.contains(QStringLiteral("/nonexistent/file.bin")));
    }

    void aFileCanBeTheBody()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("data.bin"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("RAW-PAYLOAD");
        file.close();

        HttpConfig cfg = get("/echo", "PUT");
        cfg.bodyType = HttpBodyType::File;
        cfg.bodyFile = path;
        const Outcome o = run(cfg);
        QCOMPARE(o.echo.value("body").toString(), QStringLiteral("RAW-PAYLOAD"));
        QCOMPARE(o.echo.value("method").toString(), QStringLiteral("PUT"));
    }

    void graphQlWrapsTheQueryAndVariables()
    {
        HttpConfig cfg = get("/echo", "POST");
        cfg.bodyType = HttpBodyType::GraphQl;
        cfg.body = QStringLiteral("query($id: ID!) { user(id: $id) { name } }");
        cfg.graphqlVariables = QStringLiteral("{\"id\": \"{{ID}}\"}");
        const Outcome o = run(cfg);
        const QJsonObject sent = QJsonDocument::fromJson(o.echo.value("body").toString().toUtf8()).object();
        QCOMPARE(sent.value("query").toString(), cfg.body);
        QCOMPARE(sent.value("variables").toObject().value("id").toString(), QStringLiteral("7"));
        QCOMPARE(header(o, "Content-Type"), QStringLiteral("application/json"));

        cfg.graphqlVariables = QStringLiteral("not json");
        QVERIFY(!run(cfg).result.success);
    }

    void headOptionsAndCustomVerbsAreSent()
    {
        const Outcome head = run(get("/echo", "HEAD"));
        QVERIFY(head.result.success);
        QCOMPARE(head.result.requestMethod, QStringLiteral("HEAD"));
        QVERIFY(head.result.body.isEmpty());

        QCOMPARE(run(get("/echo", "OPTIONS")).echo.value("method").toString(), QStringLiteral("OPTIONS"));
        QCOMPARE(run(get("/echo", "PROPFIND")).echo.value("method").toString(), QStringLiteral("PROPFIND"));
        QCOMPARE(run(get("/echo", "PATCH")).echo.value("method").toString(), QStringLiteral("PATCH"));
        QCOMPARE(run(get("/echo", "DELETE")).echo.value("method").toString(), QStringLiteral("DELETE"));
    }

    void redirectsAreFollowedAndRecordedUnlessTurnedOff()
    {
        const Outcome followed = run(get("/redirect"));
        QVERIFY(followed.result.success);
        QCOMPARE(followed.result.redirectChain.size(), 2);
        QVERIFY(followed.result.finalUrl.endsWith(QStringLiteral("/echo")));

        HttpConfig cfg = get("/redirect");
        cfg.options.followRedirects = false;
        const Outcome manual = run(cfg);
        QCOMPARE(manual.result.statusCode, 302);
        QVERIFY(!manual.result.success);
        QVERIFY(manual.result.redirectChain.isEmpty());
    }

    void cookiesPersistBetweenRequestsUnlessOptedOut()
    {
        QVERIFY(run(get("/set-cookie")).result.success);
        QCOMPARE(header(run(get("/echo")), "Cookie"), QStringLiteral("sid=abc"));

        HttpConfig cfg = get("/echo");
        cfg.options.useCookies = false;
        QVERIFY(header(run(cfg), "Cookie").isEmpty());

        engine::HttpCookieStore::clear();
        QVERIFY(header(run(get("/echo")), "Cookie").isEmpty());
    }

    void aResponseCanBeSavedToAFileAsItArrives()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sub/dir/out.bin"));
        HttpConfig cfg = get("/bin");
        cfg.responseFile = path;
        const Outcome o = run(cfg);
        QVERIFY2(o.result.success, qPrintable(o.result.errorMessage));
        QCOMPARE(o.result.savedPath, path);
        QCOMPARE(o.result.bodySize, qint64(20000));
        QVERIFY(o.result.body.isEmpty());
        QCOMPARE(QFileInfo(path).size(), qint64(20000));
    }

    void anErrorResponseIsNotSavedOverTheFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("keep.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("previous");
        file.close();
        HttpConfig cfg = get("/missing");
        cfg.responseFile = path;
        QVERIFY(!run(cfg).result.success);
        QFile again(path);
        QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(again.readAll(), QByteArray("previous"));
    }

    void binaryAndXmlResponsesAreSummarisedAndFormatted()
    {
        const Outcome bin = run(get("/bin"));
        QVERIFY(bin.result.isBinary());
        QVERIFY(bin.logs.join(QString()).contains(QStringLiteral("binary")));

        const Outcome xml = run(get("/xml"));
        QVERIFY(!xml.result.isBinary());
        QVERIFY(xml.logs.join(QString()).contains(QStringLiteral("  <b>1</b>")));
    }

    void timingIsReported()
    {
        const Outcome o = run(get("/echo"));
        QVERIFY(o.result.elapsedMs >= 0);
        QVERIFY(o.result.ttfbMs >= 0);
        QVERIFY(o.result.ttfbMs <= o.result.elapsedMs);
    }

    void non2xxFailsWithoutAssertions()
    {
        const Outcome o = run(get("/missing"));
        QVERIFY(!o.result.success);
        QCOMPARE(o.result.statusCode, 404);
    }

    void assertionsDecideSuccessSoA404CanBeExpected()
    {
        auto assertion = [](const QString &left, const QString &op, const QString &right) {
            ExecutionCondition c;
            c.left = left;
            c.op = op;
            c.right = right;
            return c;
        };
        HttpConfig cfg = get("/missing");
        cfg.assertions = {assertion(QStringLiteral("{{RESPONSE_STATUS}}"), QStringLiteral("eq"), QStringLiteral("404")),
                          assertion(QStringLiteral("{{RESPONSE_BODY}}"), QStringLiteral("contains"), QStringLiteral("nope"))};
        const Outcome ok = run(cfg);
        QVERIFY2(ok.result.success, qPrintable(ok.result.errorMessage));

        cfg.assertions = {assertion(QStringLiteral("{{RESPONSE_STATUS}}"), QStringLiteral("eq"), QStringLiteral("200"))};
        cfg.assertions.first().name = QStringLiteral("must be 200");
        const Outcome bad = run(cfg);
        QVERIFY(!bad.result.success);
        QCOMPARE(bad.result.assertionFailures, QStringList{QStringLiteral("must be 200")});

        // As desligadas não contam: sem nenhuma ligada, vale a regra normal (404 = falha).
        cfg.assertions.first().enabled = false;
        QVERIFY(!run(cfg).result.success);
    }

    void theResponseStaysAvailableAsVariablesForLaterSteps()
    {
        run(get("/echo"));
        QCOMPARE(m_env.interpolate(QStringLiteral("{{RESPONSE_STATUS}}")), QStringLiteral("200"));
        QVERIFY(m_env.interpolate(QStringLiteral("{{RESPONSE_CONTENT_TYPE}}")).startsWith(QStringLiteral("application/json")));
        QCOMPARE(m_env.interpolate(QStringLiteral("{{RESPONSE_HEADER_CONTENT_TYPE}}")).left(16), QStringLiteral("application/json"));
    }

    void theResponseBodyIsInterpolatedButNeverExportedToProcesses()
    {
        run(get("/echo"));
        QVERIFY(m_env.interpolate(QStringLiteral("{{RESPONSE_BODY}}")).contains(QStringLiteral("GET")));
        QVERIFY(!m_env.resolvedEnv().contains(QStringLiteral("RESPONSE_BODY")));
        QVERIFY(m_env.contains(QStringLiteral("RESPONSE_BODY")));
        // as outras (pequenas) continuam no ambiente
        QVERIFY(m_env.resolvedEnv().contains(QStringLiteral("RESPONSE_STATUS")));
    }

    void aConnectionErrorIsReported()
    {
        HttpConfig cfg;
        cfg.url = QStringLiteral("http://127.0.0.1:1/never");
        const Outcome o = run(cfg);
        QVERIFY(!o.result.success);
        QCOMPARE(o.result.statusCode, 0);
        QVERIFY(!o.result.errorMessage.isEmpty());
    }

    void abortCancelsARequestThatNeverAnswers()
    {
        HttpRunner runner;
        bool done = false;
        HttpResult result;
        connect(&runner, &HttpRunner::finished, this, [&](const HttpResult &r) {
            result = r;
            done = true;
        });
        runner.execute(resolveHttpRequest(get("/hang"), {}), m_env);
        QTest::qWait(150);
        QVERIFY(!done);
        runner.abort();
        QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
        QVERIFY(result.canceled);
        QVERIFY(!result.success);
        QVERIFY(!result.errorMessage.isEmpty());
    }

    void abortAfterTheResponseIsHarmless()
    {
        HttpRunner runner;
        bool done = false;
        connect(&runner, &HttpRunner::finished, this, [&](const HttpResult &) { done = true; });
        runner.execute(resolveHttpRequest(get("/echo"), {}), m_env);
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        runner.abort(); // nada em curso: não deve fazer nada nem travar
        QVERIFY(done);
    }

    void serverSentEventsAreShownAsTheyArrive()
    {
        const Outcome o = run(get("/sse"));
        QVERIFY2(o.result.success, qPrintable(o.result.errorMessage));
        const QString logs = o.logs.join(QString());
        QVERIFY(logs.contains(QStringLiteral("event stream")));
        QVERIFY(logs.contains(QStringLiteral("data: one")));
        QVERIFY(logs.contains(QStringLiteral("data: two")));
        // O corpo guardado é o do fluxo, e não é repetido no fim.
        QVERIFY(o.result.body.contains("data: two"));
        QCOMPARE(logs.count(QStringLiteral("data: one")), 1);
    }

    void aRequestWithATimeoutGivesUpAndSaysSo()
    {
        HttpConfig cfg = get("/hang");
        cfg.options.timeoutSec = 1;
        const Outcome o = run(cfg);
        QVERIFY(!o.result.success);
        QVERIFY(o.result.timedOut);
        QVERIFY(!o.result.canceled);
        QCOMPARE(o.result.statusCode, 0);
        QCOMPARE(o.result.timeoutSec, 1);
        QVERIFY2(o.result.errorMessage.contains(QStringLiteral("1")), qPrintable(o.result.errorMessage));
    }

    void withoutATimeoutARequestKeepsWaitingUntilStopped()
    {
        HttpRunner runner;
        std::optional<HttpResult> result;
        connect(&runner, &HttpRunner::finished, this, [&](const HttpResult &r) { result = r; });
        runner.execute(resolveHttpRequest(get("/hang"), {}), m_env);
        QTest::qWait(2500);
        QVERIFY(!result.has_value()); // nenhum limite: segue esperando (um servidor parado num breakpoint)
        runner.abort();
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 3000);
        QVERIFY(result->canceled);
        QVERIFY(!result->timedOut);
        QCOMPARE(result->timeoutSec, 0);
    }

    void theProxyOptionNoneConnectsDirectly()
    {
        HttpConfig cfg = get("/echo");
        cfg.options.proxy = QStringLiteral("none");
        QVERIFY(run(cfg).result.success);
    }
};

QTEST_MAIN(TestHttpRunner)
#include "test_http_runner.moc"
