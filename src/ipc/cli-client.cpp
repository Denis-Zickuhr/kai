#include "ipc/cli-client.h"
#include "ipc/ipc-server.h"
#include "utils/translation-manager.h"

#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QFile>

namespace kai::ipc {

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}
QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

void printUsage()
{
    out() << kai::utils::tr(QStringLiteral("cli.usage.banner")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.label")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.run")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.list")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.env_list")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.env_use")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.import")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.ps")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.attach")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.kill")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.show")) << "\n"
          << kai::utils::tr(QStringLiteral("cli.usage.help")) << "\n";
    out().flush();
}

// Envia uma requisição JSON ao servidor e devolve a resposta (objeto).
// Em erro de conexão, preenche connectError.
QJsonObject sendRequest(const QJsonObject &req, bool &connected)
{
    connected = false;
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kSocketName));
    if (!socket.waitForConnected(500)) {
        return {};
    }
    connected = true;
    const QByteArray payload = QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n";
    socket.write(payload);
    socket.flush();
    socket.waitForBytesWritten(1000);
    if (!socket.waitForReadyRead(5000)) {
        return {};
    }
    const QByteArray line = socket.readLine().trimmed();
    socket.disconnectFromServer();
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    return doc.object();
}

int dispatch(const QJsonObject &req, bool printLines)
{
    bool connected = false;
    const QJsonObject reply = sendRequest(req, connected);
    if (!connected) {
        err() << kai::utils::tr(QStringLiteral("cli.error.not_running")) << "\n";
        err().flush();
        return 2;
    }
    const bool ok = reply.value(QStringLiteral("ok")).toBool();
    const QString message = reply.value(QStringLiteral("message")).toString();
    if (printLines && reply.contains(QStringLiteral("lines"))) {
        const QJsonArray lines = reply.value(QStringLiteral("lines")).toArray();
        for (const QJsonValue &v : lines) {
            out() << v.toString() << "\n";
        }
        out().flush();
    }
    if (!message.isEmpty()) {
        (ok ? out() : err()) << message << "\n";
        (ok ? out() : err()).flush();
    }
    return ok ? 0 : 1;
}

} // namespace

CliOutcome runCliIfRequested(const QStringList &args)
{
    // args[0] é o caminho do executável. O verbo é args[1].
    if (args.size() < 2) {
        return {false, 0};
    }
    const QString verb = args.at(1);

    if (verb == QStringLiteral("run")) {
        if (args.size() < 3) {
            err() << kai::utils::tr(QStringLiteral("cli.error.usage.run")) << "\n";
            err().flush();
            return {true, 2};
        }
        QJsonObject req;
        req["cmd"] = QStringLiteral("run");
        req["arg"] = args.at(2);
        return {true, dispatch(req, false)};
    }
    if (verb == QStringLiteral("list")) {
        QJsonObject req;
        req["cmd"] = QStringLiteral("list");
        return {true, dispatch(req, true)};
    }
    if (verb == QStringLiteral("show")) {
        QJsonObject req;
        req["cmd"] = QStringLiteral("show");
        return {true, dispatch(req, false)};
    }
    if (verb == QStringLiteral("ps")) {
        QJsonObject req;
        req["cmd"] = QStringLiteral("ps");
        return {true, dispatch(req, true)};
    }
    if (verb == QStringLiteral("attach")) {
        if (args.size() < 3) {
            err() << kai::utils::tr(QStringLiteral("cli.error.usage.attach")) << "\n"; err().flush();
            return {true, 2};
        }
        QJsonObject req;
        req["cmd"] = QStringLiteral("attach");
        req["arg"] = args.at(2);
        return {true, dispatch(req, false)};
    }
    if (verb == QStringLiteral("kill")) {
        if (args.size() < 3) {
            err() << kai::utils::tr(QStringLiteral("cli.error.usage.kill")) << "\n"; err().flush();
            return {true, 2};
        }
        QJsonObject req;
        req["cmd"] = QStringLiteral("kill");
        req["arg"] = args.at(2);
        return {true, dispatch(req, false)};
    }
    if (verb == QStringLiteral("env")) {
        const QString sub = args.size() >= 3 ? args.at(2) : QString();
        if (sub == QStringLiteral("list")) {
            QJsonObject req;
            req["cmd"] = QStringLiteral("env-list");
            return {true, dispatch(req, true)};
        }
        if (sub == QStringLiteral("use")) {
            if (args.size() < 4) {
                err() << kai::utils::tr(QStringLiteral("cli.error.usage.env_use")) << "\n";
                err().flush();
                return {true, 2};
            }
            QJsonObject req;
            req["cmd"] = QStringLiteral("env-use");
            req["arg"] = args.at(3);
            return {true, dispatch(req, false)};
        }
        err() << kai::utils::tr(QStringLiteral("cli.error.usage.env")) << "\n";
        err().flush();
        return {true, 2};
    }
    if (verb == QStringLiteral("import")) {
        if (args.size() < 3) {
            err() << kai::utils::tr(QStringLiteral("cli.error.usage.import")) << "\n";
            err().flush();
            return {true, 2};
        }
        const QString filePath = args.at(2);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            err() << kai::utils::tr(QStringLiteral("cli.error.open_file")) << filePath << "\n";
            err().flush();
            return {true, 1};
        }

        const QString jsonContent = QString::fromUtf8(file.readAll());
        file.close();

        QJsonObject req;
        req["cmd"] = QStringLiteral("import");
        req["json"] = jsonContent;
        return {true, dispatch(req, false)};
    }
    if (verb == QStringLiteral("help") || verb == QStringLiteral("--help") || verb == QStringLiteral("-h")) {
        printUsage();
        return {true, 0};
    }

    // Não é um verbo de CLI conhecido — deixa o main abrir a GUI.
    return {false, 0};
}

} // namespace kai::ipc
