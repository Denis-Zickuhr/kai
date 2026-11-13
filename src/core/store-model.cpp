#include "core/store-model.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

#ifndef KAI_VERSION_STRING
#define KAI_VERSION_STRING "0.0.0"
#endif

namespace kai::core {

namespace {

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    if (value.isString()) {
        out << value.toString();
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            if (item.isString() && !item.toString().isEmpty()) {
                out << item.toString();
            }
        }
    }
    return out;
}

void setError(StoreParseError *error, const QString &message, bool needsUpdate = false)
{
    if (error) {
        error->message = message;
        error->needsUpdate = needsUpdate;
    }
}

// Identificadores de pré-release: numéricos < alfanuméricos; numéricos comparam como número.
int comparePre(const QStringList &a, const QStringList &b)
{
    if (a.isEmpty() != b.isEmpty()) {
        return a.isEmpty() ? 1 : -1; // estável > pré-release
    }
    const int n = std::min(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        bool aNum = false;
        bool bNum = false;
        const qlonglong an = a[i].toLongLong(&aNum);
        const qlonglong bn = b[i].toLongLong(&bNum);
        if (aNum && bNum) {
            if (an != bn) return an < bn ? -1 : 1;
        } else if (aNum != bNum) {
            return aNum ? -1 : 1;
        } else if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

QVector<StoreVersionEntry> parseVersions(const QJsonValue &value)
{
    QVector<StoreVersionEntry> out;
    for (const QJsonValue &item : value.toArray()) {
        const QJsonObject obj = item.toObject();
        StoreVersionEntry v;
        v.version = obj.value(QStringLiteral("version")).toString();
        if (!SemVer::parse(v.version)) {
            continue;
        }
        v.kaiMin = obj.value(QStringLiteral("kai_min")).toString();
        v.requires_ = stringList(obj.value(QStringLiteral("requires")));
        v.manifest = obj.value(QStringLiteral("manifest")).toString();
        v.sha256 = obj.value(QStringLiteral("sha256")).toString().toLower();
        out << v;
    }
    return out;
}

std::optional<StoreIndexEntry> parseIndexEntry(const QJsonObject &obj)
{
    StoreIndexEntry e;
    e.id = obj.value(QStringLiteral("id")).toString();
    e.kind = obj.value(QStringLiteral("kind")).toString();
    if (!isValidStoreId(e.id) || !storePackageKinds().contains(e.kind)) {
        return std::nullopt;
    }
    e.name = obj.value(QStringLiteral("name")).toString(e.id);
    e.summary = LocalizedText::fromJson(obj.value(QStringLiteral("summary")));
    e.category = obj.value(QStringLiteral("category")).toString();
    e.tags = stringList(obj.value(QStringLiteral("tags")));
    e.latest = obj.value(QStringLiteral("latest")).toString();
    e.icon = obj.value(QStringLiteral("icon")).toString();
    e.versions = parseVersions(obj.value(QStringLiteral("versions")));
    if (e.versions.isEmpty()) {
        return std::nullopt;
    }
    if (e.latest.isEmpty() || !findVersion(e, e.latest)) {
        e.latest = e.versions.first().version;
        for (const StoreVersionEntry &v : e.versions) {
            if (compareVersions(v.version, e.latest) > 0) e.latest = v.version;
        }
    }
    return e;
}

} // namespace

// --- SemVer ----------------------------------------------------------------------------------------------------

std::optional<SemVer> SemVer::parse(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$)"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    SemVer v;
    v.major = m.captured(1).toInt();
    v.minor = m.captured(2).toInt();
    v.patch = m.captured(3).toInt();
    if (!m.captured(4).isEmpty()) {
        v.pre = m.captured(4).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    }
    return v;
}

int compareVersions(const QString &a, const QString &b)
{
    const auto va = SemVer::parse(a);
    const auto vb = SemVer::parse(b);
    if (!va || !vb) {
        return va ? 1 : (vb ? -1 : 0);
    }
    if (va->major != vb->major) return va->major < vb->major ? -1 : 1;
    if (va->minor != vb->minor) return va->minor < vb->minor ? -1 : 1;
    if (va->patch != vb->patch) return va->patch < vb->patch ? -1 : 1;
    return comparePre(va->pre, vb->pre);
}

QStringList kaiCapabilities()
{
    return {QStringLiteral("http"),         QStringLiteral("pipelines"),   QStringLiteral("kip"),
            QStringLiteral("folder_actions"), QStringLiteral("hooks"),     QStringLiteral("cron"),
            QStringLiteral("collections"),  QStringLiteral("notes"),       QStringLiteral("responders")};
}

// --- LocalizedText -----------------------------------------------------------------------------------------------

LocalizedText LocalizedText::fromJson(const QJsonValue &value)
{
    LocalizedText t;
    if (value.isString()) {
        t.byLang.insert(QStringLiteral("en"), value.toString());
    } else if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.value().isString()) {
                t.byLang.insert(it.key(), it.value().toString());
            }
        }
    }
    return t;
}

QString LocalizedText::resolve(const QString &lang) const
{
    if (byLang.contains(lang)) return byLang.value(lang);
    const QString base = lang.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('-'), 0, 0);
    if (byLang.contains(base)) return byLang.value(base);
    if (byLang.contains(QStringLiteral("en"))) return byLang.value(QStringLiteral("en"));
    return byLang.isEmpty() ? QString() : byLang.constBegin().value();
}

// --- Provedor ----------------------------------------------------------------------------------------------------

QString storeKindName(StoreProviderKind kind)
{
    switch (kind) {
    case StoreProviderKind::File: return QStringLiteral("file");
    case StoreProviderKind::Repo: return QStringLiteral("repo");
    case StoreProviderKind::Api: return QStringLiteral("api");
    }
    return QStringLiteral("file");
}

std::optional<StoreProviderKind> storeKindFromName(const QString &name)
{
    if (name == QLatin1String("file")) return StoreProviderKind::File;
    if (name == QLatin1String("repo")) return StoreProviderKind::Repo;
    if (name == QLatin1String("api")) return StoreProviderKind::Api;
    return std::nullopt;
}

QString storeTrustName(StoreTrust trust)
{
    switch (trust) {
    case StoreTrust::Official: return QStringLiteral("official");
    case StoreTrust::Verified: return QStringLiteral("verified");
    case StoreTrust::Community: return QStringLiteral("community");
    case StoreTrust::Custom: return QStringLiteral("custom");
    }
    return QStringLiteral("custom");
}

StoreTrust storeTrustFromName(const QString &name)
{
    if (name == QLatin1String("official")) return StoreTrust::Official;
    if (name == QLatin1String("verified")) return StoreTrust::Verified;
    if (name == QLatin1String("community")) return StoreTrust::Community;
    return StoreTrust::Custom;
}

QJsonObject StoreProviderConfig::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("kind"), storeKindName(kind));
    o.insert(QStringLiteral("location"), location);
    if (!token.isEmpty()) o.insert(QStringLiteral("token"), token);
    o.insert(QStringLiteral("trust"), storeTrustName(trust));
    o.insert(QStringLiteral("enabled"), enabled);
    return o;
}

StoreProviderConfig StoreProviderConfig::fromJson(const QJsonObject &obj)
{
    StoreProviderConfig p;
    p.id = obj.value(QStringLiteral("id")).toString();
    p.name = obj.value(QStringLiteral("name")).toString();
    p.kind = storeKindFromName(obj.value(QStringLiteral("kind")).toString()).value_or(StoreProviderKind::File);
    p.location = obj.value(QStringLiteral("location")).toString();
    p.token = obj.value(QStringLiteral("token")).toString();
    p.trust = storeTrustFromName(obj.value(QStringLiteral("trust")).toString());
    p.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    return p;
}

StoreProviderKind suggestStoreKind(const QString &location)
{
    const QString text = location.trimmed();
    const QString lower = text.toLower();
    static const QStringList forges = {QStringLiteral("github.com"), QStringLiteral("gitlab.com"),
                                       QStringLiteral("codeberg.org"), QStringLiteral("bitbucket.org")};
    const QUrl url(text);
    if (lower.endsWith(QLatin1String(".git")) || lower.startsWith(QLatin1String("git@"))) {
        return StoreProviderKind::Repo;
    }
    const QString host = url.host().toLower();
    if (!host.isEmpty() && forges.contains(host)) {
        return StoreProviderKind::Repo;
    }
    if (lower.startsWith(QLatin1String("github.com/")) || lower.startsWith(QLatin1String("gitlab.com/"))
        || lower.startsWith(QLatin1String("codeberg.org/"))) {
        return StoreProviderKind::Repo;
    }
    if (lower.endsWith(QLatin1String(".yml")) || lower.endsWith(QLatin1String(".yaml"))
        || lower.endsWith(QLatin1String(".json"))) {
        return StoreProviderKind::File;
    }
    const bool remote = lower.startsWith(QLatin1String("http://")) || lower.startsWith(QLatin1String("https://"));
    return remote ? StoreProviderKind::Api : StoreProviderKind::File;
}

// --- Catálogo ----------------------------------------------------------------------------------------------------

std::optional<StoreHeader> parseStoreHeader(const QJsonObject &obj, StoreParseError *error)
{
    const int format = obj.value(QStringLiteral("store_format")).toInt(0);
    if (format <= 0) {
        setError(error, QStringLiteral("missing_store_format"));
        return std::nullopt;
    }
    if (format > kStoreFormatSupported) {
        setError(error, QStringLiteral("store_format_newer"), true);
        return std::nullopt;
    }
    StoreHeader h;
    h.storeFormat = format;
    h.name = obj.value(QStringLiteral("name")).toString();
    h.description = LocalizedText::fromJson(obj.value(QStringLiteral("description")));
    h.homepage = obj.value(QStringLiteral("homepage")).toString();
    for (const QJsonValue &item : obj.value(QStringLiteral("categories")).toArray()) {
        const QJsonObject c = item.toObject();
        StoreCategory cat;
        cat.id = c.value(QStringLiteral("id")).toString();
        if (cat.id.isEmpty()) continue;
        cat.name = LocalizedText::fromJson(c.value(QStringLiteral("name")));
        cat.icon = c.value(QStringLiteral("icon")).toString();
        h.categories << cat;
    }
    const QJsonObject signing = obj.value(QStringLiteral("signing")).toObject();
    h.signingAlgorithm = signing.value(QStringLiteral("algorithm")).toString();
    h.signingKey = signing.value(QStringLiteral("key")).toString();
    const QString index = obj.value(QStringLiteral("index")).toString();
    if (!index.isEmpty()) {
        h.indexPath = index;
    }
    return h;
}

std::optional<StoreCatalog> parseStoreIndex(const QJsonObject &obj, StoreParseError *error, int *skipped)
{
    const int format = obj.value(QStringLiteral("store_format")).toInt(0);
    if (format > kStoreFormatSupported) {
        setError(error, QStringLiteral("store_format_newer"), true);
        return std::nullopt;
    }
    if (!obj.value(QStringLiteral("packages")).isArray()) {
        setError(error, QStringLiteral("missing_packages"));
        return std::nullopt;
    }
    StoreCatalog c;
    c.header.storeFormat = format;
    c.signature = obj.value(QStringLiteral("signature")).toString();
    int bad = 0;
    for (const QJsonValue &item : obj.value(QStringLiteral("packages")).toArray()) {
        if (const auto entry = parseIndexEntry(item.toObject())) {
            c.packages << *entry;
        } else {
            ++bad;
        }
    }
    if (skipped) *skipped = bad;
    return c;
}

std::optional<StoreCatalog> parseStoreCatalogFile(const QJsonObject &obj, StoreParseError *error, int *skipped)
{
    const auto header = parseStoreHeader(obj, error);
    if (!header) {
        return std::nullopt;
    }
    auto catalog = parseStoreIndex(obj, error, skipped);
    if (!catalog) {
        return std::nullopt;
    }
    catalog->header = *header;
    return catalog;
}

// --- Compatibilidade ---------------------------------------------------------------------------------------------

StoreCompatibility checkCompatibility(const QString &kaiMin, const QStringList &requires_, const QString &kaiVersion,
                                      const QStringList &capabilities)
{
    if (!kaiMin.isEmpty() && compareVersions(kaiVersion, kaiMin) < 0) {
        return {false, QStringLiteral("kai_min:") + kaiMin};
    }
    for (const QString &capability : requires_) {
        if (!capabilities.contains(capability)) {
            return {false, QStringLiteral("capability:") + capability};
        }
    }
    return {};
}

StoreCompatibility checkCompatibility(const StoreVersionEntry &version)
{
    return checkCompatibility(version.kaiMin, version.requires_, QString::fromLatin1(KAI_VERSION_STRING),
                              kaiCapabilities());
}

const StoreVersionEntry *latestCompatible(const StoreIndexEntry &entry, const QString &kaiVersion,
                                          const QStringList &capabilities)
{
    const StoreVersionEntry *best = nullptr;
    for (const StoreVersionEntry &v : entry.versions) {
        if (!checkCompatibility(v.kaiMin, v.requires_, kaiVersion, capabilities).ok) continue;
        if (!best || compareVersions(v.version, best->version) > 0) best = &v;
    }
    return best;
}

const StoreVersionEntry *latestCompatible(const StoreIndexEntry &entry)
{
    return latestCompatible(entry, QString::fromLatin1(KAI_VERSION_STRING), kaiCapabilities());
}

const StoreVersionEntry *findVersion(const StoreIndexEntry &entry, const QString &version)
{
    for (const StoreVersionEntry &v : entry.versions) {
        if (v.version == version) return &v;
    }
    return nullptr;
}

// --- Pacote ------------------------------------------------------------------------------------------------------

const QStringList &storePackageKinds()
{
    static const QStringList kinds = {QStringLiteral("command"),     QStringLiteral("folder"),
                                      QStringLiteral("pipeline"),    QStringLiteral("project"),
                                      QStringLiteral("environment"), QStringLiteral("profile")};
    return kinds;
}

bool isValidStoreId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral(R"(^[a-z0-9][a-z0-9_.-]*/[a-z0-9][a-z0-9_.-]*$)"));
    return re.match(id).hasMatch();
}

bool isSafeStorePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\'))
        || path.contains(QLatin1Char('\0')) || path.contains(QLatin1Char(':'))) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String("..") || part == QLatin1String(".")) {
            return false;
        }
    }
    return true;
}

bool isHexSha256(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return re.match(text).hasMatch();
}

std::optional<StoreManifest> parseStoreManifest(const QJsonObject &obj, QVector<StoreManifestIssue> *issues)
{
    StoreManifest m;
    m.packageFormat = obj.value(QStringLiteral("package_format")).toInt(0);
    m.id = obj.value(QStringLiteral("id")).toString();
    m.version = obj.value(QStringLiteral("version")).toString();
    if (m.packageFormat <= 0 || m.id.isEmpty() || m.version.isEmpty()) {
        if (issues) issues->append({true, QStringLiteral("package_format"), QStringLiteral("missing_required")});
        return std::nullopt;
    }
    m.kind = obj.value(QStringLiteral("kind")).toString();
    m.name = obj.value(QStringLiteral("name")).toString(m.id);
    m.description = LocalizedText::fromJson(obj.value(QStringLiteral("description")));
    m.category = obj.value(QStringLiteral("category")).toString();
    m.tags = stringList(obj.value(QStringLiteral("tags")));
    m.author = obj.value(QStringLiteral("author")).toString();
    m.license = obj.value(QStringLiteral("license")).toString();
    m.homepage = obj.value(QStringLiteral("homepage")).toString();
    const QJsonObject kai = obj.value(QStringLiteral("kai")).toObject();
    m.kaiMin = kai.value(QStringLiteral("min")).toString();
    m.requires_ = stringList(kai.value(QStringLiteral("requires")));
    m.needs = stringList(obj.value(QStringLiteral("needs")));
    m.platforms = stringList(obj.value(QStringLiteral("platforms")));
    m.secrets = stringList(obj.value(QStringLiteral("secrets")));
    const QJsonObject media = obj.value(QStringLiteral("media")).toObject();
    m.icon = media.value(QStringLiteral("icon")).toString();
    m.screenshots = stringList(media.value(QStringLiteral("screenshots")));
    for (const QJsonValue &item : obj.value(QStringLiteral("files")).toArray()) {
        const QJsonObject f = item.toObject();
        m.files << StoreFile{f.value(QStringLiteral("path")).toString(),
                             f.value(QStringLiteral("sha256")).toString().toLower()};
    }
    const QJsonObject source = obj.value(QStringLiteral("source")).toObject();
    const QJsonObject clone = source.value(QStringLiteral("clone")).toObject();
    if (!clone.isEmpty()) {
        StoreCloneSource c;
        c.url = clone.value(QStringLiteral("url")).toString();
        c.ref = clone.value(QStringLiteral("ref")).toString();
        const QString path = clone.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) c.path = path;
        m.clone = c;
    }
    m.changelog = obj.value(QStringLiteral("changelog")).toString();
    return m;
}

QVector<StoreManifestIssue> validateStoreManifest(const StoreManifest &m)
{
    QVector<StoreManifestIssue> out;
    const auto err = [&out](const QString &path, const QString &code) { out.append({true, path, code}); };
    const auto warn = [&out](const QString &path, const QString &code) { out.append({false, path, code}); };

    if (m.packageFormat != 1) err(QStringLiteral("package_format"), QStringLiteral("unsupported_package_format"));
    if (!isValidStoreId(m.id)) err(QStringLiteral("id"), QStringLiteral("bad_id"));
    if (!storePackageKinds().contains(m.kind)) err(QStringLiteral("kind"), QStringLiteral("bad_kind"));
    if (!SemVer::parse(m.version)) err(QStringLiteral("version"), QStringLiteral("bad_version"));
    if (!m.kaiMin.isEmpty() && !SemVer::parse(m.kaiMin)) err(QStringLiteral("kai.min"), QStringLiteral("bad_version"));
    if (m.name.isEmpty()) err(QStringLiteral("name"), QStringLiteral("missing_name"));
    for (const QString &c : m.requires_) {
        if (!kaiCapabilities().contains(c)) warn(QStringLiteral("kai.requires"), QStringLiteral("unknown_capability"));
    }

    const bool byReference = m.clone.has_value();
    if (m.kind == QLatin1String("project")) {
        if (byReference == !m.files.isEmpty()) {
            err(QStringLiteral("source"), QStringLiteral("project_needs_one_source"));
        }
    } else if (byReference) {
        err(QStringLiteral("source"), QStringLiteral("clone_only_for_project"));
    } else if (m.files.isEmpty()) {
        err(QStringLiteral("files"), QStringLiteral("no_files"));
    }
    QStringList seen;
    for (int i = 0; i < m.files.size(); ++i) {
        const StoreFile &f = m.files[i];
        const QString base = QStringLiteral("files[%1]").arg(i);
        if (!isSafeStorePath(f.path)) err(base + QStringLiteral(".path"), QStringLiteral("unsafe_path"));
        if (!isHexSha256(f.sha256)) err(base + QStringLiteral(".sha256"), QStringLiteral("bad_sha256"));
        if (seen.contains(f.path)) err(base + QStringLiteral(".path"), QStringLiteral("duplicate_path"));
        seen << f.path;
    }
    if (!m.files.isEmpty() && !seen.contains(QStringLiteral("kai.yml"))) {
        err(QStringLiteral("files"), QStringLiteral("missing_kai_yml"));
    }
    if (m.clone) {
        if (!m.clone->url.startsWith(QLatin1String("https://")) && !m.clone->url.startsWith(QLatin1String("file://"))
            && !m.clone->url.startsWith(QLatin1Char('/'))) {
            err(QStringLiteral("source.clone.url"), QStringLiteral("clone_url_not_https"));
        }
        if (m.clone->ref.isEmpty()) err(QStringLiteral("source.clone.ref"), QStringLiteral("missing_ref"));
        if (!isSafeStorePath(m.clone->path)) err(QStringLiteral("source.clone.path"), QStringLiteral("unsafe_path"));
    }
    return out;
}

bool hasStoreErrors(const QVector<StoreManifestIssue> &issues)
{
    return std::any_of(issues.begin(), issues.end(), [](const StoreManifestIssue &i) { return i.error; });
}

} // namespace kai::core
