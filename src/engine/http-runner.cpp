#include "engine/http-runner.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>

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
QString formatResponseBody(const QByteArray &body)
{
    if (body.trimmed().isEmpty()) {
        return QString();
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
    const QString interpolatedUrl = envManager.interpolate(config.url);
    QNetworkRequest request{QUrl(interpolatedUrl)};

    for (auto it = config.headers.constBegin(); it != config.headers.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), envManager.interpolate(it.value()).toUtf8());
    }

    // QNetworkRequest::setTransferTimeout está disponível desde Qt 5.15 e
    // cobre o requisito de timeout padrão de 10s.
    request.setTransferTimeout(m_timeoutMs);

    const QByteArray interpolatedBody = envManager.interpolate(config.body).toUtf8();

    // SAÍDA V2: guarda o que foi ENVIADO (para a aba Raw/Request) e inicia o
    // cronômetro da latência.
    m_pendingRequestMethod = core::httpMethodToString(config.method);
    m_pendingRequestUrl = interpolatedUrl;
    m_pendingRequestHeaders.clear();
    for (auto it = config.headers.constBegin(); it != config.headers.constEnd(); ++it) {
        m_pendingRequestHeaders.append({it.key(), envManager.interpolate(it.value())});
    }
    m_pendingRequestBody = QString::fromUtf8(interpolatedBody);
    m_timer.restart();

    utils::Logger::info(kLogTag,
        QStringLiteral("%1 %2").arg(core::httpMethodToString(config.method), interpolatedUrl));

    QNetworkReply *reply = nullptr;
    switch (config.method) {
    case core::HttpMethod::Get:
        reply = m_networkManager->get(request);
        break;
    case core::HttpMethod::Post:
        reply = m_networkManager->post(request, interpolatedBody);
        break;
    case core::HttpMethod::Put:
        reply = m_networkManager->put(request, interpolatedBody);
        break;
    case core::HttpMethod::Delete:
        reply = m_networkManager->deleteResource(request);
        break;
    case core::HttpMethod::Patch:
        reply = m_networkManager->sendCustomRequest(request, "PATCH", interpolatedBody);
        break;
    case core::HttpMethod::Query:
        // QUERY não tem verbo nativo no Qt Network (é recente/RFC draft) —
        // mesmo caminho de PATCH, despachado como método customizado.
        reply = m_networkManager->sendCustomRequest(request, "QUERY", interpolatedBody);
        break;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, config, &envManager]() {
        handleReplyFinished(reply, config, &envManager);
    });
}

void HttpRunner::handleReplyFinished(QNetworkReply *reply, const core::HttpConfig &config, core::EnvironmentManager *envManager)
{
    reply->deleteLater();

    HttpResult result;
    result.body = reply->readAll();

    const QVariant statusVariant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    result.statusCode = statusVariant.isValid() ? statusVariant.toInt() : 0;

    // --- SAÍDA V2: metadados que a interface precisa para as abas ---
    result.elapsedMs = m_timer.isValid() ? m_timer.elapsed() : 0;
    result.bodySize = result.body.size();
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

    if (reply->error() != QNetworkReply::NoError) {
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
        } else {
            // Erro de transporte real (sem resposta do servidor: DNS,
            // conexão recusada, timeout). Aí a mensagem do Qt é útil.
            result.errorMessage = rawQtError;
        }
        utils::Logger::error(kLogTag,
            QStringLiteral("Falha na requisição (%1): %2").arg(result.statusCode).arg(rawQtError));
        // Erro HTTP formatado (feedback do usuário: aparecia só um "broken
        // pipe" cru). Monta um bloco legível com método, URL, status (se
        // houver) e a mensagem; anexa o corpo da resposta formatado se veio.
        QString block;
        block += utils::tr(QStringLiteral("http_runner.block.error_header")) + QStringLiteral("\n");
        block += QStringLiteral("%1 %2\n").arg(core::httpMethodToString(config.method), config.url);
        if (result.statusCode > 0) {
            block += utils::tr(QStringLiteral("http_runner.block.status")).arg(result.statusCode) + QStringLiteral("\n");
        } else {
            // Só mostra "Motivo" quando é erro de transporte (sem status),
            // pois aí a mensagem do Qt agrega informação. Com status, o
            // status + a Resposta abaixo já dizem tudo.
            block += utils::tr(QStringLiteral("http_runner.block.reason")).arg(rawQtError) + QStringLiteral("\n");
        }
        const QString formattedErrBody = formatResponseBody(result.body);
        if (!formattedErrBody.trimmed().isEmpty()) {
            block += utils::tr(QStringLiteral("http_runner.block.response")).arg(formattedErrBody) + QStringLiteral("\n");
        }
        block += QStringLiteral("───────────────────────────");
        emit logMessage(block, true);
        emit finished(result);
        return;
    }

    result.success = (result.statusCode >= 200 && result.statusCode < 300);

    if (!result.success) {
        // 4xx/5xx tratado como erro de pipeline se for pre-hook;
        // a decisão de abortar cabe ao ExecutionPipeline, aqui só reportamos.
        result.errorMessage = utils::tr(QStringLiteral("http_runner.error.http_status_full")).arg(result.statusCode);
        utils::Logger::warning(kLogTag, QStringLiteral("Resposta não-2xx: %1").arg(result.statusCode));
        QString block;
        block += utils::tr(QStringLiteral("http_runner.block.non2xx_header")).arg(result.statusCode) + QStringLiteral("\n");
        block += QStringLiteral("%1 %2\n").arg(core::httpMethodToString(config.method), config.url);
        const QString formattedErrBody = formatResponseBody(result.body);
        if (!formattedErrBody.trimmed().isEmpty()) {
            block += utils::tr(QStringLiteral("http_runner.block.response")).arg(formattedErrBody) + QStringLiteral("\n");
        }
        block += QStringLiteral("───────────────────────────");
        emit logMessage(block, true);
        emit finished(result);
        return;
    }

    // Log de sucesso: sem isso, uma requisição 2xx não deixa nenhum rastro
    // visível no Terminal Drawer, dando a falsa impressão de que "não
    // funcionou" mesmo quando a resposta foi bem-sucedida.
    // SAÍDA V2: linha de status rica — método, URL INTERPOLADA (antes usava
    // config.url cru, então aparecia "{{API_BASE}}/api/docs.json" na saída em
    // vez da URL real), status + reason phrase, tamanho legível e LATÊNCIA.
    auto humanSize = [](qint64 bytes) -> QString {
        if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
        if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    };
    QString statusLine = QStringLiteral("HTTP %1 %2 -> %3")
                             .arg(result.requestMethod.isEmpty()
                                      ? core::httpMethodToString(config.method)
                                      : result.requestMethod,
                                  result.requestUrl.isEmpty() ? config.url : result.requestUrl)
                             .arg(result.statusCode);
    if (!result.reasonPhrase.isEmpty()) {
        statusLine += QStringLiteral(" %1").arg(result.reasonPhrase);
    }
    statusLine += QStringLiteral("  •  %1  •  %2 ms")
                      .arg(humanSize(result.bodySize))
                      .arg(result.elapsedMs);
    if (!result.contentType.isEmpty()) {
        statusLine += QStringLiteral("  •  %1").arg(result.contentType.section(QLatin1Char(';'), 0, 0));
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
    emit logMessage(formatResponseBody(result.body), false);

    if (!config.envExtractors.isEmpty() && envManager != nullptr) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(result.body, &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Payload de resposta não é JSON válido (%1). Extractors ignorados.")
                    .arg(parseError.errorString()));
            emit logMessage(utils::tr(QStringLiteral("http_runner.warning.invalid_json_body")), true);
        } else {
            applyExtractors(doc, config.envExtractors, *envManager);
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
