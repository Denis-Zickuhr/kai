#pragma once

#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QStringList>
#include <memory>

#include "core/environment-manager.h"
#include "core/http-resolve.h"
#include "core/models.h"

class QNetworkAccessManager;
#include <QElapsedTimer>
#include <QList>
#include <QPair>

class QNetworkReply;
class QFile;

namespace kai::engine {

// Resultado final de uma requisição HTTP disparada pelo HttpRunner.
//
// SAÍDA V2: antes o resultado carregava apenas status/success/erro/corpo, então
// a interface não tinha como mostrar aba de Headers, latência ou tipo de
// conteúdo — informação que qualquer cliente de API decente exibe. Os campos
// abaixo alimentam as abas da Saída v2 (Corpo, Headers, Raw) e a barra de
// métricas (status, tempo, tamanho).
struct HttpResult {
    int statusCode = 0;
    bool success = false; // true apenas para status 2xx e sem erro de rede/timeout
    QString errorMessage;
    QByteArray body;

    // --- Metadados da resposta (Saída v2) ---
    QString reasonPhrase;                  // ex: "Not Found"
    QList<QPair<QString, QString>> headers; // ordem preservada, permite repetidos
    QString contentType;                   // ex: "application/json; charset=utf-8"
    qint64 elapsedMs = 0;                  // latência total medida
    qint64 bodySize = 0;                   // bytes do corpo
    QString finalUrl;                      // URL após eventuais redirects
    QString requestMethod;                 // método efetivamente usado
    QString requestUrl;                    // URL interpolada da requisição
    QList<QPair<QString, QString>> requestHeaders; // headers enviados (interpolados)
    QString requestBody;                   // corpo enviado (interpolado)

    qint64 ttfbMs = 0;                     // até chegarem os headers da resposta
    QStringList redirectChain;             // URLs percorridas (a primeira é a pedida)
    QString savedPath;                     // preenchido quando a resposta foi gravada em arquivo
    QStringList assertionFailures;         // rótulos das asserções que não passaram
    bool canceled = false;                 // interrompida por HttpRunner::abort()
    bool timedOut = false;                 // o limite de tempo da requisição acabou sem resposta completa
    int timeoutSec = 0;                    // limite aplicado (0 = sem limite)

    // Corpo que não é texto (imagem, PDF, zip...): a interface mostra um resumo.
    bool isBinary() const;
    bool isImage() const
    {
        return contentType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive)
               && !contentType.contains(QStringLiteral("svg"), Qt::CaseInsensitive);
    }

    // Conveniência: o corpo é JSON válido?
    bool isJson() const
    {
        return contentType.contains(QStringLiteral("json"), Qt::CaseInsensitive);
    }
};

// Wrapper assíncrono sobre QNetworkAccessManager.
// Interpola variáveis {{VAR}} em URL/headers/body usando o EnvironmentManager
// fornecido, aplica o timeout configurado (padrão: nenhum) e, em respostas 2xx, executa os
// EnvExtractors configurados via QJsonDocument, escrevendo o resultado de
// volta no EnvironmentManager (como variável dinâmica).
class HttpRunner : public QObject {
    Q_OBJECT

public:
    explicit HttpRunner(QObject *parent = nullptr);
    ~HttpRunner() override;

    void setTimeoutMs(int ms);

    // Dispara a requisição descrita em `config`, interpolando variáveis a
    // partir de `envManager`. Extrações bem-sucedidas são gravadas de volta
    // em `envManager` como variáveis dinâmicas.
    //
    // IMPORTANTE: `envManager` é referenciado internamente até o sinal
    // finished() ser emitido (a requisição é assíncrona). O chamador deve
    // garantir que a instância viva durante toda a execução — o uso
    // recomendado é manter o EnvironmentManager como membro de longa
    // duração (ex: dentro do ExecutionPipeline), nunca como variável local.
    void execute(const core::HttpConfig &config, core::EnvironmentManager &envManager);
    // Versão já resolvida (comando + padrões das pastas). É a que o pipeline usa.
    void execute(const core::HttpRequestSpec &spec, core::EnvironmentManager &envManager);
    // Interrompe a requisição em curso (botão Parar): finished() sai com canceled = true.
    void abort();

signals:
    void finished(const HttpResult &result);
    void logMessage(const QString &text, bool isError);
    // Um EnvExtractor com persist == true acabou de gravar um valor — o
    // chamador (MainWindow) é quem tem o ConfigManager, então é ele quem
    // grava em dynamic-vars.json; o HttpRunner só avisa o quê e onde
    // (scopeKey vem de EnvironmentManager::currentDynamicVarScope()).
    void dynamicVarPersistRequested(const QString &scopeKey, const QString &name, const QString &value);

private:
    void handleReplyFinished(QNetworkReply *reply, const core::HttpRequestSpec &spec, core::EnvironmentManager *envManager);
    void failEarly(const core::HttpRequestSpec &spec, const QString &message);
    void runAssertions(const core::HttpRequestSpec &spec, HttpResult &result, core::EnvironmentManager &envManager);
    void applyExtractors(const QJsonDocument &doc, const QVector<core::EnvExtractor> &extractors, core::EnvironmentManager &envManager);
    static QJsonValue resolveJsonPath(const QJsonValue &root, const QString &jsonPath);

    std::unique_ptr<QNetworkAccessManager> m_networkManager;
    int m_timeoutMs = 0; // 0 = sem limite (um servidor parado num breakpoint segura a requisição)
    int m_pendingTimeoutMs = 0;

    // Estado da requisição em voo, para compor o HttpResult da Saída v2.
    QElapsedTimer m_timer;
    QString m_pendingRequestMethod;
    QString m_pendingRequestUrl;
    QList<QPair<QString, QString>> m_pendingRequestHeaders;
    QString m_pendingRequestBody;
    QPointer<QNetworkReply> m_reply;
    bool m_canceled = false;
    // Server-Sent Events: os eventos vão para a saída conforme chegam (a resposta não termina sozinha).
    bool m_streaming = false;
    QByteArray m_streamed;
    qint64 m_ttfbMs = -1;
    QStringList m_redirectChain;
    QString m_savedPath;
    qint64 m_savedBytes = 0;
    std::unique_ptr<QFile> m_responseFile;
};

// Proxy para uma URL a partir do ajuste do Kai ("system" = HTTPS_PROXY/HTTP_PROXY/ALL_PROXY e NO_PROXY, "none", ou
// http://host:porta / socks5://host:porta). Compartilhado com o cliente de IA, que também precisa atravessar o proxy
// da empresa.
QNetworkProxy resolveHttpProxy(const QString &setting, const QUrl &url);

} // namespace kai::engine
