#include "engine/store-source.h"

#include <memory>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QThreadPool>

#include "core/store-model.h"

namespace kai::engine {

void storeRunAsync(QObject *context, std::function<void()> work, std::function<void()> done)
{
    QPointer<QObject> guard(context);
    QThreadPool::globalInstance()->start([guard, work = std::move(work), done = std::move(done)]() {
        work();
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, done]() { if (guard) done(); }, Qt::QueuedConnection);
    });
}

// --- LocalStoreSource -------------------------------------------------------------------------------------------

LocalStoreSource::LocalStoreSource(const QString &baseDir, QObject *parent)
    : StoreSource(parent), m_baseDir(QDir::cleanPath(baseDir))
{
}

StoreFetchResult LocalStoreSource::readConfined(const QString &baseDir, const QString &relPath, qint64 maxBytes)
{
    StoreFetchResult r;
    if (!core::isSafeStorePath(relPath)) {
        r.error = QStringLiteral("unsafe_path");
        return r;
    }
    const QString base = QFileInfo(baseDir).canonicalFilePath();
    const QString target = QFileInfo(QDir(baseDir).filePath(relPath)).canonicalFilePath();
    if (target.isEmpty()) {
        r.notFound = true;
        r.error = QStringLiteral("not_found");
        return r;
    }
    if (base.isEmpty() || !(target == base || target.startsWith(base + QLatin1Char('/')))) {
        r.error = QStringLiteral("off_base");
        return r;
    }
    QFile file(target);
    if (!QFileInfo(target).isFile() || !file.open(QIODevice::ReadOnly)) {
        r.notFound = true;
        r.error = QStringLiteral("not_found");
        return r;
    }
    if (file.size() > maxBytes) {
        r.error = QStringLiteral("too_large");
        return r;
    }
    r.data = file.readAll();
    r.ok = true;
    return r;
}

void LocalStoreSource::fetch(const QString &relPath, qint64 maxBytes, StoreFetchCallback done)
{
    auto result = std::make_shared<StoreFetchResult>();
    const QString base = m_baseDir;
    storeRunAsync(
        this, [result, base, relPath, maxBytes]() { *result = readConfined(base, relPath, maxBytes); },
        [result, done = std::move(done)]() { done(*result); });
}

// --- HttpStoreSource --------------------------------------------------------------------------------------------

HttpStoreSource::HttpStoreSource(const QUrl &baseUrl, const QString &token, const QString &cacheDir, QObject *parent)
    : StoreSource(parent), m_base(baseUrl), m_token(token), m_cacheDir(cacheDir), m_nam(this)
{
    QString path = m_base.path();
    if (!path.endsWith(QLatin1Char('/'))) {
        m_base.setPath(path + QLatin1Char('/'));
    }
}

bool HttpStoreSource::isAllowedUrl(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https")) {
        return true;
    }
    const QString host = url.host().toLower();
    return scheme == QLatin1String("http")
        && (host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1"));
}

void HttpStoreSource::cancelAll()
{
    const auto replies = findChildren<QNetworkReply *>(QString(), Qt::FindDirectChildrenOnly);
    for (QNetworkReply *reply : replies) {
        reply->abort();
    }
}

void HttpStoreSource::fetch(const QString &relPath, qint64 maxBytes, StoreFetchCallback done)
{
    const QUrl url = m_base.resolved(QUrl(relPath));
    StoreFetchResult bad;
    if (!isAllowedUrl(url)) {
        bad.error = QStringLiteral("insecure_url");
        done(bad);
        return;
    }
    if (url.host() != m_base.host() || url.port() != m_base.port()) {
        bad.error = QStringLiteral("off_host");
        done(bad);
        return;
    }
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex());
    if (m_cacheDir.isEmpty()) {
        send(url, maxBytes, 5, {}, {}, QString(), std::move(done));
        return;
    }
    // O cache (ETag + corpo) é lido fora da thread da GUI.
    auto etag = std::make_shared<QByteArray>();
    auto body = std::make_shared<QByteArray>();
    const QString dir = m_cacheDir;
    storeRunAsync(
        this,
        [etag, body, dir, key]() {
            QFile meta(QDir(dir).filePath(key + QStringLiteral(".etag")));
            QFile data(QDir(dir).filePath(key + QStringLiteral(".body")));
            if (meta.open(QIODevice::ReadOnly) && data.open(QIODevice::ReadOnly)) {
                *etag = meta.readAll().trimmed();
                *body = data.readAll();
            }
        },
        [this, url, maxBytes, etag, body, key, done = std::move(done)]() {
            send(url, maxBytes, 5, *etag, *body, key, done);
        });
}

void HttpStoreSource::send(const QUrl &url, qint64 maxBytes, int redirectsLeft, const QByteArray &cachedEtag,
                           const QByteArray &cachedBody, const QString &cacheKey, StoreFetchCallback done)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(m_timeoutMs);
    request.setRawHeader("User-Agent", "Kai-Store");
    // O token só acompanha pedidos ao host do provedor (a base).
    if (!m_token.isEmpty() && url.host() == m_base.host() && url.port() == m_base.port()) {
        request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    }
    if (!cachedEtag.isEmpty()) {
        request.setRawHeader("If-None-Match", cachedEtag);
    }
    QNetworkReply *reply = m_nam.get(request);
    reply->setParent(this);

    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes, tooLarge](qint64 received, qint64 total) {
        if (received > maxBytes || total > maxBytes) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, url, maxBytes, redirectsLeft, cachedEtag, cachedBody, cacheKey, tooLarge, done]() {
        reply->deleteLater();
        StoreFetchResult r;
        if (*tooLarge) {
            r.error = QStringLiteral("too_large");
            done(r);
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400 && status != 304) {
            const QUrl target = url.resolved(reply->header(QNetworkRequest::LocationHeader).toUrl());
            if (redirectsLeft <= 0 || !target.isValid()) {
                r.error = QStringLiteral("too_many_redirects");
            } else if (!isAllowedUrl(target)) {
                r.error = QStringLiteral("insecure_url");
            } else if (target.host() != m_base.host() || target.port() != m_base.port()) {
                r.error = QStringLiteral("off_host");
            } else {
                send(target, maxBytes, redirectsLeft - 1, cachedEtag, cachedBody, cacheKey, done);
                return;
            }
            done(r);
            return;
        }
        if (status == 304 && !cachedBody.isEmpty()) {
            r.ok = true;
            r.data = cachedBody;
            done(r);
            return;
        }
        if (reply->error() == QNetworkReply::OperationCanceledError
            || reply->error() == QNetworkReply::TimeoutError) {
            r.error = QStringLiteral("timeout");
        } else if (status == 404) {
            r.notFound = true;
            r.error = QStringLiteral("not_found");
        } else if (status >= 400) {
            r.error = QStringLiteral("http_%1").arg(status);
        } else if (reply->error() != QNetworkReply::NoError) {
            r.error = QStringLiteral("network");
        } else {
            r.data = reply->readAll();
            if (r.data.size() > maxBytes) {
                r.data.clear();
                r.error = QStringLiteral("too_large");
            } else {
                r.ok = true;
            }
        }
        if (r.ok && !m_cacheDir.isEmpty() && !cacheKey.isEmpty()) {
            const QByteArray etag = reply->rawHeader("ETag");
            const QString dir = m_cacheDir;
            const QByteArray body = r.data;
            if (!etag.isEmpty()) {
                storeRunAsync(this, [dir, cacheKey, etag, body]() {
                    QDir().mkpath(dir);
                    QSaveFile meta(QDir(dir).filePath(cacheKey + QStringLiteral(".etag")));
                    QSaveFile data(QDir(dir).filePath(cacheKey + QStringLiteral(".body")));
                    if (meta.open(QIODevice::WriteOnly) && data.open(QIODevice::WriteOnly)) {
                        meta.write(etag);
                        data.write(body);
                        meta.commit();
                        data.commit();
                    }
                }, [] {});
            }
        }
        done(r);
    });
}

} // namespace kai::engine
