#include "ipc/stream-channel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

namespace kai::ipc {

StreamChannel::StreamChannel(QLocalSocket *socket)
    : QObject(socket)
    , m_socket(socket)
{
    connect(socket, &QLocalSocket::disconnected, this, [this]() {
        if (!m_finished) {
            emit closed();
        }
    });
}

void StreamChannel::send(const QByteArray &json)
{
    if (m_socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    m_socket->write(json + '\n');
    m_socket->flush();
}

void StreamChannel::sendOutput(const QString &text, bool isError)
{
    if (m_finished || text.isEmpty()) {
        return;
    }
    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("output");
    msg[QStringLiteral("text")] = text;
    msg[QStringLiteral("err")] = isError;
    send(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void StreamChannel::sendBackground(qint64 pid)
{
    if (m_finished) {
        return;
    }
    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("background");
    msg[QStringLiteral("pid")] = static_cast<double>(pid);
    send(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void StreamChannel::sendFinished(int exitCode, const QString &message)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("finished");
    msg[QStringLiteral("code")] = exitCode;
    msg[QStringLiteral("message")] = message;
    send(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    QLocalSocket *socket = m_socket;
    if (socket->bytesToWrite() == 0) {
        socket->disconnectFromServer();
    } else {
        connect(socket, &QLocalSocket::bytesWritten, socket, [socket](qint64) {
            if (socket->bytesToWrite() == 0) {
                socket->disconnectFromServer();
            }
        });
    }
}

void StreamChannel::handleLine(const QByteArray &line)
{
    const QJsonObject msg = QJsonDocument::fromJson(line).object();
    if (msg.value(QStringLiteral("type")).toString() == QStringLiteral("input")) {
        emit inputReceived(msg.value(QStringLiteral("text")).toString());
    }
}

} // namespace kai::ipc
