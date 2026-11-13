#pragma once

#include <optional>

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace kai::core {

// Kai Store (specs/14-store.md): modelo e parse do catálogo (store.yml + index.json) e do package.yml.
// Puro: sem rede, sem disco, sem UI.

// --- Versões (semver) e capacidades -----------------------------------------------------------------------------

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    QStringList pre; // identificadores de pré-release ("beta", "3"); vazio = estável

    static std::optional<SemVer> parse(const QString &text);
};

// <0, 0, >0. Texto inválido vale como "menor que tudo" (nunca lança).
int compareVersions(const QString &a, const QString &b);

// Capacidades que ESTE Kai oferece; um pacote com `kai.requires` fora desta lista fica cinza.
QStringList kaiCapabilities();

// --- Texto localizado ("string" ou {en:, pt:}) ------------------------------------------------------------------

struct LocalizedText {
    QMap<QString, QString> byLang;

    static LocalizedText fromJson(const QJsonValue &value);
    bool isEmpty() const { return byLang.isEmpty(); }
    // Idioma pedido, depois "en", depois o primeiro que existir.
    QString resolve(const QString &lang) const;
};

// --- Provedor configurado pelo usuário --------------------------------------------------------------------------

enum class StoreProviderKind { File, Repo, Api };
enum class StoreTrust { Official, Verified, Community, Custom };

struct StoreProviderConfig {
    QString id;
    QString name;
    StoreProviderKind kind = StoreProviderKind::File;
    QString location;
    QString token; // só local (modo 0600), nunca exportado
    StoreTrust trust = StoreTrust::Custom;
    bool enabled = true;

    QJsonObject toJson() const;
    static StoreProviderConfig fromJson(const QJsonObject &obj);
};

QString storeKindName(StoreProviderKind kind);
std::optional<StoreProviderKind> storeKindFromName(const QString &name);
QString storeTrustName(StoreTrust trust);
StoreTrust storeTrustFromName(const QString &name);

// Sugere o tipo pelo que o usuário digitou (.git/forge conhecido -> repo; .yml/.yaml/.json ou caminho local -> file;
// o resto -> api). O usuário pode sobrescrever.
StoreProviderKind suggestStoreKind(const QString &location);

// --- Catálogo -----------------------------------------------------------------------------------------------------

struct StoreCategory {
    QString id;
    LocalizedText name;
    QString icon;
};

struct StoreHeader {
    int storeFormat = 0;
    QString name;
    LocalizedText description;
    QString homepage;
    QVector<StoreCategory> categories;
    QString signingAlgorithm;
    QString signingKey; // chave pública base64; vazio = provedor sem assinatura declarada
    QString indexPath = QStringLiteral("index.json");
};

struct StoreVersionEntry {
    QString version;
    QString kaiMin;
    QStringList requires_;
    QString manifest; // caminho/URL relativo ao provedor
    QString sha256;   // do package.yml
};

struct StoreIndexEntry {
    QString id; // publisher/slug
    QString kind;
    QString name;
    LocalizedText summary;
    QString category;
    QStringList tags;
    QString latest;
    QString icon;
    QVector<StoreVersionEntry> versions;
};

struct StoreCatalog {
    StoreHeader header;
    QVector<StoreIndexEntry> packages;
    QString signature; // base64 sobre o corpo canônico do índice (vazio = sem)
};

// Maior store_format que este Kai entende.
constexpr int kStoreFormatSupported = 1;

struct StoreParseError {
    QString message;
    bool needsUpdate = false; // store_format maior que o suportado: "atualize o Kai"
};

// `obj`: o store.yml (header) já convertido para JSON.
std::optional<StoreHeader> parseStoreHeader(const QJsonObject &obj, StoreParseError *error = nullptr);
// `obj`: o index.json. Linhas inválidas são descartadas (e contadas em `skipped`), nunca derrubam o resto.
std::optional<StoreCatalog> parseStoreIndex(const QJsonObject &obj, StoreParseError *error = nullptr,
                                            int *skipped = nullptr);
// Catálogo de um `file` provider: cabeçalho + "packages:" inline no mesmo objeto.
std::optional<StoreCatalog> parseStoreCatalogFile(const QJsonObject &obj, StoreParseError *error = nullptr,
                                                  int *skipped = nullptr);

// --- Compatibilidade ---------------------------------------------------------------------------------------------

struct StoreCompatibility {
    bool ok = true;
    // Vazio se ok. "kai_min:<versão>" ou "capability:<nome>" (a UI traduz).
    QString reason;
};

StoreCompatibility checkCompatibility(const QString &kaiMin, const QStringList &requires_,
                                      const QString &kaiVersion, const QStringList &capabilities);
StoreCompatibility checkCompatibility(const StoreVersionEntry &version);

// Mais nova COMPATÍVEL pela ordem semver (uma pré-release só ganha de uma estável anterior se for maior). nullptr se
// nenhuma serve.
const StoreVersionEntry *latestCompatible(const StoreIndexEntry &entry, const QString &kaiVersion,
                                          const QStringList &capabilities);
const StoreVersionEntry *latestCompatible(const StoreIndexEntry &entry);
const StoreVersionEntry *findVersion(const StoreIndexEntry &entry, const QString &version);

// --- Pacote (package.yml) ----------------------------------------------------------------------------------------

struct StoreFile {
    QString path;
    QString sha256;
};

struct StoreCloneSource {
    QString url;
    QString ref;
    QString path = QStringLiteral("kai.yml");
};

struct StoreManifest {
    int packageFormat = 0;
    QString id;
    QString kind;
    QString version;
    QString name;
    LocalizedText description;
    QString category;
    QStringList tags;
    QString author;
    QString license;
    QString homepage;
    QString kaiMin;
    QStringList requires_;
    QStringList needs;
    QStringList platforms;
    QStringList secrets;
    QString icon;
    QStringList screenshots;
    QVector<StoreFile> files;
    std::optional<StoreCloneSource> clone;
    QString changelog;
};

// Tipos de pacote.
const QStringList &storePackageKinds();

struct StoreManifestIssue {
    bool error = true;
    QString path; // ex: "files[1].sha256"
    QString message; // código estável sem tradução ("bad_sha256"...): quem exibe traduz
};

// Lê o package.yml (JSON). Nunca lança; campos inválidos viram problemas em `issues` (e o parse falha só se
// faltar o que impede qualquer uso: package_format/id/version).
std::optional<StoreManifest> parseStoreManifest(const QJsonObject &obj, QVector<StoreManifestIssue> *issues = nullptr);
QVector<StoreManifestIssue> validateStoreManifest(const StoreManifest &manifest);
bool hasStoreErrors(const QVector<StoreManifestIssue> &issues);

// `publisher/slug`: minúsculas, dígitos, '-', '_' e '.'.
bool isValidStoreId(const QString &id);
// Caminho de payload SEGURO: relativo, sem "..", sem barra inicial/letra de drive, sem NUL/backslash.
bool isSafeStorePath(const QString &path);
bool isHexSha256(const QString &text);

} // namespace kai::core
