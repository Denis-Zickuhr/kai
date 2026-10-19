// Tradução entre o modelo neutro do Kai e os formatos Anthropic / OpenAI-compatível: corpos de requisição,
// o fluxo SSE (cortado em pedaços arbitrários), respostas completas, erros e lista de modelos.

#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>

#include "engine/ai/ai-sse-parser.h"
#include "engine/ai/ai-wire.h"

using namespace kai::core;
using namespace kai::engine;

namespace {

AiProfile profile(AiWireFormat f)
{
    AiProfile p;
    p.id = QStringLiteral("p");
    p.wireFormat = f;
    p.model = QStringLiteral("model-x");
    p.apiKey = QStringLiteral("KEY");
    p.maxTokens = 1000;
    return p;
}

AiMessage user(const QString &t)
{
    AiMessage m;
    m.role = AiMessage::Role::User;
    m.text = t;
    return m;
}

AiMessage assistantWithCall(const QString &id, const QString &name, const QJsonObject &args, const QString &text = {})
{
    AiMessage m;
    m.role = AiMessage::Role::Assistant;
    m.text = text;
    m.toolCalls.append(AiToolCall{id, name, args});
    return m;
}

AiMessage toolResult(const QString &id, const QString &content, bool error = false)
{
    AiMessage m;
    m.role = AiMessage::Role::Tool;
    AiToolResult r;
    r.toolCallId = id;
    r.name = QStringLiteral("t");
    r.content = content;
    r.isError = error;
    m.toolResults.append(r);
    return m;
}

AiToolSpec spec()
{
    return {QStringLiteral("list_commands"), QStringLiteral("lists"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}};
}

// Alimenta o decodificador em pedaços de `step` bytes (pior caso de fragmentação).
void feedInPieces(AiStreamDecoder &d, const QByteArray &stream, int step)
{
    for (int i = 0; i < stream.size(); i += step) {
        d.feed(stream.mid(i, step));
    }
    d.finish();
}

const QByteArray kAnthropicStream =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":25,\"cache_read_input_tokens\":3}}}\n\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
    "event: ping\ndata: {\"type\":\"ping\"}\n\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Vou \"}}\n\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"listar.\"}}\n\n"
    "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"list_commands\",\"input\":{}}}\n\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"query\\\"\"}}\n\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\": \\\"dep\\\"}\"}}\n\n"
    "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},\"usage\":{\"output_tokens\":42}}\n\n"
    "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

const QByteArray kOpenAiStream =
    "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"Vou \"}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"content\":\"listar.\"}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_9\",\"type\":\"function\",\"function\":{\"name\":\"list_commands\",\"arguments\":\"\"}}]}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"{\\\"query\\\"\"}}]}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\": \\\"dep\\\"}\"}}]}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
    "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":25,\"completion_tokens\":42,\"prompt_tokens_details\":{\"cached_tokens\":3}}}\n\n"
    "data: [DONE]\n\n";

} // namespace

class TestAiWire : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------------------- SSE
    void sseParserHandlesFragmentsCommentsAndCrlf()
    {
        QList<QPair<QString, QByteArray>> events;
        AiSseParser p([&events](const QString &e, const QByteArray &d) { events.append(qMakePair(e, d)); });
        const QByteArray text = ": keep-alive\r\nevent: a\r\ndata: one\r\n\r\ndata: two\ndata: three\n\nevent: b\ndata: last";
        for (int i = 0; i < text.size(); i += 3) p.feed(text.mid(i, 3));
        p.finish();
        QCOMPARE(events.size(), 3);
        QCOMPARE(events.at(0).first, QStringLiteral("a"));
        QCOMPARE(events.at(0).second, QByteArray("one"));
        QCOMPARE(events.at(1).second, QByteArray("two\nthree"));
        QCOMPARE(events.at(2).first, QStringLiteral("b"));
        QCOMPARE(events.at(2).second, QByteArray("last")); // evento final sem linha em branco
    }

    // ----------------------------------------------------------- Anthropic
    void anthropicBodyHasSystemToolsAndCacheMarks()
    {
        AiRequest r;
        r.profile = profile(AiWireFormat::Anthropic);
        r.system = QStringLiteral("SYS");
        r.messages << user(QStringLiteral("oi"));
        r.tools << spec();
        const QJsonObject b = buildRequestBody(r);
        QCOMPARE(b.value(QStringLiteral("model")).toString(), QStringLiteral("model-x"));
        QCOMPARE(b.value(QStringLiteral("max_tokens")).toInt(), 1000);
        QVERIFY(b.value(QStringLiteral("stream")).toBool());
        const QJsonObject sys = b.value(QStringLiteral("system")).toArray().first().toObject();
        QCOMPARE(sys.value(QStringLiteral("text")).toString(), QStringLiteral("SYS"));
        QVERIFY(sys.contains(QStringLiteral("cache_control")));
        const QJsonObject tool = b.value(QStringLiteral("tools")).toArray().last().toObject();
        QCOMPARE(tool.value(QStringLiteral("name")).toString(), QStringLiteral("list_commands"));
        QVERIFY(tool.contains(QStringLiteral("input_schema")));
        QVERIFY(tool.contains(QStringLiteral("cache_control")));
        const QJsonObject m = b.value(QStringLiteral("messages")).toArray().first().toObject();
        QCOMPARE(m.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    }

    void anthropicToolRoundTripIsSerialisedAsBlocks()
    {
        AiRequest r;
        r.profile = profile(AiWireFormat::Anthropic);
        r.stream = false;
        r.tools << spec();
        r.messages << user(QStringLiteral("lista"))
                   << assistantWithCall(QStringLiteral("t1"), QStringLiteral("list_commands"), QJsonObject{{QStringLiteral("q"), QStringLiteral("a")}}, QStringLiteral("ok"))
                   << toolResult(QStringLiteral("t1"), QStringLiteral("[]"), true)
                   << user(QStringLiteral("e agora?"));
        const QJsonArray msgs = buildRequestBody(r).value(QStringLiteral("messages")).toArray();
        QVERIFY(!buildRequestBody(r).contains(QStringLiteral("stream")));
        QCOMPARE(msgs.size(), 3); // o resultado e a pergunta seguinte (ambos "user") se juntam
        const QJsonArray assistant = msgs.at(1).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(assistant.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
        const QJsonObject use = assistant.at(1).toObject();
        QCOMPARE(use.value(QStringLiteral("type")).toString(), QStringLiteral("tool_use"));
        QCOMPARE(use.value(QStringLiteral("id")).toString(), QStringLiteral("t1"));
        QCOMPARE(use.value(QStringLiteral("input")).toObject().value(QStringLiteral("q")).toString(), QStringLiteral("a"));
        const QJsonArray last = msgs.at(2).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(last.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("tool_result"));
        QVERIFY(last.at(0).toObject().value(QStringLiteral("is_error")).toBool());
        QCOMPARE(last.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    }

    void anthropicHeaders()
    {
        const auto h = buildRequestHeaders(profile(AiWireFormat::Anthropic), true);
        auto value = [&h](const char *name) {
            for (const auto &p : h) if (p.first == name) return p.second;
            return QByteArray();
        };
        QCOMPARE(value("x-api-key"), QByteArray("KEY"));
        QCOMPARE(value("anthropic-version"), QByteArray("2023-06-01"));
        QCOMPARE(value("Accept"), QByteArray("text/event-stream"));
        QVERIFY(value("Authorization").isEmpty());
    }

    void anthropicStreamDecodesAtEveryFragmentSize()
    {
        for (int step : {1, 2, 7, 64, 100000}) {
            QStringList texts;
            QStringList tools;
            AiStreamDecoder::Callbacks cb;
            cb.onText = [&texts](const QString &t) { texts << t; };
            cb.onToolStarted = [&tools](const QString &n) { tools << n; };
            auto d = AiStreamDecoder::create(AiWireFormat::Anthropic, cb);
            feedInPieces(*d, kAnthropicStream, step);
            QVERIFY2(!d->hasError(), qPrintable(QString::number(step)));
            QCOMPARE(texts.join(QString()), QStringLiteral("Vou listar."));
            QCOMPARE(tools, QStringList{QStringLiteral("list_commands")});
            const AiTurn t = d->turn();
            QCOMPARE(t.text, QStringLiteral("Vou listar."));
            QCOMPARE(t.toolCalls.size(), 1);
            QCOMPARE(t.toolCalls.first().id, QStringLiteral("toolu_1"));
            QCOMPARE(t.toolCalls.first().arguments.value(QStringLiteral("query")).toString(), QStringLiteral("dep"));
            QCOMPARE(t.stopReason, QStringLiteral("tool_use"));
            QCOMPARE(t.usage.inputTokens, 25);
            QCOMPARE(t.usage.outputTokens, 42);
            QCOMPARE(t.usage.cacheReadTokens, 3);
            QVERIFY(d->completed());
        }
    }

    void anthropicStreamErrorEvent()
    {
        auto d = AiStreamDecoder::create(AiWireFormat::Anthropic, {});
        d->feed("event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}\n\n");
        d->finish();
        QVERIFY(d->hasError());
        QCOMPARE(d->error().kind, AiError::Kind::Overloaded);
    }

    void anthropicResponseParses()
    {
        const QByteArray body = "{\"type\":\"message\",\"content\":[{\"type\":\"text\",\"text\":\"ola\"},"
            "{\"type\":\"tool_use\",\"id\":\"t\",\"name\":\"x\",\"input\":{\"a\":1}}],\"stop_reason\":\"tool_use\","
            "\"usage\":{\"input_tokens\":5,\"output_tokens\":6}}";
        AiTurn t;
        AiError e;
        QVERIFY(parseResponse(AiWireFormat::Anthropic, body, t, e));
        QCOMPARE(t.text, QStringLiteral("ola"));
        QCOMPARE(t.toolCalls.first().arguments.value(QStringLiteral("a")).toInt(), 1);
        QCOMPARE(t.usage.outputTokens, 6);
        QVERIFY(!parseResponse(AiWireFormat::Anthropic, "nope", t, e));
        QCOMPARE(e.kind, AiError::Kind::Parse);
    }

    // -------------------------------------------------------- OpenAI-compat
    void openAiBodyUsesTheRightTokenFieldAndToolShape()
    {
        AiRequest r;
        r.profile = profile(AiWireFormat::OpenAiCompatible);
        r.system = QStringLiteral("SYS");
        r.messages << user(QStringLiteral("oi"));
        r.tools << spec();

        r.profile.baseUrl = QStringLiteral("http://localhost:11434/v1");
        QJsonObject b = buildRequestBody(r);
        QVERIFY(b.contains(QStringLiteral("max_tokens")));
        QVERIFY(!b.contains(QStringLiteral("max_completion_tokens")));
        QVERIFY(b.value(QStringLiteral("stream_options")).toObject().value(QStringLiteral("include_usage")).toBool());
        QCOMPARE(b.value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("role")).toString(),
                 QStringLiteral("system"));
        const QJsonObject fn = b.value(QStringLiteral("tools")).toArray().first().toObject().value(QStringLiteral("function")).toObject();
        QCOMPARE(fn.value(QStringLiteral("name")).toString(), QStringLiteral("list_commands"));
        QVERIFY(fn.contains(QStringLiteral("parameters")));

        r.profile.baseUrl.clear(); // api.openai.com
        b = buildRequestBody(r);
        QVERIFY(b.contains(QStringLiteral("max_completion_tokens")));
        QVERIFY(!b.contains(QStringLiteral("max_tokens")));
    }

    void openAiToolRoundTripUsesToolRoleMessages()
    {
        AiRequest r;
        r.profile = profile(AiWireFormat::OpenAiCompatible);
        r.tools << spec();
        r.messages << user(QStringLiteral("lista"))
                   << assistantWithCall(QStringLiteral("c1"), QStringLiteral("list_commands"), QJsonObject{{QStringLiteral("q"), QStringLiteral("a")}})
                   << toolResult(QStringLiteral("c1"), QStringLiteral("[]"));
        const QJsonArray msgs = buildRequestBody(r).value(QStringLiteral("messages")).toArray();
        QCOMPARE(msgs.size(), 3);
        const QJsonObject a = msgs.at(1).toObject();
        QVERIFY(a.value(QStringLiteral("content")).isNull());
        const QJsonObject call = a.value(QStringLiteral("tool_calls")).toArray().first().toObject();
        QCOMPARE(call.value(QStringLiteral("id")).toString(), QStringLiteral("c1"));
        // Os argumentos viajam como TEXTO JSON neste formato.
        QVERIFY(call.value(QStringLiteral("function")).toObject().value(QStringLiteral("arguments")).isString());
        QCOMPARE(msgs.at(2).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
        QCOMPARE(msgs.at(2).toObject().value(QStringLiteral("tool_call_id")).toString(), QStringLiteral("c1"));
    }

    void openAiHeaders()
    {
        auto h = buildRequestHeaders(profile(AiWireFormat::OpenAiCompatible), false);
        bool bearer = false;
        for (const auto &p : h) if (p.first == "Authorization") bearer = p.second == "Bearer KEY";
        QVERIFY(bearer);
        AiProfile local = profile(AiWireFormat::OpenAiCompatible);
        local.apiKey.clear();
        for (const auto &p : buildRequestHeaders(local, false)) QVERIFY(p.first != "Authorization");
    }

    void openAiStreamDecodesAtEveryFragmentSize()
    {
        for (int step : {1, 5, 33, 100000}) {
            QString text;
            QStringList tools;
            AiStreamDecoder::Callbacks cb;
            cb.onText = [&text](const QString &t) { text += t; };
            cb.onToolStarted = [&tools](const QString &n) { tools << n; };
            auto d = AiStreamDecoder::create(AiWireFormat::OpenAiCompatible, cb);
            feedInPieces(*d, kOpenAiStream, step);
            QVERIFY(!d->hasError());
            QCOMPARE(text, QStringLiteral("Vou listar."));
            QCOMPARE(tools.size(), 1);
            const AiTurn t = d->turn();
            QCOMPARE(t.toolCalls.size(), 1);
            QCOMPARE(t.toolCalls.first().id, QStringLiteral("call_9"));
            QCOMPARE(t.toolCalls.first().arguments.value(QStringLiteral("query")).toString(), QStringLiteral("dep"));
            QCOMPARE(t.stopReason, QStringLiteral("tool_use"));
            QCOMPARE(t.usage.inputTokens, 25);
            QCOMPARE(t.usage.cacheReadTokens, 3);
            QVERIFY(d->completed());
        }
    }

    void openAiStreamToleratesMissingToolIdsAndWholeCallsInOneChunk()
    {
        auto d = AiStreamDecoder::create(AiWireFormat::OpenAiCompatible, {});
        d->feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"function\":{\"name\":\"a\",\"arguments\":\"{}\"}},"
                "{\"function\":{\"name\":\"b\",\"arguments\":\"{\\\"x\\\":1}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\ndata: [DONE]\n\n");
        d->finish();
        const AiTurn t = d->turn();
        QCOMPARE(t.toolCalls.size(), 2);
        QVERIFY(!t.toolCalls.at(0).id.isEmpty());
        QVERIFY(t.toolCalls.at(0).id != t.toolCalls.at(1).id);
        QCOMPARE(t.toolCalls.at(1).arguments.value(QStringLiteral("x")).toInt(), 1);
    }

    void invalidToolArgumentsAreFlaggedNotLost()
    {
        auto d = AiStreamDecoder::create(AiWireFormat::OpenAiCompatible, {});
        d->feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"i\",\"function\":{\"name\":\"a\",\"arguments\":\"{broken\"}}]}}]}\n\n");
        d->finish();
        QVERIFY(d->turn().toolCalls.first().arguments.contains(QStringLiteral("__parse_error")));
    }

    void openAiResponseParses()
    {
        const QByteArray body = "{\"choices\":[{\"message\":{\"content\":\"ola\",\"tool_calls\":["
            "{\"id\":\"c\",\"type\":\"function\",\"function\":{\"name\":\"x\",\"arguments\":\"{\\\"a\\\":2}\"}}]},"
            "\"finish_reason\":\"tool_calls\"}],\"usage\":{\"prompt_tokens\":7,\"completion_tokens\":8}}";
        AiTurn t;
        AiError e;
        QVERIFY(parseResponse(AiWireFormat::OpenAiCompatible, body, t, e));
        QCOMPARE(t.toolCalls.first().arguments.value(QStringLiteral("a")).toInt(), 2);
        QCOMPARE(t.stopReason, QStringLiteral("tool_use"));
        QCOMPARE(t.usage.inputTokens, 7);
        QVERIFY(!parseResponse(AiWireFormat::OpenAiCompatible, R"({"error":{"message":"boom"}})", t, e));
    }

    // Gemini (endpoint compatível): a `thought_signature` vem em `extra_content` da chamada e precisa voltar no histórico,
    // senão o próximo passo da conversa dá 400 ("function call is missing a thought_signature").
    void geminiThoughtSignatureSurvivesTheRoundTrip()
    {
        const QByteArray signature = "{\"google\":{\"thought_signature\":\"SIG123\"}}";
        const QJsonObject extra = QJsonDocument::fromJson(signature).object();

        // Sem streaming.
        const QByteArray body = "{\"choices\":[{\"message\":{\"tool_calls\":[{\"id\":\"c\",\"type\":\"function\","
            "\"extra_content\":" + signature + ",\"function\":{\"name\":\"x\",\"arguments\":\"{}\"}}]},\"finish_reason\":\"tool_calls\"}]}";
        AiTurn plain;
        AiError e;
        QVERIFY(parseResponse(AiWireFormat::OpenAiCompatible, body, plain, e));
        QCOMPARE(plain.toolCalls.first().extra, extra);

        // Com streaming (a assinatura chega no primeiro fragmento da chamada).
        auto d = AiStreamDecoder::create(AiWireFormat::OpenAiCompatible, {});
        d->feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"c\",\"extra_content\":" + signature
                + ",\"function\":{\"name\":\"x\",\"arguments\":\"{\"}}]}}]}\n\n"
                "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"}\"}}]}}]}\n\n");
        d->finish();
        QCOMPARE(d->turn().toolCalls.first().extra, extra);

        // O histórico devolve a assinatura junto da chamada.
        AiRequest r;
        r.profile = profile(AiWireFormat::OpenAiCompatible);
        r.tools << spec();
        AiMessage assistant = assistantWithCall(QStringLiteral("c"), QStringLiteral("x"), {});
        assistant.toolCalls[0].extra = extra;
        r.messages << user(QStringLiteral("oi")) << assistant << toolResult(QStringLiteral("c"), QStringLiteral("[]"));
        const QJsonArray msgs = buildRequestBody(r).value(QStringLiteral("messages")).toArray();
        const QJsonObject call = msgs.at(1).toObject().value(QStringLiteral("tool_calls")).toArray().first().toObject();
        QCOMPARE(call.value(QStringLiteral("extra_content")).toObject(), extra);

        // E ela sobrevive ao salvar e reabrir a conversa.
        QCOMPARE(AiMessage::fromJson(assistant.toJson()).toolCalls.first().extra, extra);
        // Provedores sem assinatura não ganham campo nenhum.
        AiMessage bare = assistantWithCall(QStringLiteral("c"), QStringLiteral("x"), {});
        QVERIFY(!AiMessage::fromJson(bare.toJson()).toolCalls.first().extra.contains(QStringLiteral("google")));
    }

    // -------------------------------------------------------------- comuns
    void sanitizeFillsUnansweredCallsAndDropsOrphanResults()
    {
        QVector<AiMessage> v;
        v << user(QStringLiteral("a"))
          << assistantWithCall(QStringLiteral("x"), QStringLiteral("t"), {})
          << user(QStringLiteral("b")) // a chamada x ficou sem resposta
          << toolResult(QStringLiteral("zzz"), QStringLiteral("orfao"));
        const QVector<AiMessage> out = sanitizeMessages(v);
        QCOMPARE(out.size(), 4);
        QCOMPARE(out.at(2).role, AiMessage::Role::Tool);
        QVERIFY(out.at(2).toolResults.first().isError);
        QCOMPARE(out.at(2).toolResults.first().toolCallId, QStringLiteral("x"));
        QCOMPARE(out.at(3).text, QStringLiteral("b"));
    }

    void historyWithToolsIsFlattenedWhenNoToolsAreOffered()
    {
        AiRequest r;
        r.profile = profile(AiWireFormat::Anthropic);
        r.messages << user(QStringLiteral("a"))
                   << assistantWithCall(QStringLiteral("x"), QStringLiteral("t"), {})
                   << toolResult(QStringLiteral("x"), QStringLiteral("resultado"));
        const QByteArray json = QJsonDocument(buildRequestBody(r)).toJson();
        QVERIFY(!json.contains("tool_use"));
        QVERIFY(!json.contains("tool_result"));
        QVERIFY(json.contains("resultado"));
    }

    void httpErrorsAreClassified()
    {
        QCOMPARE(errorFromHttp(401, R"({"error":{"message":"bad key"}})").kind, AiError::Kind::Auth);
        QCOMPARE(errorFromHttp(401, R"({"error":{"message":"bad key"}})").message, QStringLiteral("bad key"));
        QCOMPARE(errorFromHttp(429, "{}").kind, AiError::Kind::RateLimit);
        QCOMPARE(errorFromHttp(529, "x").kind, AiError::Kind::Overloaded);
        QCOMPARE(errorFromHttp(400, R"({"error":"plain"})").message, QStringLiteral("plain"));
        QCOMPARE(errorFromHttp(400, "{}").kind, AiError::Kind::BadRequest);
        QCOMPARE(errorFromHttp(503, "oops").kind, AiError::Kind::Server);
        QVERIFY(errorFromHttp(429, "{}").retryable());
        QVERIFY(!errorFromHttp(401, "{}").retryable());
        QVERIFY(!errorFromHttp(400, "{}").retryable());
    }

    void modelListsParseBothShapes()
    {
        QCOMPARE(parseModelList(R"({"data":[{"id":"b"},{"id":"a"},{"id":"a"}]})"), (QStringList{"a", "b"}));
        QCOMPARE(parseModelList(R"({"models":[{"name":"llama3"}]})"), QStringList{"llama3"});
        QCOMPARE(parseModelList(R"({"data":[{"id":"models/gemini-2.5-flash"}]})"), QStringList{"gemini-2.5-flash"});
        QVERIFY(parseModelList("garbage").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAiWire)
#include "test_ai_wire.moc"
