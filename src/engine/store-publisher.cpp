#include "engine/store-publisher.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include "core/kai-file-validator.h"
#include "core/store-model.h"
#include "core/yaml-bridge.h"
#include "engine/store-client.h"
#include "engine/store-signature.h"

namespace kai::engine {

int StorePublishReport::errorCount() const
{
    return static_cast<int>(std::count_if(issues.begin(), issues.end(), [](const StorePublishIssue &i) { return i.error; }));
}

int StorePublishReport::warningCount() const
{
    return static_cast<int>(issues.size()) - errorCount();
}

namespace {

QString sha256File(const QString &path, bool *ok = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    if (ok) *ok = true;
    return QString::fromLatin1(hash.result().toHex());
}

bool writeText(const QString &path, const QByteArray &data)
{
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

QJsonObject readObject(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("not_found");
        return {};
    }
    return StoreClient::parseDocument(f.readAll(), error);
}

QJsonArray toJsonArray(const QStringList &list)
{
    QJsonArray a;
    for (const QString &s : list) a.append(s);
    return a;
}

QJsonValue localizedToJson(const core::LocalizedText &t)
{
    QJsonObject o;
    for (auto it = t.byLang.begin(); it != t.byLang.end(); ++it) o.insert(it.key(), it.value());
    return o;
}

bool isMediaPath(const core::StoreManifest &m, const QString &rel)
{
    return rel == m.icon || m.screenshots.contains(rel) || rel == m.changelog;
}

bool isRasterImage(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".png")) || lower.endsWith(QLatin1String(".jpg"))
        || lower.endsWith(QLatin1String(".jpeg")) || lower.endsWith(QLatin1String(".webp"))
        || lower.endsWith(QLatin1String(".gif"));
}

} // namespace

StorePublishReport validateStorePackageDir(const QString &dir)
{
    StorePublishReport report;
    const auto err = [&report](const QString &path, const QString &code, const QString &text = QString()) {
        report.issues.append({true, path, code, text});
    };
    const auto warn = [&report](const QString &path, const QString &code, const QString &text = QString()) {
        report.issues.append({false, path, code, text});
    };

    QString readError;
    const QJsonObject obj = readObject(QDir(dir).filePath(QStringLiteral("package.yml")), &readError);
    if (obj.isEmpty()) {
        err(QStringLiteral("package.yml"), readError == QLatin1String("not_found") ? QStringLiteral("no_package_yml")
                                                                                   : QStringLiteral("bad_document"));
        return report;
    }
    QVector<core::StoreManifestIssue> parseIssues;
    const auto manifest = core::parseStoreManifest(obj, &parseIssues);
    if (!manifest) {
        err(QStringLiteral("package.yml"), QStringLiteral("missing_required"));
        return report;
    }
    for (const core::StoreManifestIssue &i : core::validateStoreManifest(*manifest)) {
        report.issues.append({i.error, i.path, i.message, QString()});
    }

    qint64 total = 0;
    QSet<QString> declared;
    for (const core::StoreFile &f : manifest->files) {
        declared.insert(f.path);
        if (!core::isSafeStorePath(f.path)) continue; // já reportado
        const QString full = QDir(dir).filePath(f.path);
        const QFileInfo info(full);
        if (!info.isFile()) {
            err(f.path, QStringLiteral("file_missing"));
            continue;
        }
        if (info.isSymLink()) {
            err(f.path, QStringLiteral("symlink"));
            continue;
        }
        total += info.size();
        if (info.size() > store_limits::kFileBytes) err(f.path, QStringLiteral("file_too_large"));
        bool ok = false;
        const QString actual = sha256File(full, &ok);
        if (ok && actual != f.sha256.toLower()) err(f.path, QStringLiteral("hash_mismatch"));
    }
    if (total > store_limits::kPackageBytes) err(QStringLiteral("files"), QStringLiteral("package_too_large"));
    if (manifest->files.size() > store_limits::kMaxFiles) err(QStringLiteral("files"), QStringLiteral("too_many_files"));

    if (declared.contains(QStringLiteral("kai.yml"))) {
        QFile f(QDir(dir).filePath(QStringLiteral("kai.yml")));
        if (f.open(QIODevice::ReadOnly)) {
            const core::ValidationResult v = core::validateKaiFileText(QString::fromUtf8(f.readAll()));
            for (const core::ValidationIssue &i : v.issues) {
                report.issues.append({i.severity == core::ValidationSeverity::Error,
                                      QStringLiteral("kai.yml: ") + i.path, QStringLiteral("kai_file"), i.message});
            }
        }
    }

    // Arquivos na pasta que o manifesto não lista (esquecidos no pack?).
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString rel = QDir(dir).relativeFilePath(it.filePath());
        if (rel == QLatin1String("package.yml") || rel.startsWith(QLatin1Char('.')) || rel.contains(QLatin1String("/."))
            || isMediaPath(*manifest, rel) || declared.contains(rel) || manifest->clone) {
            continue;
        }
        warn(rel, QStringLiteral("unlisted_file"));
    }

    QStringList media = manifest->screenshots;
    if (!manifest->icon.isEmpty()) media << manifest->icon;
    for (const QString &rel : media) {
        if (!core::isSafeStorePath(rel)) {
            err(rel, QStringLiteral("unsafe_path"));
            continue;
        }
        const QFileInfo info(QDir(dir).filePath(rel));
        if (!info.isFile()) {
            err(rel, QStringLiteral("file_missing"));
        } else if (!isRasterImage(rel)) {
            err(rel, QStringLiteral("media_not_raster"));
        } else if (info.size() > store_limits::kMediaBytes) {
            err(rel, QStringLiteral("media_too_large"));
        }
    }
    report.ok = report.errorCount() == 0;
    return report;
}

StorePublishReport packStorePackageDir(const QString &dir)
{
    StorePublishReport report;
    const QString manifestPath = QDir(dir).filePath(QStringLiteral("package.yml"));
    QString readError;
    QJsonObject obj = readObject(manifestPath, &readError);
    if (obj.isEmpty()) {
        report.issues.append({true, QStringLiteral("package.yml"),
                              readError == QLatin1String("not_found") ? QStringLiteral("no_package_yml")
                                                                      : QStringLiteral("bad_document"),
                              QString()});
        return report;
    }
    const auto manifest = core::parseStoreManifest(obj);
    if (!manifest) {
        report.issues.append({true, QStringLiteral("package.yml"), QStringLiteral("missing_required"), QString()});
        return report;
    }

    QJsonArray files;
    if (!manifest->clone) {
        QStringList rels;
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString rel = QDir(dir).relativeFilePath(it.filePath());
            if (rel == QLatin1String("package.yml") || rel.startsWith(QLatin1Char('.')) || rel.contains(QLatin1String("/."))
                || isMediaPath(*manifest, rel)) {
                continue;
            }
            if (it.fileInfo().isSymLink()) {
                report.issues.append({false, rel, QStringLiteral("symlink_skipped"), QString()});
                continue;
            }
            rels << rel;
        }
        std::sort(rels.begin(), rels.end());
        for (const QString &rel : rels) {
            if (!core::isSafeStorePath(rel)) {
                report.issues.append({true, rel, QStringLiteral("unsafe_path"), QString()});
                continue;
            }
            files.append(QJsonObject{{QStringLiteral("path"), rel},
                                     {QStringLiteral("sha256"), sha256File(QDir(dir).filePath(rel))}});
        }
    }
    if (report.errorCount() > 0) {
        return report;
    }
    obj.insert(QStringLiteral("files"), files);
    if (manifest->clone) obj.remove(QStringLiteral("files"));
    const QString yaml = core::jsonTextToYamlText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    if (!writeText(manifestPath, yaml.toUtf8())) {
        report.issues.append({true, QStringLiteral("package.yml"), QStringLiteral("write_failed"), QString()});
        return report;
    }
    report.written << manifestPath;
    report.ok = true;
    return report;
}

StorePublishReport buildStoreIndex(const QString &repoDir, const QString &catalogFile, const QString &signKeyFile)
{
    StorePublishReport report;
    const auto fail = [&report](const QString &path, const QString &code) {
        report.issues.append({true, path, code, QString()});
        return report;
    };

    QString headerError;
    const QJsonObject headerObj = readObject(QDir(repoDir).filePath(QStringLiteral("store.yml")), &headerError);
    if (headerObj.isEmpty()) {
        return fail(QStringLiteral("store.yml"), headerError == QLatin1String("not_found") ? QStringLiteral("no_store_yml")
                                                                                          : QStringLiteral("bad_document"));
    }
    core::StoreParseError parseError;
    const auto header = core::parseStoreHeader(headerObj, &parseError);
    if (!header) {
        return fail(QStringLiteral("store.yml"), parseError.message);
    }

    // packages/<publisher>/<slug>/<versão>/package.yml
    QMap<QString, QJsonObject> byId; // id -> linha do índice
    QMap<QString, QVector<QJsonObject>> versionsById;
    const QDir packages(QDir(repoDir).filePath(QStringLiteral("packages")));
    QDirIterator it(packages.absolutePath(), {QStringLiteral("package.yml")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString pkgDir = QFileInfo(it.filePath()).absolutePath();
        const QString relManifest = QDir(repoDir).relativeFilePath(it.filePath());
        const StorePublishReport v = validateStorePackageDir(pkgDir);
        for (StorePublishIssue issue : v.issues) {
            issue.path = relManifest.section(QLatin1Char('/'), 0, -2) + QLatin1Char('/') + issue.path;
            report.issues.append(issue);
        }
        if (!v.ok) continue;
        const QJsonObject obj = readObject(it.filePath(), nullptr);
        const auto m = core::parseStoreManifest(obj);
        if (!m) continue;
        QFile f(it.filePath());
        f.open(QIODevice::ReadOnly);
        const QString manifestSha = QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());

        QJsonObject version{{QStringLiteral("version"), m->version},
                            {QStringLiteral("manifest"), relManifest},
                            {QStringLiteral("sha256"), manifestSha}};
        if (!m->kaiMin.isEmpty()) version.insert(QStringLiteral("kai_min"), m->kaiMin);
        if (!m->requires_.isEmpty()) version.insert(QStringLiteral("requires"), toJsonArray(m->requires_));
        versionsById[m->id] << version;

        const bool first = !byId.contains(m->id);
        const bool newer = !first && core::compareVersions(m->version, byId[m->id].value(QStringLiteral("latest")).toString()) > 0;
        if (first || newer) {
            QJsonObject row;
            row.insert(QStringLiteral("id"), m->id);
            row.insert(QStringLiteral("kind"), m->kind);
            row.insert(QStringLiteral("name"), m->name);
            row.insert(QStringLiteral("summary"), localizedToJson(m->description));
            if (!m->category.isEmpty()) row.insert(QStringLiteral("category"), m->category);
            if (!m->tags.isEmpty()) row.insert(QStringLiteral("tags"), toJsonArray(m->tags));
            row.insert(QStringLiteral("latest"), m->version);
            if (!m->icon.isEmpty()) {
                row.insert(QStringLiteral("icon"), QDir(repoDir).relativeFilePath(QDir(pkgDir).filePath(m->icon)));
            }
            byId[m->id] = row;
        }
    }
    if (report.errorCount() > 0) {
        return report;
    }

    QJsonArray rows;
    for (auto idIt = byId.begin(); idIt != byId.end(); ++idIt) {
        QVector<QJsonObject> versions = versionsById.value(idIt.key());
        std::sort(versions.begin(), versions.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return core::compareVersions(a.value(QStringLiteral("version")).toString(),
                                         b.value(QStringLiteral("version")).toString()) > 0;
        });
        QJsonArray vs;
        for (const QJsonObject &v : versions) vs.append(v);
        QJsonObject row = idIt.value();
        row.insert(QStringLiteral("versions"), vs);
        rows.append(row);
    }

    QJsonObject out;
    const bool single = !catalogFile.isEmpty();
    if (single) out = headerObj;
    out.insert(QStringLiteral("store_format"), core::kStoreFormatSupported);
    out.insert(QStringLiteral("packages"), rows);
    out.insert(QStringLiteral("generated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!signKeyFile.isEmpty()) {
        QFile keyFile(signKeyFile);
        if (!keyFile.open(QIODevice::ReadOnly)) {
            return fail(signKeyFile, QStringLiteral("key_unreadable"));
        }
        const QByteArray seed = store_signature::fromBase64(QString::fromLatin1(keyFile.readAll()).trimmed());
        if (seed.size() != 32) {
            return fail(signKeyFile, QStringLiteral("key_invalid"));
        }
        const QString declared = header->signingKey;
        const QString mine = store_signature::toBase64(store_signature::publicKeyFromSecret(seed));
        if (!declared.isEmpty() && declared != mine) {
            return fail(signKeyFile, QStringLiteral("key_mismatch"));
        }
        if (declared.isEmpty()) {
            report.issues.append({false, QStringLiteral("store.yml"), QStringLiteral("no_signing_key_in_header"), QString()});
        } else {
            out.insert(QStringLiteral("signature"),
                       store_signature::toBase64(store_signature::sign(store_signature::canonicalBody(out), seed)));
        }
    }

    const QString target = single ? catalogFile : QDir(repoDir).filePath(header->indexPath);
    QByteArray bytes;
    if (single && !target.endsWith(QLatin1String(".json"))) {
        bytes = core::jsonTextToYamlText(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact))).toUtf8();
    } else {
        bytes = QJsonDocument(out).toJson(QJsonDocument::Indented);
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!writeText(target, bytes)) {
        return fail(target, QStringLiteral("write_failed"));
    }
    report.written << target;
    report.ok = true;
    return report;
}

} // namespace kai::engine
