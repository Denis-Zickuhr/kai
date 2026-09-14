#include "ipc/cli-client.h"
#include "ipc/ipc-server.h"
#include "core/kai-file-validator.h"
#include "utils/translation-manager.h"

#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QFile>
#include <QSet>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

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

// true só quando a saída padrão é um TERMINAL interativo de verdade (não um
// clique no ícone/launcher, nem um pipe/redirect) — é o que diferencia
// "rodei `kai` solto no terminal" de "abri o app normalmente" (pedido do
// usuário: "ao rodar via terminal, só chamando KAI... abra o help
// automático... atualmente tem comando pra isso?" — não tinha; sem esta
// checagem, teria que ser um verbo explícito, ou teria quebrado abrir o
// app pelo launcher/ícone, que também invoca o binário sem argumentos mas
// SEM tty nenhum atrás).
//
// BUG REAL encontrado depois (relatado: "subi o ctn e não abriu o app"):
// isatty(stdout) sozinho não basta — o loop de dev (docker/watch.sh via
// entr) relança `./bin/kai` sem argumento nenhum DENTRO de um pty de
// verdade (o próprio terminal onde `docker exec` foi chamado), então essa
// relançada automática também caía no "discover" e fechava na hora, em
// vez de abrir a GUI. KAI_FORCE_GUI é a válvula de escape explícita —
// setada por watch.sh — pra dizer "isto aqui é sempre abertura de GUI,
// mesmo tendo um tty atrás".
bool stdoutIsInteractiveTerminal()
{
    if (qEnvironmentVariableIsSet("KAI_FORCE_GUI")) {
        return false;
    }
#ifdef Q_OS_WIN
    // kai.exe é WIN32 subsystem (sem console próprio) — main.cpp SEMPRE
    // tenta AttachConsole(ATTACH_PARENT_PROCESS) antes de chegar aqui (ver
    // attachParentConsoleForCli, chamado incondicionalmente, inclusive na
    // invocação SOLTA — pedido do usuário: "se eu rodar o kai do Windows a
    // partir do WSL, via interop, não quero que abra a GUI", que exige
    // detectar um console de verdade atrás mesmo sem argumento nenhum).
    // Quando NÃO existe console pai (duplo-clique/atalho), AttachConsole
    // falha e stdout nunca chega a ser anexado — nesse estado
    // _fileno(stdout) devolve o sentinela -2 da MSVC CRT ("arquivo não
    // associado a um stream C aberto"). Chamar _isatty com um fd negativo é
    // território de comportamento não-confiável da CRT (pode devolver
    // não-zero em vez de 0 dependendo da versão/build) — checar o fd ANTES
    // evita a ambiguidade: sem console anexado (tentativa já feita e
    // falhou), nunca é "interativo". Com console anexado de verdade
    // (console real, OU o que o WSL cria pro processo Windows via
    // interop), o fd é válido e _isatty decide certo.
    const int fd = _fileno(stdout);
    if (fd < 0) {
        return false;
    }
    return _isatty(fd) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
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
          << kai::utils::tr(QStringLiteral("cli.usage.validate")) << "\n"
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
    socket.connectToServer(ipcSocketName());
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

// Verbos reconhecidos por runCliIfRequested — ver o if-chain lá embaixo.
// Lista PRÓPRIA (não reservedCliVerbs() de core/cli-reserved-verbs.h): esta
// aqui é só pra decidir QCoreApplication vs QApplication em main.cpp,
// enquanto a outra decide colisão de CLI Path — universos relacionados mas
// não idênticos (esta inclui "help"/"--help"/"-h", que não é um verbo
// reservado pra fins de cli_path).
bool isKnownVerb(const QString &verb)
{
    static const QSet<QString> verbs = {
        QStringLiteral("run"), QStringLiteral("list"), QStringLiteral("show"),
        QStringLiteral("ps"), QStringLiteral("attach"), QStringLiteral("kill"),
        QStringLiteral("env"), QStringLiteral("import"), QStringLiteral("validate"),
        QStringLiteral("help"), QStringLiteral("--help"), QStringLiteral("-h"),
    };
    return verbs.contains(verb);
}

} // namespace

bool shouldHandleAsCli(const QStringList &args)
{
    if (args.size() < 2) {
        return stdoutIsInteractiveTerminal();
    }
    return isKnownVerb(args.at(1));
}

CliOutcome runCliIfRequested(const QStringList &args)
{
    // args[0] é o caminho do executável. O verbo é args[1].
    if (args.size() < 2) {
        // `kai` solto, sem verbo nenhum: só conta como "discover" (ajuda +
        // lista de comandos do projeto atual) quando tem um TERMINAL
        // interativo de verdade atrás — senão é o launcher/ícone abrindo o
        // app normalmente, e isto teria que continuar abrindo a GUI (ver
        // stdoutIsInteractiveTerminal).
        if (!stdoutIsInteractiveTerminal()) {
            return {false, 0};
        }
        printUsage();
        out() << "\n";
        out().flush();
        QJsonObject req;
        req["cmd"] = QStringLiteral("list");
        // Sem instância rodando, dispatch() já imprime o erro
        // "cli.error.not_running" em stderr sozinho — não precisa de
        // tratamento especial aqui, só não quebra o fluxo.
        dispatch(req, true);
        return {true, 0};
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
    if (verb == QStringLiteral("validate")) {
        // Ao contrário dos outros verbos, NÃO precisa de uma instância do
        // Kai rodando — é uma checagem estrutural puramente local do
        // arquivo (pedido do usuário: "kai validate file, pra yml e json").
        if (args.size() < 3) {
            err() << kai::utils::tr(QStringLiteral("cli.error.usage.validate")) << "\n";
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
        const QString text = QString::fromUtf8(file.readAll());
        file.close();

        const core::ValidationResult result = core::validateKaiFileText(text);
        for (const core::ValidationIssue &issue : result.issues) {
            QTextStream &stream = (issue.severity == core::ValidationSeverity::Error) ? err() : out();
            const QString label = (issue.severity == core::ValidationSeverity::Error)
                ? kai::utils::tr(QStringLiteral("validate.label.error"))
                : kai::utils::tr(QStringLiteral("validate.label.warning"));
            stream << label << " " << issue.path << ": " << issue.message << "\n";
        }
        (result.hasErrors() ? err() : out())
            << kai::utils::tr(QStringLiteral("validate.summary"))
                   .arg(result.errorCount())
                   .arg(result.warningCount())
            << "\n";
        out().flush();
        err().flush();
        return {true, result.hasErrors() ? 1 : 0};
    }
    if (verb == QStringLiteral("help") || verb == QStringLiteral("--help") || verb == QStringLiteral("-h")) {
        printUsage();
        return {true, 0};
    }

    // Não é um verbo de CLI conhecido — deixa o main abrir a GUI.
    return {false, 0};
}

} // namespace kai::ipc
