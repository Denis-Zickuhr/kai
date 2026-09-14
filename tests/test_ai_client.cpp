// AiClient contra um servidor HTTP falso: streaming, resposta completa, erros, repetição com espera,
// cancelamento, tempo limite do primeiro token e lista de modelos.

#include <QTest>
#include <QSignalSpy>

#include "ai-fake-server.h"
#include "engine/ai/ai-client.h"
#include "engine/http-runner.h"
#include <QNetworkProxy>

using namespace kai::core;
using namespace kai::engine;

namespace {

AiProfile profile(const FakeAiServer &server, AiWireFormat f)
{
    AiProfile p;
    p.id = QStringLiteral("p");
    p.wireFormat = f;
    p.model = QStringLiteral("m");
    p.apiKey = QStringLiteral("SECRET-KEY");
    p.baseUrl = server.baseUrl();
    p.firstTokenTimeoutSec = 30;
    return p;
}

AiRequest request(const AiProfile &p, bool stream = true)
{
    AiRequest r;
    r.profile = p;
    r.stream = stream;
    r.system = QStringLiteral("S");
    AiMessage m;
    m.role = AiMessage::Role::User;
    m.text = QStringLiteral("oi");
    r.messages << m;
    return r;
}

const QList<QByteArray> kAnthropicOk = {
    "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":3}}}\n\n",
    "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"ola\"}}\n\n",
    "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":1}}\n\n",
    "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n"};

} // namespace

class TestAiClient : public QObject {
    Q_OBJECT

private slots:
    void anthropicStreamEndToEnd()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        server.enqueue(FakeAiServer::sse(kAnthropicOk));
        AiClient client;
        QSignalSpy deltas(&client, &AiClient::textDelta);
        QSignalSpy done(&client, &AiClient::completed);
        QSignalSpy failed(&client, &AiClient::failed);
        client.send(request(profile(server, AiWireFormat::Anthropic)));
        QVERIFY(client.isBusy());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(deltas.first().first().toString(), QStringLiteral("ola"));
        const AiTurn turn = done.first().first().value<AiTurn>();
        QCOMPARE(turn.text, QStringLiteral("ola"));
        QCOMPARE(turn.usage.inputTokens, 3);
        QVERIFY(!client.isBusy());

        QCOMPARE(server.requests().size(), 1);
        const auto r = server.requests().first();
        QCOMPARE(r.path, QByteArray("/v1/messages"));
        QCOMPARE(r.headers.value("x-api-key"), QByteArray("SECRET-KEY"));
        QVERIFY(r.json().value(QStringLiteral("stream")).toBool());
        QCOMPARE(r.json().value(QStringLiteral("model")).toString(), QStringLiteral("m"));
    }

    void openAiNonStreamEndToEnd()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        server.enqueue(FakeAiServer::json(200, R"({"choices":[{"message":{"content":"pronto"},"finish_reason":"stop"}],
            "usage":{"prompt_tokens":2,"completion_tokens":3}})"));
        AiClient client;
        QSignalSpy done(&client, &AiClient::completed);
        client.send(request(profile(server, AiWireFormat::OpenAiCompatible), false));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.first().first().value<AiTurn>().text, QStringLiteral("pronto"));
        QCOMPARE(server.requests().first().path, QByteArray("/chat/completions"));
        QCOMPARE(server.requests().first().headers.value("authorization"), QByteArray("Bearer SECRET-KEY"));
        QVERIFY(!server.requests().first().json().contains(QStringLiteral("stream")));
    }

    void authErrorsAreNotRetried()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        server.enqueue(FakeAiServer::json(401, R"({"error":{"message":"invalid x-api-key"}})"));
        AiClient client;
        client.setRetryBaseDelayMs(5);
        QSignalSpy failed(&client, &AiClient::failed);
        QSignalSpy retrying(&client, &AiClient::retrying);
        client.send(request(profile(server, AiWireFormat::Anthropic)));
        QTRY_COMPARE(failed.size(), 1);
        const AiError e = failed.first().first().value<AiError>();
        QCOMPARE(e.kind, AiError::Kind::Auth);
        QCOMPARE(e.httpStatus, 401);
        QVERIFY(e.message.contains(QStringLiteral("invalid x-api-key")));
        QVERIFY(!e.message.contains(QStringLiteral("SECRET-KEY")));
        QCOMPARE(retrying.size(), 0);
        QCOMPARE(server.requests().size(), 1);
    }

    void rateLimitIsRetriedThenSucceeds()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        auto limited = FakeAiServer::json(429, R"({"error":{"message":"slow down"}})");
        server.enqueue(limited);
        server.enqueue(FakeAiServer::sse(kAnthropicOk));
        AiClient client;
        client.setRetryBaseDelayMs(5);
        QSignalSpy retrying(&client, &AiClient::retrying);
        QSignalSpy done(&client, &AiClient::completed);
        client.send(request(profile(server, AiWireFormat::Anthropic)));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(retrying.size(), 1);
        QCOMPARE(server.requests().size(), 2);
    }

    void givesUpAfterTheMaximumAttempts()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        for (int i = 0; i < 5; ++i) server.enqueue(FakeAiServer::json(503, R"({"error":{"message":"down"}})"));
        AiClient client;
        client.setRetryBaseDelayMs(5);
        QSignalSpy failed(&client, &AiClient::failed);
        client.send(request(profile(server, AiWireFormat::OpenAiCompatible)));
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(server.requests().size(), AiClient::kMaxAttempts);
        QCOMPARE(failed.first().first().value<AiError>().kind, AiError::Kind::Server);
    }

    void cancelStopsAStreamInTheMiddle()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        FakeAiServer::Reply r = FakeAiServer::sse({kAnthropicOk.at(0), kAnthropicOk.at(1)});
        r.hang = true;
        server.enqueue(r);
        AiClient client;
        QSignalSpy deltas(&client, &AiClient::textDelta);
        QSignalSpy failed(&client, &AiClient::failed);
        client.send(request(profile(server, AiWireFormat::Anthropic)));
        QTRY_VERIFY(!deltas.isEmpty());
        client.cancel();
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().value<AiError>().kind, AiError::Kind::Cancelled);
        QVERIFY(!client.isBusy());
    }

    void firstTokenTimeoutFails()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        for (int i = 0; i < AiClient::kMaxAttempts; ++i) {
            FakeAiServer::Reply silent;
            silent.silent = true;
            server.enqueue(silent);
        }
        AiClient client;
        client.setRetryBaseDelayMs(5);
        AiProfile p = profile(server, AiWireFormat::Anthropic);
        p.firstTokenTimeoutSec = 1;
        QSignalSpy failed(&client, &AiClient::failed);
        client.send(request(p));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
        QCOMPARE(failed.first().first().value<AiError>().kind, AiError::Kind::Timeout);
    }

    void aDroppedConnectionIsANetworkError()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        for (int i = 0; i < AiClient::kMaxAttempts; ++i) {
            FakeAiServer::Reply drop;
            drop.drop = true;
            server.enqueue(drop);
        }
        AiClient client;
        client.setRetryBaseDelayMs(5);
        QSignalSpy failed(&client, &AiClient::failed);
        client.send(request(profile(server, AiWireFormat::Anthropic)));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
        // Qt pode reportar o fecho como erro de rede ou como resposta vazia; os dois são repetíveis.
        QVERIFY(failed.first().first().value<AiError>().retryable());
        QVERIFY(server.requests().size() >= AiClient::kMaxAttempts); // o Qt ainda reenvia sozinho uma vez
    }

    // A empresa atrás de proxy: o cliente de IA usa as mesmas variáveis das requisições HTTP do Kai.
    void theProxyComesFromTheEnvironmentLikeInHttpCommands()
    {
        qputenv("HTTPS_PROXY", "http://proxy.corp:3128");
        qputenv("NO_PROXY", "localhost,.internal");
        const QNetworkProxy viaProxy = resolveHttpProxy(QStringLiteral("system"), QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
        QCOMPARE(viaProxy.type(), QNetworkProxy::HttpProxy);
        QCOMPARE(viaProxy.hostName(), QStringLiteral("proxy.corp"));
        QCOMPARE(int(viaProxy.port()), 3128);
        QCOMPARE(resolveHttpProxy(QStringLiteral("system"), QUrl(QStringLiteral("http://localhost:11434/v1"))).type(), QNetworkProxy::NoProxy);
        QCOMPARE(resolveHttpProxy(QStringLiteral("none"), QUrl(QStringLiteral("https://x.test"))).type(), QNetworkProxy::NoProxy);
        qunsetenv("HTTPS_PROXY");
        qunsetenv("NO_PROXY");
    }

    void listsModels()
    {
        FakeAiServer server;
        QVERIFY(server.start());
        server.enqueue(FakeAiServer::json(200, R"({"data":[{"id":"zeta"},{"id":"alpha"}]})"));
        server.enqueue(FakeAiServer::json(401, R"({"error":{"message":"no"}})"));
        AiClient client;
        QSignalSpy models(&client, &AiClient::modelsListed);
        QSignalSpy bad(&client, &AiClient::modelsFailed);
        client.listModels(profile(server, AiWireFormat::OpenAiCompatible));
        QTRY_COMPARE(models.size(), 1);
        QCOMPARE(models.first().first().toStringList(), (QStringList{"alpha", "zeta"}));
        QCOMPARE(server.requests().first().path, QByteArray("/models"));
        QCOMPARE(server.requests().first().method, QByteArray("GET"));
        client.listModels(profile(server, AiWireFormat::OpenAiCompatible));
        QTRY_COMPARE(bad.size(), 1);
        QCOMPARE(bad.first().first().value<AiError>().kind, AiError::Kind::Auth);
    }
};

QTEST_GUILESS_MAIN(TestAiClient)
#include "test_ai_client.moc"
