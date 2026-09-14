#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

#include "core/ai-conversation.h"
#include "core/ai-profile.h"

namespace kai::engine {

// Uma ferramenta oferecida ao modelo (JSON Schema dos argumentos).
struct AiToolSpec {
    QString name;
    QString description;
    QJsonObject inputSchema;
};

struct AiRequest {
    core::AiProfile profile;
    QString system;
    QVector<core::AiMessage> messages;
    QVector<AiToolSpec> tools;
    bool stream = true;
    // 0 = o limite do perfil.
    int maxTokens = 0;
};

// O que o modelo respondeu numa chamada: texto e/ou chamadas de ferramenta.
struct AiTurn {
    QString text;
    QVector<core::AiToolCall> toolCalls;
    core::AiUsage usage;
    // end_turn | tool_use | max_tokens | other
    QString stopReason = QStringLiteral("end_turn");

    bool wantsTools() const { return !toolCalls.isEmpty(); }
};

struct AiError {
    enum class Kind { Network, Auth, RateLimit, Overloaded, BadRequest, Timeout, Cancelled, Server, Parse };

    Kind kind = Kind::Network;
    int httpStatus = 0;
    // Texto do provedor/Qt, já sem cabeçalhos nem chaves.
    QString message;

    bool retryable() const;
};

} // namespace kai::engine

Q_DECLARE_METATYPE(kai::engine::AiTurn)
Q_DECLARE_METATYPE(kai::engine::AiError)
