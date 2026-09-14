// Instância única: um segundo IpcServer com o mesmo nome NÃO assume o socket de
// um servidor vivo; um socket ÓRFÃO (arquivo sem ninguém atrás) é reaproveitado.

#include <QTest>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>

#include "ipc/ipc-server.h"

#if defined(Q_OS_UNIX)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using namespace kai::ipc;

class TestIpcSingleInstance : public QObject {
    Q_OBJECT

private slots:
    void aSecondServerDoesNotTakeOverALiveOne()
    {
        qputenv("KAI_IPC_SOCKET_NAME_OVERRIDE", QByteArray("kai-test-single-live-") + QByteArray::number(QCoreApplication::applicationPid()));
        IpcServer first;
        QVERIFY(first.start());
        IpcServer second;
        QVERIFY2(!second.start(), "o segundo servidor não pode assumir o socket de uma instância viva");

        // E o primeiro continua alcançável.
        QLocalSocket client;
        client.connectToServer(ipcSocketName());
        QVERIFY(client.waitForConnected(1000));
    }

    void anOrphanSocketIsReclaimed()
    {
#if !defined(Q_OS_UNIX)
        QSKIP("Socket órfão (arquivo) só existe em Unix; no Windows o pipe some com o processo.");
#else
        const QByteArray name = QByteArray("kai-test-single-stale-") + QByteArray::number(QCoreApplication::applicationPid());
        qputenv("KAI_IPC_SOCKET_NAME_OVERRIDE", name);
        QLocalServer::removeServer(QString::fromLatin1(name));
        const QString path = QDir(QDir::tempPath()).filePath(QString::fromLatin1(name));

        // Cria o arquivo de socket e abandona o descritor: ninguém escuta.
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        QVERIFY(fd >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const QByteArray pathBytes = QFile::encodeName(path);
        QVERIFY(pathBytes.size() < static_cast<int>(sizeof(addr.sun_path)));
        ::strncpy(addr.sun_path, pathBytes.constData(), sizeof(addr.sun_path) - 1);
        QCOMPARE(::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
        ::close(fd);
        QVERIFY(QFile::exists(path));

        IpcServer server;
        QVERIFY2(server.start(), "socket órfão deveria ser removido e reaproveitado");
#endif
    }
};

QTEST_MAIN(TestIpcSingleInstance)
#include "test_ipc_single_instance.moc"
