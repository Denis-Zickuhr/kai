#pragma once

// Servidor HTTP mínimo para os testes de IA (sem rede real nem chave de verdade): responde com o roteiro
// enfileirado, em ordem, e guarda o que recebeu.

#include <QByteArray>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class FakeAiServer : public QObject {
public:
    struct Reply {
        int status = 200;
        QByteArray contentType = "text/event-stream";
        QList<QByteArray> chunks;
        int chunkDelayMs = 0;
        // Depois dos chunks não fecha a conexão (simula um servidor travado).
        bool hang = false;
        // Não envia nada (nem cabeçalhos).
        bool silent = false;
        // Fecha a conexão sem responder (erro de rede).
        bool drop = false;
        QByteArray retryAfter;
    };
    struct Request {
        QByteArray method;
        QByteArray path;
        QHash<QByteArray, QByteArray> headers; // nomes em minúsculas
        QByteArray body;
        QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
    };

    explicit FakeAiServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                s->setParent(this);
                connect(s, &QTcpSocket::readyRead, this, [this, s]() { onReadyRead(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(port()); }
    void enqueue(const Reply &r) { m_replies.append(r); }
    const QList<Request> &requests() const { return m_requests; }

    static Reply sse(const QList<QByteArray> &chunks)
    {
        Reply r;
        r.chunks = chunks;
        return r;
    }
    static Reply json(int status, const QByteArray &body)
    {
        Reply r;
        r.status = status;
        r.contentType = "application/json";
        r.chunks = {body};
        return r;
    }

private:
    void onReadyRead(QTcpSocket *s)
    {
        QByteArray &buf = m_buffers[s];
        buf.append(s->readAll());
        const int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        Request req;
        const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
        const QList<QByteArray> first = lines.first().trimmed().split(' ');
        req.method = first.value(0);
        req.path = first.value(1);
        for (int i = 1; i < lines.size(); ++i) {
            const int colon = lines.at(i).indexOf(':');
            if (colon > 0) req.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
        }
        const int length = req.headers.value("content-length").toInt();
        if (buf.size() < headerEnd + 4 + length) return;
        req.body = buf.mid(headerEnd + 4, length);
        m_buffers.remove(s);
        m_requests.append(req);

        Reply reply;
        if (!m_replies.isEmpty()) {
            reply = m_replies.takeFirst();
        } else {
            reply = json(500, R"({"error":{"message":"no scripted reply"}})");
        }
        if (reply.drop) {
            s->disconnectFromHost();
            return;
        }
        if (reply.silent) return;
        QByteArray head = "HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\nContent-Type: " + reply.contentType
                          + "\r\nConnection: close\r\n";
        if (!reply.retryAfter.isEmpty()) head += "Retry-After: " + reply.retryAfter + "\r\n";
        const bool buffered = reply.contentType != "text/event-stream";
        if (buffered) {
            QByteArray all;
            for (const QByteArray &c : reply.chunks) all += c;
            head += "Content-Length: " + QByteArray::number(all.size()) + "\r\n\r\n" + all;
            s->write(head);
            s->disconnectFromHost();
            return;
        }
        head += "\r\n";
        s->write(head);
        sendChunks(s, reply, 0);
    }

    void sendChunks(QTcpSocket *s, const Reply &reply, int index)
    {
        if (!s || s->state() != QAbstractSocket::ConnectedState) return;
        if (index >= reply.chunks.size()) {
            if (!reply.hang) s->disconnectFromHost();
            return;
        }
        s->write(reply.chunks.at(index));
        s->flush();
        if (reply.chunkDelayMs > 0) {
            QPointer<QTcpSocket> guard(s);
            QTimer::singleShot(reply.chunkDelayMs, this, [this, guard, reply, index]() { sendChunks(guard, reply, index + 1); });
        } else {
            sendChunks(s, reply, index + 1);
        }
    }

    QTcpServer m_server;
    QList<Reply> m_replies;
    QList<Request> m_requests;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
