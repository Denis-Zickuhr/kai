#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>
#include <optional>

namespace kai::core {

// Peças da requisição HTTP compartilhadas entre o comando (HttpConfig) e a pasta (HttpDefaults).
// Só dados + JSON: a regra de herança é de core/http-resolve.h, o envio de engine/http-runner.h.

// Par nome/valor ordenado (headers, query, campos de formulário). Lista, não mapa: preserva a ordem,
// aceita nomes repetidos e permite DESLIGAR uma linha sem apagá-la (numa pasta, "apaga" o herdado).
struct HttpEntry {
    QString name;
    QString value;
    bool enabled = true;
    bool isFile = false; // só multipart: `value` é o caminho de um arquivo

    bool operator==(const HttpEntry &other) const
    {
        return name == other.name && value == other.value && enabled == other.enabled && isFile == other.isFile;
    }

    QJsonObject toJson() const;
    static HttpEntry fromJson(const QJsonObject &obj);
};

// Aceita a lista nova ([{name, value, enabled?}]) e o mapa antigo
// ({Nome: valor}) — arquivos anteriores continuam abrindo.
QVector<HttpEntry> httpEntriesFromJson(const QJsonValue &value);
QJsonArray httpEntriesToJson(const QVector<HttpEntry> &entries);
// Valor da primeira linha com esse nome (sem diferenciar maiúsculas); vazio se não houver.
QString httpEntryValue(const QVector<HttpEntry> &entries, const QString &name);

enum class HttpBodyType {
    Raw,       // texto como está; nenhum Content-Type automático (comportamento antigo)
    None,      // sem corpo
    Json,      // texto JSON; Content-Type application/json se não houver um
    Xml,       // texto XML; Content-Type application/xml se não houver um
    Form,      // application/x-www-form-urlencoded a partir dos campos
    Multipart, // multipart/form-data a partir dos campos (texto e arquivos)
    File,      // o corpo é o conteúdo binário de um arquivo
    GraphQl,   // `body` é a query; as variáveis vão em graphqlVariables
};

QString httpBodyTypeToString(HttpBodyType type);
HttpBodyType httpBodyTypeFromString(const QString &value);

enum class HttpAuthType {
    Inherit, // usa a da pasta (e assim por diante); sem nenhuma, não autentica
    None,    // corta a herança: este comando não autentica
    Basic,
    Bearer,
    ApiKey,
};

QString httpAuthTypeToString(HttpAuthType type);
HttpAuthType httpAuthTypeFromString(const QString &value);

// Os valores aceitam {{VAR}} — o jeito certo de usar um segredo (a variável
// pode ser marcada como secreta e nunca sai do computador).
struct HttpAuth {
    HttpAuthType type = HttpAuthType::Inherit;
    QString username;
    QString password;
    QString token;
    QString keyName;
    QString keyValue;
    bool keyInQuery = false; // ApiKey: false = header, true = parâmetro de query

    bool isDefault() const { return type == HttpAuthType::Inherit; }

    QJsonObject toJson() const;
    static HttpAuth fromJson(const QJsonObject &obj);
};

// Opções de conexão. Cada uma é opcional: ausente = herda (comando ← pasta ←
// padrão do Kai). Os padrões estão em HttpEffectiveOptions.
struct HttpOptions {
    std::optional<bool> followRedirects;
    std::optional<int> maxRedirects;
    std::optional<bool> verifyTls;
    std::optional<QString> proxy;     // "system", "none" ou http://host:porta / socks5://host:porta
    std::optional<int> timeoutSec;
    std::optional<bool> useCookies;

    bool isEmpty() const
    {
        return !followRedirects && !maxRedirects && !verifyTls && !proxy && !timeoutSec && !useCookies;
    }

    QJsonObject toJson() const;
    static HttpOptions fromJson(const QJsonObject &obj);
};

// Valor final de cada opção depois de resolvida a herança.
struct HttpEffectiveOptions {
    bool followRedirects = true;
    int maxRedirects = 10;
    bool verifyTls = true;
    QString proxy = QStringLiteral("system");
    int timeoutSec = 0; // 0 = padrão do HttpRunner
    bool useCookies = true;
};

// O que uma pasta oferece, por herança, a todos os comandos HTTP abaixo dela.
struct HttpDefaults {
    QVector<HttpEntry> headers;
    HttpAuth auth;
    HttpOptions options;

    bool isEmpty() const { return headers.isEmpty() && auth.isDefault() && options.isEmpty(); }

    QJsonObject toJson() const;
    static HttpDefaults fromJson(const QJsonObject &obj);
};

} // namespace kai::core
