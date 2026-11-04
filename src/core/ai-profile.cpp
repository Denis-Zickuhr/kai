#include "core/ai-profile.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QUrl>
#include <algorithm>

namespace kai::core {

QString aiWireFormatToString(AiWireFormat format)
{
    return format == AiWireFormat::Anthropic ? QStringLiteral("anthropic") : QStringLiteral("openai_compatible");
}

AiWireFormat aiWireFormatFromString(const QString &value)
{
    return value == QLatin1String("anthropic") ? AiWireFormat::Anthropic : AiWireFormat::OpenAiCompatible;
}

QString AiProfile::resolvedKey() const
{
    if (!apiKey.trimmed().isEmpty()) {
        return apiKey.trimmed();
    }
    const QString envName = apiKeyEnv.trimmed();
    if (envName.isEmpty()) {
        return QString();
    }
    return QProcessEnvironment::systemEnvironment().value(envName).trimmed();
}

QString AiProfile::resolvedBaseUrl() const
{
    QString url = baseUrl.trimmed();
    if (url.isEmpty()) {
        url = wireFormat == AiWireFormat::Anthropic ? QStringLiteral("https://api.anthropic.com")
                                                    : QStringLiteral("https://api.openai.com/v1");
    }
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

QString AiProfile::chatUrl() const
{
    const QString base = resolvedBaseUrl();
    if (wireFormat == AiWireFormat::Anthropic) {
        return base.endsWith(QLatin1String("/v1")) ? base + QStringLiteral("/messages")
                                                   : base + QStringLiteral("/v1/messages");
    }
    return base.endsWith(QLatin1String("/chat/completions")) ? base : base + QStringLiteral("/chat/completions");
}

QString AiProfile::modelsUrl() const
{
    const QString base = resolvedBaseUrl();
    if (wireFormat == AiWireFormat::Anthropic) {
        return base.endsWith(QLatin1String("/v1")) ? base + QStringLiteral("/models")
                                                   : base + QStringLiteral("/v1/models");
    }
    return base + QStringLiteral("/models");
}

bool AiProfile::isLocalEndpoint() const
{
    const QString host = QUrl(resolvedBaseUrl()).host().toLower();
    if (host.isEmpty()) {
        return false;
    }
    if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".local"))
        || host.endsWith(QLatin1String(".localhost"))) {
        return true;
    }
    QHostAddress address;
    if (!address.setAddress(host)) {
        return false;
    }
    if (address.isLoopback()) {
        return true;
    }
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 ip = address.toIPv4Address();
        const quint8 a = ip >> 24;
        const quint8 b = (ip >> 16) & 0xff;
        return a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31);
    }
    return false;
}

bool AiProfile::needsKey() const
{
    // Um servidor na própria máquina/rede costuma dispensar chave.
    return wireFormat == AiWireFormat::Anthropic || !isLocalEndpoint();
}

bool AiProfile::isUsable() const
{
    if (model.trimmed().isEmpty()) {
        return false;
    }
    return !needsKey() || !resolvedKey().isEmpty();
}

QJsonObject AiProfile::toJson(bool includeKey) const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("wire_format")] = aiWireFormatToString(wireFormat);
    obj[QStringLiteral("base_url")] = baseUrl;
    obj[QStringLiteral("model")] = model;
    if (includeKey) {
        obj[QStringLiteral("api_key")] = apiKey;
    }
    obj[QStringLiteral("api_key_env")] = apiKeyEnv;
    obj[QStringLiteral("supports_tools")] = supportsTools;
    obj[QStringLiteral("max_tokens")] = maxTokens;
    obj[QStringLiteral("context_tokens")] = contextTokens;
    obj[QStringLiteral("first_token_timeout_sec")] = firstTokenTimeoutSec;
    obj[QStringLiteral("hide_secret_variables")] = hideSecretVariables;
    return obj;
}

AiProfile AiProfile::fromJson(const QJsonObject &obj)
{
    AiProfile p;
    p.id = obj.value(QStringLiteral("id")).toString();
    p.name = obj.value(QStringLiteral("name")).toString();
    p.wireFormat = aiWireFormatFromString(obj.value(QStringLiteral("wire_format")).toString());
    p.baseUrl = obj.value(QStringLiteral("base_url")).toString();
    p.model = obj.value(QStringLiteral("model")).toString();
    p.apiKey = obj.value(QStringLiteral("api_key")).toString();
    p.apiKeyEnv = obj.value(QStringLiteral("api_key_env")).toString();
    p.supportsTools = obj.value(QStringLiteral("supports_tools")).toBool(true);
    p.maxTokens = obj.value(QStringLiteral("max_tokens")).toInt(p.maxTokens);
    p.contextTokens = obj.value(QStringLiteral("context_tokens")).toInt(0);
    p.firstTokenTimeoutSec = obj.value(QStringLiteral("first_token_timeout_sec")).toInt(p.firstTokenTimeoutSec);
    p.hideSecretVariables = obj.value(QStringLiteral("hide_secret_variables")).toBool(false);
    return p.clamped();
}

AiProfile AiProfile::clamped() const
{
    AiProfile p = *this;
    p.maxTokens = std::clamp(p.maxTokens, 64, kMaxTokensLimit);
    p.contextTokens = std::clamp(p.contextTokens, 0, 4000000);
    p.firstTokenTimeoutSec = std::clamp(p.firstTokenTimeoutSec, 5, 900);
    return p;
}

bool AiSettings::isConfigured() const
{
    return std::any_of(profiles.cbegin(), profiles.cend(), [](const AiProfile &p) { return p.isUsable(); });
}

const AiProfile *AiSettings::profileById(const QString &id) const
{
    for (const AiProfile &p : profiles) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

const AiProfile *AiSettings::defaultProfile() const
{
    if (const AiProfile *p = profileById(defaultProfileId); p && p->isUsable()) {
        return p;
    }
    for (const AiProfile &p : profiles) {
        if (p.isUsable()) {
            return &p;
        }
    }
    return nullptr;
}

QString AiSettings::newMcpToken()
{
    QByteArray bytes(32, Qt::Uninitialized);
    QRandomGenerator *rng = QRandomGenerator::system();
    for (int i = 0; i < bytes.size(); ++i) {
        bytes[i] = char(rng->bounded(256));
    }
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QJsonObject AiSettings::toJson(bool includeKeys) const
{
    QJsonArray arr;
    for (const AiProfile &p : profiles) {
        arr.append(p.toJson(includeKeys));
    }
    QJsonObject obj;
    obj[QStringLiteral("profiles")] = arr;
    obj[QStringLiteral("default_profile_id")] = defaultProfileId;
    obj[QStringLiteral("max_agent_steps")] = maxAgentSteps;
    obj[QStringLiteral("extra_instructions")] = extraInstructions;
    obj[QStringLiteral("mcp_enabled")] = mcpEnabled;
    // O token autentica a conexão: só vai para o ai.json (includeKeys), como as chaves de API.
    if (includeKeys) {
        obj[QStringLiteral("mcp_token")] = mcpToken;
    }
    obj[QStringLiteral("mcp_hide_secrets")] = mcpHideSecrets;
    return obj;
}

AiSettings AiSettings::fromJson(const QJsonObject &obj)
{
    AiSettings s;
    for (const QJsonValue &v : obj.value(QStringLiteral("profiles")).toArray()) {
        AiProfile p = AiProfile::fromJson(v.toObject());
        if (!p.id.isEmpty()) {
            s.profiles.append(p);
        }
    }
    s.defaultProfileId = obj.value(QStringLiteral("default_profile_id")).toString();
    s.maxAgentSteps = std::clamp(obj.value(QStringLiteral("max_agent_steps")).toInt(s.maxAgentSteps), 1,
                                 kMaxAgentStepsLimit);
    s.extraInstructions = obj.value(QStringLiteral("extra_instructions")).toString();
    s.mcpEnabled = obj.value(QStringLiteral("mcp_enabled")).toBool(false);
    s.mcpToken = obj.value(QStringLiteral("mcp_token")).toString();
    s.mcpHideSecrets = obj.value(QStringLiteral("mcp_hide_secrets")).toBool(true);
    return s;
}

} // namespace kai::core
