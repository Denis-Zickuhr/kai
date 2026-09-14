#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace kai::core {

// Formato de fio do provedor. Dois cobrem tudo: Anthropic (Messages API) e OpenAI-compatível
// (Chat Completions: OpenAI, Ollama, LM Studio, llama.cpp, OpenRouter, Azure...). "Local" é só um
// endpoint OpenAI-compatível dado por URL.
enum class AiWireFormat { Anthropic, OpenAiCompatible };

QString aiWireFormatToString(AiWireFormat format);
AiWireFormat aiWireFormatFromString(const QString &value);

// Um perfil de provedor/modelo. A chave de API é SEGREDO: vive só no ai.json (modo 0600), nunca em
// settings.json nem em qualquer exportação/sincronização, e nunca entra em prompt, log ou histórico.
struct AiProfile {
    QString id;
    QString name;
    AiWireFormat wireFormat = AiWireFormat::Anthropic;
    // Vazio = o endereço padrão do formato (api.anthropic.com / api.openai.com/v1).
    QString baseUrl;
    QString model;
    QString apiKey;
    // Alternativa à chave guardada: nome de uma variável de ambiente do processo do Kai.
    QString apiKeyEnv;
    // Modelo local sem tool calling: o chat vira só conversa e o editor continua gerando o JSON.
    bool supportsTools = true;
    int maxTokens = 4096;
    // Dica do tamanho de contexto (tokens); 0 = desconhecido (sem corte do histórico).
    int contextTokens = 0;
    int firstTokenTimeoutSec = 60;
    // Esconde dos prompts o valor das variáveis marcadas como secretas.
    bool hideSecretVariables = false;

    static constexpr int kMaxTokensLimit = 200000;

    // Chave efetiva: a guardada, ou a da variável de ambiente.
    QString resolvedKey() const;
    QString resolvedBaseUrl() const;
    // Endereço do endpoint de chat e de listagem de modelos.
    QString chatUrl() const;
    QString modelsUrl() const;
    // O endpoint está na própria máquina/rede local? (selo "local" x "remoto" no chat)
    bool isLocalEndpoint() const;
    // Tem o necessário para enviar uma requisição: modelo e, se o provedor exige, a chave.
    bool isUsable() const;
    bool needsKey() const;

    // `includeKey` só é verdadeiro ao gravar o ai.json: qualquer outro destino fica sem a chave.
    QJsonObject toJson(bool includeKey) const;
    static AiProfile fromJson(const QJsonObject &obj);
    AiProfile clamped() const;
};

struct AiSettings {
    QVector<AiProfile> profiles;
    QString defaultProfileId;
    // Limite de passos (chamadas ao modelo) por mensagem do usuário.
    int maxAgentSteps = 12;
    // Instruções extras do usuário, anexadas ao prompt do sistema (que nunca pode ser removido).
    QString extraInstructions;

    // PONTE MCP (fase 4): deixa um agente externo (Claude Desktop, um IDE...) usar as ferramentas do Kai
    // pelo `kai mcp`. Desligada por padrão; o token autentica quem conecta e fica só no ai.json (0600).
    // Os segredos ficam escondidos do agente externo, salvo escolha em contrário.
    bool mcpEnabled = false;
    QString mcpToken;
    bool mcpHideSecrets = true;

    static constexpr int kMaxAgentStepsLimit = 40;

    // Há ao menos um perfil utilizável? Só então os botões de IA aparecem.
    bool isConfigured() const;
    const AiProfile *profileById(const QString &id) const;
    // O perfil padrão (ou o primeiro utilizável).
    const AiProfile *defaultProfile() const;

    // Token novo (32 bytes aleatórios, base64url) para a ponte MCP.
    static QString newMcpToken();

    QJsonObject toJson(bool includeKeys) const;
    static AiSettings fromJson(const QJsonObject &obj);
};

} // namespace kai::core
