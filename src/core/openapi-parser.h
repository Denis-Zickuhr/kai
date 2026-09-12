#pragma once

#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Um endpoint importado de uma spec OpenAPI, já pronto para virar um
// Command HTTP do Kai.
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

// Faz o parse de uma spec OpenAPI 3.x / Swagger 2.0 em JSON e extrai os
// endpoints (paths x métodos) como HttpConfigs. A URL base vem de
// servers[0].url (OpenAPI 3) ou host+basePath (Swagger 2). Um corpo de
// exemplo simples é gerado a partir do requestBody/parameters quando
// disponível. YAML não é suportado nesta versão (apenas JSON).
OpenApiParseResult parseOpenApi(const QString &jsonText);

} // namespace kai::core
