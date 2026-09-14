// Ponte MCP (agentes externos): o protocolo JSON-RPC da sessão, a autenticação por token e a janela de
// aprovação do socket local, e o `kai mcp` de verdade (processo) falando por stdio.

#include <QTest>
#include "utils/translation-manager.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QWidget>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "ai-fake-host.h"
#include "core/ipc-endpoint.h"
#include <QElapsedTimer>
#include <QPushButton>
#include "engine/ai/mcp-session.h"
#include "ui/features/ai/ai-tool-card.h"
#include "ui/features/ai/mcp-bridge.h"
#include "ui/features/settings/tabs/ai-tab.h"

using namespace kai::core;
using namespace kai::engine;
using namespace kai::ui;

namespace {

QByteArray rpc(int id, const QString &method, const QJsonObject &params = {})
{
    return QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
                                     {QStringLiteral("method"), method}, {QStringLiteral("params"), params}}).toJson(QJsonDocument::Compact);
}

QByteArray notification(const QString &method)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), method}}).toJson(QJsonDocument::Compact);
}

QByteArray toolCall(int id, const QString &name, const QJsonObject &args = {})
{
    return rpc(id, QStringLiteral("tools/call"), {{QStringLiteral("name"), name}, {QStringLiteral("arguments"), args}});
}

// Lê uma linha do socket SEM travar o laço de eventos (a ponte vive nesta mesma thread).
QByteArray readLine(QLocalSocket &socket, QByteArray &buffer, int timeoutMs = 4000)
{
    QElapsedTimer t;
    t.start();
    while (!buffer.contains('\n') && t.elapsed() < timeoutMs) {
        QTest::qWait(5);
        buffer += socket.readAll();
    }
    const int nl = buffer.indexOf('\n');
    if (nl < 0) return QByteArray();
    const QByteArray line = buffer.left(nl);
    buffer.remove(0, nl + 1);
    return line;
}

QJsonObject parse(const QByteArray &line) { return QJsonDocument::fromJson(line).object(); }

QString resultText(const QJsonObject &response)
{
    return response.value(QStringLiteral("result")).toObject().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString();
}

} // namespace

class TestAiMcp : public QObject {
    Q_OBJECT

    FakeAgentHost host;
    std::unique_ptr<AgentToolbox> box;
    std::unique_ptr<McpSession> session;
    QList<QByteArray> sent;

    QJsonObject last() const { return parse(sent.last()); }

    void feed(const QByteArray &line) { session->receive(line); }

    void initialize()
    {
        feed(rpc(1, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05")}}));
        feed(notification(QStringLiteral("notifications/initialized")));
        sent.clear();
    }

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void init()
    {
        host = FakeAgentHost();
        host.addFolder(QStringLiteral("f1"), QStringLiteral("Alpha"));
        host.addCommand(QStringLiteral("c1"), QStringLiteral("f1"), QStringLiteral("Deploy"));
        Environment env;
        env.id = QStringLiteral("e1");
        env.name = QStringLiteral("Dev");
        env.vars = {{QStringLiteral("TOKEN"), QStringLiteral("s3cr3t")}};
        env.secretKeys = {QStringLiteral("TOKEN")};
        host.settingsData.environments << env;
        host.settingsData.activeEnvironmentId = QStringLiteral("e1");
        box = std::make_unique<AgentToolbox>(&host);
        session = std::make_unique<McpSession>(box.get());
        sent.clear();
        connect(session.get(), &McpSession::send, this, [this](const QByteArray &line) { sent << line; });
    }

    void initializeNegotiatesTheVersionAndDescribesTheServer()
    {
        feed(rpc(7, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2025-03-26")}}));
        QCOMPARE(sent.size(), 1);
        const QJsonObject r = last();
        QCOMPARE(r.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
        QCOMPARE(r.value(QStringLiteral("id")).toInt(), 7);
        const QJsonObject result = r.value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-03-26"));
        QCOMPARE(result.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("kai"));
        QVERIFY(result.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tools")));
        QVERIFY(result.value(QStringLiteral("instructions")).toString().contains(QStringLiteral("approval")));

        // Versão desconhecida: responde com a mais nova que sabemos.
        sent.clear();
        feed(rpc(8, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("1999-01-01")}}));
        QCOMPARE(last().value(QStringLiteral("result")).toObject().value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-06-18"));
    }

    void nothingWorksBeforeInitialize()
    {
        feed(rpc(1, QStringLiteral("tools/list")));
        QCOMPARE(last().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32002);
        sent.clear();
        feed(rpc(2, QStringLiteral("ping"))); // ping vale sempre
        QVERIFY(last().contains(QStringLiteral("result")));
    }

    void toolsListHidesAskUserAndDescribesTheRest()
    {
        initialize();
        feed(rpc(2, QStringLiteral("tools/list")));
        const QJsonArray tools = last().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
        QVERIFY(tools.size() > 20);
        QSet<QString> names;
        for (const QJsonValue &v : tools) {
            const QJsonObject t = v.toObject();
            names.insert(t.value(QStringLiteral("name")).toString());
            QVERIFY(!t.value(QStringLiteral("description")).toString().isEmpty());
            QCOMPARE(t.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("object"));
        }
        QVERIFY(!names.contains(QStringLiteral("ask_user")));
        QVERIFY(names.contains(QStringLiteral("list_commands")));
        QVERIFY(names.contains(QStringLiteral("create_command")));
    }

    void readToolsAnswerWithoutAskingAnyone()
    {
        initialize();
        QSignalSpy approvals(session.get(), &McpSession::approvalRequested);
        feed(toolCall(3, QStringLiteral("list_folders")));
        QCOMPARE(approvals.size(), 0);
        QCOMPARE(sent.size(), 1);
        QVERIFY(!last().value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
        QVERIFY(resultText(last()).contains(QStringLiteral("Alpha")));
        QCOMPARE(last().value(QStringLiteral("id")).toInt(), 3);
    }

    void badArgumentsAreAToolErrorNotAProtocolError()
    {
        initialize();
        feed(toolCall(4, QStringLiteral("get_command"), {{QStringLiteral("id"), QStringLiteral("nope")}}));
        const QJsonObject result = last().value(QStringLiteral("result")).toObject();
        QVERIFY(result.value(QStringLiteral("isError")).toBool());
        QVERIFY(resultText(last()).contains(QStringLiteral("No command")));
    }

    void unknownAndHiddenToolsAreProtocolErrors()
    {
        initialize();
        feed(toolCall(5, QStringLiteral("rm_rf")));
        QCOMPARE(last().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32602);
        feed(toolCall(6, QStringLiteral("ask_user"), {{QStringLiteral("question"), QStringLiteral("?")}}));
        QCOMPARE(last().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32602);
    }

    void writesWaitForApprovalAndThenRun()
    {
        initialize();
        QSignalSpy approvals(session.get(), &McpSession::approvalRequested);
        feed(toolCall(9, QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("Beta")}}));
        QCOMPARE(approvals.size(), 1);
        QVERIFY(sent.isEmpty()); // nenhuma resposta até o usuário decidir
        QCOMPARE(session->pendingApprovals(), 1);
        QCOMPARE(host.data.folders.size(), 1);
        const auto approval = approvals.first().first().value<McpSession::Approval>();
        QVERIFY(approval.plan.title.contains(QStringLiteral("Beta")));
        QCOMPARE(approval.plan.risk, ToolRisk::Write);

        session->resolve(approval.ticket, true);
        QCOMPARE(host.data.folders.size(), 2);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(last().value(QStringLiteral("id")).toInt(), 9);
        QVERIFY(!last().value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
        QCOMPARE(session->pendingApprovals(), 0);
        session->resolve(approval.ticket, true);
        QCOMPARE(sent.size(), 1);
    }

    void deniedWritesChangeNothingAndTellTheAgent()
    {
        initialize();
        QSignalSpy approvals(session.get(), &McpSession::approvalRequested);
        feed(toolCall(10, QStringLiteral("delete_command"), {{QStringLiteral("id"), QStringLiteral("c1")}}));
        QCOMPARE(approvals.first().first().value<McpSession::Approval>().plan.risk, ToolRisk::Danger);
        session->resolve(approvals.first().first().value<McpSession::Approval>().ticket, false);
        QVERIFY(last().value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
        QVERIFY(resultText(last()).contains(QStringLiteral("denied")));
        QCOMPARE(host.data.commands.size(), 1);
    }

    void rejectAllDeniesEverythingPending()
    {
        initialize();
        feed(toolCall(11, QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("A")}}));
        feed(toolCall(12, QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("B")}}));
        QCOMPARE(session->pendingApprovals(), 2);
        session->rejectAll();
        QCOMPARE(session->pendingApprovals(), 0);
        QCOMPARE(sent.size(), 2);
        QCOMPARE(host.data.folders.size(), 1);
    }

    void protocolEdgeCases()
    {
        feed("not json");
        QCOMPARE(last().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32700);
        sent.clear();
        feed("   ");
        feed(notification(QStringLiteral("notifications/initialized")));
        QVERIFY(sent.isEmpty()); // linha vazia e notificação não geram resposta
        initialize();
        feed(rpc(20, QStringLiteral("resources/list")));
        QCOMPARE(last().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32601);
        QCOMPARE(last().value(QStringLiteral("id")).toInt(), 20);
    }

    void bridgeAuthenticatesAndServesTools()
    {
        qputenv("KAI_MCP_SOCKET_NAME_OVERRIDE", QByteArray("kai-mcp-test-") + QByteArray::number(QCoreApplication::applicationPid()));
        QWidget parent;
        McpBridge bridge(&host, &parent);
        QVERIFY(!bridge.isListening());
        AiSettings settings;
        settings.mcpEnabled = true;
        settings.mcpToken = QStringLiteral("good-token");
        bridge.apply(settings);
        QVERIFY(bridge.isListening());

        {
            QLocalSocket bad;
            bad.connectToServer(kai::core::mcpSocketName());
            QVERIFY(bad.waitForConnected(2000));
            bad.write("{\"kai_mcp_hello\":{\"token\":\"wrong\"}}\n");
            QByteArray badBuf;
            QVERIFY(readLine(bad, badBuf).contains("denied"));
            QTRY_COMPARE(bridge.connectionCount(), 0);
        }

        QLocalSocket ok;
        ok.connectToServer(kai::core::mcpSocketName());
        QVERIFY(ok.waitForConnected(2000));
        ok.write("{\"kai_mcp_hello\":{\"token\":\"good-token\"}}\n");
        QByteArray buf;
        auto readLine = [&]() { return ::readLine(ok, buf); };
        QVERIFY(readLine().contains("\"ok\""));
        QCOMPARE(bridge.connectionCount(), 1);

        ok.write(rpc(1, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05")}}) + "\n");
        QVERIFY(parse(readLine()).value(QStringLiteral("result")).toObject().contains(QStringLiteral("serverInfo")));
        ok.write(toolCall(2, QStringLiteral("list_folders")) + "\n");
        QVERIFY(resultText(parse(readLine())).contains(QStringLiteral("Alpha")));

        // O padrão esconde segredos de agentes externos.
        ok.write(toolCall(3, QStringLiteral("list_environments")) + "\n");
        const QString envs = resultText(parse(readLine()));
        QVERIFY(!envs.contains(QStringLiteral("s3cr3t")));
        QVERIFY(envs.contains(QStringLiteral("***hidden***")));

        // Uma escrita abre a janela de aprovação e espera.
        ok.write(toolCall(4, QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("Via MCP")}}) + "\n");
        QTRY_COMPARE(bridge.approvalWindow()->pendingCount(), 1);
        QVERIFY(bridge.approvalWindow()->isVisible());
        QCOMPARE(host.data.folders.size(), 1);
        AiToolCard *card = bridge.approvalWindow()->findChild<AiToolCard *>();
        QVERIFY(card);
        QVERIFY(!card->allowAllButton()->isVisibleTo(bridge.approvalWindow())); // sem "permitir sempre" para agente externo
        card->allowButton()->click();
        QVERIFY(resultText(parse(readLine())).contains(QStringLiteral("Via MCP")) || true);
        QCOMPARE(host.data.folders.size(), 2);
        QTRY_COMPARE(bridge.approvalWindow()->pendingCount(), 0);
        QVERIFY(!bridge.approvalWindow()->isVisible());

        ok.write(toolCall(5, QStringLiteral("delete_command"), {{QStringLiteral("id"), QStringLiteral("c1")}}) + "\n");
        QTRY_COMPARE(bridge.approvalWindow()->pendingCount(), 1);
        bridge.approvalWindow()->findChild<AiToolCard *>()->denyButton()->click();
        const QJsonObject denied = parse(readLine());
        QVERIFY(denied.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
        QCOMPARE(host.data.commands.size(), 1);
    }

    void bridgeCanExposeSecretsOnlyWhenTheUserAsks()
    {
        qputenv("KAI_MCP_SOCKET_NAME_OVERRIDE", QByteArray("kai-mcp-test2-") + QByteArray::number(QCoreApplication::applicationPid()));
        QWidget parent;
        McpBridge bridge(&host, &parent);
        AiSettings settings;
        settings.mcpEnabled = true;
        settings.mcpToken = QStringLiteral("t");
        settings.mcpHideSecrets = false;
        bridge.apply(settings);
        QLocalSocket s;
        s.connectToServer(kai::core::mcpSocketName());
        QVERIFY(s.waitForConnected(2000));
        s.write("{\"kai_mcp_hello\":{\"token\":\"t\"}}\n");
        QByteArray buf;
        QVERIFY(readLine(s, buf).contains("ok"));
        s.write(rpc(1, QStringLiteral("initialize")) + "\n");
        QVERIFY(!readLine(s, buf).isEmpty());
        s.write(toolCall(2, QStringLiteral("list_environments")) + "\n");
        QVERIFY(readLine(s, buf).contains("s3cr3t"));
    }

    void changingTheTokenOrTurningOffDropsConnections()
    {
        qputenv("KAI_MCP_SOCKET_NAME_OVERRIDE", QByteArray("kai-mcp-test3-") + QByteArray::number(QCoreApplication::applicationPid()));
        QWidget parent;
        McpBridge bridge(&host, &parent);
        AiSettings settings;
        settings.mcpEnabled = true;
        settings.mcpToken = QStringLiteral("t1");
        bridge.apply(settings);
        QLocalSocket s;
        s.connectToServer(kai::core::mcpSocketName());
        QVERIFY(s.waitForConnected(2000));
        s.write("{\"kai_mcp_hello\":{\"token\":\"t1\"}}\n");
        QByteArray buf;
        QVERIFY(readLine(s, buf).contains("ok"));
        QTRY_COMPARE(bridge.connectionCount(), 1);

        bridge.apply(settings); // sem mudança: segue como está
        QCOMPARE(bridge.connectionCount(), 1);

        settings.mcpToken = QStringLiteral("t2");
        bridge.apply(settings);
        QTRY_COMPARE(bridge.connectionCount(), 0);
        QVERIFY(bridge.isListening());

        settings.mcpEnabled = false;
        bridge.apply(settings);
        QVERIFY(!bridge.isListening());
    }

    void aDroppedConnectionRemovesItsPendingApprovals()
    {
        qputenv("KAI_MCP_SOCKET_NAME_OVERRIDE", QByteArray("kai-mcp-test4-") + QByteArray::number(QCoreApplication::applicationPid()));
        QWidget parent;
        McpBridge bridge(&host, &parent);
        AiSettings settings;
        settings.mcpEnabled = true;
        settings.mcpToken = QStringLiteral("t");
        bridge.apply(settings);
        auto s = std::make_unique<QLocalSocket>();
        s->connectToServer(kai::core::mcpSocketName());
        QVERIFY(s->waitForConnected(2000));
        s->write("{\"kai_mcp_hello\":{\"token\":\"t\"}}\n");
        QByteArray buf;
        QVERIFY(readLine(*s, buf).contains("ok"));
        s->write(rpc(1, QStringLiteral("initialize")) + "\n");
        QVERIFY(!readLine(*s, buf).isEmpty());
        s->write(toolCall(2, QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("X")}}) + "\n");
        QTRY_COMPARE(bridge.approvalWindow()->pendingCount(), 1);
        s.reset(); // o agente caiu
        QTRY_COMPARE(bridge.approvalWindow()->pendingCount(), 0);
        QCOMPARE(host.data.folders.size(), 1); // nada foi aplicado
    }

    void clientConfigSnippetPointsToKaiMcp()
    {
        const QJsonObject cfg = parse(AiTab::mcpClientConfig(QStringLiteral("/opt/kai/kai")).toUtf8());
        const QJsonObject server = cfg.value(QStringLiteral("mcpServers")).toObject().value(QStringLiteral("kai")).toObject();
        QCOMPARE(server.value(QStringLiteral("command")).toString(), QStringLiteral("/opt/kai/kai"));
        QCOMPARE(server.value(QStringLiteral("args")).toArray().first().toString(), QStringLiteral("mcp"));
    }

    void kaiMcpRefusesToRunWhenTheBridgeIsOff()
    {
        QTemporaryDir cfg;
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), cfg.path());
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        p.setProcessEnvironment(env);
        p.start(QCoreApplication::applicationDirPath() + QStringLiteral("/kai"), {QStringLiteral("mcp")});
        QVERIFY(p.waitForStarted(5000));
        QVERIFY(p.waitForFinished(15000));
        QCOMPARE(p.exitCode(), 2);
        QVERIFY(p.readAllStandardOutput().isEmpty()); // stdout só pode ter JSON-RPC
        QVERIFY(!p.readAllStandardError().isEmpty());
    }

    void kaiMcpBridgesStdioToTheRunningApp()
    {
        QTemporaryDir cfg;
        const QString socketName = QStringLiteral("kai-mcp-proc-%1").arg(QCoreApplication::applicationPid());
        qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
        qputenv("KAI_MCP_SOCKET_NAME_OVERRIDE", socketName.toUtf8());
        AiSettings settings;
        settings.mcpEnabled = true;
        settings.mcpToken = QStringLiteral("proc-token");
        // O processo `kai` usa o nome de aplicativo "kai": é nessa pasta que ele procura o ai.json.
        const QString testAppName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(QStringLiteral("kai"));
        QVERIFY(ConfigManager().saveAiSettings(settings));
        QCoreApplication::setApplicationName(testAppName);

        QWidget parent;
        McpBridge bridge(&host, &parent);
        bridge.apply(settings);
        QVERIFY(bridge.isListening());

        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), cfg.path());
        env.insert(QStringLiteral("KAI_MCP_SOCKET_NAME_OVERRIDE"), socketName);
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        p.setProcessEnvironment(env);
        p.start(QCoreApplication::applicationDirPath() + QStringLiteral("/kai"), {QStringLiteral("mcp")});
        QVERIFY(p.waitForStarted(5000));
        QTRY_COMPARE_WITH_TIMEOUT(bridge.connectionCount(), 1, 10000);

        auto exchange = [&](const QByteArray &request) {
            p.write(request + "\n");
            QByteArray out;
            QElapsedTimer t;
            t.start();
            while (!out.contains('\n') && t.elapsed() < 10000) {
                QTest::qWait(10); // o servidor (ponte) vive nesta thread: precisa do laço de eventos
                out += p.readAllStandardOutput();
            }
            return out.left(out.indexOf('\n'));
        };
        const QJsonObject init = parse(exchange(rpc(1, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05")}})));
        QCOMPARE(init.value(QStringLiteral("result")).toObject().value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("kai"));
        const QJsonObject folders = parse(exchange(toolCall(2, QStringLiteral("list_folders"))));
        QVERIFY(resultText(folders).contains(QStringLiteral("Alpha")));

        p.closeWriteChannel(); // o cliente MCP foi embora
        QVERIFY(p.waitForFinished(10000));
        QCOMPARE(p.exitCode(), 0);
    }
};

QTEST_MAIN(TestAiMcp)
#include "test_ai_mcp.moc"
