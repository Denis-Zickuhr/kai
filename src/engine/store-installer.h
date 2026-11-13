#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QString>

#include "core/kai-file-validator.h"
#include "core/store-model.h"
#include "core/store-preview.h"
#include "engine/store-client.h"

namespace kai::engine {

// Resultado do "buscar + verificar" (specs/14-store.md §5, passos 4-5): o conteúdo está em `dir`, conferido, validado e
// com a prévia pronta. Nada foi importado ainda; quem importa chama `StoreInstaller::discard` depois.
struct StoreStagedPackage {
    bool ok = false;
    QString error;       // "hash_mismatch", "unsafe_path", "too_large", "too_many_files", "cancelled", "invalid_kai_yml"...
    QString errorDetail; // ex.: o caminho do arquivo que falhou
    core::StoreManifest manifest;
    QString dir;         // pasta com o payload (kai.yml dentro dela)
    QString kaiFilePath; // o kai.yml dentro de `dir` (em clone por referência, dentro do checkout)
    core::ValidationResult validation;
    core::StorePreview preview;
    bool refMoves = false; // clone por referência com ref que não parece tag/commit
};

class StoreInstaller : public QObject {
    Q_OBJECT

public:
    explicit StoreInstaller(StoreClient *client, QObject *parent = nullptr);

    // `stagingRoot`: pasta (criada se faltar) sob a qual nasce uma subpasta única para esta instalação.
    void stage(const core::StoreManifest &manifest, const QString &manifestPath, const QString &stagingRoot,
               std::function<void(const StoreStagedPackage &)> done);
    void cancel();

    static void discard(const StoreStagedPackage &staged);
    // sha256 em hex do conteúdo bruto (para testes e para `kai store pack`).
    static QString sha256Hex(const QByteArray &data);
    // Heurística: o `ref` parece tag de versão ou commit (reprodutível)?
    static bool refLooksPinned(const QString &ref);

signals:
    void progress(int done, int total);

private:
    void fetchNext();
    void finishBundled();
    void finishWith(const QString &error, const QString &detail = QString());
    void stageClone();
    void analyse(StoreStagedPackage &staged);

    QPointer<StoreClient> m_client; // o cliente pode morrer (troca de provedor) antes do instalador
    core::StoreManifest m_manifest;
    QString m_manifestDir;
    QString m_dir;
    int m_index = 0;
    qint64 m_totalBytes = 0;
    bool m_cancelled = false;
    bool m_done = true;
    std::function<void(const StoreStagedPackage &)> m_callback;
    StoreGitCheckout *m_git = nullptr;
};

} // namespace kai::engine
