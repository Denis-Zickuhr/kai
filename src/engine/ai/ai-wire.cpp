#include "engine/ai/ai-wire.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QUrl>
#include <algorithm>

namespace kai::engine {

using core::AiMessage;
using core::AiToolCall;
using core::AiToolResult;
using core::AiWireFormat;

namespace {

QString compactJson(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject parseObject(const QByteArray &bytes, bool *ok = nullptr)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (ok) {
        *ok = err.error == QJsonParseError::NoError && doc.isObject();
    }
    return doc.isObject() ? doc.object() : QJsonObject();
}

// Argumentos acumulados como texto -> objeto. Vazio vira {}; lixo vira {"__parse_error": texto}.
QJsonObject argumentsFromText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    bool ok = false;
    const QJsonObject obj = parseObject(trimmed.toUtf8(), &ok);
    if (!ok) {
        return {{QStringLiteral("__parse_error"), trimmed}};
    }
    return obj;
}

// Sem ferramentas na requisição, o histórico não pode carregar blocos de ferramenta (o provedor
// recusa): vira texto corrido. Acontece num modelo sem tool calling e ao trocar de provedor.
QVector<AiMessage> flattenTools(const QVector<AiMessage> &messages)
{
    QVector<AiMessage> out;
    for (const AiMessage &m : messages) {
        AiMessage copy = m;
        if (m.role == AiMessage::Role::Assistant && !m.toolCalls.isEmpty()) {
            QStringList notes;
            for (const AiToolCall &c : m.toolCalls) {
                notes << QStringLiteral("[tool call %1 %2]").arg(c.name, compactJson(c.arguments));
            }
            copy.text = (m.text.isEmpty() ? QString() : m.text + QLatin1Char('\n')) + notes.join(QLatin1Char('\n'));
            copy.toolCalls.clear();
        } else if (m.role == AiMessage::Role::Tool) {
            QStringList notes;
            for (const AiToolResult &r : m.toolResults) {
                notes << QStringLiteral("[tool result %1]\n%2").arg(r.name, r.content);
            }
            copy.role = AiMessage::Role::User;
            copy.text = notes.join(QStringLiteral("\n\n"));
            copy.toolResults.clear();
        }
        out.append(copy);
    }
    return out;
}

// ---------------------------------------------------------------- Anthropic

QJsonObject anthropicBody(const AiRequest &req)
{
    const QVector<AiMessage> msgs = sanitizeMessages(req.tools.isEmpty() ? flattenTools(req.messages) : req.messages);

    QJsonArray messages;
    auto pushBlocks = [&messages](const QString &role, const QJsonArray &blocks) {
        if (blocks.isEmpty()) {
            return;
        }
        // O formato pede alternância user/assistant: blocos seguidos do mesmo papel entram juntos.
        if (!messages.isEmpty() && messages.last().toObject().value(QStringLiteral("role")).toString() == role) {
            QJsonObject last = messages.last().toObject();
            QJsonArray content = last.value(QStringLiteral("content")).toArray();
            for (const QJsonValue &b : blocks) content.append(b);
            last[QStringLiteral("content")] = content;
            messages[messages.size() - 1] = last;
            return;
        }
        messages.append(QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), blocks}});
    };

    for (const AiMessage &m : msgs) {
        QJsonArray blocks;
        switch (m.role) {
        case AiMessage::Role::User:
            if (!m.text.isEmpty()) {
                blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), m.text}});
            }
            pushBlocks(QStringLiteral("user"), blocks);
            break;
        case AiMessage::Role::Assistant:
            if (!m.text.isEmpty()) {
                blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), m.text}});
            }
            for (const AiToolCall &c : m.toolCalls) {
                blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                          {QStringLiteral("id"), c.id},
                                          {QStringLiteral("name"), c.name},
                                          {QStringLiteral("input"), c.arguments}});
            }
            pushBlocks(QStringLiteral("assistant"), blocks);
            break;
        case AiMessage::Role::Tool:
            for (const AiToolResult &r : m.toolResults) {
                QJsonObject block{{QStringLiteral("type"), QStringLiteral("tool_result")},
                                  {QStringLiteral("tool_use_id"), r.toolCallId},
                                  {QStringLiteral("content"), r.content}};
                if (r.isError) {
                    block[QStringLiteral("is_error")] = true;
                }
                blocks.append(block);
            }
            pushBlocks(QStringLiteral("user"), blocks);
            break;
        }
    }

    QJsonObject body;
    body[QStringLiteral("model")] = req.profile.model;
    body[QStringLiteral("max_tokens")] = req.maxTokens > 0 ? req.maxTokens : req.profile.maxTokens;
    if (!req.system.isEmpty()) {
        // O prompt do sistema (fixo entre as voltas) vai marcado para cache.
        body[QStringLiteral("system")] = QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), req.system},
            {QStringLiteral("cache_control"), QJsonObject{{QStringLiteral("type"), QStringLiteral("ephemeral")}}}}};
    }
    body[QStringLiteral("messages")] = messages;
    if (!req.tools.isEmpty()) {
        QJsonArray tools;
        for (const AiToolSpec &t : req.tools) {
            tools.append(QJsonObject{{QStringLiteral("name"), t.name},
                                     {QStringLiteral("description"), t.description},
                                     {QStringLiteral("input_schema"), t.inputSchema}});
        }
        QJsonObject last = tools.last().toObject();
        last[QStringLiteral("cache_control")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("ephemeral")}};
        tools[tools.size() - 1] = last;
        body[QStringLiteral("tools")] = tools;
    }
    if (req.stream) {
        body[QStringLiteral("stream")] = true;
    }
    return body;
}

QString anthropicStop(const QString &reason)
{
    if (reason == QLatin1String("tool_use")) return QStringLiteral("tool_use");
    if (reason == QLatin1String("max_tokens")) return QStringLiteral("max_tokens");
    if (reason == QLatin1String("end_turn") || reason == QLatin1String("stop_sequence")) return QStringLiteral("end_turn");
    return QStringLiteral("other");
}

void anthropicUsage(const QJsonObject &u, core::AiUsage &usage)
{
    if (u.contains(QStringLiteral("input_tokens"))) usage.inputTokens = u.value(QStringLiteral("input_tokens")).toInt();
    if (u.contains(QStringLiteral("output_tokens"))) usage.outputTokens = u.value(QStringLiteral("output_tokens")).toInt();
    if (u.contains(QStringLiteral("cache_read_input_tokens"))) {
        usage.cacheReadTokens = u.value(QStringLiteral("cache_read_input_tokens")).toInt();
    }
    if (u.contains(QStringLiteral("cache_creation_input_tokens"))) {
        usage.cacheWriteTokens = u.value(QStringLiteral("cache_creation_input_tokens")).toInt();
    }
}

bool parseAnthropicResponse(const QByteArray &bytes, AiTurn &turn, AiError &error)
{
    bool ok = false;
    const QJsonObject root = parseObject(bytes, &ok);
    if (!ok) {
        error = {AiError::Kind::Parse, 0, QStringLiteral("invalid JSON response")};
        return false;
    }
    if (root.value(QStringLiteral("type")).toString() == QLatin1String("error")) {
        error = errorFromHttp(500, bytes);
        return false;
    }
    turn = AiTurn();
    for (const QJsonValue &v : root.value(QStringLiteral("content")).toArray()) {
        const QJsonObject block = v.toObject();
        const QString type = block.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            turn.text += block.value(QStringLiteral("text")).toString();
        } else if (type == QLatin1String("tool_use")) {
            turn.toolCalls.append(AiToolCall{block.value(QStringLiteral("id")).toString(),
                                   block.value(QStringLiteral("name")).toString(),
                                   block.value(QStringLiteral("input")).toObject()});
        }
    }
    turn.stopReason = anthropicStop(root.value(QStringLiteral("stop_reason")).toString());
    anthropicUsage(root.value(QStringLiteral("usage")).toObject(), turn.usage);
    return true;
}

// ------------------------------------------------------------- OpenAI-compat

QJsonObject openAiBody(const AiRequest &req)
{
    const QVector<AiMessage> msgs = sanitizeMessages(req.tools.isEmpty() ? flattenTools(req.messages) : req.messages);

    QJsonArray messages;
    if (!req.system.isEmpty()) {
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), req.system}});
    }
    for (const AiMessage &m : msgs) {
        switch (m.role) {
        case AiMessage::Role::User:
            messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), m.text}});
            break;
        case AiMessage::Role::Assistant: {
            QJsonObject o{{QStringLiteral("role"), QStringLiteral("assistant")}};
            o[QStringLiteral("content")] = m.text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m.text);
            if (!m.toolCalls.isEmpty()) {
                QJsonArray calls;
                for (const AiToolCall &c : m.toolCalls) {
                    calls.append(QJsonObject{{QStringLiteral("id"), c.id},
                                             {QStringLiteral("type"), QStringLiteral("function")},
                                             {QStringLiteral("function"),
                                              QJsonObject{{QStringLiteral("name"), c.name},
                                                          {QStringLiteral("arguments"), compactJson(c.arguments)}}}});
                }
                o[QStringLiteral("tool_calls")] = calls;
            }
            messages.append(o);
            break;
        }
        case AiMessage::Role::Tool:
            for (const AiToolResult &r : m.toolResults) {
                messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                                            {QStringLiteral("tool_call_id"), r.toolCallId},
                                            {QStringLiteral("content"), r.content}});
            }
            break;
        }
    }

    QJsonObject body;
    body[QStringLiteral("model")] = req.profile.model;
    body[QStringLiteral("messages")] = messages;
    const int maxTokens = req.maxTokens > 0 ? req.maxTokens : req.profile.maxTokens;
    // A OpenAI nova recusa `max_tokens`; os servidores compatíveis ainda só conhecem esse.
    const bool official = QUrl(req.profile.resolvedBaseUrl()).host().endsWith(QLatin1String("openai.com"));
    body[official ? QStringLiteral("max_completion_tokens") : QStringLiteral("max_tokens")] = maxTokens;
    if (!req.tools.isEmpty()) {
        QJsonArray tools;
        for (const AiToolSpec &t : req.tools) {
            tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                     {QStringLiteral("function"),
                                      QJsonObject{{QStringLiteral("name"), t.name},
                                                  {QStringLiteral("description"), t.description},
                                                  {QStringLiteral("parameters"), t.inputSchema}}}});
        }
        body[QStringLiteral("tools")] = tools;
    }
    if (req.stream) {
        body[QStringLiteral("stream")] = true;
        body[QStringLiteral("stream_options")] = QJsonObject{{QStringLiteral("include_usage"), true}};
    }
    return body;
}

QString openAiStop(const QString &reason)
{
    if (reason == QLatin1String("tool_calls") || reason == QLatin1String("function_call")) return QStringLiteral("tool_use");
    if (reason == QLatin1String("length")) return QStringLiteral("max_tokens");
    if (reason == QLatin1String("stop")) return QStringLiteral("end_turn");
    return QStringLiteral("other");
}

void openAiUsage(const QJsonObject &u, core::AiUsage &usage)
{
    if (u.isEmpty()) {
        return;
    }
    usage.inputTokens = u.value(QStringLiteral("prompt_tokens")).toInt();
    usage.outputTokens = u.value(QStringLiteral("completion_tokens")).toInt();
    usage.cacheReadTokens = u.value(QStringLiteral("prompt_tokens_details")).toObject()
                                .value(QStringLiteral("cached_tokens")).toInt();
}

bool parseOpenAiResponse(const QByteArray &bytes, AiTurn &turn, AiError &error)
{
    bool ok = false;
    const QJsonObject root = parseObject(bytes, &ok);
    if (!ok) {
        error = {AiError::Kind::Parse, 0, QStringLiteral("invalid JSON response")};
        return false;
    }
    if (root.contains(QStringLiteral("error")) && !root.contains(QStringLiteral("choices"))) {
        error = errorFromHttp(500, bytes);
        return false;
    }
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        error = {AiError::Kind::Parse, 0, QStringLiteral("response without choices")};
        return false;
    }
    turn = AiTurn();
    const QJsonObject choice = choices.first().toObject();
    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
    turn.text = message.value(QStringLiteral("content")).toString();
    int n = 0;
    for (const QJsonValue &v : message.value(QStringLiteral("tool_calls")).toArray()) {
        const QJsonObject c = v.toObject();
        const QJsonObject fn = c.value(QStringLiteral("function")).toObject();
        QString id = c.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) id = QStringLiteral("call_%1").arg(n);
        ++n;
        turn.toolCalls.append(AiToolCall{id, fn.value(QStringLiteral("name")).toString(),
                               argumentsFromText(fn.value(QStringLiteral("arguments")).toString())});
    }
    turn.stopReason = openAiStop(choice.value(QStringLiteral("finish_reason")).toString());
    if (!turn.toolCalls.isEmpty()) {
        turn.stopReason = QStringLiteral("tool_use");
    }
    openAiUsage(root.value(QStringLiteral("usage")).toObject(), turn.usage);
    return true;
}

// ---------------------------------------------------------------- Decoders

class AnthropicDecoder : public AiStreamDecoder {
public:
    explicit AnthropicDecoder(Callbacks cb) : AiStreamDecoder(std::move(cb)) {}

protected:
    void handleEvent(const QString &event, const QByteArray &data) override
    {
        bool ok = false;
        const QJsonObject o = parseObject(data, &ok);
        if (!ok) {
            return;
        }
        const QString type = o.value(QStringLiteral("type")).toString(event);
        if (type == QLatin1String("message_start")) {
            anthropicUsage(o.value(QStringLiteral("message")).toObject().value(QStringLiteral("usage")).toObject(),
                           m_turn.usage);
        } else if (type == QLatin1String("content_block_start")) {
            const int index = o.value(QStringLiteral("index")).toInt();
            const QJsonObject block = o.value(QStringLiteral("content_block")).toObject();
            Block b;
            b.type = block.value(QStringLiteral("type")).toString();
            b.id = block.value(QStringLiteral("id")).toString();
            b.name = block.value(QStringLiteral("name")).toString();
            m_blocks.insert(index, b);
            if (b.type == QLatin1String("tool_use")) {
                m_content = true;
                if (m_callbacks.onToolStarted) m_callbacks.onToolStarted(b.name);
            }
        } else if (type == QLatin1String("content_block_delta")) {
            const int index = o.value(QStringLiteral("index")).toInt();
            const QJsonObject delta = o.value(QStringLiteral("delta")).toObject();
            const QString dt = delta.value(QStringLiteral("type")).toString();
            if (dt == QLatin1String("text_delta")) {
                emitText(delta.value(QStringLiteral("text")).toString());
            } else if (dt == QLatin1String("input_json_delta")) {
                m_blocks[index].args += delta.value(QStringLiteral("partial_json")).toString();
            }
        } else if (type == QLatin1String("content_block_stop")) {
            closeBlock(o.value(QStringLiteral("index")).toInt());
        } else if (type == QLatin1String("message_delta")) {
            const QString stop = o.value(QStringLiteral("delta")).toObject().value(QStringLiteral("stop_reason")).toString();
            if (!stop.isEmpty()) {
                m_turn.stopReason = anthropicStop(stop);
            }
            anthropicUsage(o.value(QStringLiteral("usage")).toObject(), m_turn.usage);
        } else if (type == QLatin1String("message_stop")) {
            m_completed = true;
        } else if (type == QLatin1String("error")) {
            const AiError e = errorFromHttp(500, data);
            fail(e.kind, e.message);
        }
    }

    void finalize() override
    {
        const QList<int> open = m_blocks.keys();
        for (int index : open) {
            closeBlock(index);
        }
        if (!m_turn.toolCalls.isEmpty()) {
            m_turn.stopReason = QStringLiteral("tool_use");
        }
    }

private:
    struct Block {
        QString type;
        QString id;
        QString name;
        QString args;
    };

    void closeBlock(int index)
    {
        const auto it = m_blocks.find(index);
        if (it == m_blocks.end()) {
            return;
        }
        if (it->type == QLatin1String("tool_use")) {
            m_turn.toolCalls.append(AiToolCall{it->id, it->name, argumentsFromText(it->args)});
        }
        m_blocks.erase(it);
    }

    QMap<int, Block> m_blocks;
};

class OpenAiDecoder : public AiStreamDecoder {
public:
    explicit OpenAiDecoder(Callbacks cb) : AiStreamDecoder(std::move(cb)) {}

protected:
    void handleEvent(const QString &, const QByteArray &data) override
    {
        if (data.trimmed() == "[DONE]") {
            m_completed = true;
            return;
        }
        bool ok = false;
        const QJsonObject o = parseObject(data, &ok);
        if (!ok) {
            return;
        }
        if (o.contains(QStringLiteral("error")) && !o.contains(QStringLiteral("choices"))) {
            const AiError e = errorFromHttp(500, data);
            fail(e.kind, e.message);
            return;
        }
        openAiUsage(o.value(QStringLiteral("usage")).toObject(), m_turn.usage);
        const QJsonArray choices = o.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            return;
        }
        const QJsonObject choice = choices.first().toObject();
        const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
        const QString text = delta.value(QStringLiteral("content")).toString();
        if (!text.isEmpty()) {
            emitText(text);
        }
        for (const QJsonValue &v : delta.value(QStringLiteral("tool_calls")).toArray()) {
            const QJsonObject c = v.toObject();
            const int index = c.value(QStringLiteral("index")).toInt(m_calls.size());
            Call &call = m_calls[index];
            const QJsonObject fn = c.value(QStringLiteral("function")).toObject();
            if (call.id.isEmpty()) call.id = c.value(QStringLiteral("id")).toString();
            const QString name = fn.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) {
                call.name += name;
                if (!call.announced) {
                    call.announced = true;
                    m_content = true;
                    if (m_callbacks.onToolStarted) m_callbacks.onToolStarted(call.name);
                }
            }
            call.args += fn.value(QStringLiteral("arguments")).toString();
        }
        const QString finish = choice.value(QStringLiteral("finish_reason")).toString();
        if (!finish.isEmpty()) {
            m_turn.stopReason = openAiStop(finish);
        }
    }

    void finalize() override
    {
        for (auto it = m_calls.cbegin(); it != m_calls.cend(); ++it) {
            QString id = it->id;
            if (id.isEmpty()) id = QStringLiteral("call_%1").arg(it.key());
            m_turn.toolCalls.append(AiToolCall{id, it->name, argumentsFromText(it->args)});
        }
        m_calls.clear();
        if (!m_turn.toolCalls.isEmpty()) {
            m_turn.stopReason = QStringLiteral("tool_use");
        }
    }

private:
    struct Call {
        QString id;
        QString name;
        QString args;
        bool announced = false;
    };
    QMap<int, Call> m_calls;
};

} // namespace

bool AiError::retryable() const
{
    return kind == Kind::RateLimit || kind == Kind::Overloaded || kind == Kind::Server || kind == Kind::Network
        || kind == Kind::Timeout;
}

QJsonObject buildRequestBody(const AiRequest &request)
{
    return request.profile.wireFormat == AiWireFormat::Anthropic ? anthropicBody(request) : openAiBody(request);
}

QList<QPair<QByteArray, QByteArray>> buildRequestHeaders(const core::AiProfile &profile, bool stream)
{
    using Header = QPair<QByteArray, QByteArray>;
    QList<Header> h;
    h.append(Header("Content-Type", "application/json"));
    h.append(Header("Accept", stream ? "text/event-stream" : "application/json"));
    const QByteArray key = profile.resolvedKey().toUtf8();
    if (profile.wireFormat == AiWireFormat::Anthropic) {
        h.append(Header("anthropic-version", "2023-06-01"));
        if (!key.isEmpty()) h.append(Header("x-api-key", key));
    } else if (!key.isEmpty()) {
        h.append(Header("Authorization", "Bearer " + key));
    }
    return h;
}

QList<QPair<QByteArray, QByteArray>> buildModelsHeaders(const core::AiProfile &profile)
{
    QList<QPair<QByteArray, QByteArray>> h = buildRequestHeaders(profile, false);
    h.removeIf([](const QPair<QByteArray, QByteArray> &p) { return p.first == "Content-Type"; });
    return h;
}

QVector<AiMessage> sanitizeMessages(const QVector<AiMessage> &messages)
{
    QVector<AiMessage> out;
    for (int i = 0; i < messages.size(); ++i) {
        const AiMessage &m = messages.at(i);
        if (m.role == AiMessage::Role::Assistant && !m.toolCalls.isEmpty()) {
            // Resultados que vieram logo depois, por id.
            QMap<QString, AiToolResult> answered;
            int j = i + 1;
            for (; j < messages.size() && messages.at(j).role == AiMessage::Role::Tool; ++j) {
                for (const AiToolResult &r : messages.at(j).toolResults) {
                    answered.insert(r.toolCallId, r);
                }
            }
            out.append(m);
            AiMessage tool;
            tool.role = AiMessage::Role::Tool;
            for (const AiToolCall &c : m.toolCalls) {
                AiToolResult r = answered.value(c.id);
                if (r.toolCallId.isEmpty()) {
                    r.toolCallId = c.id;
                    r.name = c.name;
                    r.content = QStringLiteral("Not executed (the conversation was interrupted).");
                    r.isError = true;
                }
                tool.toolResults.append(r);
            }
            out.append(tool);
            i = j - 1;
            continue;
        }
        if (m.role == AiMessage::Role::Tool) {
            continue; // resultado sem a chamada correspondente
        }
        out.append(m);
    }
    return out;
}

bool parseResponse(AiWireFormat format, const QByteArray &body, AiTurn &turn, AiError &error)
{
    return format == AiWireFormat::Anthropic ? parseAnthropicResponse(body, turn, error)
                                             : parseOpenAiResponse(body, turn, error);
}

AiError errorFromHttp(int status, const QByteArray &body)
{
    AiError e;
    e.httpStatus = status;
    bool ok = false;
    const QJsonObject root = parseObject(body, &ok);
    QString type;
    if (ok) {
        const QJsonValue err = root.value(QStringLiteral("error"));
        if (err.isObject()) {
            e.message = err.toObject().value(QStringLiteral("message")).toString();
            type = err.toObject().value(QStringLiteral("type")).toString();
        } else if (err.isString()) {
            e.message = err.toString();
        } else {
            e.message = root.value(QStringLiteral("message")).toString();
        }
    }
    if (e.message.isEmpty()) {
        e.message = QString::fromUtf8(body.left(300)).simplified();
    }
    if (type == QLatin1String("overloaded_error") || status == 529) {
        e.kind = AiError::Kind::Overloaded;
    } else if (status == 401 || status == 403) {
        e.kind = AiError::Kind::Auth;
    } else if (status == 429) {
        e.kind = AiError::Kind::RateLimit;
    } else if (status >= 500) {
        e.kind = AiError::Kind::Server;
    } else if (status >= 400) {
        e.kind = AiError::Kind::BadRequest;
    } else {
        e.kind = type == QLatin1String("authentication_error") ? AiError::Kind::Auth : AiError::Kind::Server;
    }
    return e;
}

QStringList parseModelList(const QByteArray &body)
{
    bool ok = false;
    const QJsonObject root = parseObject(body, &ok);
    QStringList ids;
    if (!ok) {
        return ids;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("data")).toArray()) {
        const QString id = v.toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) ids << id;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("models")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString(o.value(QStringLiteral("name")).toString());
        if (!id.isEmpty()) ids << id;
    }
    ids.removeDuplicates();
    ids.sort(Qt::CaseInsensitive);
    return ids;
}

AiStreamDecoder::AiStreamDecoder(Callbacks callbacks)
    : m_callbacks(std::move(callbacks))
    , m_parser([this](const QString &event, const QByteArray &data) {
        if (!m_failed) handleEvent(event, data);
    })
{
}

void AiStreamDecoder::feed(const QByteArray &chunk)
{
    m_parser.feed(chunk);
}

void AiStreamDecoder::finish()
{
    m_parser.finish();
    finalize();
}

void AiStreamDecoder::emitText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    m_content = true;
    m_turn.text += text;
    if (m_callbacks.onText) {
        m_callbacks.onText(text);
    }
}

void AiStreamDecoder::fail(AiError::Kind kind, const QString &message)
{
    m_failed = true;
    m_error = {kind, 0, message};
}

std::unique_ptr<AiStreamDecoder> AiStreamDecoder::create(AiWireFormat format, Callbacks callbacks)
{
    if (format == AiWireFormat::Anthropic) {
        return std::make_unique<AnthropicDecoder>(std::move(callbacks));
    }
    return std::make_unique<OpenAiDecoder>(std::move(callbacks));
}

} // namespace kai::engine
