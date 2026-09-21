#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QLocalSocket;

namespace kai::ipc {

// Conexão IPC de LONGA duração pra uma execução delegada (`kai -g <path>`,
// ou `kai <path>` fora de pasta com kai.json/kai.yml): o CLI pede pro APP
// rodar o comando (registrado na lista de processos/histórico como qualquer
// execução da GUI) e o app devolve a saída pelo mesmo socket, enquanto o CLI
// manda o que o usuário digita. Uma linha JSON por mensagem:
//   app -> CLI: {"type":"output","text":...,"err":bool}
//               {"type":"background","pid":N}      (comando is_background)
//               {"type":"finished","code":N,"message":...}
//   CLI -> app: {"type":"input","text":...}
// Fechar a conexão NÃO para o comando — ele continua gerenciado pelo app.
class StreamChannel : public QObject {
    Q_OBJECT
public:
    // Filho de `socket`: morre junto com a conexão.
    explicit StreamChannel(QLocalSocket *socket);

    void sendOutput(const QString &text, bool isError);
    void sendBackground(qint64 pid);
    // Encerra a sessão (a conexão é fechada depois do envio).
    void sendFinished(int exitCode, const QString &message = QString());

    bool isFinished() const { return m_finished; }

    // Uma linha já recebida pelo IpcServer depois da abertura do canal.
    void handleLine(const QByteArray &line);

signals:
    void inputReceived(const QString &text);
    // O CLI desconectou (terminal fechado, Ctrl+C no próprio kai...).
    void closed();

private:
    void send(const QByteArray &json);

    QLocalSocket *m_socket = nullptr;
    bool m_finished = false;
};

} // namespace kai::ipc
