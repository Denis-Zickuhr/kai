#include "core/openapi-parser.h"
#include "utils/translation-manager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace kai::core {

namespace {

const QStringList kHttpMethods = {
    QStringLiteral("get"), QStringLiteral("post"), QStringLiteral("put"),
    QStringLiteral("patch"), QStringLiteral("delete")
};

// Determina a URL base a partir do documento (OpenAPI 3 servers[0].url ou
// Swagger 2 host+basePath+scheme). Retorna vazio se não houver.
QString resolveBaseUrl(const QJsonObject &root)
{
    // OpenAPI 3.x.
    const QJsonArray servers = root.value(QStringLiteral("servers")).toArray();
    if (!servers.isEmpty()) {
        const QString url = servers.first().toObject().value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) {
            return url;
        }
    }
    // Swagger 2.0.
    const QString host = root.value(QStringLiteral("host")).toString();
    if (!host.isEmpty()) {
        const QString basePath = root.value(QStringLiteral("basePath")).toString();
        const QJsonArray schemes = root.value(QStringLiteral("schemes")).toArray();
        const QString scheme = schemes.isEmpty() ? QStringLiteral("https")
                                                  : schemes.first().toString();
        return scheme + QStringLiteral("://") + host + basePath;
    }
    return QString();
}

// Gera um valor JSON de exemplo (recursivo raso) a partir de um schema.
QJsonValue exampleFromSchema(const QJsonObject &schema, int depth = 0)
{
    if (depth > 4) {
        return QJsonValue();
    }
    if (schema.contains(QStringLiteral("example"))) {
        return schema.value(QStringLiteral("example"));
    }
    const QString type = schema.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("object") || schema.contains(QStringLiteral("properties"))) {
        QJsonObject obj;
        const QJsonObject props = schema.value(QStringLiteral("properties")).toObject();
        for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
            obj[it.key()] = exampleFromSchema(it.value().toObject(), depth + 1);
        }
        return obj;
    }
    if (type == QStringLiteral("array")) {
        QJsonArray arr;
        arr.append(exampleFromSchema(schema.value(QStringLiteral("items")).toObject(), depth + 1));
        return arr;
    }
    if (type == QStringLiteral("integer") || type == QStringLiteral("number")) {
        return 0;
    }
    if (type == QStringLiteral("boolean")) {
        return false;
    }
    return QStringLiteral("");
}

// Extrai o schema do requestBody (OpenAPI 3) preferindo application/json.
QJsonObject requestBodySchema(const QJsonObject &op)
{
    const QJsonObject rb = op.value(QStringLiteral("requestBody")).toObject();
    const QJsonObject content = rb.value(QStringLiteral("content")).toObject();
    if (content.contains(QStringLiteral("application/json"))) {
        return content.value(QStringLiteral("application/json")).toObject()
            .value(QStringLiteral("schema")).toObject();
    }
    // primeiro content-type disponível
    if (!content.isEmpty()) {
        return content.begin().value().toObject().value(QStringLiteral("schema")).toObject();
    }
    return {};
}

} // namespace

OpenApiParseResult parseOpenApi(const QString &jsonText)
{
    OpenApiParseResult result;

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        result.errorMessage = utils::tr(QStringLiteral("openapi.error.invalid_json"))
            .arg(perr.errorString());
        return result;
    }
    const QJsonObject root = doc.object();

    // Valida que parece OpenAPI/Swagger.
    if (!root.contains(QStringLiteral("openapi")) && !root.contains(QStringLiteral("swagger"))) {
        result.errorMessage = utils::tr(QStringLiteral("openapi.error.not_openapi"));
        return result;
    }

    result.apiTitle = root.value(QStringLiteral("info")).toObject()
        .value(QStringLiteral("title")).toString(QStringLiteral("API importada"));

    const QString baseUrl = resolveBaseUrl(root);
    const QJsonObject paths = root.value(QStringLiteral("paths")).toObject();

    for (auto pit = paths.constBegin(); pit != paths.constEnd(); ++pit) {
        const QString path = pit.key();
        const QJsonObject pathItem = pit.value().toObject();

        for (const QString &method : kHttpMethods) {
            if (!pathItem.contains(method)) {
                continue;
            }
            const QJsonObject op = pathItem.value(method).toObject();

            OpenApiEndpoint ep;
            HttpConfig cfg;
            cfg.method = httpMethodFromString(method.toUpper());
            cfg.url = baseUrl + path;

            // Corpo de exemplo para métodos com body.
            const QJsonObject schema = requestBodySchema(op);
            if (!schema.isEmpty()) {
                const QJsonValue example = exampleFromSchema(schema);
                if (!example.isNull() && !example.isUndefined()) {
                    const QJsonDocument bodyDoc(example.isObject()
                        ? QJsonDocument(example.toObject())
                        : QJsonDocument(QJsonArray{example}));
                    cfg.body = QString::fromUtf8(bodyDoc.toJson(QJsonDocument::Indented));
                    cfg.headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
                }
            }

            // Nome amigável: summary > operationId > "METHOD /path".
            QString name = op.value(QStringLiteral("summary")).toString();
            if (name.isEmpty()) {
                name = op.value(QStringLiteral("operationId")).toString();
            }
            if (name.isEmpty()) {
                name = method.toUpper() + QStringLiteral(" ") + path;
            }
            ep.name = name;
            ep.config = cfg;
            result.endpoints.append(ep);
        }
    }

    if (result.endpoints.isEmpty()) {
        result.errorMessage = utils::tr(QStringLiteral("openapi.error.no_endpoints"));
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace kai::core
