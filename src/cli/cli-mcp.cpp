#include "cli/cli-mcp.h"

#include "core/config-manager.h"
#include "core/ipc-endpoint.h"
#include "utils/console-context.h"
#include "utils/translation-manager.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMetaObject>
#include <QThread>
#include <QTextStream>

#include <cstdio>
#include <iostream>
#include <string>

namespace kai::cli {

namespace {

QTextStream &err()
{
    return kai::utils::cliErr();
}

int fail(const QString &message, int code)
{
    err() << message << "\n";
    err().flush();
    return code;
}

void writeStdout(const QByteArray &line)
{
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

} // namespace

int runMcpVerb(const QStringList &)
{
    const core::AiSettings settings = core::ConfigManager().loadAiSettings();
    if (!settings.mcpEnabled || settings.mcpToken.isEmpty()) {
        return fail(utils::tr(QStringLiteral("cli.mcp.disabled")), 2);
    }

    QLocalSocket socket;
    socket.connectToServer(core::mcpSocketName());
    if (!socket.waitForConnected(5000)) {
        return fail(utils::tr(QStringLiteral("cli.mcp.not_running")), 2);
    }
    const QJsonObject hello{{QStringLiteral("kai_mcp_hello"), QJsonObject{{QStringLiteral("token"), settings.mcpToken}}}};
    socket.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();

    QByteArray buffer;
    while (!buffer.contains('\n')) {
        if (!socket.waitForReadyRead(5000)) {
            return fail(utils::tr(QStringLiteral("cli.mcp.denied")), 2);
        }
        buffer.append(socket.readAll());
    }
    const int nl = buffer.indexOf('\n');
    const QByteArray reply = buffer.left(nl);
    buffer.remove(0, nl + 1);
    if (QJsonDocument::fromJson(reply).object().value(QStringLiteral("kai_mcp_hello")).toString() != QLatin1String("ok")) {
        return fail(utils::tr(QStringLiteral("cli.mcp.denied")), 2);
    }

    auto drain = [&buffer]() {
        int pos;
        while ((pos = buffer.indexOf('\n')) >= 0) {
            const QByteArray line = buffer.left(pos);
            buffer.remove(0, pos + 1);
            if (!line.trimmed().isEmpty()) writeStdout(line);
        }
    };
    drain();
    QObject::connect(&socket, &QLocalSocket::readyRead, &socket, [&socket, &buffer, drain]() {
        buffer.append(socket.readAll());
        drain();
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, QCoreApplication::instance(), []() {
        QCoreApplication::exit(0); // o Kai fechou
    });

    // Do cliente para o Kai: uma thread lê o stdin (não há leitura assíncrona portável de stdin).
    QThread *reader = QThread::create([&socket]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const QByteArray bytes = QByteArray::fromStdString(line);
            QMetaObject::invokeMethod(&socket, [&socket, bytes]() {
                socket.write(bytes + '\n');
                socket.flush();
            }, Qt::QueuedConnection);
        }
        // Fim do stdin: o cliente foi embora.
        QMetaObject::invokeMethod(QCoreApplication::instance(), []() { QCoreApplication::exit(0); }, Qt::QueuedConnection);
    });
    reader->start(); // bloqueada no stdin até o fim: o processo termina sem esperar por ela

    return QCoreApplication::exec();
}

} // namespace kai::cli
