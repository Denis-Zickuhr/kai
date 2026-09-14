#include "core/http-resolve.h"

#include <QHash>
#include <QMap>
#include <QSet>

namespace kai::core {

QVector<Folder> folderChain(const QString &folderId, const QVector<Folder> &folders)
{
    QMap<QString, const Folder *> byId;
    for (const Folder &f : folders) {
        byId.insert(f.id, &f);
    }
    QVector<Folder> chain;
    QString current = folderId;
    QSet<QString> seen;
    while (!current.isEmpty() && byId.contains(current) && !seen.contains(current)) {
        seen.insert(current);
        const Folder &f = *byId.value(current);
        chain.prepend(f); // raiz primeiro
        current = f.parentId.value_or(QString());
    }
    return chain;
}

namespace {

QString headerKey(const QString &name)
{
    return name.trimmed().toLower();
}

// Mescla mantendo as linhas desligadas (o chamador decide se as descarta):
// uma camada que menciona um nome substitui todas as anteriores de mesmo nome.
QVector<HttpEntry> mergeKeepingDisabled(const QVector<QVector<HttpEntry>> &layers)
{
    QVector<HttpEntry> out;
    for (const QVector<HttpEntry> &layer : layers) {
        QSet<QString> names;
        for (const HttpEntry &e : layer) {
            if (!headerKey(e.name).isEmpty()) {
                names.insert(headerKey(e.name));
            }
        }
        if (names.isEmpty()) {
            continue;
        }
        out.removeIf([&names](const HttpEntry &e) { return names.contains(headerKey(e.name)); });
        for (const HttpEntry &e : layer) {
            if (!headerKey(e.name).isEmpty()) {
                out << e;
            }
        }
    }
    return out;
}

QVector<HttpEntry> onlyEnabled(const QVector<HttpEntry> &entries)
{
    QVector<HttpEntry> out;
    for (const HttpEntry &e : entries) {
        if (e.enabled && !e.name.trimmed().isEmpty()) {
            out << e;
        }
    }
    return out;
}

} // namespace

QVector<HttpEntry> mergeHttpHeaders(const QVector<QVector<HttpEntry>> &layers)
{
    return onlyEnabled(mergeKeepingDisabled(layers));
}

HttpRequestSpec resolveHttpRequest(const HttpConfig &config, const QVector<Folder> &chain)
{
    HttpRequestSpec spec;
    spec.method = config.methodName();
    spec.url = config.url;

    QVector<QVector<HttpEntry>> layers;
    for (const Folder &f : chain) {
        layers << f.httpDefaults.headers;
    }
    layers << config.headers;
    spec.headers = mergeHttpHeaders(layers);

    spec.queryParams = onlyEnabled(config.queryParams);

    spec.auth = config.auth;
    if (spec.auth.type == HttpAuthType::Inherit) {
        spec.auth = inheritedHttpAuth(chain);
    }

    spec.bodyType = config.bodyType;
    spec.body = config.body;
    spec.graphqlVariables = config.graphqlVariables;
    spec.formFields = onlyEnabled(config.formFields);
    spec.bodyFile = config.bodyFile;
    spec.responseFile = config.responseFile;
    spec.envExtractors = config.envExtractors;
    spec.assertions = config.assertions;

    // Opções: o comando primeiro, depois as pastas da mais próxima para a mais distante.
    QVector<const HttpOptions *> levels;
    levels << &config.options;
    for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
        levels << &it->httpDefaults.options;
    }
    HttpEffectiveOptions eff;
    bool gotFollow = false, gotMax = false, gotTls = false, gotProxy = false, gotTimeout = false, gotCookies = false;
    for (const HttpOptions *o : levels) {
        if (!gotFollow && o->followRedirects) { eff.followRedirects = *o->followRedirects; gotFollow = true; }
        if (!gotMax && o->maxRedirects) { eff.maxRedirects = *o->maxRedirects; gotMax = true; }
        if (!gotTls && o->verifyTls) { eff.verifyTls = *o->verifyTls; gotTls = true; }
        if (!gotProxy && o->proxy) { eff.proxy = o->proxy->trimmed().isEmpty() ? QStringLiteral("system") : *o->proxy; gotProxy = true; }
        if (!gotTimeout && o->timeoutSec) { eff.timeoutSec = *o->timeoutSec; gotTimeout = true; }
        if (!gotCookies && o->useCookies) { eff.useCookies = *o->useCookies; gotCookies = true; }
    }
    spec.options = eff;
    return spec;
}

QVector<InheritedHttpHeader> inheritedHttpHeaders(const QVector<Folder> &chain)
{
    // Mesma mescla, mas lembrando de qual pasta cada linha veio.
    struct Tagged { HttpEntry entry; QString folderId; QString folderName; };
    QVector<Tagged> out;
    for (const Folder &f : chain) {
        QSet<QString> names;
        for (const HttpEntry &e : f.httpDefaults.headers) {
            if (!headerKey(e.name).isEmpty()) {
                names.insert(headerKey(e.name));
            }
        }
        if (names.isEmpty()) {
            continue;
        }
        out.removeIf([&names](const Tagged &t) { return names.contains(headerKey(t.entry.name)); });
        for (const HttpEntry &e : f.httpDefaults.headers) {
            if (!headerKey(e.name).isEmpty()) {
                out << Tagged{e, f.id, f.name};
            }
        }
    }
    QVector<InheritedHttpHeader> result;
    for (const Tagged &t : out) {
        if (t.entry.enabled) {
            result << InheritedHttpHeader{t.entry, t.folderId, t.folderName};
        }
    }
    return result;
}

HttpAuth inheritedHttpAuth(const QVector<Folder> &chain, QString *originFolderName)
{
    for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
        if (it->httpDefaults.auth.type != HttpAuthType::Inherit) {
            if (originFolderName) {
                *originFolderName = it->name;
            }
            return it->httpDefaults.auth;
        }
    }
    if (originFolderName) {
        originFolderName->clear();
    }
    HttpAuth none;
    none.type = HttpAuthType::None;
    return none;
}

} // namespace kai::core
