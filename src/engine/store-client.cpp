#include "engine/store-client.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

#include "core/yaml-bridge.h"
#include "engine/store-signature.h"

namespace kai::engine {

namespace {

StoreCatalogResult failed(const QString &code, const QString &detail = QString(), bool needsUpdate = false)
{
    StoreCatalogResult r;
    r.error = code;
    r.errorDetail = detail;
    r.needsUpdate = needsUpdate;
    return r;
}

} // namespace

StoreClient::StoreClient(const core::StoreProviderConfig &config, const Options &options, QObject *parent)
    : QObject(parent), m_config(config), m_options(options)
{
}

QJsonObject StoreClient::parseDocument(const QByteArray &data, QString *error)
{
    const QString text = QString::fromUtf8(data);
    QString json = text;
    if (!core::looksLikeJson(text)) {
        bool ok = false;
        QString message;
        json = core::yamlTextToJsonText(text, &ok, &message);
        if (!ok) {
            if (error) *error = message.isEmpty() ? QStringLiteral("bad_document") : message;
            return {};
        }
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("bad_document");
        return {};
    }
    return doc.object();
}

QString StoreClient::repoRef(const QString &repoUrl)
{
    const int hash = repoUrl.indexOf(QLatin1Char('#'));
    return hash < 0 ? QString() : repoUrl.mid(hash + 1);
}

QString StoreClient::repoUrlWithoutRef(const QString &repoUrl)
{
    QString url = repoUrl.trimmed();
    const int hash = url.indexOf(QLatin1Char('#'));
    if (hash >= 0) url = url.left(hash);
    // "github.com/o/r" sem esquema vira https.
    static const QRegularExpression bare(QStringLiteral(R"(^(github\.com|gitlab\.com|codeberg\.org)/)"));
    if (bare.match(url).hasMatch()) {
        url = QStringLiteral("https://") + url;
    }
    return url;
}

QString StoreClient::rawBaseFor(const QString &repoUrl)
{
    const QUrl url(repoUrlWithoutRef(repoUrl));
    const QString host = url.host().toLower();
    QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        return {};
    }
    QString repo = parts[1];
    if (repo.endsWith(QLatin1String(".git"))) repo.chop(4);
    const QString ref = repoRef(repoUrl).isEmpty() ? QStringLiteral("HEAD") : repoRef(repoUrl);
    if (host == QLatin1String("github.com")) {
        return QStringLiteral("https://raw.githubusercontent.com/%1/%2/%3/").arg(parts[0], repo, ref);
    }
    if (host == QLatin1String("gitlab.com")) {
        return QStringLiteral("https://gitlab.com/%1/%2/-/raw/%3/").arg(parts[0], repo, ref);
    }
    return {};
}

QString StoreClient::manifestDir(const QString &manifestPath)
{
    const int slash = manifestPath.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : manifestPath.left(slash);
}

void StoreClient::cancel()
{
    if (m_source) m_source->cancelAll();
    if (m_git) m_git->cancel();
}

void StoreClient::fetch(const QString &path, qint64 maxBytes, StoreFetchCallback done)
{
    if (!m_source) {
        StoreFetchResult r;
        r.error = QStringLiteral("not_loaded");
        done(r);
        return;
    }
    m_source->fetch(m_prefix + path, maxBytes, std::move(done));
}

void StoreClient::finishCatalog(StoreCatalogResult result, CatalogDone done, const QJsonObject &signedObject)
{
    if (result.ok) {
        switch (store_signature::verifyIndex(signedObject, result.catalog.header.signingKey)) {
        case store_signature::Result::NoKeyDeclared: result.signature = StoreSignatureState::None; break;
        case store_signature::Result::Ok: result.signature = StoreSignatureState::Ok; break;
        case store_signature::Result::Invalid:
        case store_signature::Result::Missing: result.signature = StoreSignatureState::Invalid; break;
        }
        m_requireManifestHash = !result.catalog.header.signingKey.isEmpty();
    }
    done(result);
}

void StoreClient::loadCatalog(CatalogDone done)
{
    if (m_source) {
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_prefix.clear();
    switch (m_config.kind) {
    case core::StoreProviderKind::File: loadFile(std::move(done)); break;
    case core::StoreProviderKind::Repo: loadRepo(std::move(done)); break;
    case core::StoreProviderKind::Api: loadApi(std::move(done)); break;
    }
}

// --- file -------------------------------------------------------------------------------------------------------

void StoreClient::loadFile(CatalogDone done)
{
    const QString location = m_config.location.trimmed();
    const QUrl url(location);
    QString name;
    if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) {
        QUrl base = url;
        name = QFileInfo(url.path()).fileName();
        base.setPath(url.path().left(url.path().size() - name.size()));
        m_source = new HttpStoreSource(base, m_config.token, m_options.cacheDir, this);
    } else {
        const QString path = url.isLocalFile() ? url.toLocalFile() : location;
        const QFileInfo info(path);
        name = info.fileName();
        m_source = new LocalStoreSource(info.absolutePath(), this);
    }
    readFileCatalog(name, std::move(done));
}

void StoreClient::readFileCatalog(const QString &name, CatalogDone done)
{
    m_source->fetch(name, store_limits::kCatalogBytes, [this, done](const StoreFetchResult &r) {
        if (!r.ok) {
            finishCatalog(failed(r.error), done);
            return;
        }
        QString parseError;
        const QJsonObject obj = parseDocument(r.data, &parseError);
        if (obj.isEmpty()) {
            finishCatalog(failed(QStringLiteral("bad_document"), parseError), done);
            return;
        }
        core::StoreParseError err;
        int skipped = 0;
        const auto catalog = core::parseStoreCatalogFile(obj, &err, &skipped);
        if (!catalog) {
            finishCatalog(failed(err.message, QString(), err.needsUpdate), done);
            return;
        }
        StoreCatalogResult result;
        result.ok = true;
        result.catalog = *catalog;
        result.skippedRows = skipped;
        finishCatalog(result, done, obj);
    });
}

// --- repo / api (cabeçalho + índice separados) ------------------------------------------------------------------

void StoreClient::readSplitCatalog(const QString &prefix, const QString &headerPath, const QString &indexOverride,
                                   CatalogDone done)
{
    m_prefix = prefix;
    m_source->fetch(prefix + headerPath, store_limits::kCatalogBytes, [this, prefix, indexOverride, done](const StoreFetchResult &h) {
        if (!h.ok) {
            finishCatalog(failed(h.error), done);
            return;
        }
        QString parseError;
        const QJsonObject headerObj = parseDocument(h.data, &parseError);
        core::StoreParseError err;
        const auto header = headerObj.isEmpty() ? std::nullopt : core::parseStoreHeader(headerObj, &err);
        if (!header) {
            finishCatalog(failed(headerObj.isEmpty() ? QStringLiteral("bad_document") : err.message, parseError,
                                 err.needsUpdate),
                          done);
            return;
        }
        const QString indexPath = indexOverride.isEmpty() ? header->indexPath : indexOverride;
        if (!core::isSafeStorePath(indexPath.section(QLatin1Char('?'), 0, 0))) {
            finishCatalog(failed(QStringLiteral("unsafe_path")), done);
            return;
        }
        const core::StoreHeader headerCopy = *header;
        m_source->fetch(prefix + indexPath, store_limits::kCatalogBytes, [this, headerCopy, done](const StoreFetchResult &i) {
            if (!i.ok) {
                finishCatalog(failed(i.error), done);
                return;
            }
            QString indexError;
            const QJsonObject indexObj = parseDocument(i.data, &indexError);
            core::StoreParseError err;
            int skipped = 0;
            auto catalog = indexObj.isEmpty() ? std::nullopt : core::parseStoreIndex(indexObj, &err, &skipped);
            if (!catalog) {
                finishCatalog(failed(indexObj.isEmpty() ? QStringLiteral("bad_document") : err.message, indexError,
                                     err.needsUpdate),
                              done);
                return;
            }
            catalog->header = headerCopy;
            StoreCatalogResult result;
            result.ok = true;
            result.catalog = *catalog;
            result.skippedRows = skipped;
            finishCatalog(result, done, indexObj);
        });
    });
}

void StoreClient::loadRepo(CatalogDone done)
{
    const QString base = m_options.rawBaseOverride.isEmpty() ? rawBaseFor(m_config.location)
                                                             : m_options.rawBaseOverride;
    if (base.isEmpty()) {
        loadRepoByClone(QStringLiteral("no_raw"), std::move(done));
        return;
    }
    m_source = new HttpStoreSource(QUrl(base), m_config.token, m_options.cacheDir, this);
    // O catálogo fica na raiz do repositório ou numa pasta de distribuição (dist/).
    readSplitCatalog(QString(), QStringLiteral("store.yml"), QString(), [this, done](const StoreCatalogResult &root) {
        if (root.ok || root.error != QLatin1String("not_found")) {
            if (!root.ok && root.error != QLatin1String("not_found")) {
                loadRepoByClone(root.error, done);
                return;
            }
            done(root);
            return;
        }
        readSplitCatalog(QStringLiteral("dist/"), QStringLiteral("store.yml"), QString(),
                         [this, done](const StoreCatalogResult &dist) {
                             if (dist.ok || dist.error == QLatin1String("not_found")) {
                                 done(dist);
                                 return;
                             }
                             loadRepoByClone(dist.error, done);
                         });
    });
}

void StoreClient::loadRepoByClone(const QString &rawError, CatalogDone done)
{
    // Caso o "raw" não exista ou falhe, cai para um checkout raso (se houver git).
    if (StoreGitCheckout::gitExecutable().isEmpty()) {
        done(failed(rawError == QLatin1String("no_raw") ? QStringLiteral("git_missing") : rawError));
        return;
    }
    if (m_source) {
        m_source->deleteLater();
        m_source = nullptr;
    }
    const QString cacheRoot = m_options.cacheDir.isEmpty() ? QDir::temp().filePath(QStringLiteral("kai-store-cache"))
                                                           : m_options.cacheDir;
    const QString dest = QDir(cacheRoot).filePath(QStringLiteral("checkout"));
    if (!m_git) m_git = new StoreGitCheckout(this);
    m_git->checkout(repoUrlWithoutRef(m_config.location), repoRef(m_config.location), dest,
                    [this, dest, done](const QString &error) {
        if (!error.isEmpty()) {
            const QString code = error.section(QLatin1Char(':'), 0, 0);
            finishCatalog(failed(code, error.section(QLatin1Char(':'), 1)), done);
            return;
        }
        m_source = new LocalStoreSource(dest, this);
        readSplitCatalog(QString(), QStringLiteral("store.yml"), QString(), [this, done](const StoreCatalogResult &root) {
            if (root.ok || root.error != QLatin1String("not_found")) {
                done(root);
                return;
            }
            readSplitCatalog(QStringLiteral("dist/"), QStringLiteral("store.yml"), QString(), done);
        });
    });
}

// --- api --------------------------------------------------------------------------------------------------------

void StoreClient::loadApi(CatalogDone done)
{
    const QUrl base(m_config.location.trimmed());
    m_source = new HttpStoreSource(base, m_config.token, m_options.cacheDir, this);
    readSplitCatalog(QString(), QStringLiteral("v1/store"), QStringLiteral("v1/index"), std::move(done));
}

// --- manifesto --------------------------------------------------------------------------------------------------

void StoreClient::fetchManifest(const core::StoreVersionEntry &version,
                                std::function<void(const StoreManifestResult &)> done)
{
    StoreManifestResult bad;
    if (!core::isSafeStorePath(version.manifest.section(QLatin1Char('?'), 0, 0))) {
        bad.error = QStringLiteral("unsafe_path");
        done(bad);
        return;
    }
    const QString expected = version.sha256;
    if (m_requireManifestHash && expected.isEmpty()) {
        bad.error = QStringLiteral("missing_manifest_hash");
        done(bad);
        return;
    }
    fetch(version.manifest, store_limits::kManifestBytes, [done, expected](const StoreFetchResult &r) {
        StoreManifestResult result;
        if (!r.ok) {
            result.error = r.error;
            done(result);
            return;
        }
        if (!expected.isEmpty()) {
            const QString actual = QString::fromLatin1(QCryptographicHash::hash(r.data, QCryptographicHash::Sha256).toHex());
            if (actual != expected.toLower()) {
                result.error = QStringLiteral("hash_mismatch");
                done(result);
                return;
            }
        }
        QString parseError;
        const QJsonObject obj = parseDocument(r.data, &parseError);
        if (obj.isEmpty()) {
            result.error = QStringLiteral("bad_document");
            done(result);
            return;
        }
        const auto manifest = core::parseStoreManifest(obj, &result.issues);
        if (!manifest) {
            result.error = QStringLiteral("bad_manifest");
            done(result);
            return;
        }
        result.manifest = *manifest;
        result.issues += core::validateStoreManifest(*manifest);
        result.ok = !core::hasStoreErrors(result.issues);
        if (!result.ok) result.error = QStringLiteral("invalid_manifest");
        done(result);
    });
}

} // namespace kai::engine
