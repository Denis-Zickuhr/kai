#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Uma requisição vinda de outro cliente HTTP, já no formato do Kai.
struct ImportedRequest {
    QString name;
    QStringList folderPath; // pastas por baixo da raiz do que foi importado (vazio = direto na raiz)
    HttpConfig config;
};

// Pasta com padrões próprios (autenticação/headers herdados).
struct ImportedFolder {
    QStringList path;
    HttpDefaults defaults;
};

// O que se lê de uma coleção Postman, uma exportação do Insomnia ou um arquivo HAR.
struct ImportedHttpCollection {
    bool ok = false;
    QString errorMessage;
    QString name;                       // nome sugerido para a pasta raiz
    HttpDefaults rootDefaults;          // autenticação/headers da coleção inteira
    QMap<QString, QString> variables;   // variáveis da coleção/ambiente -> env_vars da pasta raiz
    QVector<ImportedFolder> folders;    // só as pastas que têm padrões; as demais nascem pelo caminho
    QVector<ImportedRequest> requests;
};

// Coleção Postman v2.0/v2.1 (JSON).
ImportedHttpCollection parsePostmanCollection(const QString &jsonText);
// Exportação do Insomnia v4 (JSON).
ImportedHttpCollection parseInsomniaExport(const QString &jsonText);
// Arquivo HAR (JSON, gravado pelo navegador).
ImportedHttpCollection parseHar(const QString &jsonText);

// Reconhecimento pelo conteúdo (o JSON já lido).
bool looksLikePostmanCollection(const QJsonObject &root);
bool looksLikeInsomniaExport(const QJsonObject &root);
bool looksLikeHar(const QJsonObject &root);

} // namespace kai::core
