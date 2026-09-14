#include "core/http-types.h"

namespace kai::core {

QJsonObject HttpEntry::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["value"] = value;
    if (!enabled) obj["enabled"] = false; // default é true — só grava a exceção
    if (isFile) obj["file"] = true;
    return obj;
}

HttpEntry HttpEntry::fromJson(const QJsonObject &obj)
{
    HttpEntry e;
    e.name = obj.value("name").toString();
    e.value = obj.value("value").toString();
    e.enabled = obj.value("enabled").toBool(true);
    e.isFile = obj.value("file").toBool(false);
    return e;
}

QVector<HttpEntry> httpEntriesFromJson(const QJsonValue &value)
{
    QVector<HttpEntry> entries;
    if (value.isArray()) {
        for (const QJsonValue &v : value.toArray()) {
            if (v.isObject()) {
                entries << HttpEntry::fromJson(v.toObject());
            }
        }
    } else if (value.isObject()) {
        // Formato antigo: mapa Nome -> valor (a ordem vira a alfabética do JSON).
        const QJsonObject obj = value.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            HttpEntry e;
            e.name = it.key();
            e.value = it.value().toString();
            entries << e;
        }
    }
    return entries;
}

QJsonArray httpEntriesToJson(const QVector<HttpEntry> &entries)
{
    QJsonArray arr;
    for (const HttpEntry &e : entries) {
        arr.append(e.toJson());
    }
    return arr;
}

QString httpEntryValue(const QVector<HttpEntry> &entries, const QString &name)
{
    for (const HttpEntry &e : entries) {
        if (e.name.compare(name, Qt::CaseInsensitive) == 0) {
            return e.value;
        }
    }
    return QString();
}

QString httpBodyTypeToString(HttpBodyType type)
{
    switch (type) {
    case HttpBodyType::Raw:       return QStringLiteral("raw");
    case HttpBodyType::None:      return QStringLiteral("none");
    case HttpBodyType::Json:      return QStringLiteral("json");
    case HttpBodyType::Xml:       return QStringLiteral("xml");
    case HttpBodyType::Form:      return QStringLiteral("form");
    case HttpBodyType::Multipart: return QStringLiteral("multipart");
    case HttpBodyType::File:      return QStringLiteral("file");
    case HttpBodyType::GraphQl:   return QStringLiteral("graphql");
    }
    return QStringLiteral("raw");
}

HttpBodyType httpBodyTypeFromString(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == QStringLiteral("none"))      return HttpBodyType::None;
    if (v == QStringLiteral("json"))      return HttpBodyType::Json;
    if (v == QStringLiteral("xml"))       return HttpBodyType::Xml;
    if (v == QStringLiteral("form"))      return HttpBodyType::Form;
    if (v == QStringLiteral("multipart")) return HttpBodyType::Multipart;
    if (v == QStringLiteral("file"))      return HttpBodyType::File;
    if (v == QStringLiteral("graphql"))   return HttpBodyType::GraphQl;
    return HttpBodyType::Raw;
}

QString httpAuthTypeToString(HttpAuthType type)
{
    switch (type) {
    case HttpAuthType::Inherit: return QStringLiteral("inherit");
    case HttpAuthType::None:    return QStringLiteral("none");
    case HttpAuthType::Basic:   return QStringLiteral("basic");
    case HttpAuthType::Bearer:  return QStringLiteral("bearer");
    case HttpAuthType::ApiKey:  return QStringLiteral("api_key");
    }
    return QStringLiteral("inherit");
}

HttpAuthType httpAuthTypeFromString(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == QStringLiteral("none"))    return HttpAuthType::None;
    if (v == QStringLiteral("basic"))   return HttpAuthType::Basic;
    if (v == QStringLiteral("bearer"))  return HttpAuthType::Bearer;
    if (v == QStringLiteral("api_key") || v == QStringLiteral("apikey")) return HttpAuthType::ApiKey;
    return HttpAuthType::Inherit;
}

QJsonObject HttpAuth::toJson() const
{
    QJsonObject obj;
    obj["type"] = httpAuthTypeToString(type);
    switch (type) {
    case HttpAuthType::Basic:
        if (!username.isEmpty()) obj["username"] = username;
        if (!password.isEmpty()) obj["password"] = password;
        break;
    case HttpAuthType::Bearer:
        if (!token.isEmpty()) obj["token"] = token;
        break;
    case HttpAuthType::ApiKey:
        if (!keyName.isEmpty()) obj["key_name"] = keyName;
        if (!keyValue.isEmpty()) obj["key_value"] = keyValue;
        if (keyInQuery) obj["key_in"] = QStringLiteral("query");
        break;
    case HttpAuthType::Inherit:
    case HttpAuthType::None:
        break;
    }
    return obj;
}

HttpAuth HttpAuth::fromJson(const QJsonObject &obj)
{
    HttpAuth a;
    a.type = httpAuthTypeFromString(obj.value("type").toString());
    a.username = obj.value("username").toString();
    a.password = obj.value("password").toString();
    a.token = obj.value("token").toString();
    a.keyName = obj.value("key_name").toString();
    a.keyValue = obj.value("key_value").toString();
    a.keyInQuery = obj.value("key_in").toString().compare(QStringLiteral("query"), Qt::CaseInsensitive) == 0;
    return a;
}

QJsonObject HttpOptions::toJson() const
{
    QJsonObject obj;
    if (followRedirects) obj["follow_redirects"] = *followRedirects;
    if (maxRedirects) obj["max_redirects"] = *maxRedirects;
    if (verifyTls) obj["verify_tls"] = *verifyTls;
    if (proxy) obj["proxy"] = *proxy;
    if (timeoutSec) obj["timeout_sec"] = *timeoutSec;
    if (useCookies) obj["use_cookies"] = *useCookies;
    return obj;
}

HttpOptions HttpOptions::fromJson(const QJsonObject &obj)
{
    HttpOptions o;
    if (obj.contains("follow_redirects")) o.followRedirects = obj.value("follow_redirects").toBool(true);
    if (obj.contains("max_redirects")) o.maxRedirects = obj.value("max_redirects").toInt(10);
    if (obj.contains("verify_tls")) o.verifyTls = obj.value("verify_tls").toBool(true);
    if (obj.contains("proxy")) o.proxy = obj.value("proxy").toString();
    if (obj.contains("timeout_sec")) o.timeoutSec = obj.value("timeout_sec").toInt(0);
    if (obj.contains("use_cookies")) o.useCookies = obj.value("use_cookies").toBool(true);
    return o;
}

QJsonObject HttpDefaults::toJson() const
{
    QJsonObject obj;
    if (!headers.isEmpty()) obj["headers"] = httpEntriesToJson(headers);
    if (!auth.isDefault()) obj["auth"] = auth.toJson();
    if (!options.isEmpty()) obj["options"] = options.toJson();
    return obj;
}

HttpDefaults HttpDefaults::fromJson(const QJsonObject &obj)
{
    HttpDefaults d;
    d.headers = httpEntriesFromJson(obj.value("headers"));
    d.auth = HttpAuth::fromJson(obj.value("auth").toObject());
    d.options = HttpOptions::fromJson(obj.value("options").toObject());
    return d;
}

} // namespace kai::core
