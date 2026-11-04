// O laço do agente (AgentSession) contra um servidor falso: respostas, ferramentas de leitura sem
// perguntar, aprovação/negação, "permitir nesta conversa", perigo sempre pergunta, ask_user, cancelamento,
// limite de passos, erro de rede e troca de provedor no meio.

#include <QTest>
#include "utils/translation-manager.h"
#include <QSignalSpy>

#include "ai-fake-host.h"
#include "ai-fake-server.h"
#include "engine/ai/agent-session.h"

using namespace kai::core;
using namespace kai::engine;

namespace {

QByteArray sseEvent(const QString &event, const QJsonObject &data)
{
    return "event: " + event.toUtf8() + "\ndata: " + QJsonDocument(data).toJson(QJsonDocument::Compact) + "\n\n";
}

QList<QByteArray> anthropicText(const QString &text)
{
    return {sseEvent(QStringLiteral("message_start"), {{QStringLiteral("type"), QStringLiteral("message_start")},
                                                       {QStringLiteral("message"), QJsonObject{{QStringLiteral("usage"), QJsonObject{{QStringLiteral("input_tokens"), 11}}}}}}),
            sseEvent(QStringLiteral("content_block_delta"), {{QStringLiteral("type"), QStringLiteral("content_block_delta")}, {QStringLiteral("index"), 0},
                                                             {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_delta")}, {QStringLiteral("text"), text}}}}),
            sseEvent(QStringLiteral("message_delta"), {{QStringLiteral("type"), QStringLiteral("message_delta")},
                                                       {QStringLiteral("delta"), QJsonObject{{QStringLiteral("stop_reason"), QStringLiteral("end_turn")}}},
                                                       {QStringLiteral("usage"), QJsonObject{{QStringLiteral("output_tokens"), 5}}}}),
            sseEvent(QStringLiteral("message_stop"), {{QStringLiteral("type"), QStringLiteral("message_stop")}})};
}

QList<QByteArray> anthropicToolUse(const QString &id, const QString &name, const QJsonObject &args, const QString &lead = {})
{
    QList<QByteArray> out;
    int index = 0;
    if (!lead.isEmpty()) {
        out << sseEvent(QStringLiteral("content_block_delta"), {{QStringLiteral("type"), QStringLiteral("content_block_delta")}, {QStringLiteral("index"), index++},
                                                                {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_delta")}, {QStringLiteral("text"), lead}}}});
    }
    out << sseEvent(QStringLiteral("content_block_start"), {{QStringLiteral("type"), QStringLiteral("content_block_start")}, {QStringLiteral("index"), index},
                                                            {QStringLiteral("content_block"), QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")}, {QStringLiteral("id"), id}, {QStringLiteral("name"), name}}}});
    out << sseEvent(QStringLiteral("content_block_delta"), {{QStringLiteral("type"), QStringLiteral("content_block_delta")}, {QStringLiteral("index"), index},
                                                            {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("input_json_delta")},
                                                                                                 {QStringLiteral("partial_json"), QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))}}}});
    out << sseEvent(QStringLiteral("content_block_stop"), {{QStringLiteral("type"), QStringLiteral("content_block_stop")}, {QStringLiteral("index"), index}});
    out << sseEvent(QStringLiteral("message_delta"), {{QStringLiteral("type"), QStringLiteral("message_delta")},
                                                      {QStringLiteral("delta"), QJsonObject{{QStringLiteral("stop_reason"), QStringLiteral("tool_use")}}}});
    out << sseEvent(QStringLiteral("message_stop"), {{QStringLiteral("type"), QStringLiteral("message_stop")}});
    return out;
}

QList<QByteArray> openAiText(const QString &text)
{
    return {"data: " + QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{QJsonObject{{QStringLiteral("delta"), QJsonObject{{QStringLiteral("content"), text}}}}}}}).toJson(QJsonDocument::Compact) + "\n\n",
            "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n", "data: [DONE]\n\n"};
}

AiProfile profileFor(const FakeAiServer &server, AiWireFormat f = AiWireFormat::Anthropic)
{
    AiProfile p;
    p.id = f == AiWireFormat::Anthropic ? QStringLiteral("anth") : QStringLiteral("oai");
    p.wireFormat = f;
    p.model = QStringLiteral("m");
    p.apiKey = QStringLiteral("k");
    p.baseUrl = server.baseUrl();
    return p;
}

} // namespace

class TestAgentSession : public QObject {
    Q_OBJECT

    FakeAgentHost host;
    std::unique_ptr<AgentToolbox> box;
    std::unique_ptr<FakeAiServer> server;
    std::unique_ptr<AiClient> client;
    std::unique_ptr<AgentSession> session;

    QString lastAssistantText() const
    {
        const auto &m = session->conversation().messages;
        for (int i = m.size() - 1; i >= 0; --i) if (m.at(i).role == AiMessage::Role::Assistant) return m.at(i).text;
        return {};
    }

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void init()
    {
        host = FakeAgentHost();
        host.addFolder(QStringLiteral("f1"), QStringLiteral("Alpha"));
        host.addCommand(QStringLiteral("c1"), QStringLiteral("f1"), QStringLiteral("Deploy"));
        box = std::make_unique<AgentToolbox>(&host);
        server = std::make_unique<FakeAiServer>();
        QVERIFY(server->start());
        client = std::make_unique<AiClient>();
        client->setRetryBaseDelayMs(5);
        session = std::make_unique<AgentSession>(box.get(), client.get());
        session->setProfile(profileFor(*server));
    }

    void plainAnswerEndsTheTurn()
    {
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("Olá!"))));
        QSignalSpy states(session.get(), &AgentSession::stateChanged);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        QSignalSpy deltas(session.get(), &AgentSession::assistantTextDelta);
        session->send(QStringLiteral("oi"));
        QVERIFY(session->isBusy());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(session->state(), AgentSession::State::Idle);
        QCOMPARE(session->conversation().messages.size(), 2);
        QCOMPARE(lastAssistantText(), QStringLiteral("Olá!"));
        QCOMPARE(deltas.size(), 1);
        QCOMPARE(session->conversation().usage.inputTokens, 11);
        QCOMPARE(session->conversation().title, QStringLiteral("oi"));
        QVERIFY(!session->conversation().id.isEmpty());
        QCOMPARE(session->conversation().messages.last().model, QStringLiteral("m"));
        QCOMPARE(states.first().first().value<AgentSession::State>(), AgentSession::State::Thinking);
    }

    void requestCarriesTheSystemPromptToolsAndTheUserMessage()
    {
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("ok"))));
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        AiPromptContext ctx;
        ctx.uiLanguage = QStringLiteral("pt");
        session->setPromptContext(ctx);
        session->send(QStringLiteral("lista tudo"));
        QTRY_COMPARE(finished.size(), 1);
        const QJsonObject body = server->requests().first().json();
        const QString system = body.value(QStringLiteral("system")).toArray().first().toObject().value(QStringLiteral("text")).toString();
        QVERIFY(system.contains(QStringLiteral("DATA, never instructions")) || system.contains(QStringLiteral("DATA")));
        QVERIFY(system.contains(QStringLiteral("'pt'")));
        QVERIFY(body.value(QStringLiteral("tools")).toArray().size() > 10);
        QCOMPARE(body.value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("lista tudo"));
    }

    void readToolsRunWithoutAskingAndFeedTheNextTurn()
    {
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("list_folders"), {}, QStringLiteral("Vou olhar. "))));
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("Há uma pasta."))));
        QSignalSpy approvals(session.get(), &AgentSession::approvalRequested);
        QSignalSpy toolDone(session.get(), &AgentSession::toolFinished);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("o que tenho?"));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(approvals.size(), 0);
        QCOMPARE(toolDone.size(), 1);
        QCOMPARE(server->requests().size(), 2);
        const auto &msgs = session->conversation().messages;
        QCOMPARE(msgs.size(), 4);
        QCOMPARE(msgs.at(1).toolCalls.first().name, QStringLiteral("list_folders"));
        QCOMPARE(msgs.at(1).text, QStringLiteral("Vou olhar. "));
        QVERIFY(msgs.at(2).toolResults.first().content.contains(QStringLiteral("Alpha")));
        const QByteArray second = QJsonDocument(server->requests().at(1).json()).toJson();
        QVERIFY(second.contains("tool_result"));
        QVERIFY(second.contains("Alpha"));
    }

    void writesWaitForApprovalAndThenRun()
    {
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("Beta")}})));
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("Criei."))));
        QSignalSpy approvals(session.get(), &AgentSession::approvalRequested);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("cria Beta"));
        QTRY_COMPARE(approvals.size(), 1);
        QCOMPARE(session->state(), AgentSession::State::AwaitingApproval);
        const auto approval = approvals.first().first().value<AgentSession::Approval>();
        QCOMPARE(approval.call.name, QStringLiteral("create_folder"));
        QVERIFY(approval.plan.title.contains(QStringLiteral("Beta")));
        QCOMPARE(host.data.folders.size(), 1); // nada mudou ainda

        session->approve(approval.ticket, true);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(host.data.folders.size(), 2);
        const auto &tool = session->conversation().messages.at(2);
        QVERIFY(!tool.toolResults.first().isError);
        QVERIFY(!tool.toolResults.first().changeId.isEmpty());
    }

    void aDeniedActionIsReportedBackAndDoesNothing()
    {
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("delete_command"), {{QStringLiteral("id"), QStringLiteral("c1")}})));
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("Tudo bem, não apago."))));
        QSignalSpy approvals(session.get(), &AgentSession::approvalRequested);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("apaga o deploy"));
        QTRY_COMPARE(approvals.size(), 1);
        session->approve(approvals.first().first().value<AgentSession::Approval>().ticket, false);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(host.data.commands.size(), 1);
        const AiToolResult &r = session->conversation().messages.at(2).toolResults.first();
        QVERIFY(r.denied);
        QVERIFY(r.content.contains(QStringLiteral("denied")));
        QVERIFY(QJsonDocument(server->requests().at(1).json()).toJson().contains("denied"));
    }

    void allowForTheConversationSkipsWriteCardsButNeverDangerCards()
    {
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("One")}})));
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t2"), QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("Two")}})));
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t3"), QStringLiteral("delete_command"), {{QStringLiteral("id"), QStringLiteral("c1")}})));
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("fim"))));
        QSignalSpy approvals(session.get(), &AgentSession::approvalRequested);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("faz"));
        QTRY_COMPARE(approvals.size(), 1);
        session->approve(approvals.at(0).first().value<AgentSession::Approval>().ticket, true, /*allowForConversation=*/true);
        // "Two" passa direto; o delete (perigo) volta a pedir.
        QTRY_COMPARE(approvals.size(), 2);
        QCOMPARE(approvals.at(1).first().value<AgentSession::Approval>().call.name, QStringLiteral("delete_command"));
        QCOMPARE(host.data.folders.size(), 3);
        QVERIFY(session->autoAllowing());
        session->approve(approvals.at(1).first().value<AgentSession::Approval>().ticket, true);
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(host.data.commands.isEmpty());
    }

    void askUserWaitsForTheAnswer()
    {
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("ask_user"),
            {{QStringLiteral("question"), QStringLiteral("Qual pasta?")}, {QStringLiteral("options"), QJsonArray{QStringLiteral("Alpha"), QStringLiteral("Beta")}}})));
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("certo"))));
        QSignalSpy questions(session.get(), &AgentSession::questionRequested);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("cria um comando"));
        QTRY_COMPARE(questions.size(), 1);
        QCOMPARE(session->state(), AgentSession::State::AwaitingAnswer);
        QCOMPARE(questions.first().at(1).toString(), QStringLiteral("Qual pasta?"));
        QCOMPARE(questions.first().at(2).toStringList(), (QStringList{"Alpha", "Beta"}));
        session->answer(questions.first().first().toString(), QStringLiteral("Beta"));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(session->conversation().messages.at(2).toolResults.first().content, QStringLiteral("Beta"));
    }

    void invalidArgumentsBecomeAnErrorResultAndTheLoopGoesOn()
    {
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("get_command"), {{QStringLiteral("id"), QStringLiteral("nope")}})));
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("não achei"))));
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("x"));
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(session->conversation().messages.at(2).toolResults.first().isError);
    }

    void unknownToolsAreAnswered()
    {
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("rm_rf"), {})));
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("ok"))));
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("x"));
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(session->conversation().messages.at(2).toolResults.first().content.contains(QStringLiteral("Unknown tool")));
    }

    void cancellingWhileWaitingForApprovalKeepsThePairingValid()
    {
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("Zed")}})));
        QSignalSpy approvals(session.get(), &AgentSession::approvalRequested);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("x"));
        QTRY_COMPARE(approvals.size(), 1);
        session->cancel();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(session->state(), AgentSession::State::Idle);
        QCOMPARE(host.data.folders.size(), 1);
        const auto &msgs = session->conversation().messages;
        QCOMPARE(msgs.last().role, AiMessage::Role::Tool);
        QVERIFY(msgs.last().toolResults.first().content.contains(QStringLiteral("Cancelled")));
        // A conversa continua utilizável: uma nova mensagem gera uma requisição válida.
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("de volta"))));
        session->send(QStringLiteral("e agora"));
        QTRY_COMPARE(finished.size(), 2);
        const QJsonArray sent = server->requests().last().json().value(QStringLiteral("messages")).toArray();
        QVERIFY(QJsonDocument(sent).toJson().contains("tool_result"));
    }

    void cancellingWhileStreamingKeepsThePartialText()
    {
        FakeAiServer::Reply r = FakeAiServer::sse(anthropicText(QStringLiteral("parcial")).mid(0, 2));
        r.hang = true;
        server->enqueue(r);
        QSignalSpy deltas(session.get(), &AgentSession::assistantTextDelta);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("x"));
        QTRY_VERIFY(!deltas.isEmpty());
        session->cancel();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(lastAssistantText(), QStringLiteral("parcial"));
        QCOMPARE(session->state(), AgentSession::State::Idle);
    }

    void stepLimitStopsARunawayLoop()
    {
        session->setLimits(3);
        for (int i = 0; i < 6; ++i) {
            server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t%1").arg(i), QStringLiteral("list_folders"), {})));
        }
        QSignalSpy limit(session.get(), &AgentSession::stepLimitReached);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("x"));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(limit.size(), 1);
        QCOMPARE(server->requests().size(), 3);
        QCOMPARE(session->conversation().messages.last().role, AiMessage::Role::Tool); // pareado, nada pendente
    }

    void networkErrorsAreReportedAndRetryContinues()
    {
        for (int i = 0; i < AiClient::kMaxAttempts; ++i) server->enqueue(FakeAiServer::json(503, R"({"error":{"message":"down"}})"));
        QSignalSpy errors(session.get(), &AgentSession::errorOccurred);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("oi"));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.first().first().value<AiError>().kind, AiError::Kind::Server);
        QCOMPARE(session->conversation().messages.size(), 1); // só a pergunta

        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("agora foi"))));
        session->retry();
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(lastAssistantText(), QStringLiteral("agora foi"));
        QCOMPARE(session->conversation().messages.size(), 2); // a pergunta não foi duplicada
    }

    void aModelWithoutToolSupportGetsNoToolsAndItsCallsAreIgnored()
    {
        AiProfile p = profileFor(*server);
        p.supportsTools = false;
        session->setProfile(p);
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("só conversa"))));
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("oi"));
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!server->requests().first().json().contains(QStringLiteral("tools")));
        const QString system = server->requests().first().json().value(QStringLiteral("system")).toArray().first().toObject().value(QStringLiteral("text")).toString();
        QVERIFY(system.contains(QStringLiteral("no tool access")));
    }

    void hidingSecretsIsToldToTheModel()
    {
        AiProfile p = profileFor(*server);
        p.hideSecretVariables = true;
        session->setProfile(p);
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("ok"))));
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("oi"));
        QTRY_COMPARE(finished.size(), 1);
        const QString system = server->requests().first().json().value(QStringLiteral("system")).toArray().first().toObject().value(QStringLiteral("text")).toString();
        QVERIFY(system.contains(QStringLiteral("***hidden***")));
    }

    void aSlowRunCommandKeepsTheTurnOpenUntilItFinishes()
    {
        host.deferRun = true;
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c1")}})));
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("rodou"))));
        QSignalSpy approvals(session.get(), &AgentSession::approvalRequested);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("roda o deploy"));
        QTRY_COMPARE(approvals.size(), 1);
        session->approve(approvals.first().first().value<AgentSession::Approval>().ticket, true);
        QCOMPARE(session->state(), AgentSession::State::RunningTool);
        QCOMPARE(host.runRequests, QStringList{QStringLiteral("c1")});
        QCOMPARE(finished.size(), 0);
        QCOMPARE(server->requests().size(), 1);
        host.pendingDone(host.nextOutcome);
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(session->conversation().messages.at(2).toolResults.first().content.contains(QStringLiteral("hello")));
    }

    void cancellingAWaitingRunCommandIgnoresItsLateResult()
    {
        host.deferRun = true;
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c1")}})));
        QSignalSpy approvals(session.get(), &AgentSession::approvalRequested);
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("roda"));
        QTRY_COMPARE(approvals.size(), 1);
        session->approve(approvals.first().first().value<AgentSession::Approval>().ticket, true);
        session->cancel();
        QCOMPARE(finished.size(), 1);
        const int messagesBefore = session->conversation().messages.size();
        host.pendingDone(host.nextOutcome); // chega tarde
        QTest::qWait(30);
        QCOMPARE(session->conversation().messages.size(), messagesBefore);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(server->requests().size(), 1);
    }

    void switchingProviderMidConversationKeepsTheHistory()
    {
        server->enqueue(FakeAiServer::sse(anthropicToolUse(QStringLiteral("t1"), QStringLiteral("list_folders"), {})));
        server->enqueue(FakeAiServer::sse(anthropicText(QStringLiteral("uma pasta"))));
        QSignalSpy finished(session.get(), &AgentSession::turnFinished);
        session->send(QStringLiteral("quais pastas?"));
        QTRY_COMPARE(finished.size(), 1);

        FakeAiServer other;
        QVERIFY(other.start());
        other.enqueue(FakeAiServer::sse(openAiText(QStringLiteral("segunda resposta"))));
        session->setProfile(profileFor(other, AiWireFormat::OpenAiCompatible));
        session->send(QStringLiteral("e comandos?"));
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(lastAssistantText(), QStringLiteral("segunda resposta"));
        // O histórico anterior (com a chamada de ferramenta) chegou no formato do novo provedor.
        const QJsonArray msgs = other.requests().first().json().value(QStringLiteral("messages")).toArray();
        bool sawToolRole = false;
        bool sawToolCalls = false;
        for (const QJsonValue &v : msgs) {
            if (v.toObject().value(QStringLiteral("role")).toString() == QLatin1String("tool")) sawToolRole = true;
            if (v.toObject().contains(QStringLiteral("tool_calls"))) sawToolCalls = true;
        }
        QVERIFY(sawToolRole && sawToolCalls);
        QCOMPARE(session->conversation().profileId, QStringLiteral("oai"));
        QCOMPARE(session->conversation().messages.at(1).profileId, QStringLiteral("anth"));
    }

    void busySessionsIgnoreNewMessages()
    {
        FakeAiServer::Reply r = FakeAiServer::sse(anthropicText(QStringLiteral("x")).mid(0, 1));
        r.hang = true;
        server->enqueue(r);
        session->send(QStringLiteral("um"));
        session->send(QStringLiteral("dois"));
        QCOMPARE(session->conversation().messages.size(), 1);
        session->cancel();
        QTRY_VERIFY(!session->isBusy());
        session->send(QStringLiteral("   "));
        QCOMPARE(session->conversation().messages.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestAgentSession)
#include "test_agent_session.moc"
