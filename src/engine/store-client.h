#pragma once

#include <functional>
#include <memory>

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "core/store-model.h"
#include "engine/store-git.h"
#include "engine/store-source.h"

namespace kai::engine {

// Limites duros (specs/14-store.md §9).
namespace store_limits {
constexpr qint64 kCatalogBytes = 8 * 1024 * 1024;
constexpr qint64 kManifestBytes = 256 * 1024;
constexpr qint64 kFileBytes = 5 * 1024 * 1024;
constexpr qint64 kMediaBytes = 2 * 1024 * 1024;
constexpr int kMaxFiles = 500;
constexpr qint64 kPackageBytes = 25 * 1024 * 1024;
} // namespace store_limits

enum class StoreSignatureState {
    None,       // o provedor não declara chave
    Ok,
    Invalid,    // assinatura não confere: bloqueia a instalação
};

struct StoreCatalogResult {
    bool ok = false;
    core::StoreCatalog catalog;
    StoreSignatureState signature = StoreSignatureState::None;
    int skippedRows = 0;
    // Código estável ("not_found", "store_format_newer", "git_missing"...), traduzido por quem exibe.
    QString error;
    QString errorDetail;
    bool needsUpdate = false;
};

struct StoreManifestResult {
    bool ok = false;
    core::StoreManifest manifest;
    QVector<core::StoreManifestIssue> issues;
    QString error;
};

// Lê o catálogo de UM provedor configurado e entrega os arquivos dele (manifestos, payload, mídia).
// Os três tipos expõem o mesmo contrato lógico; só muda de onde os documentos vêm.
class StoreClient : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString cacheDir;
        // Teste/espelho: base "raw" no lugar da derivada da URL do repositório.
        QString rawBaseOverride;
    };

    explicit StoreClient(const core::StoreProviderConfig &config, const Options &options, QObject *parent = nullptr);

    const core::StoreProviderConfig &config() const { return m_config; }

    void loadCatalog(std::function<void(const StoreCatalogResult &)> done);
    // `manifestPath`: o caminho do package.yml como está no índice.
    void fetchManifest(const core::StoreVersionEntry &version, std::function<void(const StoreManifestResult &)> done);
    // Pasta do manifesto (sem barra final) — base dos caminhos de `files[]`.
    static QString manifestDir(const QString &manifestPath);
    void fetch(const QString &path, qint64 maxBytes, StoreFetchCallback done);
    void cancel();

    // "https://raw.githubusercontent.com/o/r/HEAD/" para repositórios de forges conhecidos; vazio se não souber.
    static QString rawBaseFor(const QString &repoUrl);
    static QString repoUrlWithoutRef(const QString &repoUrl);
    static QString repoRef(const QString &repoUrl);
    // YAML ou JSON -> objeto. Vazio e `error` preenchido se inválido.
    static QJsonObject parseDocument(const QByteArray &data, QString *error = nullptr);

private:
    using CatalogDone = std::function<void(const StoreCatalogResult &)>;

    void loadFile(CatalogDone done);
    void loadRepo(CatalogDone done);
    void loadRepoByClone(const QString &rawError, CatalogDone done);
    void loadApi(CatalogDone done);
    // Lê o cabeçalho (store.yml) + o índice de uma fonte; `prefix` é a subpasta ("" ou "dist/").
    void readSplitCatalog(const QString &prefix, const QString &headerPath, const QString &indexOverride,
                          CatalogDone done);
    void readFileCatalog(const QString &name, CatalogDone done);
    void finishCatalog(StoreCatalogResult result, CatalogDone done, const QJsonObject &signedObject = {});

    core::StoreProviderConfig m_config;
    Options m_options;
    StoreSource *m_source = nullptr;
    QString m_prefix;
    bool m_requireManifestHash = false;
    StoreGitCheckout *m_git = nullptr;
};

} // namespace kai::engine
