#include "ipc/ipc-server.h"

#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "utils/logger.h"
#include "utils/translation-manager.h"

namespace kai::ipc {

namespace {
constexpr const char *kLogTag = "IpcServer";
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
    const QString name = QString::fromLatin1(kSocketName);
    if (m_server->listen(name)) {
        utils::Logger::info(kLogTag, QStringLiteral("IPC escutando em '%1'.").arg(name));
        return true;
    }

    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(200)) {
        // Já existe uma instância viva.
        probe.disconnectFromServer();
        utils::Logger::info(kLogTag, QStringLiteral("Outra instância do Kai já está escutando."));
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
            if (!conn->canReadLine()) {
                return; // aguarda a linha completa
            }
            const QByteArray line = conn->readLine().trimmed();
            dispatchRequest(conn, line);
        });

        // Guarda-chuva: descarta conexões que não mandam nada (evita
        // acumular sockets abertos), sem bloquear a GUI.
        QTimer::singleShot(5000, conn, [conn]() {
            if (conn->state() != QLocalSocket::UnconnectedState) {
                conn->disconnectFromServer();
            }
        });
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
                const QString jsonContent = req.value(QStringLiteral("json")).toString();
                emit importRequested(jsonContent, ok, message);
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
