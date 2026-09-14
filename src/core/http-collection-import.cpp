#include "core/http-collection-import.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

#include "utils/translation-manager.h"

namespace kai::core {

namespace {

ImportedHttpCollection fail(const QString &message)
{
    ImportedHttpCollection c;
    c.errorMessage = message;
    return c;
}

bool parseObject(const QString &jsonText, QJsonObject *out, ImportedHttpCollection *failure)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &error);
    if (!doc.isObject()) {
        *failure = fail(error.error != QJsonParseError::NoError ? error.errorString()
                                                                 : utils::tr(QStringLiteral("http_import.error.not_object")));
        return false;
    }
    *out = doc.object();
    return true;
}

HttpEntry entry(const QString &name, const QString &value, bool disabled = false)
{
    HttpEntry e;
    e.name = name;
    e.value = value;
    e.enabled = !disabled;
    return e;
}

QString stringOf(const QJsonValue &v)
{
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 15);
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return QString();
}

// Variáveis dinâmicas do Postman que o Kai chama de outro nome.
QString postmanVars(QString text)
{
    text.replace(QStringLiteral("{{$guid}}"), QStringLiteral("{{$uuid}}"));
    text.replace(QStringLiteral("{{$randomUUID}}"), QStringLiteral("{{$uuid}}"));
    text.replace(QStringLiteral("{{$isoTimestamp}}"), QStringLiteral("{{$isoTimestamp}}"));
    return text;
}

// [{key: "token", value: "..."}] -> mapa
QMap<QString, QString> postmanKeyValues(const QJsonValue &v)
{
    QMap<QString, QString> map;
    for (const QJsonValue &item : v.toArray()) {
        const QJsonObject o = item.toObject();
        map.insert(o.value("key").toString(), postmanVars(stringOf(o.value("value"))));
    }
    return map;
}

HttpAuth postmanAuth(const QJsonObject &auth)
{
    HttpAuth a; // Inherit
    if (auth.isEmpty()) {
        return a;
    }
    const QString type = auth.value("type").toString();
    if (type == QStringLiteral("noauth")) {
        a.type = HttpAuthType::None;
    } else if (type == QStringLiteral("bearer")) {
        a.type = HttpAuthType::Bearer;
        a.token = postmanKeyValues(auth.value("bearer")).value("token");
    } else if (type == QStringLiteral("basic")) {
        a.type = HttpAuthType::Basic;
        const QMap<QString, QString> kv = postmanKeyValues(auth.value("basic"));
        a.username = kv.value("username");
        a.password = kv.value("password");
    } else if (type == QStringLiteral("apikey")) {
        a.type = HttpAuthType::ApiKey;
        const QMap<QString, QString> kv = postmanKeyValues(auth.value("apikey"));
        a.keyName = kv.value("key");
        a.keyValue = kv.value("value");
        a.keyInQuery = kv.value("in") == QStringLiteral("query");
    }
    // Outros tipos (oauth2, digest, awsv4...) ficam como "herdar": o Kai ainda não os executa.
    return a;
}

void postmanRequest(const QJsonObject &requestObj, HttpConfig &cfg)
{
    cfg.setMethodName(requestObj.value("method").toString(QStringLiteral("GET")));

    for (const QJsonValue &h : requestObj.value("header").toArray()) {
        const QJsonObject o = h.toObject();
        cfg.headers << entry(o.value("key").toString(), postmanVars(stringOf(o.value("value"))), o.value("disabled").toBool());
    }

    const QJsonValue urlValue = requestObj.value("url");
    if (urlValue.isString()) {
        cfg.url = postmanVars(urlValue.toString());
    } else {
        const QJsonObject urlObj = urlValue.toObject();
        QString raw = urlObj.value("raw").toString();
        if (raw.isEmpty()) {
            QStringList host;
            for (const QJsonValue &v : urlObj.value("host").toArray()) host << v.toString();
            QStringList path;
            for (const QJsonValue &v : urlObj.value("path").toArray()) path << v.toString();
            const QString protocol = urlObj.value("protocol").toString();
            raw = (protocol.isEmpty() ? QString() : protocol + QStringLiteral("://")) + host.join(QLatin1Char('.'))
                  + (path.isEmpty() ? QString() : QStringLiteral("/") + path.join(QLatin1Char('/')));
        }
        // A query sai da URL e vai para a tabela de parâmetros (cada linha pode ser desligada).
        const QJsonArray query = urlObj.value("query").toArray();
        if (!query.isEmpty()) {
            raw = raw.section(QLatin1Char('?'), 0, 0);
            for (const QJsonValue &q : query) {
                const QJsonObject o = q.toObject();
                cfg.queryParams << entry(o.value("key").toString(), postmanVars(stringOf(o.value("value"))),
                                         o.value("disabled").toBool());
            }
        }
        cfg.url = postmanVars(raw);
    }

    const QJsonObject body = requestObj.value("body").toObject();
    const QString mode = body.value("mode").toString();
    if (mode == QStringLiteral("raw")) {
        cfg.body = postmanVars(body.value("raw").toString());
        const QString language = body.value("options").toObject().value("raw").toObject().value("language").toString();
        cfg.bodyType = language == QStringLiteral("json") ? HttpBodyType::Json
                     : language == QStringLiteral("xml") ? HttpBodyType::Xml
                     : HttpBodyType::Raw;
    } else if (mode == QStringLiteral("urlencoded")) {
        cfg.bodyType = HttpBodyType::Form;
        for (const QJsonValue &f : body.value("urlencoded").toArray()) {
            const QJsonObject o = f.toObject();
            cfg.formFields << entry(o.value("key").toString(), postmanVars(stringOf(o.value("value"))), o.value("disabled").toBool());
        }
    } else if (mode == QStringLiteral("formdata")) {
        cfg.bodyType = HttpBodyType::Multipart;
        for (const QJsonValue &f : body.value("formdata").toArray()) {
            const QJsonObject o = f.toObject();
            const bool isFile = o.value("type").toString() == QStringLiteral("file");
            HttpEntry e = entry(o.value("key").toString(),
                                postmanVars(isFile ? stringOf(o.value("src")) : stringOf(o.value("value"))),
                                o.value("disabled").toBool());
            e.isFile = isFile;
            cfg.formFields << e;
        }
    } else if (mode == QStringLiteral("graphql")) {
        cfg.bodyType = HttpBodyType::GraphQl;
        const QJsonObject g = body.value("graphql").toObject();
        cfg.body = postmanVars(g.value("query").toString());
        cfg.graphqlVariables = postmanVars(g.value("variables").toString());
    } else if (mode == QStringLiteral("file")) {
        cfg.bodyType = HttpBodyType::File;
        cfg.bodyFile = body.value("file").toObject().value("src").toString();
    } else {
        cfg.bodyType = HttpBodyType::None;
    }

    cfg.auth = postmanAuth(requestObj.value("auth").toObject());
}

void postmanItems(const QJsonArray &items, const QStringList &path, ImportedHttpCollection &out)
{
    for (const QJsonValue &v : items) {
        const QJsonObject item = v.toObject();
        const QString name = item.value("name").toString();
        if (item.contains("item")) {
            const QStringList childPath = path + QStringList{name.isEmpty() ? QStringLiteral("Folder") : name};
            const HttpAuth auth = postmanAuth(item.value("auth").toObject());
            if (!auth.isDefault()) {
                ImportedFolder folder;
                folder.path = childPath;
                folder.defaults.auth = auth;
                out.folders << folder;
            }
            postmanItems(item.value("item").toArray(), childPath, out);
        } else if (item.contains("request")) {
            ImportedRequest request;
            request.name = name.isEmpty() ? QStringLiteral("Request") : name;
            request.folderPath = path;
            const QJsonValue req = item.value("request");
            if (req.isString()) {
                request.config.url = postmanVars(req.toString());
            } else {
                postmanRequest(req.toObject(), request.config);
            }
            out.requests << request;
        }
    }
}

QString insomniaVars(QString text)
{
    static const QRegularExpression variable(QStringLiteral("\\{\\{\\s*_\\.([A-Za-z0-9_.]+)\\s*\\}\\}"));
    return text.replace(variable, QStringLiteral("{{\\1}}"));
}

void flattenInsomniaEnv(const QJsonObject &data, const QString &prefix, QMap<QString, QString> &out)
{
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const QString key = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key();
        if (it.value().isObject()) {
            flattenInsomniaEnv(it.value().toObject(), key, out);
        } else if (!it.value().isArray()) {
            out.insert(key, insomniaVars(stringOf(it.value())));
        }
    }
}

HttpAuth insomniaAuth(const QJsonObject &auth)
{
    HttpAuth a;
    if (auth.isEmpty() || auth.value("disabled").toBool()) {
        return a;
    }
    const QString type = auth.value("type").toString();
    if (type == QStringLiteral("bearer")) {
        a.type = HttpAuthType::Bearer;
        a.token = insomniaVars(auth.value("token").toString());
    } else if (type == QStringLiteral("basic")) {
        a.type = HttpAuthType::Basic;
        a.username = insomniaVars(auth.value("username").toString());
        a.password = insomniaVars(auth.value("password").toString());
    } else if (type == QStringLiteral("apikey")) {
        a.type = HttpAuthType::ApiKey;
        a.keyName = insomniaVars(auth.value("key").toString());
        a.keyValue = insomniaVars(auth.value("value").toString());
        a.keyInQuery = auth.value("addTo").toString() == QStringLiteral("queryParams");
    } else if (type == QStringLiteral("none")) {
        a.type = HttpAuthType::None;
    }
    return a;
}

HttpBodyType bodyTypeForMime(const QString &mime)
{
    const QString m = mime.toLower();
    if (m.contains(QStringLiteral("json"))) return HttpBodyType::Json;
    if (m.contains(QStringLiteral("xml"))) return HttpBodyType::Xml;
    if (m.contains(QStringLiteral("x-www-form-urlencoded"))) return HttpBodyType::Form;
    if (m.contains(QStringLiteral("multipart/form-data"))) return HttpBodyType::Multipart;
    return HttpBodyType::Raw;
}

} // namespace

bool looksLikePostmanCollection(const QJsonObject &root)
{
    const QJsonObject info = root.value("info").toObject();
    return root.value("item").isArray()
           && (info.value("schema").toString().contains(QStringLiteral("getpostman.com"))
               || info.contains("_postman_id") || info.contains("name"));
}

bool looksLikeInsomniaExport(const QJsonObject &root)
{
    return root.value("_type").toString() == QStringLiteral("export") && root.value("resources").isArray();
}

bool looksLikeHar(const QJsonObject &root)
{
    return root.value("log").isObject() && root.value("log").toObject().value("entries").isArray();
}

ImportedHttpCollection parsePostmanCollection(const QString &jsonText)
{
    ImportedHttpCollection out;
    QJsonObject root;
    if (!parseObject(jsonText, &root, &out)) {
        return out;
    }
    if (!looksLikePostmanCollection(root)) {
        return fail(utils::tr(QStringLiteral("http_import.error.not_postman")));
    }
    out.name = root.value("info").toObject().value("name").toString(QStringLiteral("Postman"));
    out.rootDefaults.auth = postmanAuth(root.value("auth").toObject());
    for (const QJsonValue &v : root.value("variable").toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value("key").toString().isEmpty()) {
            out.variables.insert(o.value("key").toString(), postmanVars(stringOf(o.value("value"))));
        }
    }
    postmanItems(root.value("item").toArray(), {}, out);
    out.ok = true;
    return out;
}

ImportedHttpCollection parseInsomniaExport(const QString &jsonText)
{
    ImportedHttpCollection out;
    QJsonObject root;
    if (!parseObject(jsonText, &root, &out)) {
        return out;
    }
    if (!looksLikeInsomniaExport(root)) {
        return fail(utils::tr(QStringLiteral("http_import.error.not_insomnia")));
    }
    QMap<QString, QJsonObject> byId;
    for (const QJsonValue &v : root.value("resources").toArray()) {
        const QJsonObject r = v.toObject();
        byId.insert(r.value("_id").toString(), r);
    }
    out.name = QStringLiteral("Insomnia");
    for (const QJsonObject &r : std::as_const(byId)) {
        if (r.value("_type").toString() == QStringLiteral("workspace") && !r.value("name").toString().isEmpty()) {
            out.name = r.value("name").toString();
            break;
        }
    }

    auto pathOf = [&byId](const QString &parentId) {
        QStringList path;
        QString current = parentId;
        QSet<QString> seen;
        while (byId.contains(current) && !seen.contains(current)) {
            seen.insert(current);
            const QJsonObject group = byId.value(current);
            if (group.value("_type").toString() != QStringLiteral("request_group")) {
                break;
            }
            path.prepend(group.value("name").toString());
            current = group.value("parentId").toString();
        }
        return path;
    };

    for (const QJsonObject &r : std::as_const(byId)) {
        const QString type = r.value("_type").toString();
        if (type == QStringLiteral("environment")) {
            // O ambiente base (sem subambientes) vira as variáveis da pasta raiz.
            if (byId.value(r.value("parentId").toString()).value("_type").toString() == QStringLiteral("workspace")) {
                flattenInsomniaEnv(r.value("data").toObject(), QString(), out.variables);
            }
        } else if (type == QStringLiteral("request_group")) {
            const HttpAuth auth = insomniaAuth(r.value("authentication").toObject());
            QVector<HttpEntry> headers;
            for (const QJsonValue &h : r.value("headers").toArray()) {
                const QJsonObject o = h.toObject();
                headers << entry(o.value("name").toString(), insomniaVars(o.value("value").toString()), o.value("disabled").toBool());
            }
            if (!auth.isDefault() || !headers.isEmpty()) {
                ImportedFolder folder;
                folder.path = pathOf(r.value("_id").toString());
                folder.defaults.auth = auth;
                folder.defaults.headers = headers;
                out.folders << folder;
            }
        }
    }

    QVector<QPair<double, ImportedRequest>> ordered; // o Insomnia ordena pelo campo metaSortKey
    for (const QJsonObject &r : std::as_const(byId)) {
        if (r.value("_type").toString() != QStringLiteral("request")) {
            continue;
        }
        ImportedRequest request;
        request.name = r.value("name").toString(QStringLiteral("Request"));
        request.folderPath = pathOf(r.value("parentId").toString());
        HttpConfig &cfg = request.config;
        cfg.setMethodName(r.value("method").toString(QStringLiteral("GET")));
        cfg.url = insomniaVars(r.value("url").toString());
        for (const QJsonValue &h : r.value("headers").toArray()) {
            const QJsonObject o = h.toObject();
            cfg.headers << entry(o.value("name").toString(), insomniaVars(o.value("value").toString()), o.value("disabled").toBool());
        }
        for (const QJsonValue &p : r.value("parameters").toArray()) {
            const QJsonObject o = p.toObject();
            cfg.queryParams << entry(o.value("name").toString(), insomniaVars(o.value("value").toString()), o.value("disabled").toBool());
        }
        const QJsonObject body = r.value("body").toObject();
        const QString mime = body.value("mimeType").toString();
        if (mime.contains(QStringLiteral("graphql"))) {
            cfg.bodyType = HttpBodyType::GraphQl;
            const QJsonObject g = QJsonDocument::fromJson(body.value("text").toString().toUtf8()).object();
            cfg.body = insomniaVars(g.value("query").toString());
            if (g.value("variables").isObject()) {
                cfg.graphqlVariables = QString::fromUtf8(QJsonDocument(g.value("variables").toObject()).toJson(QJsonDocument::Compact));
            }
        } else if (mime.isEmpty() && body.isEmpty()) {
            cfg.bodyType = HttpBodyType::None;
        } else if (mime.contains(QStringLiteral("x-www-form-urlencoded")) || mime.contains(QStringLiteral("multipart"))) {
            cfg.bodyType = mime.contains(QStringLiteral("multipart")) ? HttpBodyType::Multipart : HttpBodyType::Form;
            for (const QJsonValue &p : body.value("params").toArray()) {
                const QJsonObject o = p.toObject();
                const bool isFile = o.value("type").toString() == QStringLiteral("file");
                HttpEntry e = entry(o.value("name").toString(),
                                    insomniaVars(isFile ? o.value("fileName").toString() : o.value("value").toString()),
                                    o.value("disabled").toBool());
                e.isFile = isFile;
                cfg.formFields << e;
            }
        } else if (mime.contains(QStringLiteral("octet-stream")) || body.contains("fileName")) {
            cfg.bodyType = HttpBodyType::File;
            cfg.bodyFile = body.value("fileName").toString();
        } else {
            cfg.bodyType = bodyTypeForMime(mime);
            cfg.body = insomniaVars(body.value("text").toString());
        }
        cfg.auth = insomniaAuth(r.value("authentication").toObject());
        ordered.append({r.value("metaSortKey").toDouble(), request});
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &pair : std::as_const(ordered)) {
        out.requests << pair.second;
    }
    out.ok = true;
    return out;
}

ImportedHttpCollection parseHar(const QString &jsonText)
{
    ImportedHttpCollection out;
    QJsonObject root;
    if (!parseObject(jsonText, &root, &out)) {
        return out;
    }
    if (!looksLikeHar(root)) {
        return fail(utils::tr(QStringLiteral("http_import.error.not_har")));
    }
    out.name = QStringLiteral("HAR");
    static const QStringList dropped = {QStringLiteral("host"), QStringLiteral("content-length"), QStringLiteral("cookie"),
                                        QStringLiteral("connection")};
    for (const QJsonValue &v : root.value("log").toObject().value("entries").toArray()) {
        const QJsonObject req = v.toObject().value("request").toObject();
        if (req.isEmpty()) {
            continue;
        }
        ImportedRequest request;
        HttpConfig &cfg = request.config;
        cfg.setMethodName(req.value("method").toString(QStringLiteral("GET")));
        cfg.url = req.value("url").toString();
        const QUrl url(cfg.url);
        request.name = QStringLiteral("%1 %2").arg(cfg.methodName(), url.path().isEmpty() ? QStringLiteral("/") : url.path());
        for (const QJsonValue &h : req.value("headers").toArray()) {
            const QJsonObject o = h.toObject();
            const QString name = o.value("name").toString();
            if (name.startsWith(QLatin1Char(':')) || dropped.contains(name.toLower())) {
                continue;
            }
            cfg.headers << entry(name, o.value("value").toString());
        }
        const QJsonObject post = req.value("postData").toObject();
        if (!post.isEmpty()) {
            cfg.bodyType = bodyTypeForMime(post.value("mimeType").toString());
            const QJsonArray params = post.value("params").toArray();
            if ((cfg.bodyType == HttpBodyType::Form || cfg.bodyType == HttpBodyType::Multipart) && !params.isEmpty()) {
                for (const QJsonValue &p : params) {
                    const QJsonObject o = p.toObject();
                    HttpEntry e = entry(o.value("name").toString(), o.value("value").toString());
                    if (!o.value("fileName").toString().isEmpty()) {
                        e.isFile = true;
                        e.value = o.value("fileName").toString();
                    }
                    cfg.formFields << e;
                }
            } else {
                if (cfg.bodyType == HttpBodyType::Form || cfg.bodyType == HttpBodyType::Multipart) {
                    cfg.bodyType = HttpBodyType::Raw; // sem os campos separados, o texto vai como veio
                }
                cfg.body = post.value("text").toString();
            }
        } else {
            cfg.bodyType = HttpBodyType::None;
        }
        out.requests << request;
    }
    out.ok = true;
    return out;
}

} // namespace kai::core
