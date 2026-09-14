#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

#include "core/environment-manager.h"
#include "core/http-resolve.h"

namespace kai::core {

// A requisição pronta para sair: tudo interpolado, autenticação aplicada,
// query montada e Content-Type automático definido. Puramente lógico (sem
// rede) — o HttpRunner envia e o exportador (curl/código) descreve a MESMA
// requisição, então os dois nunca divergem.
struct MaterializedHttpRequest {
    QString method;
    QUrl url;
    // Na ordem em que vão; headers repetidos continuam repetidos (quem envia junta).
    QList<QPair<QString, QString>> headers;

    // Como o corpo vai: Raw/Json/Xml/Form/GraphQl já viraram `payload` (bytes);
    // Multipart usa `fields`; File usa `bodyFile`.
    HttpBodyType bodyType = HttpBodyType::None;
    QByteArray payload;
    QVector<HttpEntry> fields; // Multipart: nomes/valores interpolados
    QString bodyFile;          // File: caminho interpolado
    // Resumo do corpo para mostrar na aba Request.
    QString bodyPreview;

    // Preenchido quando a requisição não pode ser montada (ex.: variáveis do GraphQL inválidas).
    QString errorKey;
};

MaterializedHttpRequest materializeHttpRequest(const HttpRequestSpec &spec, const EnvironmentManager &env);

// Valor de um header, sem diferenciar maiúsculas; vazio se não houver.
QString materializedHeader(const MaterializedHttpRequest &request, const QString &name);

} // namespace kai::core
