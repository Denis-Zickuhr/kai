#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace kai::engine::store_signature {

// Ed25519 (RFC 8032) para assinar/verificar o índice da Kai Store (specs/14-store.md §9). Implementação própria e
// pequena (porte do TweetNaCl, domínio público) sobre o SHA-512 do Qt: evita uma dependência nova. Não é código de
// tempo constante para segredos de longa vida; serve ao publicador offline e à verificação (sem segredo) no app.

struct KeyPair {
    QByteArray secretKey; // semente de 32 bytes
    QByteArray publicKey; // 32 bytes
};

KeyPair generateKeyPair();
QByteArray publicKeyFromSecret(const QByteArray &secretSeed);
QByteArray sign(const QByteArray &message, const QByteArray &secretSeed); // 64 bytes
bool verify(const QByteArray &message, const QByteArray &signature, const QByteArray &publicKey);

// Corpo canônico do índice: o objeto SEM a chave "signature", JSON compacto, chaves ordenadas por ponto de código,
// UTF-8 cru (só `"`, `\\` e controles escapados), inteiros sem ponto. Independente da ordem interna do Qt, para que
// qualquer ferramenta consiga reproduzir (ex.: Python json.dumps(sort_keys=True, separators=(',', ':'), ensure_ascii=False)).
QByteArray canonicalBody(const QJsonObject &indexObject);

// Base64 de assinatura/chave pública <-> bytes (vazio se inválido).
QString toBase64(const QByteArray &bytes);
QByteArray fromBase64(const QString &text);

enum class Result { NoKeyDeclared, Ok, Invalid, Missing };
// `publicKeyBase64`: a chave declarada no cabeçalho do provedor. `indexObject`: o índice com o campo "signature".
Result verifyIndex(const QJsonObject &indexObject, const QString &publicKeyBase64);

} // namespace kai::engine::store_signature
