#include "core/store-texts.h"

#include <QHash>

#include "utils/translation-manager.h"

namespace kai::core {

QString storeErrorText(const QString &code, const QString &detail)
{
    static const QHash<QString, QString> keys = {
        {QStringLiteral("not_found"), QStringLiteral("store.err.not_found")},
        {QStringLiteral("too_large"), QStringLiteral("store.err.too_large")},
        {QStringLiteral("timeout"), QStringLiteral("store.err.timeout")},
        {QStringLiteral("off_host"), QStringLiteral("store.err.off_host")},
        {QStringLiteral("insecure_url"), QStringLiteral("store.err.insecure_url")},
        {QStringLiteral("hash_mismatch"), QStringLiteral("store.err.hash_mismatch")},
        {QStringLiteral("unsafe_path"), QStringLiteral("store.err.unsafe_path")},
        {QStringLiteral("invalid_manifest"), QStringLiteral("store.err.invalid_manifest")},
        {QStringLiteral("invalid_kai_yml"), QStringLiteral("store.err.invalid_kai_yml")},
        {QStringLiteral("store_format_newer"), QStringLiteral("store.err.store_format_newer")},
        {QStringLiteral("git_missing"), QStringLiteral("store.err.git_missing")},
        {QStringLiteral("bad_url"), QStringLiteral("store.err.bad_url")},
        {QStringLiteral("git_failed"), QStringLiteral("store.err.git_failed")},
        {QStringLiteral("network"), QStringLiteral("store.err.network")},
        {QStringLiteral("missing_manifest_hash"), QStringLiteral("store.err.missing_manifest_hash")},
        {QStringLiteral("bad_document"), QStringLiteral("store.err.bad_document")},
        {QStringLiteral("cancelled"), QStringLiteral("store.err.cancelled")},
        {QStringLiteral("disk"), QStringLiteral("store.err.disk")},
        {QStringLiteral("too_many_files"), QStringLiteral("store.err.too_many_files")},
        {QStringLiteral("missing_kai_yml"), QStringLiteral("store.err.missing_kai_yml")},
        {QStringLiteral("no_raw"), QStringLiteral("store.err.no_raw")},
        {QStringLiteral("bad_manifest"), QStringLiteral("store.err.bad_manifest")},
        {QStringLiteral("too_many_redirects"), QStringLiteral("store.err.too_many_redirects")},
        {QStringLiteral("missing_packages"), QStringLiteral("store.err.missing_packages")},
        {QStringLiteral("missing_store_format"), QStringLiteral("store.err.missing_store_format")},
        {QStringLiteral("off_base"), QStringLiteral("store.err.off_base")},
        {QStringLiteral("not_loaded"), QStringLiteral("store.err.not_loaded")},
        {QStringLiteral("signature_invalid"), QStringLiteral("store.err.signature_invalid")},
        {QStringLiteral("http"), QStringLiteral("store.err.http")},
        {QStringLiteral("unknown"), QStringLiteral("store.err.unknown")},
    };
    QString base = code;
    // Login recusado: o motivo mais comum é um token ausente, errado ou vencido.
    if (code == QLatin1String("http_401") || code == QLatin1String("http_403")) {
        return utils::tr(QStringLiteral("store.err.auth")).arg(code.mid(5));
    }
    if (code.startsWith(QLatin1String("http_"))) {
        return utils::tr(keys.value(QStringLiteral("http"))).arg(code.mid(5));
    }
    const QString key = keys.value(base, keys.value(QStringLiteral("unknown")));
    const QString text = utils::tr(key);
    return detail.isEmpty() ? text : text + QStringLiteral(" (%1)").arg(detail);
}

QString storeIssueText(const QString &code, const QString &path)
{
    static const QHash<QString, QString> keys = {
        {QStringLiteral("missing_required"), QStringLiteral("store.issue.missing_required")},
        {QStringLiteral("unsupported_package_format"), QStringLiteral("store.issue.unsupported_package_format")},
        {QStringLiteral("bad_id"), QStringLiteral("store.issue.bad_id")},
        {QStringLiteral("bad_kind"), QStringLiteral("store.issue.bad_kind")},
        {QStringLiteral("bad_version"), QStringLiteral("store.issue.bad_version")},
        {QStringLiteral("missing_name"), QStringLiteral("store.issue.missing_name")},
        {QStringLiteral("unknown_capability"), QStringLiteral("store.issue.unknown_capability")},
        {QStringLiteral("project_needs_one_source"), QStringLiteral("store.issue.project_needs_one_source")},
        {QStringLiteral("clone_only_for_project"), QStringLiteral("store.issue.clone_only_for_project")},
        {QStringLiteral("no_files"), QStringLiteral("store.issue.no_files")},
        {QStringLiteral("unsafe_path"), QStringLiteral("store.issue.unsafe_path")},
        {QStringLiteral("bad_sha256"), QStringLiteral("store.issue.bad_sha256")},
        {QStringLiteral("duplicate_path"), QStringLiteral("store.issue.duplicate_path")},
        {QStringLiteral("missing_kai_yml"), QStringLiteral("store.issue.missing_kai_yml")},
        {QStringLiteral("clone_url_not_https"), QStringLiteral("store.issue.clone_url_not_https")},
        {QStringLiteral("missing_ref"), QStringLiteral("store.issue.missing_ref")},
        {QStringLiteral("no_package_yml"), QStringLiteral("store.issue.no_package_yml")},
        {QStringLiteral("bad_document"), QStringLiteral("store.issue.bad_document")},
        {QStringLiteral("file_missing"), QStringLiteral("store.issue.file_missing")},
        {QStringLiteral("symlink"), QStringLiteral("store.issue.symlink")},
        {QStringLiteral("file_too_large"), QStringLiteral("store.issue.file_too_large")},
        {QStringLiteral("hash_mismatch"), QStringLiteral("store.issue.hash_mismatch")},
        {QStringLiteral("package_too_large"), QStringLiteral("store.issue.package_too_large")},
        {QStringLiteral("too_many_files"), QStringLiteral("store.issue.too_many_files")},
        {QStringLiteral("unlisted_file"), QStringLiteral("store.issue.unlisted_file")},
        {QStringLiteral("media_not_raster"), QStringLiteral("store.issue.media_not_raster")},
        {QStringLiteral("media_too_large"), QStringLiteral("store.issue.media_too_large")},
        {QStringLiteral("symlink_skipped"), QStringLiteral("store.issue.symlink_skipped")},
        {QStringLiteral("write_failed"), QStringLiteral("store.issue.write_failed")},
        {QStringLiteral("no_store_yml"), QStringLiteral("store.issue.no_store_yml")},
        {QStringLiteral("key_unreadable"), QStringLiteral("store.issue.key_unreadable")},
        {QStringLiteral("key_invalid"), QStringLiteral("store.issue.key_invalid")},
        {QStringLiteral("key_mismatch"), QStringLiteral("store.issue.key_mismatch")},
        {QStringLiteral("no_signing_key_in_header"), QStringLiteral("store.issue.no_signing_key_in_header")},
        {QStringLiteral("kai_file"), QStringLiteral("store.issue.kai_file")},
        {QStringLiteral("missing_store_format"), QStringLiteral("store.issue.missing_store_format")},
        {QStringLiteral("missing_packages"), QStringLiteral("store.issue.missing_packages")},
        {QStringLiteral("store_format_newer"), QStringLiteral("store.issue.store_format_newer")},
    };
    const QString key = keys.value(code);
    const QString text = key.isEmpty() ? code : utils::tr(key);
    return path.isEmpty() ? text : QStringLiteral("%1: %2").arg(path, text);
}

QString storeCompatText(const QString &reason)
{
    if (reason.startsWith(QLatin1String("kai_min:"))) {
        return utils::tr(QStringLiteral("store.compat.kai_min")).arg(reason.mid(8));
    }
    if (reason.startsWith(QLatin1String("capability:"))) {
        return utils::tr(QStringLiteral("store.compat.capability")).arg(reason.mid(11));
    }
    return reason;
}

QString storeKindLabel(const QString &kind)
{
    static const QHash<QString, QString> keys = {
        {QStringLiteral("command"), QStringLiteral("store.kind.command")},
        {QStringLiteral("folder"), QStringLiteral("store.kind.folder")},
        {QStringLiteral("pipeline"), QStringLiteral("store.kind.pipeline")},
        {QStringLiteral("project"), QStringLiteral("store.kind.project")},
        {QStringLiteral("environment"), QStringLiteral("store.kind.environment")},
        {QStringLiteral("profile"), QStringLiteral("store.kind.profile")},
    };
    const QString key = keys.value(kind);
    return key.isEmpty() ? kind : utils::tr(key);
}

QString storeTrustLabel(const QString &trust)
{
    static const QHash<QString, QString> keys = {
        {QStringLiteral("official"), QStringLiteral("store.trust.official")},
        {QStringLiteral("verified"), QStringLiteral("store.trust.verified")},
        {QStringLiteral("community"), QStringLiteral("store.trust.community")},
        {QStringLiteral("custom"), QStringLiteral("store.trust.custom")},
    };
    const QString key = keys.value(trust);
    return key.isEmpty() ? trust : utils::tr(key);
}

} // namespace kai::core
