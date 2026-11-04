#pragma once

#include <QJsonObject>
#include <QString>

#include "core/models.h"

namespace kai::engine {

class ManifestoLibrary;

struct AiPromptContext {
    QString uiLanguage;        // "pt", "en"...
    QString today;             // yyyy-MM-dd
    QString activeEnvironment; // nome, se houver
    bool toolsEnabled = true;
    bool secretsHidden = false;
    // Instruções extras do usuário (anexadas depois das regras fixas, que não saem).
    QString extraInstructions;
};

// Prompt do sistema do chat/agente. Compacto de propósito (cerca de 1k tokens): o detalhe do formato
// vem sob demanda por `read_manifesto`.
QString buildAgentSystemPrompt(const AiPromptContext &context);

// Prompt do sistema do assistente do editor (sem ferramentas: responde um JSON). Leva o trecho do
// manifesto que descreve o modelo do comando (e o do HTTP, se for o caso).
QString buildAssistSystemPrompt(const AiPromptContext &context, bool http, ManifestoLibrary &manifesto);

// Mensagem do usuário do assistente: o pedido e o comando atual (JSON).
QString buildAssistUserPrompt(const QString &request, const QJsonObject &currentCommand);

struct AssistReply {
    bool ok = false;
    QString explanation;
    QJsonObject command;
    QString error;
};

// Separa a explicação do objeto JSON do comando que o modelo devolveu (com ou sem cerca ```json).
AssistReply parseAssistReply(const QString &text);

} // namespace kai::engine
