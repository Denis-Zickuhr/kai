#include "engine/ai/ai-client.h"

#include "engine/http-runner.h"
#include "utils/logger.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace kai::engine {

namespace {
constexpr const char *kLogTag = "AiClient";

int statusOf(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

QNetworkRequest makeRequest(const QString &url, const QList<QPair<QByteArray, QByteArray>> &headers, int transferTimeoutMs)
{
    QNetworkRequest req{QUrl(url)};
    for (const auto &h : headers) {
        req.setRawHeader(h.first, h.second);
    }
    req.setTransferTimeout(transferTimeoutMs);
    return req;
}
} // namespace

AiClient::AiClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_firstTokenTimer(new QTimer(this))
{
    qRegisterMetaType<kai::engine::AiTurn>();
    qRegisterMetaType<kai::engine::AiError>();
    m_firstTokenTimer->setSingleShot(true);
    connect(m_firstTokenTimer, &QTimer::timeout, this, [this]() {
        if (m_reply && !m_gotData) {
            m_timedOut = true;
            m_reply->abort();
        }
    });
}

AiClient::~AiClient()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

void AiClient::send(const AiRequest &request)
{
    if (m_busy) {
        cancel();
    }
    m_request = request;
    m_busy = true;
    m_cancelled = false;
    m_attempt = 0;
    startAttempt();
}

void AiClient::startAttempt()
{
    ++m_attempt;
    m_body.clear();
    m_errorBody.clear();
    m_gotData = false;
    m_timedOut = false;

    AiStreamDecoder::Callbacks cb;
    cb.onText = [this](const QString &t) { emit textDelta(t); };
    cb.onToolStarted = [this](const QString &n) { emit toolCallStarted(n); };
    m_decoder = m_request.stream ? AiStreamDecoder::create(m_request.profile.wireFormat, cb) : nullptr;

    const int firstTokenSec = m_request.stream ? m_request.profile.firstTokenTimeoutSec
                                               : std::max(m_request.profile.firstTokenTimeoutSec, 180);
    const QByteArray payload = QJsonDocument(buildRequestBody(m_request)).toJson(QJsonDocument::Compact);
    QNetworkRequest req = makeRequest(m_request.profile.chatUrl(),
                                      buildRequestHeaders(m_request.profile, m_request.stream),
                                      std::max(firstTokenSec, 120) * 1000);
    // Atrás de um proxy de empresa: as variáveis HTTPS_PROXY/NO_PROXY valem como nas requisições HTTP do Kai.
    m_nam->setProxy(resolveHttpProxy(QStringLiteral("system"), req.url()));
    m_reply = m_nam->post(req, payload);
    connect(m_reply, &QNetworkReply::readyRead, this, &AiClient::handleReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &AiClient::handleFinished);
    m_firstTokenTimer->start(firstTokenSec * 1000);
}

void AiClient::handleReadyRead()
{
    if (!m_reply) {
        return;
    }
    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty()) {
        return;
    }
    const int status = statusOf(m_reply);
    if (status >= 400) {
        m_errorBody.append(chunk);
        return;
    }
    m_gotData = true;
    m_firstTokenTimer->stop();
    if (m_decoder) {
        m_decoder->feed(chunk);
    } else {
        m_body.append(chunk);
    }
}

void AiClient::handleFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply) {
        return;
    }
    m_firstTokenTimer->stop();
    // O que sobrou no buffer.
    handleReadyRead();
    const int status = statusOf(reply);
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netMessage = reply->errorString();
    const int retryAfter = reply->rawHeader("Retry-After").toInt();
    reply->deleteLater();
    m_reply.clear();

    if (m_cancelled) {
        m_busy = false;
        emit failed({AiError::Kind::Cancelled, 0, QString()});
        return;
    }
    if (m_timedOut) {
        finishWithError({AiError::Kind::Timeout, 0, QStringLiteral("timeout waiting for the first token")});
        return;
    }
    if (status >= 400) {
        finishWithError(errorFromHttp(status, m_errorBody), retryAfter);
        return;
    }
    if (netError != QNetworkReply::NoError && status == 0) {
        finishWithError({AiError::Kind::Network, 0, netMessage});
        return;
    }

    if (m_decoder) {
        m_decoder->finish();
        if (m_decoder->hasError()) {
            const AiError e = m_decoder->error();
            finishWithError(e);
            return;
        }
        if (!m_decoder->completed() && !m_decoder->receivedContent() && m_decoder->turn().toolCalls.isEmpty()) {
            finishWithError({AiError::Kind::Server, status, QStringLiteral("empty response from the provider")});
            return;
        }
        // Fluxo cortado no meio: o que chegou vale, mas a parada não foi limpa.
        AiTurn turn = m_decoder->turn();
        m_busy = false;
        emit completed(turn);
        return;
    }

    AiTurn turn;
    AiError error;
    if (!parseResponse(m_request.profile.wireFormat, m_body, turn, error)) {
        finishWithError(error);
        return;
    }
    m_busy = false;
    emit completed(turn);
}

void AiClient::finishWithError(const AiError &error, int retryAfterSec)
{
    const bool anyContent = m_decoder && (m_decoder->receivedContent() || !m_decoder->turn().toolCalls.isEmpty());
    if (error.retryable() && !anyContent && m_attempt < kMaxAttempts) {
        int delay = m_retryBaseMs * (1 << (m_attempt - 1));
        if (retryAfterSec > 0) {
            delay = std::max(delay, std::min(retryAfterSec, 15) * 1000);
        }
        delay = std::min(delay, 15000);
        utils::Logger::info(kLogTag, QStringLiteral("Tentativa %1 falhou (HTTP %2); nova tentativa em %3 ms.")
                                          .arg(m_attempt).arg(error.httpStatus).arg(delay));
        emit retrying(m_attempt, delay, error);
        QTimer::singleShot(delay, this, [this]() {
            if (m_busy && !m_cancelled) {
                startAttempt();
            }
        });
        return;
    }
    utils::Logger::warning(kLogTag, QStringLiteral("Requisição de IA falhou (HTTP %1).").arg(error.httpStatus));
    m_busy = false;
    emit failed(error);
}

void AiClient::cancel()
{
    if (!m_busy) {
        return;
    }
    m_cancelled = true;
    m_firstTokenTimer->stop();
    if (m_reply) {
        m_reply->abort(); // handleFinished emite `failed(Cancelled)`
    } else {
        // Entre tentativas (esperando o retry).
        m_busy = false;
        emit failed({AiError::Kind::Cancelled, 0, QString()});
    }
}

void AiClient::listModels(const core::AiProfile &profile)
{
    if (m_modelsReply) {
        m_modelsReply->disconnect(this);
        m_modelsReply->abort();
    }
    QNetworkRequest req = makeRequest(profile.modelsUrl(), buildModelsHeaders(profile), 30000);
    m_nam->setProxy(resolveHttpProxy(QStringLiteral("system"), req.url()));
    m_modelsReply = m_nam->get(req);
    QNetworkReply *reply = m_modelsReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = statusOf(reply);
        const QByteArray body = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        const QString netMessage = reply->errorString();
        reply->deleteLater();
        if (status >= 400) {
            emit modelsFailed(errorFromHttp(status, body));
        } else if (netError != QNetworkReply::NoError && status == 0) {
            emit modelsFailed({AiError::Kind::Network, 0, netMessage});
        } else {
            emit modelsListed(parseModelList(body));
        }
    });
}

} // namespace kai::engine
