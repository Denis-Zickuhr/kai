#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "core/environment-manager.h"
#include "core/models.h"

class QNetworkAccessManager;
#include <QElapsedTimer>
#include <QList>
#include <QPair>

class QNetworkReply;

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

    // Conveniência: o corpo é JSON válido?
    bool isJson() const
    {
        return contentType.contains(QStringLiteral("json"), Qt::CaseInsensitive);
    }
};

// Wrapper assíncrono sobre QNetworkAccessManager.
// Interpola variáveis {{VAR}} em URL/headers/body usando o EnvironmentManager
// fornecido, aplica timeout padrão de 10s e, em respostas 2xx, executa os
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

signals:
    void finished(const HttpResult &result);
    void logMessage(const QString &text, bool isError);
    // Um EnvExtractor com persist == true acabou de gravar um valor — o
    // chamador (MainWindow) é quem tem o ConfigManager, então é ele quem
    // grava em dynamic-vars.json; o HttpRunner só avisa o quê e onde
    // (scopeKey vem de EnvironmentManager::currentDynamicVarScope()).
    void dynamicVarPersistRequested(const QString &scopeKey, const QString &name, const QString &value);

private:
    void handleReplyFinished(QNetworkReply *reply, const core::HttpConfig &config, core::EnvironmentManager *envManager);
    void applyExtractors(const QJsonDocument &doc, const QVector<core::EnvExtractor> &extractors, core::EnvironmentManager &envManager);
    static QJsonValue resolveJsonPath(const QJsonValue &root, const QString &jsonPath);

    std::unique_ptr<QNetworkAccessManager> m_networkManager;
    int m_timeoutMs = 10000;

    // Estado da requisição em voo, para compor o HttpResult da Saída v2.
    QElapsedTimer m_timer;
    QString m_pendingRequestMethod;
    QString m_pendingRequestUrl;
    QList<QPair<QString, QString>> m_pendingRequestHeaders;
    QString m_pendingRequestBody;
};

} // namespace kai::engine
