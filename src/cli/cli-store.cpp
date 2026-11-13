#include "cli/cli-store.h"

#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>

#include "cli/app-delegate.h"
#include "core/config-manager.h"
#include "core/store-local.h"
#include "core/store-model.h"
#include "core/store-preview.h"
#include "core/store-texts.h"
#include "engine/store-client.h"
#include "engine/store-installer.h"
#include "engine/store-publisher.h"
#include "engine/store-signature.h"
#include "utils/console-context.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

namespace kai::cli {

using namespace kai::core;
using namespace kai::engine;

namespace {

QTextStream &out() { return utils::cliOut(); }
QTextStream &err() { return utils::cliErr(); }

QString lang()
{
    return utils::TranslationManager::instance().currentLanguage();
}

int usage()
{
    err() << utils::tr(QStringLiteral("store.cli.usage")) << "\n";
    err().flush();
    return 2;
}

// Espera um passo assíncrono terminar sem travar o event loop (o CLI tem o seu próprio).
void runAndWait(const std::function<void(std::function<void()>)> &start)
{
    QEventLoop loop;
    bool finished = false;
    start([&loop, &finished]() {
        finished = true;
        loop.quit();
    });
    if (!finished) {
        loop.exec();
    }
}

struct Opts {
    QStringList positional;
    QString provider;
    QString catalog;
    QString key;
    bool json = false;
    bool yes = false;
    bool copy = false;
    QString error;
};

Opts parseOpts(const QStringList &args)
{
    Opts o;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args[i];
        const auto value = [&]() -> QString {
            if (i + 1 >= args.size()) {
                o.error = utils::tr(QStringLiteral("store.cli.needs_value")).arg(a);
                return {};
            }
            return args[++i];
        };
        if (a == QLatin1String("--json")) o.json = true;
        else if (a == QLatin1String("--yes") || a == QLatin1String("-y")) o.yes = true;
        else if (a == QLatin1String("--copy")) o.copy = true;
        else if (a == QLatin1String("--provider")) o.provider = value();
        else if (a == QLatin1String("--catalog")) o.catalog = value();
        else if (a == QLatin1String("--key")) o.key = value();
        else if (a.startsWith(QLatin1Char('-')) && a.size() > 1) o.error = utils::tr(QStringLiteral("store.cli.unknown_option")).arg(a);
        else o.positional << a;
    }
    return o;
}

QString pad(const QString &s, int width)
{
    return s.size() >= width ? s.left(width) + QLatin1Char(' ') : s + QString(width - s.size() + 1, QLatin1Char(' '));
}

struct LoadedProvider {
    StoreProviderConfig config;
    std::shared_ptr<StoreClient> client;
    StoreCatalogResult result;
};

QString configDir()
{
    return ConfigManager().configDirPath();
}

QString dataRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("store"));
}

// Carrega o catálogo dos provedores habilitados (ou só o pedido) em paralelo.
QVector<LoadedProvider> loadProviders(const QString &only)
{
    QVector<LoadedProvider> all;
    for (const StoreProviderConfig &p : StoreLocalState(configDir()).loadProviders()) {
        if (!p.enabled) continue;
        if (!only.isEmpty() && p.id != only && p.name.compare(only, Qt::CaseInsensitive) != 0) continue;
        LoadedProvider lp;
        lp.config = p;
        StoreClient::Options opts;
        opts.cacheDir = storeCacheDir(p.id);
        lp.client = std::make_shared<StoreClient>(p, opts);
        all << lp;
    }
    if (all.isEmpty()) return all;
    runAndWait([&all](std::function<void()> done) {
        auto pending = std::make_shared<int>(static_cast<int>(all.size()));
        for (int i = 0; i < all.size(); ++i) {
            all[i].client->loadCatalog([&all, i, pending, done](const StoreCatalogResult &r) {
                all[i].result = r;
                if (--*pending == 0) done();
            });
        }
    });
    return all;
}

int noProviders(const QString &only)
{
    err() << (only.isEmpty() ? utils::tr(QStringLiteral("store.cli.no_providers"))
                             : utils::tr(QStringLiteral("store.cli.provider_unknown")).arg(only))
          << "\n";
    err().flush();
    return 1;
}

void reportProviderErrors(const QVector<LoadedProvider> &providers)
{
    for (const LoadedProvider &p : providers) {
        if (!p.result.ok) {
            err() << utils::tr(QStringLiteral("store.cli.provider_failed")).arg(p.config.name, storeErrorText(p.result.error, p.result.errorDetail))
                  << "\n";
        } else if (p.result.signature == StoreSignatureState::Invalid) {
            err() << utils::tr(QStringLiteral("store.cli.signature_invalid")).arg(p.config.name) << "\n";
        }
    }
    err().flush();
}

struct Match {
    const LoadedProvider *provider = nullptr;
    const StoreIndexEntry *entry = nullptr;
};

QVector<Match> findPackages(const QVector<LoadedProvider> &providers, const std::function<bool(const StoreIndexEntry &)> &pred)
{
    QVector<Match> out;
    for (const LoadedProvider &p : providers) {
        if (!p.result.ok) continue;
        for (const StoreIndexEntry &e : p.result.catalog.packages) {
            if (pred(e)) out.append({&p, &e});
        }
    }
    return out;
}

QString compatCell(const StoreIndexEntry &e)
{
    const StoreVersionEntry *v = latestCompatible(e);
    return v ? QString() : QStringLiteral("!");
}

int printList(const QVector<Match> &matches, bool json)
{
    if (json) {
        QJsonArray arr;
        for (const Match &m : matches) {
            const StoreIndexEntry &e = *m.entry;
            const StoreVersionEntry *best = latestCompatible(e);
            arr.append(QJsonObject{{QStringLiteral("id"), e.id},
                                   {QStringLiteral("name"), e.name},
                                   {QStringLiteral("kind"), e.kind},
                                   {QStringLiteral("latest"), e.latest},
                                   {QStringLiteral("compatible"), best != nullptr},
                                   {QStringLiteral("provider"), m.provider->config.name},
                                   {QStringLiteral("summary"), e.summary.resolve(lang())}});
        }
        out() << QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        out().flush();
        return 0;
    }
    if (matches.isEmpty()) {
        out() << utils::tr(QStringLiteral("store.cli.empty")) << "\n";
        out().flush();
        return 0;
    }
    for (const Match &m : matches) {
        const StoreIndexEntry &e = *m.entry;
        out() << pad(e.id, 32) << pad(e.latest, 14) << pad(storeKindLabel(e.kind), 12) << pad(m.provider->config.name, 16)
              << compatCell(e) << e.summary.resolve(lang()) << "\n";
    }
    out().flush();
    return 0;
}

bool pickVersion(const StoreIndexEntry &entry, const QString &wanted, const StoreVersionEntry *&version, QString &problem)
{
    if (wanted.isEmpty()) {
        version = latestCompatible(entry);
        if (!version) {
            const StoreVersionEntry *newest = findVersion(entry, entry.latest);
            problem = utils::tr(QStringLiteral("store.cli.incompatible")).arg(entry.id, newest ? storeCompatText(checkCompatibility(*newest).reason) : QString());
            return false;
        }
        return true;
    }
    version = findVersion(entry, wanted);
    if (!version) {
        problem = utils::tr(QStringLiteral("store.cli.no_such_version")).arg(entry.id, wanted);
        return false;
    }
    const StoreCompatibility c = checkCompatibility(*version);
    if (!c.ok) {
        problem = utils::tr(QStringLiteral("store.cli.incompatible")).arg(entry.id, storeCompatText(c.reason));
        return false;
    }
    return true;
}

// Resolve "id[@versão]" em um único pacote; escreve o motivo no stderr se não achar/ambíguo.
bool resolve(const QVector<LoadedProvider> &providers, const QString &text, Match &match, QString &wantedVersion)
{
    const StorePackageSpec spec = parseStorePackageSpec(text);
    wantedVersion = spec.version;
    const QVector<Match> found = findPackages(providers, [&spec](const StoreIndexEntry &e) { return e.id == spec.id; });
    if (found.isEmpty()) {
        err() << utils::tr(QStringLiteral("store.cli.not_found")).arg(spec.id) << "\n";
        err().flush();
        return false;
    }
    if (found.size() > 1) {
        QStringList names;
        for (const Match &m : found) names << m.provider->config.name;
        err() << utils::tr(QStringLiteral("store.cli.ambiguous")).arg(spec.id, names.join(QStringLiteral(", "))) << "\n";
        err().flush();
        return false;
    }
    match = found.first();
    return true;
}

QString signatureLabel(StoreSignatureState s)
{
    switch (s) {
    case StoreSignatureState::None: return utils::tr(QStringLiteral("store.cli.signature.none"));
    case StoreSignatureState::Ok: return utils::tr(QStringLiteral("store.cli.signature.ok"));
    case StoreSignatureState::Invalid: return utils::tr(QStringLiteral("store.cli.signature.invalid"));
    }
    return {};
}

bool fetchManifestSync(const LoadedProvider &p, const StoreVersionEntry &v, StoreManifestResult &out)
{
    runAndWait([&](std::function<void()> done) {
        p.client->fetchManifest(v, [&out, done](const StoreManifestResult &r) {
            out = r;
            done();
        });
    });
    return out.ok;
}

int runShow(const Opts &o)
{
    if (o.positional.size() != 1) return usage();
    const QVector<LoadedProvider> providers = loadProviders(o.provider);
    if (providers.isEmpty()) return noProviders(o.provider);
    reportProviderErrors(providers);
    Match m;
    QString wanted;
    if (!resolve(providers, o.positional.first(), m, wanted)) return 1;
    const StoreIndexEntry &e = *m.entry;
    out() << e.name << "  (" << e.id << ")\n";
    out() << utils::tr(QStringLiteral("store.cli.show.kind")).arg(storeKindLabel(e.kind)) << "\n";
    out() << utils::tr(QStringLiteral("store.cli.show.provider")).arg(m.provider->config.name, storeTrustLabel(storeTrustName(m.provider->config.trust)),
                                                                     signatureLabel(m.provider->result.signature)) << "\n";
    const QString summary = e.summary.resolve(lang());
    if (!summary.isEmpty()) out() << summary << "\n";
    out() << utils::tr(QStringLiteral("store.cli.show.versions")) << "\n";
    for (const StoreVersionEntry &v : e.versions) {
        const StoreCompatibility c = checkCompatibility(v);
        out() << "  " << pad(v.version, 14) << (c.ok ? QString() : storeCompatText(c.reason)) << "\n";
    }
    const StoreVersionEntry *version = nullptr;
    QString problem;
    if (pickVersion(e, wanted, version, problem)) {
        StoreManifestResult mr;
        if (fetchManifestSync(*m.provider, *version, mr)) {
            const StoreManifest &mf = mr.manifest;
            if (!mf.author.isEmpty()) out() << utils::tr(QStringLiteral("store.cli.show.author")).arg(mf.author) << "\n";
            if (!mf.license.isEmpty()) out() << utils::tr(QStringLiteral("store.cli.show.license")).arg(mf.license) << "\n";
            if (!mf.needs.isEmpty()) {
                QStringList needs;
                for (const QString &n : mf.needs) {
                    needs << n + (storeToolFound(n) ? QString() : QStringLiteral(" (%1)").arg(utils::tr(QStringLiteral("store.cli.not_found_tool"))));
                }
                out() << utils::tr(QStringLiteral("store.cli.show.needs")).arg(needs.join(QStringLiteral(", "))) << "\n";
            }
            if (!mf.secrets.isEmpty()) out() << utils::tr(QStringLiteral("store.cli.show.secrets")).arg(mf.secrets.join(QStringLiteral(", "))) << "\n";
        }
    } else {
        out() << problem << "\n";
    }
    out().flush();
    return 0;
}

bool ask(const QString &question, bool defaultYes)
{
    out() << question << (defaultYes ? QStringLiteral(" [Y/n] ") : QStringLiteral(" [y/N] "));
    out().flush();
    QTextStream in(stdin);
    const QString answer = in.readLine().trimmed().toLower();
    if (answer.isEmpty()) return defaultYes;
    return answer == QLatin1String("y") || answer == QLatin1String("yes") || answer == QLatin1String("s") || answer == QLatin1String("sim");
}

void printPreview(const LoadedProvider &p, const StoreStagedPackage &staged)
{
    const StoreManifest &m = staged.manifest;
    out() << utils::tr(QStringLiteral("store.cli.preview.title")).arg(m.name, m.version) << "\n";
    out() << utils::tr(QStringLiteral("store.cli.show.provider")).arg(p.config.name, storeTrustLabel(storeTrustName(p.config.trust)),
                                                                     signatureLabel(p.result.signature)) << "\n";
    if (staged.refMoves) out() << utils::tr(QStringLiteral("store.cli.preview.ref_moves")) << "\n";
    for (const QString &n : m.needs) {
        out() << utils::tr(QStringLiteral("store.cli.preview.needs")).arg(n, storeToolFound(n) ? utils::tr(QStringLiteral("store.cli.found")) : utils::tr(QStringLiteral("store.cli.not_found_tool"))) << "\n";
    }
    for (const QString &n : staged.preview.interpreters) {
        if (!m.needs.contains(n)) {
            out() << utils::tr(QStringLiteral("store.cli.preview.needs")).arg(n, storeToolFound(n) ? utils::tr(QStringLiteral("store.cli.found")) : utils::tr(QStringLiteral("store.cli.not_found_tool"))) << "\n";
        }
    }
    for (const QString &f : staged.preview.folders) out() << utils::tr(QStringLiteral("store.cli.preview.folder")).arg(f) << "\n";
    for (const StorePreviewCommand &c : staged.preview.commands) {
        out() << utils::tr(QStringLiteral("store.cli.preview.command")).arg(c.name, c.type) << "\n";
        const QString body = c.type == QLatin1String("http") ? c.url : c.body;
        for (const QString &line : body.split(QLatin1Char('\n'))) out() << "    | " << line << "\n";
    }
    if (staged.preview.hooksSkipped > 0) out() << utils::tr(QStringLiteral("store.cli.preview.hooks_skipped")).arg(staged.preview.hooksSkipped) << "\n";
    if (!m.secrets.isEmpty()) out() << utils::tr(QStringLiteral("store.cli.preview.secrets")).arg(m.secrets.join(QStringLiteral(", "))) << "\n";
    for (const ValidationIssue &w : staged.validation.issues) {
        if (w.severity == ValidationSeverity::Warning) out() << utils::tr(QStringLiteral("store.cli.preview.warning")).arg(w.path, w.message) << "\n";
    }
    out().flush();
}

// Baixa, verifica e mostra a prévia; devolve o pedido pronto para o app (ou false, com o motivo no stderr).
bool stagePackage(const LoadedProvider &p, const Match &m, const QString &wanted, const Opts &o, StoreInstallRequest &request,
                  StoreStagedPackage &staged)
{
    const StoreVersionEntry *version = nullptr;
    QString problem;
    if (!pickVersion(*m.entry, wanted, version, problem)) {
        err() << problem << "\n";
        err().flush();
        return false;
    }
    if (p.result.signature == StoreSignatureState::Invalid) {
        err() << utils::tr(QStringLiteral("store.cli.signature_invalid")).arg(p.config.name) << "\n";
        err().flush();
        return false;
    }
    StoreManifestResult mr;
    if (!fetchManifestSync(p, *version, mr)) {
        err() << storeErrorText(mr.error) << "\n";
        for (const StoreManifestIssue &i : mr.issues) {
            if (i.error) err() << "  " << storeIssueText(i.message, i.path) << "\n";
        }
        err().flush();
        return false;
    }
    const bool byReference = mr.manifest.clone.has_value();
    const QString stagingRoot = QDir(dataRoot()).filePath(byReference ? QStringLiteral("clones") : QStringLiteral("staging"));
    StoreInstaller installer(p.client.get());
    runAndWait([&](std::function<void()> done) {
        installer.stage(mr.manifest, version->manifest, stagingRoot, [&staged, done](const StoreStagedPackage &s) {
            staged = s;
            done();
        });
    });
    if (!staged.ok) {
        err() << storeErrorText(staged.error, staged.errorDetail) << "\n";
        for (const ValidationIssue &i : staged.validation.issues) {
            if (i.severity == ValidationSeverity::Error) err() << "  " << i.path << ": " << i.message << "\n";
        }
        err().flush();
        return false;
    }
    printPreview(p, staged);

    request.providerId = p.config.id;
    request.packageId = mr.manifest.id;
    request.version = mr.manifest.version;
    request.kind = mr.manifest.kind;
    request.kaiFilePath = staged.kaiFilePath;
    request.cloneDir = byReference ? staged.dir : QString();
    request.secrets = mr.manifest.secrets;

    // cron e auto_run: perguntados por comando, padrão desligado. --yes aceita os padrões (nada ligado).
    const bool interactive = !o.yes && utils::stdinIsInteractiveTerminal();
    for (const StorePreviewCommand &c : staged.preview.commands) {
        if (interactive && !c.cron.isEmpty() && ask(utils::tr(QStringLiteral("store.cli.ask.cron")).arg(c.name, c.cron), false)) request.allowCron << c.name;
        if (interactive && c.autoRun && ask(utils::tr(QStringLiteral("store.cli.ask.auto_run")).arg(c.name), false)) request.allowAutoRun << c.name;
    }
    if (interactive && !ask(utils::tr(QStringLiteral("store.cli.ask.install")), true)) {
        StoreInstaller::discard(staged);
        err() << utils::tr(QStringLiteral("store.cli.aborted")) << "\n";
        err().flush();
        return false;
    }
    return true;
}

int sendInstall(const StoreInstallRequest &request, const StoreStagedPackage &staged)
{
    QJsonObject message;
    message[QStringLiteral("cmd")] = QStringLiteral("store-install");
    message[QStringLiteral("request")] = request.toJson();
    const std::optional<QJsonObject> reply = requestApp(message);
    // O staging é do CLI: o app já copiou tudo o que precisava (um clone por referência fica, é o projeto).
    if (request.cloneDir.isEmpty() || !reply || !reply->value(QStringLiteral("ok")).toBool()) {
        StoreInstaller::discard(staged);
    }
    if (!reply) {
        err() << utils::tr(QStringLiteral("cli.error.not_running")) << "\n";
        err().flush();
        return 2;
    }
    const bool ok = reply->value(QStringLiteral("ok")).toBool();
    (ok ? out() : err()) << reply->value(QStringLiteral("message")).toString() << "\n";
    out().flush();
    err().flush();
    return ok ? 0 : 1;
}

int runInstall(const Opts &o)
{
    if (o.positional.size() != 1) return usage();
    const QVector<LoadedProvider> providers = loadProviders(o.provider);
    if (providers.isEmpty()) return noProviders(o.provider);
    reportProviderErrors(providers);
    Match m;
    QString wanted;
    if (!resolve(providers, o.positional.first(), m, wanted)) return 1;
    StoreInstallRequest request;
    StoreStagedPackage staged;
    if (!stagePackage(*m.provider, m, wanted, o, request, staged)) return 1;
    return sendInstall(request, staged);
}

int runUpdate(const Opts &o)
{
    const StoreLocalState local(configDir());
    const QVector<StoreInstalledRecord> installed = local.loadInstalled();
    const QVector<LoadedProvider> providers = loadProviders(QString());
    if (providers.isEmpty()) return noProviders(QString());
    reportProviderErrors(providers);
    const CommandsData data = ConfigManager().loadCommands();

    const QString only = o.positional.isEmpty() ? QString() : parseStorePackageSpec(o.positional.first()).id;
    int updates = 0;
    for (const StoreInstalledRecord &rec : installed) {
        if (!only.isEmpty() && rec.package != only) continue;
        const LoadedProvider *prov = nullptr;
        for (const LoadedProvider &p : providers) if (p.config.id == rec.provider) prov = &p;
        if (!prov || !prov->result.ok) continue;
        const StoreIndexEntry *entry = nullptr;
        for (const StoreIndexEntry &e : prov->result.catalog.packages) if (e.id == rec.package) entry = &e;
        if (!entry) continue;
        const StoreVersionEntry *best = latestCompatible(*entry);
        if (!best || compareVersions(best->version, rec.version) <= 0) continue;
        ++updates;
        out() << utils::tr(QStringLiteral("store.cli.update.available")).arg(rec.package, rec.version, best->version) << "\n";
        if (only.isEmpty()) continue; // sem pacote: só informa

        bool exists = false;
        for (const Folder &f : data.folders) if (f.id == rec.folderId) exists = true;
        if (!rec.folderId.isEmpty() && !exists) {
            err() << utils::tr(QStringLiteral("store.cli.update.folder_gone")).arg(rec.package) << "\n";
            continue;
        }
        const bool edited = !rec.folderId.isEmpty() && storeContentHash(rec.folderId, data) != rec.contentHash;
        if (edited && !o.copy) {
            err() << utils::tr(QStringLiteral("store.cli.update.edited")).arg(rec.package) << "\n";
            err().flush();
            return 1;
        }
        Match m{prov, entry};
        StoreInstallRequest request;
        StoreStagedPackage staged;
        if (!stagePackage(*prov, m, best->version, o, request, staged)) return 1;
        if (!o.copy) request.replaceFolderId = rec.folderId;
        return sendInstall(request, staged);
    }
    if (updates == 0) out() << utils::tr(QStringLiteral("store.cli.update.none")) << "\n";
    out().flush();
    err().flush();
    return 0;
}

// --- publicador ---------------------------------------------------------------------------------------------------

int printReport(const StorePublishReport &r)
{
    for (const StorePublishIssue &i : r.issues) {
        const QString text = i.text.isEmpty() ? storeIssueText(i.code, i.path) : QStringLiteral("%1: %2").arg(i.path, i.text);
        (i.error ? err() : out()) << (i.error ? utils::tr(QStringLiteral("store.cli.error_prefix")) : utils::tr(QStringLiteral("store.cli.warning_prefix"))) << text << "\n";
    }
    for (const QString &w : r.written) out() << utils::tr(QStringLiteral("store.cli.wrote")).arg(QDir::toNativeSeparators(w)) << "\n";
    out().flush();
    err().flush();
    return r.ok ? 0 : 1;
}

int runKeygen(const Opts &o)
{
    const store_signature::KeyPair kp = store_signature::generateKeyPair();
    const QString path = o.positional.isEmpty() ? QStringLiteral("kai-store.key") : o.positional.first();
    if (QFile::exists(path)) {
        err() << utils::tr(QStringLiteral("store.cli.keygen.exists")).arg(path) << "\n";
        err().flush();
        return 1;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        err() << utils::tr(QStringLiteral("store.cli.keygen.failed")).arg(path) << "\n";
        err().flush();
        return 1;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(store_signature::toBase64(kp.secretKey).toUtf8() + "\n");
    f.close();
    out() << utils::tr(QStringLiteral("store.cli.keygen.done")).arg(path) << "\n"
          << "signing:\n  algorithm: ed25519\n  key: \"" << store_signature::toBase64(kp.publicKey) << "\"\n";
    out().flush();
    return 0;
}

} // namespace

StorePackageSpec parseStorePackageSpec(const QString &text)
{
    const int at = text.indexOf(QLatin1Char('@'));
    if (at < 0) return {text.trimmed(), QString()};
    return {text.left(at).trimmed(), text.mid(at + 1).trimmed()};
}

int runStoreVerb(const QStringList &args)
{
    utils::Logger::setConsoleOutputEnabled(false);
    const QString sub = args.value(2);
    if (sub.isEmpty() || sub == QLatin1String("help") || sub == QLatin1String("--help")) return usage();
    const Opts o = parseOpts(args.mid(3));
    if (!o.error.isEmpty()) {
        err() << o.error << "\n";
        err().flush();
        return 2;
    }

    if (sub == QLatin1String("validate")) {
        if (o.positional.size() != 1) return usage();
        return printReport(validateStorePackageDir(o.positional.first()));
    }
    if (sub == QLatin1String("pack")) {
        if (o.positional.size() != 1) return usage();
        return printReport(packStorePackageDir(o.positional.first()));
    }
    if (sub == QLatin1String("index")) {
        if (o.positional.size() != 1) return usage();
        return printReport(buildStoreIndex(o.positional.first(), o.catalog, o.key));
    }
    if (sub == QLatin1String("keygen")) return runKeygen(o);

    if (sub == QLatin1String("list") || sub == QLatin1String("search")) {
        const bool search = sub == QLatin1String("search");
        if (search && o.positional.isEmpty()) return usage();
        const QVector<LoadedProvider> providers = loadProviders(o.provider);
        if (providers.isEmpty()) return noProviders(o.provider);
        reportProviderErrors(providers);
        const QString needle = o.positional.join(QLatin1Char(' ')).toLower();
        const QString language = lang();
        const QVector<Match> matches = findPackages(providers, [&](const StoreIndexEntry &e) {
            if (!search) return true;
            return e.id.toLower().contains(needle) || e.name.toLower().contains(needle) || e.tags.join(QLatin1Char(' ')).toLower().contains(needle)
                || e.summary.resolve(language).toLower().contains(needle);
        });
        return printList(matches, o.json);
    }
    if (sub == QLatin1String("show")) return runShow(o);
    if (sub == QLatin1String("install")) return runInstall(o);
    if (sub == QLatin1String("update")) return runUpdate(o);
    return usage();
}

} // namespace kai::cli
