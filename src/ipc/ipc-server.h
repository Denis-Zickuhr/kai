#pragma once

#include <QObject>
#include "core/ipc-endpoint.h"
#include <QString>
#include <QByteArray>
#include <QJsonArray>
#include <QMap>

class QLocalServer;
class QLocalSocket;

namespace kai::ipc {

class StreamChannel;

// Protocolo de linha do IPC do Kai (CLI <-> instância rodando).
// Uma requisição é um objeto JSON em UMA linha, terminado por '\n':
//   {"cmd":"run","arg":"Deploy"}
//   {"cmd":"list"}
//   {"cmd":"env-use","arg":"Prod"}
//   {"cmd":"env-list"}
//   {"cmd":"show"}
// A resposta é um objeto JSON em uma linha:
//   {"ok":true,"message":"...","lines":[...]}
constexpr const char *kSocketName = core::kIpcSocketName;

// Nome do socket IPC efetivo: kSocketName, a menos que KAI_IPC_SOCKET_NAME_OVERRIDE esteja definida. Só para isolar
// testes de uma instância real do Kai já escutando no nome fixo; produção nunca a define.
QString ipcSocketName();

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
    // Emitido para cada requisição recebida. `responder` deve ser chamado uma vez com o resultado (ok, message, lines).
    void runRequested(const QString &commandName, bool &ok, QString &message);
    void listRequested(QStringList &names);
    void envUseRequested(const QString &envName, bool &ok, QString &message);
    void envListRequested(QStringList &names, QString &activeName);
    void showRequested();

    // Controle de processos (kai ps/attach/kill).
    void psRequested(QStringList &lines);
    void attachRequested(const QString &name, bool &ok, QString &message);
    void killRequested(const QString &name, bool &ok, QString &message);

    // `kai import`: caminho de um kai.yml, ou da pasta dele.
    // `only`: categorias do pacote a importar (vazio = tudo); ver MainWindow::cliImportSelection.
    void importRequested(const QString &path, const QStringList &only, bool &ok, QString &message);
    // `kai import --recursive <pasta>`: todos os projetos abaixo da pasta.
    void importTreeRequested(const QString &path, bool &ok, QString &message);
    // `kai raise`: notificação (level = info|warning|error; título opcional).
    void raiseRequested(const QString &level, const QString &title, const QString &message,
                        bool &ok, QString &reply);
    // Execução delegada com streaming (ver StreamChannel). Quem recebe roda o
    // comando `commandId` com `params` e usa o canal pra devolver saída/fim.
    // `detached`: quem pediu só quer a confirmação de que foi disparado.
    // `notify` (-n): avisar na bandeja quando terminar (usado com detached).
    // `window` (-w): abrir a saída do comando numa janela desacoplada do app.
    // `invocationDir`: diretório do terminal de onde o CLI foi chamado; só vem
    // preenchido quando o comando pede rodar lá (Command::cliWorkingDir).
    void runStreamRequested(const QString &commandId, const QMap<QString, QString> &params,
                            bool detached, bool notify, bool window, const QString &invocationDir,
                            kai::ipc::StreamChannel *channel);
    // `kai attach <nome|pid>`: espelhar um processo que já está rodando.
    void attachStreamRequested(const QString &target, kai::ipc::StreamChannel *channel);
    // `kai ps --json`: {id, name, pid, status} por processo.
    void psItemsRequested(QJsonArray &items);

private slots:
    void handleNewConnection();
    // Trata uma requisição já lida (dispatch assíncrono, sem bloquear a GUI).
    void dispatchRequest(QLocalSocket *conn, const QByteArray &line);

private:
    QLocalServer *m_server = nullptr;
};

} // namespace kai::ipc
