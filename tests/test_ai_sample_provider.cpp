// O provedor de exemplo (sample/ai/fake-provider.py) fala de verdade com o cliente de IA do Kai e com o laço
// do agente — é o que o usuário vai usar para experimentar o chat sem chave.

#include <QTest>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>

#include "ai-fake-host.h"
#include "engine/ai/agent-session.h"

using namespace kai::core;
using namespace kai::engine;

class TestAiSampleProvider : public QObject {
    Q_OBJECT

    QProcess process;
    quint16 port = 0;

    static quint16 freePort()
    {
        QTcpServer s;
        s.listen(QHostAddress::LocalHost, 0);
        return s.serverPort();
    }

private slots:
    void initTestCase()
    {
        const QString script = QDir(QStringLiteral(QT_TESTCASE_SOURCEDIR)).filePath(QStringLiteral("../sample/ai/fake-provider.py"));
        if (!QFile::exists(script)) QSKIP("sample/ai/fake-provider.py not found");
        QProcess probe;
        probe.start(QStringLiteral("python3"), {QStringLiteral("--version")});
        if (!probe.waitForFinished(5000) || probe.exitCode() != 0) QSKIP("python3 not available");
        port = freePort();
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QStringLiteral("python3"), {script, QStringLiteral("--port"), QString::number(port)});
        QVERIFY(process.waitForStarted(5000));
        QVERIFY(process.waitForReadyRead(10000)); // imprime a linha de boas-vindas
    }

    void cleanupTestCase()
    {
        process.kill();
        process.waitForFinished(3000);
    }

    AiProfile profile() const
    {
        AiProfile p;
        p.id = QStringLiteral("demo");
        p.name = QStringLiteral("demo");
        p.wireFormat = AiWireFormat::OpenAiCompatible;
        p.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
        p.model = QStringLiteral("kai-demo");
        return p;
    }

private slots:
    void listsItsModel()
    {
        AiClient client;
        QSignalSpy models(&client, &AiClient::modelsListed);
        client.listModels(profile());
        QTRY_COMPARE(models.size(), 1);
        QCOMPARE(models.first().first().toStringList(), QStringList{QStringLiteral("kai-demo")});
    }

    void answersPlainTextInBothStreamAndNonStreamModes()
    {
        for (bool stream : {true, false}) {
            AiClient client;
            QSignalSpy done(&client, &AiClient::completed);
            AiRequest r;
            r.profile = profile();
            r.stream = stream;
            AiMessage m;
            m.role = AiMessage::Role::User;
            m.text = QStringLiteral("hello there");
            r.messages << m;
            client.send(r);
            QTRY_COMPARE(done.size(), 1);
            QVERIFY(done.first().first().value<AiTurn>().text.contains(QStringLiteral("demo provider")));
        }
    }

    void theWholeScriptedFlowRunsThroughTheAgent()
    {
        FakeAgentHost host;
        host.addFolder(QStringLiteral("f1"), QStringLiteral("Alpha"));
        AgentToolbox toolbox(&host);
        AgentSession session(&toolbox);
        session.setProfile(profile());
        QSignalSpy approvals(&session, &AgentSession::approvalRequested);
        QSignalSpy finished(&session, &AgentSession::turnFinished);

        session.send(QStringLiteral("list my commands"));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(approvals.size(), 0);

        session.send(QStringLiteral("create a folder"));
        QTRY_COMPARE(approvals.size(), 1);
        session.approve(approvals.first().first().value<AgentSession::Approval>().ticket, true);
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(host.data.folders.size(), 2);

        session.send(QStringLiteral("delete the folder"));
        QTRY_COMPARE(approvals.size(), 2);
        const auto danger = approvals.at(1).first().value<AgentSession::Approval>();
        QCOMPARE(danger.plan.risk, ToolRisk::Danger);
        session.approve(danger.ticket, true);
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(host.data.folders.size(), 1);

        QSignalSpy questions(&session, &AgentSession::questionRequested);
        session.send(QStringLiteral("ask me a question"));
        QTRY_COMPARE(questions.size(), 1);
        session.answer(questions.first().first().toString(), QStringLiteral("Green"));
        QTRY_COMPARE(finished.size(), 4);
        QVERIFY(session.conversation().messages.last().text.contains(QStringLiteral("Green")));
    }
};

QTEST_GUILESS_MAIN(TestAiSampleProvider)
#include "test_ai_sample_provider.moc"
