#pragma once

#include <functional>

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkReply;

namespace kai::engine {

// Kai Store (specs/14-store.md §2/§9): de onde vêm os documentos de um provedor. Tudo assíncrono; o callback roda na
// thread do objeto que o criou.

struct StoreFetchResult {
    bool ok = false;
    bool notFound = false;
    QByteArray data;
    // Código estável sem tradução ("not_found", "too_large", "insecure_url", "off_host", "timeout", "http_<n>"...).
    QString error;
};
using StoreFetchCallback = std::function<void(const StoreFetchResult &)>;

class StoreSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // `relPath`: caminho relativo à base da fonte ("packages/a/b/1.0.0/package.yml"). `maxBytes`: teto da resposta.
    virtual void fetch(const QString &relPath, qint64 maxBytes, StoreFetchCallback done) = 0;
    // A base já resolvida (URL ou pasta), para exibir em mensagens.
    virtual QString baseLabel() const = 0;
    virtual void cancelAll() {}
};

// Pasta no disco (provedor `file` local e checkout de um `repo`). Nunca sai da pasta: `..` e links simbólicos que
// apontam para fora são recusados.
class LocalStoreSource : public StoreSource {
    Q_OBJECT

public:
    explicit LocalStoreSource(const QString &baseDir, QObject *parent = nullptr);

    void fetch(const QString &relPath, qint64 maxBytes, StoreFetchCallback done) override;
    QString baseLabel() const override { return m_baseDir; }

    // Síncrono, para rodar numa thread de trabalho.
    static StoreFetchResult readConfined(const QString &baseDir, const QString &relPath, qint64 maxBytes);

private:
    QString m_baseDir;
};

// Rede: só HTTPS (http só em loopback, para testes e provedores locais), sem sair do host da base, com teto de
// tamanho e de tempo, e o token só vai para o host do provedor. Cache por ETag em `cacheDir` (opcional).
class HttpStoreSource : public StoreSource {
    Q_OBJECT

public:
    HttpStoreSource(const QUrl &baseUrl, const QString &token, const QString &cacheDir, QObject *parent = nullptr);

    void fetch(const QString &relPath, qint64 maxBytes, StoreFetchCallback done) override;
    QString baseLabel() const override { return m_base.toString(); }
    void cancelAll() override;

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    static bool isAllowedUrl(const QUrl &url);

private:
    void send(const QUrl &url, qint64 maxBytes, int redirectsLeft, const QByteArray &cachedEtag,
              const QByteArray &cachedBody, const QString &cacheKey, StoreFetchCallback done);

    QUrl m_base;
    QString m_token;
    QString m_cacheDir;
    int m_timeoutMs = 20000;
    QNetworkAccessManager m_nam;
};

// Roda `work` numa thread do pool e entrega o resultado no objeto `context` (fila de eventos). Se `context` morrer
// antes, o resultado é descartado.
void storeRunAsync(QObject *context, std::function<void()> work, std::function<void()> done);

} // namespace kai::engine
