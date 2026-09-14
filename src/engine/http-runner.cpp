#include "engine/http-runner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/http-materialize.h"
#include "engine/http-cookie-store.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

namespace kai::engine {

namespace {
constexpr const char *kLogTag = "HttpRunner";

// Formata o corpo de uma resposta HTTP para exibição no terminal. Se for
// JSON válido, faz pretty-print (indentado). Caso contrário (ex: um CURL
// que retorna texto + JSON, HTML, ou vazio), tenta extrair o primeiro
// bloco JSON ({...} ou [...]); se não houver, devolve o texto cru
// truncado. Feedback do usuário: "o corpo vem com mais que um JSON".
QString prettyXml(const QByteArray &body)
{
    QXmlStreamReader reader(body);
    QString out;
    QXmlStreamWriter writer(&out);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(2);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.error() != QXmlStreamReader::NoError) {
            return QString();
        }
        if (reader.isWhitespace()) {
            continue;
        }
        writer.writeCurrentToken(reader);
    }
    return reader.hasError() ? QString() : out;
}

QString formatResponseBody(const QByteArray &body, const QString &contentType = QString())
{
    if (body.trimmed().isEmpty()) {
        return QString();
    }
    // XML: indentado quando bem formado; qualquer problema cai no texto cru.
    if (contentType.contains(QStringLiteral("xml"), Qt::CaseInsensitive) || body.trimmed().startsWith("<?xml")) {
        const QString pretty = prettyXml(body);
        if (!pretty.trimmed().isEmpty()) {
            return pretty;
        }
    }
    // 1) Tenta o corpo inteiro como JSON.
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error == QJsonParseError::NoError && !doc.isNull()) {
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
    // 2) Tenta extrair o primeiro bloco JSON de um texto misto.
    const QString text = QString::fromUtf8(body);
    const int brace = text.indexOf(QLatin1Char('{'));
    const int bracket = text.indexOf(QLatin1Char('['));
    int start = -1;
    if (brace >= 0 && bracket >= 0) start = qMin(brace, bracket);
    else start = qMax(brace, bracket);
    if (start >= 0) {
        const QByteArray candidate = text.mid(start).toUtf8();
        QJsonParseError err2;
        const QJsonDocument doc2 = QJsonDocument::fromJson(candidate, &err2);
        if (err2.error == QJsonParseError::NoError && !doc2.isNull()) {
            // Preserva o prefixo não-JSON (ex: headers do curl) + JSON formatado.
            const QString prefix = text.left(start).trimmed();
            const QString pretty = QString::fromUtf8(doc2.toJson(QJsonDocument::Indented));
            return prefix.isEmpty() ? pretty : (prefix + QStringLiteral("\n") + pretty);
        }
    }
    // 3) Não é JSON: texto cru (truncado para não inundar o terminal).
    return text.left(4000);
}
}

bool HttpResult::isBinary() const
{
    const QString type = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (type.startsWith(QStringLiteral("text/")) || type.contains(QStringLiteral("json"))
        || type.contains(QStringLiteral("xml")) || type.contains(QStringLiteral("javascript"))
        || type.contains(QStringLiteral("x-www-form-urlencoded")) || type.contains(QStringLiteral("yaml"))) {
        return false;
    }
    if (type.startsWith(QStringLiteral("image/")) || type.startsWith(QStringLiteral("audio/"))
        || type.startsWith(QStringLiteral("video/")) || type.startsWith(QStringLiteral("font/"))
        || type.contains(QStringLiteral("octet-stream")) || type.contains(QStringLiteral("pdf"))
        || type.contains(QStringLiteral("zip")) || type.contains(QStringLiteral("gzip"))) {
        return true;
    }
    return body.left(8000).contains('\0');
}

namespace {

bool materializedHeaderIsMissing(const QList<QPair<QString, QString>> &headers, const QString &name)
{
    for (const auto &h : headers) {
        if (h.first.compare(name, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }
    return true;
}

QString humanSize(qint64 bytes)
{
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

QNetworkProxy proxyFromText(const QString &text)
{
    QString value = text.trimmed();
    if (!value.contains(QStringLiteral("://"))) {
        value.prepend(QStringLiteral("http://"));
    }
    const QUrl url(value);
    if (!url.isValid() || url.host().isEmpty()) {
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }
    const bool socks = url.scheme().toLower().startsWith(QStringLiteral("socks"));
    return QNetworkProxy(socks ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy, url.host(),
                         static_cast<quint16>(url.port(socks ? 1080 : 8080)), url.userName(), url.password());
}

bool matchesNoProxy(const QString &noProxy, const QString &host)
{
    for (QString rule : noProxy.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        rule = rule.trimmed().toLower();
        if (rule.startsWith(QLatin1Char('.'))) {
            rule.remove(0, 1);
        }
        if (rule == QStringLiteral("*") || (!rule.isEmpty() && (host == rule || host.endsWith(QLatin1Char('.') + rule)))) {
            return true;
        }
    }
    return false;
}

// "system" honra as variáveis de ambiente usuais (HTTPS_PROXY/HTTP_PROXY/ALL_PROXY e NO_PROXY). Não consulta
// o proxy do sistema operacional: isso pode bloquear a thread da interface (PAC/WPAD).
QNetworkProxy resolveProxy(const QString &setting, const QUrl &url)
{
    const QString mode = setting.trimmed().toLower();
    if (mode == QStringLiteral("none")) {
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }
    if (!mode.isEmpty() && mode != QStringLiteral("system")) {
        return proxyFromText(setting);
    }
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    auto fromEnv = [&env](const QStringList &names) {
        for (const QString &name : names) {
            for (const QString &candidate : {name, name.toLower()}) {
                const QString value = env.value(candidate).trimmed();
                if (!value.isEmpty()) {
                    return value;
                }
            }
        }
        return QString();
    };
    if (matchesNoProxy(fromEnv({QStringLiteral("NO_PROXY")}), url.host().toLower())) {
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }
    const bool https = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
    const QString value = https
        ? fromEnv({QStringLiteral("HTTPS_PROXY"), QStringLiteral("ALL_PROXY"), QStringLiteral("HTTP_PROXY")})
        : fromEnv({QStringLiteral("HTTP_PROXY"), QStringLiteral("ALL_PROXY")});
    return value.isEmpty() ? QNetworkProxy(QNetworkProxy::NoProxy) : proxyFromText(value);
}

QString envHeaderVar(const QString &headerName)
{
    QString out = QStringLiteral("RESPONSE_HEADER_");
    for (const QChar c : headerName) {
        out += c.isLetterOrNumber() ? c.toUpper() : QLatin1Char('_');
    }
    return out;
}

} // namespace

HttpRunner::HttpRunner(QObject *parent)
    : QObject(parent)
    , m_networkManager(std::make_unique<QNetworkAccessManager>(this))
{
}

HttpRunner::~HttpRunner() = default;

void HttpRunner::setTimeoutMs(int ms)
{
    m_timeoutMs = ms;
}

void HttpRunner::execute(const core::HttpConfig &config, core::EnvironmentManager &envManager)
{
    execute(core::resolveHttpRequest(config, {}), envManager);
}

void HttpRunner::failEarly(const core::HttpRequestSpec &spec, const QString &message)
{
    HttpResult result;
    result.success = false;
    result.errorMessage = message;
    result.requestMethod = spec.method;
    result.requestUrl = m_pendingRequestUrl;
    result.requestHeaders = m_pendingRequestHeaders;
    utils::Logger::error(kLogTag, message);
    emit logMessage(message, true);
    // Adiado: quem chamou ainda está montando a conexão com o sinal finished.
    QTimer::singleShot(0, this, [this, result]() { emit finished(result); });
}

void HttpRunner::execute(const core::HttpRequestSpec &spec, core::EnvironmentManager &envManager)
{
    m_canceled = false;
    m_pendingTimeoutMs = 0;
    m_streaming = false;
    m_streamed.clear();
    m_reply.clear();
    m_ttfbMs = -1;
    m_redirectChain.clear();
    m_savedPath.clear();
    m_savedBytes = 0;
    m_responseFile.reset();
    m_pendingRequestHeaders.clear();
    m_pendingRequestBody.clear();

    const core::MaterializedHttpRequest prepared = core::materializeHttpRequest(spec, envManager);
    m_pendingRequestMethod = prepared.method;
    m_pendingRequestUrl = prepared.url.toString();
    if (!prepared.errorKey.isEmpty()) {
        failEarly(spec, utils::tr(prepared.errorKey));
        return;
    }

    QNetworkRequest request{prepared.url};

    // Headers repetidos viram um só, separados por vírgula (o QNetworkRequest guarda um valor por nome).
    QMap<QString, QByteArray> combined;
    QStringList order;
    for (const auto &h : prepared.headers) {
        const QString key = h.first.toLower();
        if (combined.contains(key)) {
            combined[key] += ", " + h.second.toUtf8();
        } else {
            combined.insert(key, h.second.toUtf8());
            order << h.first;
        }
    }
    for (const QString &name : std::as_const(order)) {
        request.setRawHeader(name.toUtf8(), combined.value(name.toLower()));
    }

    // Corpo que precisa de dispositivo (arquivo, multipart): aberto aqui, preso à resposta depois do envio.
    QIODevice *bodyDevice = nullptr;
    QHttpMultiPart *multiPart = nullptr;
    QList<QPair<QString, QString>> shownHeaders = prepared.headers;
    if (prepared.bodyType == core::HttpBodyType::File) {
        auto *file = new QFile(prepared.bodyFile);
        if (!file->open(QIODevice::ReadOnly)) {
            const QString reason = file->errorString();
            delete file;
            failEarly(spec, utils::tr(QStringLiteral("http_runner.error.body_file")).arg(prepared.bodyFile, reason));
            return;
        }
        bodyDevice = file;
    } else if (prepared.bodyType == core::HttpBodyType::Multipart) {
        multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        for (const core::HttpEntry &f : prepared.fields) {
            QHttpPart part;
            if (f.isFile) {
                auto *file = new QFile(f.value, multiPart);
                if (!file->open(QIODevice::ReadOnly)) {
                    const QString reason = file->errorString();
                    delete multiPart;
                    failEarly(spec, utils::tr(QStringLiteral("http_runner.error.body_file")).arg(f.value, reason));
                    return;
                }
                part.setHeader(QNetworkRequest::ContentDispositionHeader,
                               QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"")
                                   .arg(f.name, QFileInfo(f.value).fileName()));
                part.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(f.value).name());
                part.setBodyDevice(file);
            } else {
                part.setHeader(QNetworkRequest::ContentDispositionHeader,
                               QStringLiteral("form-data; name=\"%1\"").arg(f.name));
                part.setBody(f.value.toUtf8());
            }
            multiPart->append(part);
        }
        // O Qt define o Content-Type (com a fronteira) ao enviar; aqui só para a aba Request mostrar.
        if (materializedHeaderIsMissing(shownHeaders, QStringLiteral("Content-Type"))) {
            shownHeaders.append({QStringLiteral("Content-Type"),
                                 QStringLiteral("multipart/form-data; boundary=") + QString::fromLatin1(multiPart->boundary())});
        }
    }

    m_pendingRequestHeaders = shownHeaders;
    m_pendingRequestBody = prepared.bodyPreview;
    m_redirectChain << m_pendingRequestUrl;

    // --- Opções de conexão ---
    const core::HttpEffectiveOptions &opts = spec.options;
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         opts.followRedirects ? QNetworkRequest::NoLessSafeRedirectPolicy
                                              : QNetworkRequest::ManualRedirectPolicy);
    request.setMaximumRedirectsAllowed(qMax(0, opts.maxRedirects));
    // Sem timeout por padrão: depurar o servidor (breakpoint) segura a requisição por minutos.
    m_pendingTimeoutMs = opts.timeoutSec > 0 ? opts.timeoutSec * 1000 : m_timeoutMs;
    request.setTransferTimeout(m_pendingTimeoutMs);

    if (opts.useCookies) {
        engine::HttpCookieStore::attachTo(m_networkManager.get());
    } else {
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    }
    m_networkManager->setProxy(resolveProxy(opts.proxy, prepared.url));

    m_timer.restart();
    utils::Logger::info(kLogTag, QStringLiteral("%1 %2").arg(prepared.method, m_pendingRequestUrl));

    // --- Envio ---
    const QByteArray verbBytes = prepared.method.toUtf8();
    const QString verb = prepared.method;
    const QByteArray &payload = prepared.payload;
    const bool hasBody = multiPart || bodyDevice || !payload.isEmpty();
    QNetworkReply *reply = nullptr;
    if (!hasBody && verb == QStringLiteral("GET")) {
        reply = m_networkManager->get(request);
    } else if (!hasBody && verb == QStringLiteral("HEAD")) {
        reply = m_networkManager->head(request);
    } else if (!hasBody && verb == QStringLiteral("DELETE")) {
        reply = m_networkManager->deleteResource(request);
    } else if (multiPart) {
        reply = m_networkManager->sendCustomRequest(request, verbBytes, multiPart);
    } else if (bodyDevice) {
        reply = m_networkManager->sendCustomRequest(request, verbBytes, bodyDevice);
    } else if (verb == QStringLiteral("POST")) {
        reply = m_networkManager->post(request, payload);
    } else if (verb == QStringLiteral("PUT")) {
        reply = m_networkManager->put(request, payload);
    } else {
        // PATCH, QUERY, OPTIONS e verbos livres não têm atalho no Qt Network.
        reply = m_networkManager->sendCustomRequest(request, verbBytes, payload);
    }
    m_reply = reply;
    if (multiPart) {
        multiPart->setParent(reply); // vive até a resposta terminar
    }
    if (bodyDevice) {
        bodyDevice->setParent(reply);
    }

    if (!opts.verifyTls) {
        connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });
        utils::Logger::warning(kLogTag, QStringLiteral("Verificação de TLS desligada para esta requisição."));
    }
    connect(reply, &QNetworkReply::redirected, this, [this](const QUrl &target) {
        m_redirectChain << target.toString();
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this]() {
        if (m_timer.isValid()) {
            m_ttfbMs = m_timer.elapsed();
        }
    });

    const QString responseFilePath = envManager.interpolate(spec.responseFile).trimmed();
    m_savedPath = responseFilePath;
    // Dados que chegam antes do fim: gravados num arquivo (respostas 2xx, quando pedido) ou, num fluxo de
    // eventos (text/event-stream), mostrados na saída na hora. As demais respostas esperam o fim, em memória.
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status < 200 || status >= 300) {
            return;
        }
        if (!m_savedPath.isEmpty()) {
            if (!m_responseFile) {
                QDir().mkpath(QFileInfo(m_savedPath).absolutePath());
                m_responseFile = std::make_unique<QFile>(m_savedPath);
                if (!m_responseFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    reply->abort();
                    return;
                }
            }
            const QByteArray chunk = reply->readAll();
            m_savedBytes += chunk.size();
            m_responseFile->write(chunk);
            return;
        }
        if (reply->rawHeader("Content-Type").toLower().contains("text/event-stream")) {
            if (!m_streaming) {
                emit logMessage(utils::tr(QStringLiteral("http_runner.streaming_started"))
                                    .arg(m_pendingRequestMethod, m_pendingRequestUrl) + QStringLiteral("\n"), false);
            }
            m_streaming = true;
            const QByteArray chunk = reply->readAll();
            constexpr qsizetype kKeep = 1024 * 1024; // o resultado guarda só o começo do fluxo
            if (m_streamed.size() < kKeep) {
                m_streamed.append(chunk.left(kKeep - m_streamed.size()));
            }
            emit logMessage(QString::fromUtf8(chunk), false);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, spec, &envManager]() {
        handleReplyFinished(reply, spec, &envManager);
    });
}

void HttpRunner::abort()
{
    if (m_reply) {
        m_canceled = true;
        m_reply->abort();
    }
}

void HttpRunner::runAssertions(const core::HttpRequestSpec &spec, HttpResult &result, core::EnvironmentManager &envManager)
{
    for (const core::ExecutionCondition &c : spec.assertions) {
        if (!c.enabled) {
            continue;
        }
        if (!envManager.evaluateCondition(c.left, c.op, c.right)) {
            result.assertionFailures << (c.name.trimmed().isEmpty()
                ? QStringLiteral("%1 %2 %3").arg(c.left, c.op, c.right)
                : c.name.trimmed());
        }
    }
}

void HttpRunner::handleReplyFinished(QNetworkReply *reply, const core::HttpRequestSpec &spec, core::EnvironmentManager *envManager)
{
    reply->deleteLater();

    HttpResult result;
    const QVariant statusVariant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    result.statusCode = statusVariant.isValid() ? statusVariant.toInt() : 0;
    const bool statusIs2xx = result.statusCode >= 200 && result.statusCode < 300;

    // Resposta gravada em arquivo: o que ainda faltava vai para lá; o corpo não fica em memória.
    if (m_responseFile) {
        const QByteArray rest = reply->readAll();
        m_savedBytes += rest.size();
        m_responseFile->write(rest);
        m_responseFile->close();
        m_responseFile.reset();
        result.savedPath = m_savedPath;
        result.bodySize = m_savedBytes;
    } else if (m_streaming) {
        m_streamed.append(reply->readAll());
        result.body = m_streamed;
        result.bodySize = result.body.size();
    } else {
        result.body = reply->readAll();
        result.bodySize = result.body.size();
    }

    // --- SAÍDA V2: metadados que a interface precisa para as abas ---
    result.elapsedMs = m_timer.isValid() ? m_timer.elapsed() : 0;
    result.ttfbMs = m_ttfbMs >= 0 ? m_ttfbMs : 0;
    result.reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute)
                              .toString();
    for (const QByteArray &name : reply->rawHeaderList()) {
        result.headers.append({QString::fromUtf8(name),
                               QString::fromUtf8(reply->rawHeader(name))});
    }
    result.contentType = QString::fromUtf8(reply->rawHeader("Content-Type"));
    result.finalUrl = reply->url().toString();
    result.requestMethod = m_pendingRequestMethod;
    result.requestUrl = m_pendingRequestUrl;
    result.requestHeaders = m_pendingRequestHeaders;
    result.requestBody = m_pendingRequestBody;
    result.timeoutSec = m_pendingTimeoutMs / 1000;
    result.timedOut = !m_canceled && m_pendingTimeoutMs > 0 && result.statusCode == 0
        && (reply->error() == QNetworkReply::TimeoutError || reply->error() == QNetworkReply::OperationCanceledError);
    if (m_redirectChain.size() > 1) {
        result.redirectChain = m_redirectChain;
    }

    if (m_canceled) {
        result.success = false;
        result.canceled = true;
        result.errorMessage = utils::tr(QStringLiteral("http_runner.canceled"));
        emit logMessage(result.errorMessage + QStringLiteral("\n"), true);
        emit finished(result);
        return;
    }

    // A resposta fica disponível para asserções, condições e hooks seguintes.
    if (envManager != nullptr && result.statusCode > 0) {
        envManager->setBuiltinVar(QStringLiteral("RESPONSE_STATUS"), QString::number(result.statusCode));
        envManager->setBuiltinVar(QStringLiteral("RESPONSE_TIME_MS"), QString::number(result.elapsedMs));
        envManager->setBuiltinVar(QStringLiteral("RESPONSE_SIZE"), QString::number(result.bodySize));
        envManager->setBuiltinVar(QStringLiteral("RESPONSE_CONTENT_TYPE"), result.contentType);
        // Só para {{RESPONSE_BODY}}: fora do ambiente dos processos (ver setInterpolationOnlyVar).
        envManager->setInterpolationOnlyVar(QStringLiteral("RESPONSE_BODY"),
                                  result.isBinary() ? QString() : QString::fromUtf8(result.body));
        for (const auto &h : result.headers) {
            envManager->setBuiltinVar(envHeaderVar(h.first), h.second);
        }
    }

    const bool assertMode = envManager != nullptr && result.statusCode > 0
        && std::any_of(spec.assertions.cbegin(), spec.assertions.cend(),
                       [](const core::ExecutionCondition &c) { return c.enabled; });

    const QString bodyForLog = result.isBinary()
        ? utils::tr(QStringLiteral("http_runner.binary_body")).arg(humanSize(result.bodySize), result.contentType)
        : formatResponseBody(result.body, result.contentType);

    if (reply->error() != QNetworkReply::NoError && !assertMode) {
        result.success = false;
        // Mensagem CRUA do Qt (ex: "Error transferring <url> - server
        // replied:") é feia e redundante: repete a URL inteira e termina
        // vazia quando o servidor respondeu com corpo. Quando HÁ um status
        // HTTP (o servidor respondeu), usamos uma mensagem CONCISA — o
        // corpo real aparece logo abaixo em "Resposta:", e essa mensagem
        // também é o que propaga para "Pipeline abortado/falhou", evitando
        // repetir a URL gigante três vezes (feedback do usuário).
        const QString rawQtError = reply->errorString();
        if (result.statusCode > 0) {
            result.errorMessage = utils::tr(QStringLiteral("http_runner.error.http_status")).arg(result.statusCode);
        } else if (result.timedOut) {
            result.errorMessage = utils::tr(QStringLiteral("http_runner.error.timeout")).arg(result.timeoutSec);
        } else {
            // Erro de transporte real (sem resposta do servidor: DNS,
            // conexão recusada). Aí a mensagem do Qt é útil.
            result.errorMessage = rawQtError;
        }
        utils::Logger::error(kLogTag,
            QStringLiteral("Falha na requisição (%1): %2").arg(result.statusCode).arg(rawQtError));
        // Erro HTTP formatado (feedback do usuário: aparecia só um "broken
        // pipe" cru). Monta um bloco legível com método, URL, status (se
        // houver) e a mensagem; anexa o corpo da resposta formatado se veio.
        QString block;
        block += utils::tr(QStringLiteral("http_runner.block.error_header")) + QStringLiteral("\n");
        block += QStringLiteral("%1 %2\n").arg(m_pendingRequestMethod, m_pendingRequestUrl);
        if (result.statusCode > 0) {
            block += utils::tr(QStringLiteral("http_runner.block.status")).arg(result.statusCode) + QStringLiteral("\n");
        } else {
            // Só mostra "Motivo" quando é erro de transporte (sem status),
            // pois aí a mensagem do Qt agrega informação. Com status, o
            // status + a Resposta abaixo já dizem tudo.
            block += utils::tr(QStringLiteral("http_runner.block.reason")).arg(result.timedOut ? result.errorMessage : rawQtError) + QStringLiteral("\n");
        }
        if (!bodyForLog.trimmed().isEmpty()) {
            block += utils::tr(QStringLiteral("http_runner.block.response")).arg(bodyForLog) + QStringLiteral("\n");
        }
        block += QStringLiteral("───────────────────────────");
        emit logMessage(block, true);
        emit finished(result);
        return;
    }

    if (!statusIs2xx && !assertMode) {
        // 4xx/5xx tratado como erro de pipeline se for pre-hook;
        // a decisão de abortar cabe ao ExecutionPipeline, aqui só reportamos.
        result.success = false;
        result.errorMessage = utils::tr(QStringLiteral("http_runner.error.http_status_full")).arg(result.statusCode);
        utils::Logger::warning(kLogTag, QStringLiteral("Resposta não-2xx: %1").arg(result.statusCode));
        QString block;
        block += utils::tr(QStringLiteral("http_runner.block.non2xx_header")).arg(result.statusCode) + QStringLiteral("\n");
        block += QStringLiteral("%1 %2\n").arg(m_pendingRequestMethod, m_pendingRequestUrl);
        if (!bodyForLog.trimmed().isEmpty()) {
            block += utils::tr(QStringLiteral("http_runner.block.response")).arg(bodyForLog) + QStringLiteral("\n");
        }
        block += QStringLiteral("───────────────────────────");
        emit logMessage(block, true);
        emit finished(result);
        return;
    }

    result.success = statusIs2xx;

    // Log de sucesso: sem isso, uma requisição 2xx não deixa nenhum rastro
    // visível no Terminal Drawer, dando a falsa impressão de que "não
    // funcionou" mesmo quando a resposta foi bem-sucedida.
    // SAÍDA V2: linha de status rica — método, URL INTERPOLADA (antes usava
    // config.url cru, então aparecia "{{API_BASE}}/api/docs.json" na saída em
    // vez da URL real), status + reason phrase, tamanho legível e LATÊNCIA.
    QString statusLine = QStringLiteral("HTTP %1 %2 -> %3")
                             .arg(result.requestMethod, result.requestUrl)
                             .arg(result.statusCode);
    if (!result.reasonPhrase.isEmpty()) {
        statusLine += QStringLiteral(" %1").arg(result.reasonPhrase);
    }
    statusLine += QStringLiteral("  •  %1  •  %2 ms")
                      .arg(humanSize(result.bodySize))
                      .arg(result.elapsedMs);
    if (result.ttfbMs > 0 && result.ttfbMs < result.elapsedMs) {
        statusLine += QStringLiteral(" (%1)").arg(utils::tr(QStringLiteral("http_runner.ttfb")).arg(result.ttfbMs));
    }
    if (!result.contentType.isEmpty()) {
        statusLine += QStringLiteral("  •  %1").arg(result.contentType.section(QLatin1Char(';'), 0, 0));
    }
    if (!result.redirectChain.isEmpty()) {
        statusLine += QStringLiteral("  •  %1").arg(utils::tr(QStringLiteral("http_runner.redirects"))
                                                        .arg(result.redirectChain.size() - 1));
    }
    // "\n" final é OBRIGATÓRIO aqui — bug real reportado: "a PRIMEIRA linha
    // de JSON lançada está caindo sempre colada junto a saída normal...
    // perde se o app soltar uma linha de JSON APENAS". As duas emissões
    // (linha de status + corpo) chegam no MESMO turno do event loop e o
    // TerminalDrawer as COALESCE (ver flushPendingOutput: concatena chunks
    // consecutivos do mesmo canal SEM separador nenhum). Sem \n aqui, o
    // "{" de abertura do corpo JSON ficava colado no fim da linha de
    // status ("...45 ms{") — a linha física resultante não começa com
    // "{", então LogLineModel nunca reconhecia o corpo como JSON: a
    // resposta inteira virava uma única linha crua sem estrutura nenhuma.
    emit logMessage(statusLine + QStringLiteral("\n"), false);
    // Corpo formatado (feedback do usuário: um CURL pode retornar mais que
    // um JSON puro; formatamos o JSON quando válido, ou mostramos cru).
    if (!result.savedPath.isEmpty()) {
        emit logMessage(utils::tr(QStringLiteral("http_runner.saved_to")).arg(result.savedPath, humanSize(result.bodySize)),
                        false);
    } else if (!m_streaming) { // um fluxo de eventos já foi mostrado conforme chegava
        emit logMessage(bodyForLog, false);
    }

    if (!spec.envExtractors.isEmpty() && envManager != nullptr && statusIs2xx) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(result.body, &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Payload de resposta não é JSON válido (%1). Extractors ignorados.")
                    .arg(parseError.errorString()));
            emit logMessage(utils::tr(QStringLiteral("http_runner.warning.invalid_json_body")), true);
        } else {
            applyExtractors(doc, spec.envExtractors, *envManager);
        }
    }

    // Com asserções ligadas, quem decide se a requisição deu certo são elas — assim dá para esperar um 404.
    if (assertMode) {
        runAssertions(spec, result, *envManager);
        result.success = result.assertionFailures.isEmpty();
        if (!result.success) {
            result.errorMessage = utils::tr(QStringLiteral("http_runner.error.assertions"))
                                      .arg(result.assertionFailures.join(QStringLiteral("; ")));
            emit logMessage(result.errorMessage, true);
        } else {
            emit logMessage(utils::tr(QStringLiteral("http_runner.assertions_passed")), false);
        }
    }

    emit finished(result);
}

QJsonValue HttpRunner::resolveJsonPath(const QJsonValue &root, const QString &jsonPath)
{
    const QStringList segments = jsonPath.split(QLatin1Char('.'), Qt::SkipEmptyParts);

    QJsonValue current = root;
    for (const QString &segment : segments) {
        if (!current.isObject()) {
            return QJsonValue(QJsonValue::Undefined);
        }
        const QJsonObject obj = current.toObject();
        if (!obj.contains(segment)) {
            return QJsonValue(QJsonValue::Undefined);
        }
        current = obj.value(segment);
    }
    return current;
}

void HttpRunner::applyExtractors(const QJsonDocument &doc, const QVector<core::EnvExtractor> &extractors, core::EnvironmentManager &envManager)
{
    const QJsonValue root = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());

    for (const core::EnvExtractor &extractor : extractors) {
        // Sintaxe OU (feedback do usuário: "extratores tem que suportar
        // sintaxe de OU") — "data.token || token" tenta cada caminho em
        // ordem, usa o primeiro que existir. Útil quando a mesma info vem
        // em campos diferentes conforme a API/versão respondendo.
        const QStringList candidatePaths = extractor.jsonPath.split(
            QStringLiteral("||"), Qt::SkipEmptyParts);
        QJsonValue value(QJsonValue::Undefined);
        for (const QString &candidate : candidatePaths) {
            value = resolveJsonPath(root, candidate.trimmed());
            if (!value.isUndefined()) {
                break;
            }
        }

        if (value.isUndefined()) {
            // Nenhum dos caminhos (OU nenhum, se só havia 1) existe: loga
            // aviso e NÃO sobrescreve o valor anterior da variável.
            utils::Logger::warning(kLogTag,
                QStringLiteral("json_path '%1' não encontrado na resposta. Variável '%2' mantida.")
                    .arg(extractor.jsonPath, extractor.envVar));
            emit logMessage(utils::tr(QStringLiteral("http_runner.warning.json_path_not_found"))
                                 .arg(extractor.jsonPath, extractor.envVar), true);
            continue;
        }

        QString finalValue;
        if (value.isString()) {
            finalValue = value.toString();
        } else if (value.isDouble()) {
            finalValue = QString::number(value.toDouble());
        } else if (value.isBool()) {
            finalValue = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        } else {
            finalValue = QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
        }

        // ESCOPO (pedido do usuário: "preciso QUE escolha se... salva na
        // proprio PROJETO ou Global") — "global" força gravar no escopo
        // Global ("") mesmo com um projeto selecionado; "project" (padrão)
        // mantém o comportamento de sempre, escopo AMBIENTE atual.
        const QString targetScope = extractor.scope == QStringLiteral("global")
            ? QString() : envManager.currentDynamicVarScope();
        envManager.setDynamicVarInScope(targetScope, extractor.envVar, finalValue);
        utils::Logger::info(kLogTag,
            QStringLiteral("Extraído '%1' -> variável '%2' (escopo: %3).")
                .arg(extractor.jsonPath, extractor.envVar, targetScope.isEmpty() ? QStringLiteral("Global") : targetScope));
        if (extractor.persist) {
            emit dynamicVarPersistRequested(targetScope, extractor.envVar, finalValue);
        }
    }
}

} // namespace kai::engine
