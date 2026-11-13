#include "engine/store-installer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include "core/yaml-bridge.h"

namespace kai::engine {

StoreInstaller::StoreInstaller(StoreClient *client, QObject *parent) : QObject(parent), m_client(client) {}

QString StoreInstaller::sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool StoreInstaller::refLooksPinned(const QString &ref)
{
    static const QRegularExpression tag(QStringLiteral(R"(^v?\d+\.\d+(\.\d+)?([-+][0-9A-Za-z.-]+)?$)"));
    static const QRegularExpression sha(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    return tag.match(ref).hasMatch() || sha.match(ref).hasMatch();
}

void StoreInstaller::discard(const StoreStagedPackage &staged)
{
    if (!staged.dir.isEmpty()) {
        QDir(staged.dir).removeRecursively();
    }
}

void StoreInstaller::cancel()
{
    m_cancelled = true;
    if (m_client) m_client->cancel();
    if (m_git) m_git->cancel();
}

void StoreInstaller::finishWith(const QString &error, const QString &detail)
{
    if (m_done) {
        return;
    }
    m_done = true;
    StoreStagedPackage staged;
    staged.manifest = m_manifest;
    staged.error = error;
    staged.errorDetail = detail;
    staged.dir = m_dir;
    auto callback = std::move(m_callback);
    m_callback = nullptr;
    if (!error.isEmpty()) {
        discard(staged);
        staged.dir.clear();
    }
    if (callback) {
        callback(staged);
    }
}

void StoreInstaller::stage(const core::StoreManifest &manifest, const QString &manifestPath, const QString &stagingRoot,
                           std::function<void(const StoreStagedPackage &)> done)
{
    m_manifest = manifest;
    m_manifestDir = StoreClient::manifestDir(manifestPath);
    m_callback = std::move(done);
    m_cancelled = false;
    m_done = false;
    m_index = 0;
    m_totalBytes = 0;
    m_dir = QDir(stagingRoot).filePath(QUuid::createUuid().toString(QUuid::Id128).left(12));

    // Quem chega aqui já passou por validateStoreManifest, mas o instalador não confia: refaz as checagens que
    // protegem o disco.
    if (core::hasStoreErrors(core::validateStoreManifest(manifest))) {
        QMetaObject::invokeMethod(this, [this]() { finishWith(QStringLiteral("invalid_manifest")); }, Qt::QueuedConnection);
        return;
    }
    if (manifest.files.size() > store_limits::kMaxFiles) {
        QMetaObject::invokeMethod(this, [this]() { finishWith(QStringLiteral("too_many_files")); }, Qt::QueuedConnection);
        return;
    }
    if (!QDir().mkpath(m_dir)) {
        QMetaObject::invokeMethod(this, [this]() { finishWith(QStringLiteral("disk")); }, Qt::QueuedConnection);
        return;
    }
    if (manifest.clone) {
        stageClone();
    } else {
        fetchNext();
    }
}

void StoreInstaller::fetchNext()
{
    if (m_cancelled) {
        finishWith(QStringLiteral("cancelled"));
        return;
    }
    if (m_index >= m_manifest.files.size()) {
        finishBundled();
        return;
    }
    const core::StoreFile file = m_manifest.files[m_index];
    emit progress(m_index, m_manifest.files.size());
    const QString path = m_manifestDir.isEmpty() ? file.path : m_manifestDir + QLatin1Char('/') + file.path;
    if (!m_client) {
        finishWith(QStringLiteral("cancelled"));
        return;
    }
    m_client->fetch(path, store_limits::kFileBytes, [this, file](const StoreFetchResult &r) {
        if (m_done) {
            return;
        }
        if (m_cancelled) {
            finishWith(QStringLiteral("cancelled"));
            return;
        }
        if (!r.ok) {
            finishWith(r.error, file.path);
            return;
        }
        m_totalBytes += r.data.size();
        if (m_totalBytes > store_limits::kPackageBytes) {
            finishWith(QStringLiteral("too_large"), file.path);
            return;
        }
        if (sha256Hex(r.data) != file.sha256.toLower()) {
            finishWith(QStringLiteral("hash_mismatch"), file.path);
            return;
        }
        // O destino é sempre sob `m_dir`: o caminho já passou por isSafeStorePath, e ainda assim confere.
        const QString target = QDir::cleanPath(QDir(m_dir).filePath(file.path));
        if (!target.startsWith(QDir::cleanPath(m_dir) + QLatin1Char('/'))) {
            finishWith(QStringLiteral("unsafe_path"), file.path);
            return;
        }
        auto ok = std::make_shared<bool>(false);
        storeRunAsync(
            this,
            [ok, target, data = r.data]() {
                QDir().mkpath(QFileInfo(target).absolutePath());
                QSaveFile out(target);
                *ok = out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit();
            },
            [this, ok, file]() {
                if (m_done) return;
                if (!*ok) {
                    finishWith(QStringLiteral("disk"), file.path);
                    return;
                }
                ++m_index;
                fetchNext();
            });
    });
}

void StoreInstaller::finishBundled()
{
    emit progress(m_manifest.files.size(), m_manifest.files.size());
    StoreStagedPackage staged;
    staged.manifest = m_manifest;
    staged.dir = m_dir;
    staged.kaiFilePath = QDir(m_dir).filePath(QStringLiteral("kai.yml"));
    analyse(staged);
}

void StoreInstaller::stageClone()
{
    const core::StoreCloneSource clone = *m_manifest.clone;
    if (!m_git) m_git = new StoreGitCheckout(this);
    m_git->checkout(clone.url, clone.ref, QDir(m_dir).filePath(QStringLiteral("repo")), [this, clone](const QString &error) {
        if (m_done) return;
        if (!error.isEmpty()) {
            finishWith(error.section(QLatin1Char(':'), 0, 0), error.section(QLatin1Char(':'), 1));
            return;
        }
        StoreStagedPackage staged;
        staged.manifest = m_manifest;
        staged.dir = m_dir;
        staged.kaiFilePath = QDir(m_dir).filePath(QStringLiteral("repo/") + clone.path);
        staged.refMoves = !refLooksPinned(clone.ref);
        // A pasta do projeto é a do kai.yml dentro do checkout; o clone não leva a pasta .git junto na importação.
        const QString canonicalRepo = QFileInfo(QDir(m_dir).filePath(QStringLiteral("repo"))).canonicalFilePath();
        const QString canonicalFile = QFileInfo(staged.kaiFilePath).canonicalFilePath();
        if (canonicalFile.isEmpty() || !canonicalFile.startsWith(canonicalRepo + QLatin1Char('/'))) {
            finishWith(QStringLiteral("missing_kai_yml"), clone.path);
            return;
        }
        staged.kaiFilePath = canonicalFile;
        analyse(staged);
    });
}

void StoreInstaller::analyse(StoreStagedPackage &staged)
{
    // Lê, valida e monta a prévia. É leitura de um arquivo pequeno que acabamos de gravar, na thread de trabalho.
    auto result = std::make_shared<StoreStagedPackage>(staged);
    storeRunAsync(
        this,
        [result]() {
            QFile file(result->kaiFilePath);
            if (!file.open(QIODevice::ReadOnly) || file.size() > store_limits::kFileBytes) {
                result->error = QStringLiteral("missing_kai_yml");
                return;
            }
            const QString text = QString::fromUtf8(file.readAll());
            result->validation = core::validateKaiFileText(text);
            bool ok = false;
            const QString json = core::yamlTextToJsonText(text, &ok, nullptr);
            if (!ok) {
                result->error = QStringLiteral("invalid_kai_yml");
                return;
            }
            result->preview = core::buildStorePreview(QJsonDocument::fromJson(json.toUtf8()).object());
            if (result->validation.hasErrors()) {
                result->error = QStringLiteral("invalid_kai_yml");
                return;
            }
            result->ok = true;
        },
        [this, result]() {
            if (m_done) return;
            m_done = true;
            if (!result->error.isEmpty()) {
                // Mantém a lista de problemas da validação; só o conteúdo em disco é descartado.
                discard(*result);
                result->dir.clear();
                result->ok = false;
            }
            auto callback = std::move(m_callback);
            m_callback = nullptr;
            if (callback) callback(*result);
        });
}

} // namespace kai::engine
