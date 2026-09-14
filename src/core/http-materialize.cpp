#include "core/http-materialize.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QUrlQuery>

namespace kai::core {

namespace {

bool hasHeader(const QList<QPair<QString, QString>> &headers, const QString &name)
{
    for (const auto &h : headers) {
        if (h.first.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString humanSize(qint64 bytes)
{
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace

QString materializedHeader(const MaterializedHttpRequest &request, const QString &name)
{
    for (const auto &h : request.headers) {
        if (h.first.compare(name, Qt::CaseInsensitive) == 0) {
            return h.second;
        }
    }
    return QString();
}

MaterializedHttpRequest materializeHttpRequest(const HttpRequestSpec &spec, const EnvironmentManager &env)
{
    MaterializedHttpRequest out;
    out.method = spec.method.trimmed().isEmpty() ? QStringLiteral("GET") : spec.method.trimmed().toUpper();

    QUrl url(env.interpolate(spec.url).trimmed());

    for (const HttpEntry &h : spec.headers) {
        const QString name = env.interpolate(h.name).trimmed();
        if (!name.isEmpty()) {
            out.headers.append({name, env.interpolate(h.value)});
        }
    }

    // --- Query: tabela de parâmetros + chave de API na query ---
    QUrlQuery query(url);
    bool queryTouched = false;
    for (const HttpEntry &p : spec.queryParams) {
        const QString name = env.interpolate(p.name);
        if (!name.isEmpty()) {
            query.addQueryItem(name, env.interpolate(p.value));
            queryTouched = true;
        }
    }

    // --- Autenticação (um header Authorization escrito à mão sempre vence) ---
    switch (spec.auth.type) {
    case HttpAuthType::Basic:
        if (!hasHeader(out.headers, QStringLiteral("Authorization"))) {
            const QString credentials = env.interpolate(spec.auth.username) + QLatin1Char(':')
                                        + env.interpolate(spec.auth.password);
            out.headers.append({QStringLiteral("Authorization"),
                                QStringLiteral("Basic ") + QString::fromLatin1(credentials.toUtf8().toBase64())});
        }
        break;
    case HttpAuthType::Bearer: {
        const QString token = env.interpolate(spec.auth.token).trimmed();
        if (!token.isEmpty() && !hasHeader(out.headers, QStringLiteral("Authorization"))) {
            out.headers.append({QStringLiteral("Authorization"), QStringLiteral("Bearer ") + token});
        }
        break;
    }
    case HttpAuthType::ApiKey: {
        const QString keyName = env.interpolate(spec.auth.keyName).trimmed();
        const QString keyValue = env.interpolate(spec.auth.keyValue);
        if (!keyName.isEmpty()) {
            if (spec.auth.keyInQuery) {
                query.addQueryItem(keyName, keyValue);
                queryTouched = true;
            } else if (!hasHeader(out.headers, keyName)) {
                out.headers.append({keyName, keyValue});
            }
        }
        break;
    }
    case HttpAuthType::Inherit:
    case HttpAuthType::None:
        break;
    }

    if (queryTouched) {
        url.setQuery(query);
    }
    out.url = url;

    // --- Corpo ---
    QString autoContentType;
    switch (spec.bodyType) {
    case HttpBodyType::None:
        break;
    case HttpBodyType::Raw:
    case HttpBodyType::Json:
    case HttpBodyType::Xml:
        out.payload = env.interpolate(spec.body).toUtf8();
        out.bodyPreview = QString::fromUtf8(out.payload);
        if (!out.payload.isEmpty() && spec.bodyType == HttpBodyType::Json) {
            autoContentType = QStringLiteral("application/json");
        } else if (!out.payload.isEmpty() && spec.bodyType == HttpBodyType::Xml) {
            autoContentType = QStringLiteral("application/xml");
        }
        out.bodyType = out.payload.isEmpty() ? HttpBodyType::None : HttpBodyType::Raw;
        break;
    case HttpBodyType::Form: {
        QUrlQuery form;
        for (const HttpEntry &f : spec.formFields) {
            const QString name = env.interpolate(f.name);
            if (!name.isEmpty()) {
                form.addQueryItem(name, env.interpolate(f.value));
            }
        }
        out.payload = form.query(QUrl::FullyEncoded).toUtf8();
        out.bodyPreview = QString::fromUtf8(out.payload);
        if (!out.payload.isEmpty()) {
            autoContentType = QStringLiteral("application/x-www-form-urlencoded");
            out.bodyType = HttpBodyType::Raw;
        }
        break;
    }
    case HttpBodyType::GraphQl: {
        const QString graphqlQuery = env.interpolate(spec.body);
        const QString variablesText = env.interpolate(spec.graphqlVariables).trimmed();
        QJsonObject envelope;
        envelope["query"] = graphqlQuery;
        if (!variablesText.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument variables = QJsonDocument::fromJson(variablesText.toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !variables.isObject()) {
                out.errorKey = QStringLiteral("http_runner.error.graphql_variables");
                return out;
            }
            envelope["variables"] = variables.object();
        }
        if (!graphqlQuery.trimmed().isEmpty()) {
            out.payload = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
            out.bodyPreview = QString::fromUtf8(out.payload);
            autoContentType = QStringLiteral("application/json");
            out.bodyType = HttpBodyType::Raw;
        }
        break;
    }
    case HttpBodyType::File: {
        const QString path = env.interpolate(spec.bodyFile).trimmed();
        if (!path.isEmpty()) {
            out.bodyType = HttpBodyType::File;
            out.bodyFile = path;
            const qint64 size = QFileInfo(path).size();
            out.bodyPreview = QStringLiteral("[file] %1 (%2)").arg(path, humanSize(size));
            autoContentType = QMimeDatabase().mimeTypeForFile(path).name();
            if (autoContentType.isEmpty()) {
                autoContentType = QStringLiteral("application/octet-stream");
            }
        }
        break;
    }
    case HttpBodyType::Multipart: {
        QStringList preview;
        for (const HttpEntry &f : spec.formFields) {
            HttpEntry resolved = f;
            resolved.name = env.interpolate(f.name);
            resolved.value = env.interpolate(f.value);
            if (resolved.name.isEmpty()) {
                continue;
            }
            out.fields << resolved;
            preview << (resolved.isFile
                ? QStringLiteral("%1: [file] %2 (%3)").arg(resolved.name, resolved.value,
                                                           humanSize(QFileInfo(resolved.value).size()))
                : QStringLiteral("%1: %2").arg(resolved.name, resolved.value));
        }
        if (!out.fields.isEmpty()) {
            out.bodyType = HttpBodyType::Multipart;
            out.bodyPreview = preview.join(QLatin1Char('\n'));
        }
        break;
    }
    }

    if (!autoContentType.isEmpty() && !hasHeader(out.headers, QStringLiteral("Content-Type"))) {
        out.headers.append({QStringLiteral("Content-Type"), autoContentType});
    }
    return out;
}

} // namespace kai::core
