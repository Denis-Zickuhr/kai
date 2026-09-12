#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>

class QLocalServer;

class QLocalSocket;

namespace kai::ipc {

// Protocolo de linha do IPC do Kai (CLI <-> instância rodando).
// Uma requisição é um objeto JSON em UMA linha, terminado por '\n':
//   {"cmd":"run","arg":"Deploy"}
//   {"cmd":"list"}
//   {"cmd":"env-use","arg":"Prod"}
//   {"cmd":"env-list"}
//   {"cmd":"show"}
// A resposta é um objeto JSON em uma linha:
//   {"ok":true,"message":"...","lines":[...]}
constexpr const char *kSocketName = "kai-ipc-v1";

// Servidor IPC: escuta num QLocalServer (named pipe no Windows, unix socket
// no Linux) e traduz requisições JSON em sinais que a MainWindow trata.
// Também serve de mecanismo de instância única — se o listen() falhar
// porque o socket já existe, é sinal de que outra instância está rodando.
class IpcServer : public QObject {
    Q_OBJECT

public:
    explicit IpcServer(QObject *parent = nullptr);
    ~IpcServer() override;

    // Sobe o servidor. Retorna false se já houver uma instância escutando.
    bool start();

signals:
    // Emitido para cada requisição recebida. `responder` deve ser chamado
    // (uma vez) com o resultado (ok, message, lines) — a MainWindow conecta
    // a estes sinais e responde de forma síncrona.
    void runRequested(const QString &commandName, bool &ok, QString &message);
    void listRequested(QStringList &names);
    void envUseRequested(const QString &envName, bool &ok, QString &message);
    void envListRequested(QStringList &names, QString &activeName);
    void showRequested();

    // Controle de processos (kai ps/attach/kill).
    void psRequested(QStringList &lines);
    void attachRequested(const QString &name, bool &ok, QString &message);
    void killRequested(const QString &name, bool &ok, QString &message);

    // importaçao
    void importRequested(const QString &jsonContent, bool &ok, QString &message);

private slots:
    void handleNewConnection();
    // Trata uma requisição já lida (dispatch assíncrono, sem bloquear a GUI).
    void dispatchRequest(QLocalSocket *conn, const QByteArray &line);

private:
    QLocalServer *m_server = nullptr;
};

} // namespace kai::ipc
