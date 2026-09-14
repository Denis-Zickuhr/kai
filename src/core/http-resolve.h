#pragma once

#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Requisição HTTP RESOLVIDA: o comando + o que as pastas acima oferecem (headers, autenticação,
// opções de conexão). Puramente lógico (sem rede, sem I/O, sem interpolar {{VAR}}: quem envia
// interpola). ÚNICA fonte da regra de herança (interface, pipeline e modo local da CLI):
//  - Headers: da raiz para o comando; um nome (sem diferenciar maiúsculas) num nível substitui TODOS
//    os anteriores de mesmo nome; uma linha desligada num nível mais próximo "apaga" o herdado.
//  - Autenticação: a do comando, se não for Inherit; senão a primeira decisão subindo as pastas;
//    sem nenhuma, não autentica. None corta.
//  - Opções: campo a campo, o nível mais próximo que define vence.

// Cadeia de pastas da raiz até `folderId` (inclusive). Ciclos e ids soltos são
// ignorados.
QVector<Folder> folderChain(const QString &folderId, const QVector<Folder> &folders);

// A pasta (ou alguma subpasta) tem pelo menos um comando HTTP: só então a edição rápida de HTTP da pasta faz sentido.
bool folderHasHttpCommand(const QString &folderId, const QVector<Folder> &folders, const QVector<Command> &commands);

struct HttpRequestSpec {
    QString method;
    QString url;
    QVector<HttpEntry> headers;     // só as ligadas, já mescladas
    QVector<HttpEntry> queryParams; // só as ligadas
    HttpAuth auth;                  // efetiva — nunca Inherit
    HttpBodyType bodyType = HttpBodyType::Raw;
    QString body;
    QString graphqlVariables;
    QVector<HttpEntry> formFields;  // só as ligadas
    QString bodyFile;
    HttpEffectiveOptions options;
    QString responseFile;
    QVector<EnvExtractor> envExtractors;
    QVector<ExecutionCondition> assertions;
};

// `chain` = cadeia de pastas do comando, raiz primeiro (vazia = comando solto).
HttpRequestSpec resolveHttpRequest(const HttpConfig &config, const QVector<Folder> &chain);

// Mescla camadas de headers (a primeira é a mais distante) e devolve só o resultado ligado.
QVector<HttpEntry> mergeHttpHeaders(const QVector<QVector<HttpEntry>> &layers);

// Um header que o comando recebe de uma pasta, para a interface mostrar.
struct InheritedHttpHeader {
    HttpEntry entry;
    QString folderId;
    QString folderName;
};

// Headers que as pastas da cadeia entregam (já mesclados entre si, sem os
// desligados), cada um com a pasta de onde vem.
QVector<InheritedHttpHeader> inheritedHttpHeaders(const QVector<Folder> &chain);

// Autenticação efetiva herdada só das pastas (o que o comando recebe se estiver em Inherit) e a
// pasta de origem (vazio = nenhuma).
HttpAuth inheritedHttpAuth(const QVector<Folder> &chain, QString *originFolderName = nullptr);

} // namespace kai::core
