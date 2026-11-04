#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QStringList>
#include <functional>
#include <memory>

#include "engine/ai/ai-sse-parser.h"
#include "engine/ai/ai-types.h"

namespace kai::engine {

// Tradução PURA (sem rede) entre o modelo neutro do Kai e os dois formatos de fio. Tudo o que muda de
// um provedor para o outro mora aqui: corpo, cabeçalhos, decodificação do fluxo e das respostas.

// Corpo JSON da requisição de chat.
QJsonObject buildRequestBody(const AiRequest &request);
// Cabeçalhos (inclui a chave; NUNCA logar).
QList<QPair<QByteArray, QByteArray>> buildRequestHeaders(const core::AiProfile &profile, bool stream);
QList<QPair<QByteArray, QByteArray>> buildModelsHeaders(const core::AiProfile &profile);

// Garante o pareamento chamada/resultado que os provedores exigem: chamada sem resposta ganha um
// resultado "não executado" e resultado sem chamada é descartado. Também junta mensagens seguidas de
// mesmo papel quando o formato pede alternância.
QVector<core::AiMessage> sanitizeMessages(const QVector<core::AiMessage> &messages);

// Resposta completa (sem streaming). Em erro de formato devolve false e preenche `error`.
bool parseResponse(core::AiWireFormat format, const QByteArray &body, AiTurn &turn, AiError &error);
// Erro HTTP: classifica o status e extrai a mensagem do corpo.
AiError errorFromHttp(int status, const QByteArray &body);
// Lista de modelos (`/models`) -> ids ordenados.
QStringList parseModelList(const QByteArray &body);

// Decodificador do fluxo SSE. Acumula o AiTurn e avisa o texto/as ferramentas conforme chegam.
class AiStreamDecoder {
public:
    struct Callbacks {
        std::function<void(const QString &text)> onText;
        std::function<void(const QString &toolName)> onToolStarted;
    };

    virtual ~AiStreamDecoder() = default;

    void feed(const QByteArray &chunk);
    // Fim do fluxo da rede: fecha o que ficou aberto.
    void finish();

    const AiTurn &turn() const { return m_turn; }
    bool hasError() const { return m_failed; }
    AiError error() const { return m_error; }
    // Chegou algum conteúdo (texto ou ferramenta)? Decide se ainda dá para repetir a requisição.
    bool receivedContent() const { return m_content; }
    // O provedor sinalizou o fim da mensagem?
    bool completed() const { return m_completed; }

    static std::unique_ptr<AiStreamDecoder> create(core::AiWireFormat format, Callbacks callbacks);

protected:
    explicit AiStreamDecoder(Callbacks callbacks);
    virtual void handleEvent(const QString &event, const QByteArray &data) = 0;
    virtual void finalize() = 0;

    void emitText(const QString &text);
    void fail(AiError::Kind kind, const QString &message);

    Callbacks m_callbacks;
    AiTurn m_turn;
    AiError m_error;
    bool m_failed = false;
    bool m_content = false;
    bool m_completed = false;

private:
    AiSseParser m_parser;
};

} // namespace kai::engine
