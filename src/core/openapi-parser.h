#pragma once

#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Endpoint de uma spec OpenAPI, pronto para virar um Command HTTP.
struct OpenApiEndpoint {
    QString name;        // ex: "GET /users/{id}" ou summary/operationId
    HttpConfig config;   // método, url (base + path), headers, body de exemplo
};

struct OpenApiParseResult {
    bool ok = false;
    QString errorMessage;
    QString apiTitle;                    // info.title (nome sugerido da pasta)
    QVector<OpenApiEndpoint> endpoints;
};

// Parse de OpenAPI 3.x / Swagger 2.0 em JSON (YAML não é suportado): endpoints (paths x métodos) como
// HttpConfigs. URL base de servers[0].url (OpenAPI 3) ou host+basePath (Swagger 2); corpo de exemplo
// simples gerado do requestBody/parameters.
OpenApiParseResult parseOpenApi(const QString &jsonText);

} // namespace kai::core
