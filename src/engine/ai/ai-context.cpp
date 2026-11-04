#include "engine/ai/ai-context.h"

#include <QJsonDocument>
#include <algorithm>

namespace kai::engine {

using core::AiMessage;

int estimateTokens(int chars)
{
    return (chars * 2 + 6) / 7;
}

int estimateMessageChars(const AiMessage &m)
{
    int chars = m.text.size() + 8;
    for (const core::AiToolCall &c : m.toolCalls) {
        chars += c.name.size() + QJsonDocument(c.arguments).toJson(QJsonDocument::Compact).size() + 16;
    }
    for (const core::AiToolResult &r : m.toolResults) {
        chars += r.content.size() + 16;
    }
    return chars;
}

QVector<AiMessage> fitToContext(const QVector<AiMessage> &messages, int fixedChars, int contextTokens,
                                int reservedOutputTokens)
{
    if (contextTokens <= 0) {
        return messages;
    }
    const int budgetChars = std::max(2000, int((contextTokens - reservedOutputTokens) * 3.5 * 0.9) - fixedChars);
    auto total = [](const QVector<AiMessage> &v) {
        int sum = 0;
        for (const AiMessage &m : v) sum += estimateMessageChars(m);
        return sum;
    };

    QVector<AiMessage> out = messages;
    if (total(out) <= budgetChars) {
        return out;
    }

    // 1) resultados de ferramenta antigos viram um marcador (os 6 mais recentes ficam inteiros).
    const int keepFrom = std::max(0, int(out.size()) - 6);
    for (int i = 0; i < keepFrom && total(out) > budgetChars; ++i) {
        for (core::AiToolResult &r : out[i].toolResults) {
            if (r.content.size() > 120) {
                r.content = QStringLiteral("[older tool result omitted to save context]");
            }
        }
    }
    if (total(out) <= budgetChars) {
        return out;
    }

    // 2) descarta as mais antigas até caber, deixando sempre o início num pedido do usuário.
    while (out.size() > 1 && total(out) > budgetChars) {
        out.removeFirst();
        while (out.size() > 1 && out.first().role != AiMessage::Role::User) {
            out.removeFirst();
        }
    }
    return out;
}

} // namespace kai::engine
