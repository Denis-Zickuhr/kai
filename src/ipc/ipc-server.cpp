#include "ipc/ipc-server.h"
#include "core/ipc-endpoint.h"

#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "ipc/stream-channel.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

namespace kai::ipc {

namespace {
constexpr const char *kLogTag = "IpcServer";
}

QString ipcSocketName()
{
    return core::ipcSocketName();
}

IpcServer::IpcServer(QObject *parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    connect(m_server, &QLocalServer::newConnection, this, &IpcServer::handleNewConnection);
}

IpcServer::~IpcServer()
{
    if (m_server) {
        m_server->close();
    }
}

bool IpcServer::start()
{
    // Se um socket órfão de uma execução anterior ficou para trás (crash),
    // QLocalServer::listen falha com AddressInUseError mesmo sem ninguém
    // escutando. Tentamos conectar: se conseguir, há instância viva (não
    // subimos). Se não, removemos o socket órfão e tentamos de novo.
    const QString name = ipcSocketName();
    if (m_server->listen(name)) {
        utils::Logger::info(kLogTag, QStringLiteral("IPC escutando em '%1'.").arg(name));
        return true;
    }

    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(1500)) {
        // Já existe uma instância viva.
        probe.disconnectFromServer();
        utils::Logger::info(kLogTag, QStringLiteral("Outra instância do Kai já está escutando."));
        return false;
    }
    // SÓ remove o socket quando ele é comprovadamente órfão (arquivo sem
    // ninguém atrás: conexão recusada / servidor não encontrado). Um timeout
    // significa instância viva porém ocupada — remover o socket dela a deixava
    // inalcançável e a segunda instância assumia o lugar: dois Kais abertos.
    const QLocalSocket::LocalSocketError probeError = probe.error();
    if (probeError != QLocalSocket::ConnectionRefusedError && probeError != QLocalSocket::ServerNotFoundError) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Instância existente não respondeu a tempo (%1): não vou assumir o socket.")
                .arg(probe.errorString()));
        return false;
    }

    // Socket órfão: remove e tenta de novo.
    QLocalServer::removeServer(name);
    if (m_server->listen(name)) {
        utils::Logger::info(kLogTag,
            QStringLiteral("IPC escutando em '%1' (socket órfão removido).").arg(name));
        return true;
    }
    utils::Logger::warning(kLogTag,
        QStringLiteral("Falha ao subir o IPC: %1").arg(m_server->errorString()));
    return false;
}

void IpcServer::handleNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QLocalSocket *conn = m_server->nextPendingConnection();
        connect(conn, &QLocalSocket::disconnected, conn, &QLocalSocket::deleteLater);

        // Leitura ASSÍNCRONA (achado de auditoria): antes usava
        // waitForReadyRead(1000) na thread da GUI — um cliente lento
        // congelava a interface por até 1s por conexão (e outro 1s no
        // write). Agora reagimos ao readyRead e nunca bloqueamos.
        connect(conn, &QLocalSocket::readyRead, this, [this, conn]() {
            // Várias linhas podem chegar juntas; numa sessão de streaming
            // (run-stream) as linhas seguintes pertencem ao canal aberto.
            while (conn->canReadLine()) {
                const QByteArray line = conn->readLine().trimmed();
                if (auto *channel = conn->findChild<StreamChannel *>()) {
                    channel->handleLine(line);
                } else {
                    dispatchRequest(conn, line);
                }
            }
        });

        // Guarda-chuva: descarta conexões que não mandam nada (evita
        // acumular sockets abertos), sem bloquear a GUI.
        auto *idleTimeout = new QTimer(conn);
        idleTimeout->setObjectName(QStringLiteral("kai_idle_timeout"));
        idleTimeout->setSingleShot(true);
        connect(idleTimeout, &QTimer::timeout, conn, [conn]() {
            if (conn->state() != QLocalSocket::UnconnectedState) {
                conn->disconnectFromServer();
            }
        });
        idleTimeout->start(5000);
    }
}

void IpcServer::dispatchRequest(QLocalSocket *conn, const QByteArray &line)
{

        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
        QJsonObject reply;
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            reply["ok"] = false;
            reply["message"] = kai::utils::tr(QStringLiteral("ipc.error.invalid_request"));
        } else {
            const QJsonObject req = doc.object();
            const QString cmd = req.value(QStringLiteral("cmd")).toString();
            const QString arg = req.value(QStringLiteral("arg")).toString();

            if (cmd == QStringLiteral("run-stream")) {
                // Sessão longa: sem timeout ocioso e sem resposta única — o
                // canal assume a conexão (ver StreamChannel).
                if (auto *idle = conn->findChild<QTimer *>(QStringLiteral("kai_idle_timeout"))) {
                    idle->stop();
                }
                auto *channel = new StreamChannel(conn);
                QMap<QString, QString> params;
                const QJsonObject paramsObj = req.value(QStringLiteral("params")).toObject();
                for (auto it = paramsObj.constBegin(); it != paramsObj.constEnd(); ++it) {
                    params.insert(it.key(), it.value().toString());
                }
                emit runStreamRequested(req.value(QStringLiteral("command_id")).toString(), params,
                                        req.value(QStringLiteral("detached")).toBool(false),
                                        req.value(QStringLiteral("notify")).toBool(false),
                                        req.value(QStringLiteral("window")).toBool(false),
                                        req.value(QStringLiteral("cwd")).toString(), channel);
                return;
            }
            if (cmd == QStringLiteral("attach-stream")) {
                // `kai attach`: mesma sessão longa, espelhando um processo que
                // já está rodando.
                if (auto *idle = conn->findChild<QTimer *>(QStringLiteral("kai_idle_timeout"))) {
                    idle->stop();
                }
                emit attachStreamRequested(arg, new StreamChannel(conn));
                return;
            }

            if (cmd == QStringLiteral("run")) {
                bool ok = false;
                QString message;
                emit runRequested(arg, ok, message);
                reply["ok"] = ok;
                reply["message"] = message;
            } else if (cmd == QStringLiteral("list")) {
                QStringList names;
                emit listRequested(names);
                reply["ok"] = true;
                reply["lines"] = QJsonArray::fromStringList(names);
            } else if (cmd == QStringLiteral("env-use")) {
                bool ok = false;
                QString message;
                emit envUseRequested(arg, ok, message);
                reply["ok"] = ok;
                reply["message"] = message;
            } else if (cmd == QStringLiteral("env-list")) {
                QStringList names;
                QString active;
                emit envListRequested(names, active);
                reply["ok"] = true;
                reply["message"] = active;
                reply["lines"] = QJsonArray::fromStringList(names);
            } else if (cmd == QStringLiteral("show")) {
                emit showRequested();
                reply["ok"] = true;
                reply["message"] = kai::utils::tr(QStringLiteral("ipc.show.done"));
            } else if (cmd == QStringLiteral("ps")) {
                QStringList lines;
                emit psRequested(lines);
                reply["ok"] = true;
                reply["lines"] = QJsonArray::fromStringList(lines);
                QJsonArray items;
                emit psItemsRequested(items);
                reply["items"] = items;
            } else if (cmd == QStringLiteral("attach")) {
                bool ok = false; QString message;
                emit attachRequested(arg, ok, message);
                reply["ok"] = ok; reply["message"] = message;
            } else if (cmd == QStringLiteral("kill")) {
                bool ok = false; QString message;
                emit killRequested(arg, ok, message);
                reply["ok"] = ok; reply["message"] = message;
            } else if (cmd == QStringLiteral("import")) {
                bool ok = false;
                QString message;
                QStringList only;
                for (const QJsonValue &name : req.value(QStringLiteral("only")).toArray()) {
                    only << name.toString();
                }
                emit importRequested(req.value(QStringLiteral("path")).toString(), only, ok, message);
                reply["ok"] = ok;
                reply["message"] = message;
            } else if (cmd == QStringLiteral("raise")) {
                bool ok = false;
                QString message;
                emit raiseRequested(req.value(QStringLiteral("level")).toString(),
                                    req.value(QStringLiteral("title")).toString(),
                                    req.value(QStringLiteral("message")).toString(), ok, message);
                reply["ok"] = ok;
                reply["message"] = message;
            } else {
                reply["ok"] = false;
                reply["message"] = kai::utils::tr(QStringLiteral("ipc.error.unknown_command")).arg(cmd);
            }
        }

        const QByteArray out = QJsonDocument(reply).toJson(QJsonDocument::Compact) + "\n";
        conn->write(out);
        conn->flush();
        // NÃO usar waitForBytesWritten (bloqueava a GUI até 1s — achado de
        // auditoria): desconecta quando os bytes saírem, de forma assíncrona.
        connect(conn, &QLocalSocket::bytesWritten, conn, [conn](qint64) {
            if (conn->bytesToWrite() == 0) {
                conn->disconnectFromServer();
            }
        });
        if (conn->bytesToWrite() == 0) {
            conn->disconnectFromServer();
        }
}

} // namespace kai::ipc
