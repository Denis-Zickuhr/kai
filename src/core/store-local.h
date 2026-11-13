#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

#include "core/store-model.h"

namespace kai::core {

// Kai Store (specs/14-store.md §7): estado LOCAL. Nada daqui entra em export, sync ou kai.yml.

struct StoreInstalledRecord {
    QString provider; // id do provedor
    QString package;  // publisher/slug
    QString version;
    QString kind;
    QString contentHash; // hash do conteúdo da pasta logo depois da instalação (detecta edição do usuário)
    QString folderId;    // pasta criada (vazio para environment/profile)
    QString cloneDir;    // projeto por referência: o checkout que o Kai criou (removido ao desinstalar)
    QDateTime installedAt;
};

class StoreLocalState {
public:
    // `configDir`: a pasta de configuração do Kai.
    explicit StoreLocalState(const QString &configDir);

    // Provedores (e os tokens): store-providers.json, modo 0600. Ausente/corrompido -> lista vazia (o corrompido é
    // guardado como .corrupt).
    QVector<StoreProviderConfig> loadProviders() const;
    bool saveProviders(const QVector<StoreProviderConfig> &providers) const;

    QVector<StoreInstalledRecord> loadInstalled() const;
    bool saveInstalled(const QVector<StoreInstalledRecord> &records) const;
    // Troca o registro do mesmo (provider, package) ou acrescenta.
    bool recordInstall(const StoreInstalledRecord &record) const;
    bool removeRecordForFolder(const QString &folderId) const;
    // Uma instalação específica (ambiente/perfil não têm pasta).
    bool removeRecord(const QString &provider, const QString &package, const QString &folderId) const;

    QString providersPath() const;
    QString installedPath() const;

    static QString newProviderId();

private:
    QString m_dir;
};

// Cache: <cache>/store/<providerId>. `trim` apaga os arquivos mais antigos até caber em `maxBytes`.
QString storeCacheDir(const QString &providerId);
qint64 storeCacheSize();
void trimStoreCache(qint64 maxBytes);
bool clearStoreCache();

} // namespace kai::core
