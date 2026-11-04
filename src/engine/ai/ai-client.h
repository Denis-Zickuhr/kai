#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <memory>

#include "engine/ai/ai-types.h"
#include "engine/ai/ai-wire.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace kai::engine {

// Cliente de UMA requisição de chat por vez, nos dois formatos de fio. Assíncrono (nunca bloqueia a GUI),
// com streaming SSE, cancelamento, tempo limite do primeiro token e nova tentativa com espera crescente
// (429/5xx/overloaded/rede) enquanto nada foi entregue. Não registra cabeçalhos nem corpos: a chave de
// API passa por aqui e não pode vazar em log.
class AiClient : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxAttempts = 3;

    explicit AiClient(QObject *parent = nullptr);
    ~AiClient() override;

    bool isBusy() const { return m_busy; }
    void send(const AiRequest &request);
    // Interrompe a requisição em curso; `failed` sai com Kind::Cancelled.
    void cancel();
    // Lista os modelos do provedor (`/models`).
    void listModels(const core::AiProfile &profile);
    // Para os testes: a espera-base entre tentativas (padrão 1000 ms).
    void setRetryBaseDelayMs(int ms) { m_retryBaseMs = ms; }

signals:
    void textDelta(const QString &text);
    void toolCallStarted(const QString &toolName);
    void retrying(int attempt, int delayMs, const kai::engine::AiError &error);
    void completed(const kai::engine::AiTurn &turn);
    void failed(const kai::engine::AiError &error);
    void modelsListed(const QStringList &models);
    void modelsFailed(const kai::engine::AiError &error);

private:
    void startAttempt();
    void handleReadyRead();
    void handleFinished();
    void finishWithError(const AiError &error, int retryAfterSec = 0);
    void resetReply();

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer *m_firstTokenTimer = nullptr;
    AiRequest m_request;
    std::unique_ptr<AiStreamDecoder> m_decoder;
    QByteArray m_body;
    QByteArray m_errorBody;
    bool m_busy = false;
    bool m_gotData = false;
    bool m_cancelled = false;
    bool m_timedOut = false;
    int m_attempt = 0;
    int m_retryBaseMs = 1000;

    QPointer<QNetworkReply> m_modelsReply;
};

} // namespace kai::engine
