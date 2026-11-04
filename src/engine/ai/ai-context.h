#pragma once

#include <QVector>

#include "core/ai-conversation.h"

namespace kai::engine {

// Estimativa grosseira de tokens (texto misto e JSON: ~3,5 caracteres por token).
int estimateTokens(int chars);
int estimateMessageChars(const core::AiMessage &message);

// Ajusta o histórico ao contexto do modelo (`contextTokens` = 0 não corta nada). Primeiro enxuga o
// conteúdo de resultados de ferramenta antigos; se ainda não couber, descarta as mensagens mais antigas
// (sempre começando num pedido do usuário, sem resultado órfão). A última mensagem do usuário nunca sai.
QVector<core::AiMessage> fitToContext(const QVector<core::AiMessage> &messages, int fixedChars, int contextTokens,
                                      int reservedOutputTokens);

} // namespace kai::engine
