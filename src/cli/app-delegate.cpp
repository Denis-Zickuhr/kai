#include "cli/app-delegate.h"
#include "cli/terminal-input.h"
#include "cli/terminal-mode-filter.h"

#include "engine/process-runner.h"
#include "ipc/ipc-server.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QTextStream>
#include <QThread>
#include <QTimer>



namespace kai::cli {

namespace {

constexpr const char *kLogTag = "AppDelegate";
constexpr int kLaunchTimeoutMs = 20000;

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

bool tryConnect(QLocalSocket &socket, int timeoutMs)
{
    socket.connectToServer(ipc::ipcSocketName());
    if (socket.waitForConnected(timeoutMs)) {
        return true;
    }
    socket.abort();
    return false;
}

void launchAppInTray()
{
    QProcess app;
    app.setProgram(QCoreApplication::applicationFilePath());
    app.setArguments({});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("KAI_FORCE_GUI"), QStringLiteral("1"));
    env.insert(QStringLiteral("KAI_START_HIDDEN"), QStringLiteral("1"));
    app.setProcessEnvironment(env);
    app.setWorkingDirectory(QCoreApplication::applicationDirPath());
    app.setStandardInputFile(QProcess::nullDevice());
    app.setStandardOutputFile(QProcess::nullDevice());
    app.setStandardErrorFile(QProcess::nullDevice());
    engine::isolateDetachedProcess(app);
    if (!app.startDetached()) {
        utils::Logger::warning(kLogTag, QStringLiteral("Falha ao subir o Kai na bandeja: %1").arg(app.errorString()));
    }
}

// Conecta no app, subindo-o na bandeja se preciso. A trava serializa CLIs
// concorrentes: quem pega a trava depois reencontra o app que o primeiro
// subiu, em vez de subir outro.
bool connectToApp(QLocalSocket &socket)
{
    if (tryConnect(socket, 500)) {
        return true;
    }
    // Testes: nunca subir o app (applicationFilePath seria o binário de teste).
    if (qEnvironmentVariableIsSet("KAI_CLI_NO_APP_LAUNCH")) {
        return false;
    }
    QLockFile launchLock(QDir(QDir::tempPath()).filePath(QStringLiteral("kai-app-launch.lock")));
    launchLock.setStaleLockTime(kLaunchTimeoutMs * 2);
    if (!launchLock.tryLock(kLaunchTimeoutMs)) {
        return tryConnect(socket, 500);
    }
    if (tryConnect(socket, 500)) {
        return true;
    }
    err() << utils::tr(QStringLiteral("cli.stream.launching")) << "\n";
    err().flush();
    launchAppInTray();
    const QDeadlineTimer deadline(kLaunchTimeoutMs);
    while (!deadline.hasExpired()) {
        if (tryConnect(socket, 500)) {
            return true;
        }
        QThread::msleep(250);
    }
    return false;
}

} // namespace


std::optional<QJsonObject> requestApp(const QJsonObject &request, int timeoutMs)
{
    QLocalSocket socket;
    if (!connectToApp(socket)) {
        return std::nullopt;
    }
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    // Sem waitForReadyRead: o app pode demorar (ex: import grande) e o
    // QEventLoop mantém o processo responsivo enquanto espera.
    QEventLoop loop;
    std::optional<QJsonObject> reply;
    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&]() {
        if (socket.canReadLine()) {
            reply = QJsonDocument::fromJson(socket.readLine()).object();
            loop.quit();
        }
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return reply;
}

// Sessão de streaming com o app (run-stream / attach-stream): espelha a
// saída no terminal e manda o teclado pro processo, até o "finished".
// `allowDetachKey` (attach): Ctrl+] solta o terminal sem mexer no processo.
static int streamSession(QLocalSocket &socket, const QString &displayName, bool allowDetachKey)
{
    QEventLoop loop;
    int exitCode = 1;
    bool finished = false;
    TerminalModeFilter outFilter;
    TerminalModeFilter errFilter;
    QString pendingEcho;
    bool stripEcho = false;

    bool detachedByUser = false;
    StdinForwarder stdinForwarder([&](const QString &text) {
        if (socket.state() != QLocalSocket::ConnectedState) {
            return;
        }
        if (allowDetachKey && text.contains(QChar(0x1d))) {
            detachedByUser = true;
            finished = true;
            exitCode = 0;
            loop.quit();
            return;
        }
        QJsonObject msg;
        msg[QStringLiteral("type")] = QStringLiteral("input");
        msg[QStringLiteral("text")] = text;
        socket.write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
        socket.flush();
        if (stripEcho) {
            pendingEcho += text;
        }
    });
    stripEcho = !stdinForwarder.isTty();

    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&]() {
        while (socket.canReadLine()) {
            const QJsonObject msg = QJsonDocument::fromJson(socket.readLine()).object();
            const QString type = msg.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("output")) {
                QString text = msg.value(QStringLiteral("text")).toString();
                if (stripEcho) {
                    text = stripPendingEcho(text, pendingEcho);
                }
                const bool isError = msg.value(QStringLiteral("err")).toBool();
                QTextStream &stream = isError ? err() : out();
                stream << (isError ? errFilter : outFilter).feed(text);
                stream.flush();
            } else if (type == QStringLiteral("background")) {
                out() << utils::tr(QStringLiteral("cli.stream.background_started"))
                             .arg(displayName)
                             .arg(static_cast<qint64>(msg.value(QStringLiteral("pid")).toDouble()))
                      << "\n";
                out().flush();
            } else if (type == QStringLiteral("finished")) {
                exitCode = msg.value(QStringLiteral("code")).toInt(1);
                const QString message = msg.value(QStringLiteral("message")).toString();
                if (!message.isEmpty()) {
                    err() << message << "\n";
                    err().flush();
                }
                finished = true;
                loop.quit();
            }
        }
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &loop, [&]() {
        if (!finished) {
            err() << "\n" << utils::tr(QStringLiteral("cli.stream.connection_lost")) << "\n";
            err().flush();
            loop.quit();
        }
    });

    loop.exec();
    if (detachedByUser) {
        socket.disconnectFromServer();
        err() << "\n" << utils::tr(QStringLiteral("cli.attach.detached")).arg(displayName) << "\n";
    }
    out() << outFilter.flush();
    out().flush();
    err() << errFilter.flush();
    err().flush();
    return exitCode;
}

std::optional<int> runViaApp(const core::Command &command, const QMap<QString, QString> &paramValues,
                             bool detached, bool notify, bool window)
{
    QLocalSocket socket;
    if (!connectToApp(socket)) {
        return std::nullopt;
    }

    QJsonObject params;
    for (auto it = paramValues.constBegin(); it != paramValues.constEnd(); ++it) {
        params.insert(it.key(), it.value());
    }
    QJsonObject request;
    request[QStringLiteral("cmd")] = QStringLiteral("run-stream");
    request[QStringLiteral("command_id")] = command.id;
    request[QStringLiteral("params")] = params;
    request[QStringLiteral("detached")] = detached;
    request[QStringLiteral("notify")] = notify;
    request[QStringLiteral("window")] = window;
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();

    if (detached) {
        // Só a confirmação: {"type":"finished","code":0} = aceito e disparado.
        QEventLoop acceptLoop;
        int acceptCode = 1;
        QString acceptMessage = utils::tr(QStringLiteral("cli.stream.connection_lost"));
        QObject::connect(&socket, &QLocalSocket::readyRead, &acceptLoop, [&]() {
            while (socket.canReadLine()) {
                const QJsonObject msg = QJsonDocument::fromJson(socket.readLine()).object();
                if (msg.value(QStringLiteral("type")).toString() == QStringLiteral("finished")) {
                    acceptCode = msg.value(QStringLiteral("code")).toInt(1);
                    acceptMessage = msg.value(QStringLiteral("message")).toString();
                    acceptLoop.quit();
                }
            }
        });
        QObject::connect(&socket, &QLocalSocket::disconnected, &acceptLoop, &QEventLoop::quit);
        acceptLoop.exec();
        if (acceptCode == 0) {
            out() << utils::tr(QStringLiteral("cli.detached.app_started")).arg(command.name) << "\n";
            out().flush();
        } else if (!acceptMessage.isEmpty()) {
            err() << acceptMessage << "\n";
            err().flush();
        }
        return acceptCode;
    }

    return streamSession(socket, command.name, /*allowDetachKey=*/false);
}

std::optional<int> attachViaApp(const QString &target)
{
    // Sem app aberto não há o que acompanhar — não sobe um só pra isso.
    QLocalSocket socket;
    if (!tryConnect(socket, 1000)) {
        return std::nullopt;
    }
    QJsonObject request;
    request[QStringLiteral("cmd")] = QStringLiteral("attach-stream");
    request[QStringLiteral("arg")] = target;
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    err() << utils::tr(QStringLiteral("cli.attach.hint")) << "\n";
    err().flush();
    return streamSession(socket, target, /*allowDetachKey=*/true);
}

} // namespace kai::cli
