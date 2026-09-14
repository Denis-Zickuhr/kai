#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "ipc/ipc-server.h"

// Servidor IPC de verdade, num nome só deste processo de teste, que responde como
// um Kai com dois comandos (Deploy, Build), dois ambientes (Dev ativo, Prod) e um
// processo (api). Cada pedido que muda algo fica em `calls`.
struct KaiIpcFixture {
    kai::ipc::IpcServer server;
    QStringList calls;
    bool started = false;

    KaiIpcFixture()
    {
        using namespace kai;
        qputenv("KAI_IPC_SOCKET_NAME_OVERRIDE", QByteArray("kai-ipc-test-") + QByteArray::number(QCoreApplication::applicationPid()));
        started = server.start();
        QObject::connect(&server, &ipc::IpcServer::raiseRequested,
            [this](const QString &level, const QString &title, const QString &message, bool &ok, QString &reply) {
                calls << QStringLiteral("raise|%1|%2|%3").arg(level, title, message);
                ok = true;
                reply = QStringLiteral("ok");
            });
        QObject::connect(&server, &ipc::IpcServer::listRequested,
            [](QStringList &names) { names = {QStringLiteral("Deploy"), QStringLiteral("Build")}; });
        QObject::connect(&server, &ipc::IpcServer::envListRequested,
            [](QStringList &names, QString &active) { names = {QStringLiteral("Dev"), QStringLiteral("Prod")}; active = QStringLiteral("Dev"); });
        QObject::connect(&server, &ipc::IpcServer::envUseRequested,
            [this](const QString &name, bool &ok, QString &message) {
                calls << QStringLiteral("env-use|") + name;
                ok = name != QLatin1String("Nope");
                message = ok ? QStringLiteral("active: ") + name : QStringLiteral("unknown environment");
            });
        QObject::connect(&server, &ipc::IpcServer::psItemsRequested, [](QJsonArray &items) {
            items = QJsonArray{QJsonObject{{"pid", 4242}, {"name", "api"}}};
        });
        QObject::connect(&server, &ipc::IpcServer::killRequested,
            [this](const QString &target, bool &ok, QString &message) {
                calls << QStringLiteral("kill|") + target;
                ok = true;
                message = QStringLiteral("stopped ") + target;
            });
        QObject::connect(&server, &ipc::IpcServer::runRequested,
            [this](const QString &name, bool &ok, QString &message) {
                calls << QStringLiteral("run|") + name;
                ok = true;
                message = QStringLiteral("started ") + name;
            });
        QObject::connect(&server, &ipc::IpcServer::showRequested, [this]() { calls << QStringLiteral("show"); });
        QObject::connect(&server, &ipc::IpcServer::importRequested,
            [this](const QString &path, const QStringList &only, bool &ok, QString &message) {
                calls << QStringLiteral("import|") + path + (only.isEmpty() ? QString() : QStringLiteral("|") + only.join(QLatin1Char(',')));
                ok = true;
                message = QStringLiteral("imported");
            });
    }
    ~KaiIpcFixture() { qunsetenv("KAI_IPC_SOCKET_NAME_OVERRIDE"); }
};
