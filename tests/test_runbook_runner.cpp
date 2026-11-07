#include <QtTest>

#include <QSignalSpy>
#include <QTimer>

#include <memory>

#include "engine/runbook-runner.h"

using namespace kai::core;
using namespace kai::engine;

namespace {

struct Script {
    // nome do comando -> resultados em ordem (o último se repete)
    QMap<QString, QVector<StepExecResult>> results;
    QStringList calls;
    QStringList logs;
    QStringList probes; // variáveis lidas do ambiente a cada chamada
    QMap<QString, QMap<QString, QString>> envAt; // comando -> variáveis na ÚLTIMA chamada
    QSet<QString> hold;                           // comandos que só terminam ao serem soltos/parados
    QPointer<StepExecutor> held;
    QStringList logOutput;                        // texto emitido por todo comando

    int count(const QString &name) const { return calls.count(name); }
};

StepExecResult ok(const QString &out = QString(), int exit = 0)
{
    StepExecResult r;
    r.success = true;
    r.exitCode = exit;
    r.output = out;
    return r;
}

StepExecResult bad(const QString &msg = QStringLiteral("boom"), int exit = 1)
{
    StepExecResult r;
    r.success = false;
    r.exitCode = exit;
    r.errorMessage = msg;
    return r;
}

class FakeExecutor : public StepExecutor {
public:
    FakeExecutor(Script *script, QObject *parent) : StepExecutor(parent), m_script(script) {}

    void run(const Command &command, EnvironmentManager &env) override
    {
        m_script->calls << command.name;
        QMap<QString, QString> snap;
        for (const QString &p : m_script->probes) {
            snap.insert(p, env.interpolate(QStringLiteral("{{%1}}").arg(p)));
        }
        m_script->envAt.insert(command.name, snap);
        for (const QString &line : m_script->logOutput) {
            emit logLine(line);
        }
        if (m_script->hold.contains(command.name)) {
            m_script->held = this;
            return;
        }
        auto &queue = m_script->results[command.name];
        StepExecResult r = queue.isEmpty() ? ok() : (queue.size() > 1 ? queue.takeFirst() : queue.first());
        QTimer::singleShot(0, this, [this, r]() { emit finished(r); });
    }

    void stop() override
    {
        StepExecResult r;
        r.stopped = true;
        QTimer::singleShot(0, this, [this, r]() { emit finished(r); });
    }

private:
    Script *m_script;
};

Command cmd(const QString &name)
{
    Command c;
    c.id = QStringLiteral("id_") + name;
    c.name = name;
    c.command = QStringLiteral("echo");
    return c;
}

PipelineStep stepOf(const QString &id, const QString &run)
{
    PipelineStep s;
    s.id = id;
    s.run = run;
    return s;
}

} // namespace

class TestRunbookRunner : public QObject {
    Q_OBJECT

private:
    Script m_script;
    QMap<QString, Command> m_all;
    std::unique_ptr<RunbookRunner> m_runner;
    Command m_pipeline;

    void setup(const QVector<PipelineStep> &steps)
    {
        m_script = Script();
        m_all.clear();
        m_pipeline = cmd(QStringLiteral("Pipeline"));
        m_pipeline.type = CommandType::Pipeline;
        m_pipeline.command.clear();
        m_pipeline.pipelineSteps = pipelineStepsToJson(steps);
        m_all.insert(m_pipeline.id, m_pipeline);
        for (const PipelineStep &s : steps) {
            for (const QString &ref : {s.run, s.check, s.waitUntil.check}) {
                if (!ref.isEmpty() && !ref.startsWith(QLatin1String("missing"))) {
                    m_all.insert(QStringLiteral("id_") + ref, cmd(ref));
                }
            }
        }
        m_runner = std::make_unique<RunbookRunner>([this](QObject *parent) -> StepExecutor * {
            return new FakeExecutor(&m_script, parent);
        });
        m_runner->setMillisPerSecond(1);
    }

    bool start(const RunbookRunner::Options &options = {})
    {
        return m_runner->start(m_pipeline, m_all, {}, EnvironmentManager(), {}, options);
    }

    void runToEnd()
    {
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 5000);
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<kai::engine::RunbookResult>();
        qRegisterMetaType<kai::engine::StepExecResult>();
        qRegisterMetaType<kai::core::KipStepState>();
        qRegisterMetaType<kai::core::KipSessionState>();
    }

    void runsStepsInOrderAndSucceeds()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("A")), stepOf(QStringLiteral("b"), QStringLiteral("B")),
               stepOf(QStringLiteral("c"), QStringLiteral("C"))});
        runToEnd();
        QCOMPARE(m_script.calls, (QStringList{"A", "B", "C"}));
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
        QCOMPARE(m_runner->result().exitCode, 0);
        for (const char *id : {"a", "b", "c"}) {
            QCOMPARE(m_runner->stepState(QLatin1String(id)), KipStepState::Success);
        }
        QVERIFY(m_runner->screen().done().has_value());
        QCOMPARE(m_runner->screen().done()->level, KipLevel::Success);
    }

    void checkThatPassesSkipsTheRun()
    {
        PipelineStep s = stepOf(QStringLiteral("a"), QStringLiteral("Do"));
        s.check = QStringLiteral("IsDone");
        setup({s});
        m_script.results[QStringLiteral("IsDone")] = {ok()};
        runToEnd();
        QCOMPARE(m_script.calls, (QStringList{"IsDone"}));
        QCOMPARE(m_runner->stepState(QStringLiteral("a")), KipStepState::Success);
        QVERIFY(!m_runner->stepDetail(QStringLiteral("a")).isEmpty());
    }

    void runIsVerifiedByTheCheckAfterwards()
    {
        PipelineStep s = stepOf(QStringLiteral("a"), QStringLiteral("Do"));
        s.check = QStringLiteral("IsDone");
        setup({s});
        m_script.results[QStringLiteral("IsDone")] = {bad(), ok()}; // fails before, passes after
        runToEnd();
        QCOMPARE(m_script.calls, (QStringList{"IsDone", "Do", "IsDone"}));
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
    }

    void aRunThatDoesNotTakeEffectFails()
    {
        PipelineStep s = stepOf(QStringLiteral("a"), QStringLiteral("Do"));
        s.check = QStringLiteral("IsDone");
        setup({s});
        m_script.results[QStringLiteral("IsDone")] = {bad()};
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QCOMPARE(m_runner->result().failedStepId, QStringLiteral("a"));
    }

    void abortLeavesLaterStepsPending()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("A")), stepOf(QStringLiteral("b"), QStringLiteral("B")),
               stepOf(QStringLiteral("c"), QStringLiteral("C"))});
        m_script.results[QStringLiteral("B")] = {bad(QStringLiteral("nope"))};
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QCOMPARE(m_runner->result().failedStepId, QStringLiteral("b"));
        QCOMPARE(m_runner->result().exitCode, 1);
        QCOMPARE(m_runner->stepState(QStringLiteral("b")), KipStepState::Error);
        QCOMPARE(m_runner->stepDetail(QStringLiteral("b")), QStringLiteral("nope"));
        QCOMPARE(m_runner->stepState(QStringLiteral("c")), KipStepState::Pending);
        QCOMPARE(m_script.count(QStringLiteral("C")), 0);
    }

    void continueGoesOnAndReportsErrors()
    {
        PipelineStep b = stepOf(QStringLiteral("b"), QStringLiteral("B"));
        b.onFail = QStringLiteral("continue");
        setup({stepOf(QStringLiteral("a"), QStringLiteral("A")), b, stepOf(QStringLiteral("c"), QStringLiteral("C"))});
        m_script.results[QStringLiteral("B")] = {bad()};
        runToEnd();
        QCOMPARE(m_script.count(QStringLiteral("C")), 1);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::CompletedWithErrors);
        QCOMPARE(m_runner->stepState(QStringLiteral("b")), KipStepState::Error);
    }

    void retryRerunsUntilItWorks()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Flaky"));
        a.retry.times = 2;
        setup({a});
        m_script.results[QStringLiteral("Flaky")] = {bad(), bad(), ok()};
        runToEnd();
        QCOMPARE(m_script.count(QStringLiteral("Flaky")), 3);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
    }

    void retryGivesUpAfterTheLimit()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Flaky"));
        a.retry.times = 1;
        setup({a});
        m_script.results[QStringLiteral("Flaky")] = {bad()};
        runToEnd();
        QCOMPARE(m_script.count(QStringLiteral("Flaky")), 2);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
    }

    void gotoRollbackSkipsTheStepsInBetweenAndRunsTheGuardedStep()
    {
        PipelineStep health = stepOf(QStringLiteral("health"), QStringLiteral("Health"));
        health.onFail = QStringLiteral("goto:rollback");
        PipelineStep announce = stepOf(QStringLiteral("announce"), QStringLiteral("Announce"));
        PipelineStep rollback = stepOf(QStringLiteral("rollback"), QStringLiteral("Rollback"));
        ExecutionCondition c;
        c.left = QStringLiteral("{{steps.health.ok}}");
        c.op = QStringLiteral("eq");
        c.right = QStringLiteral("false");
        rollback.when << c;
        setup({health, announce, rollback});
        m_script.results[QStringLiteral("Health")] = {bad()};
        runToEnd();
        QCOMPARE(m_script.calls, (QStringList{"Health", "Rollback"}));
        QCOMPARE(m_runner->stepState(QStringLiteral("announce")), KipStepState::Skipped);
        QCOMPARE(m_runner->stepState(QStringLiteral("rollback")), KipStepState::Success);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QCOMPARE(m_runner->result().failedStepId, QStringLiteral("health"));
    }

    void theGuardedStepIsSkippedOnTheHappyPath()
    {
        PipelineStep health = stepOf(QStringLiteral("health"), QStringLiteral("Health"));
        health.onFail = QStringLiteral("goto:rollback");
        PipelineStep rollback = stepOf(QStringLiteral("rollback"), QStringLiteral("Rollback"));
        ExecutionCondition c;
        c.left = QStringLiteral("{{steps.health.ok}}");
        c.op = QStringLiteral("eq");
        c.right = QStringLiteral("false");
        rollback.when << c;
        setup({health, rollback});
        runToEnd();
        QCOMPARE(m_script.calls, (QStringList{"Health"}));
        QCOMPARE(m_runner->stepState(QStringLiteral("rollback")), KipStepState::Skipped);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
    }

    void capturesFeedLaterSteps()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Build"));
        PipelineCapture id;
        id.name = QStringLiteral("RELEASE");
        id.from = QStringLiteral("stdout");
        id.regex = QStringLiteral("release=(\\S+)");
        PipelineCapture ver;
        ver.name = QStringLiteral("VERSION");
        ver.from = QStringLiteral("json");
        ver.path = QStringLiteral("data.version");
        PipelineCapture last;
        last.name = QStringLiteral("LAST");
        last.from = QStringLiteral("stdout");
        a.capture << id << ver << last;
        PipelineStep b = stepOf(QStringLiteral("b"), QStringLiteral("Deploy"));
        b.params.insert(QStringLiteral("REL"), QStringLiteral("{{RELEASE}}-{{steps.a.VERSION}}"));
        setup({a, b});
        m_script.probes = {QStringLiteral("REL"), QStringLiteral("LAST")};
        m_script.results[QStringLiteral("Build")] = {
            ok(QStringLiteral("noise\nrelease=r42 ok\n{\"data\":{\"version\":\"1.2\"}}\n"))};
        runToEnd();
        QCOMPARE(m_runner->variable(QStringLiteral("RELEASE")), QStringLiteral("r42"));
        QCOMPARE(m_runner->variable(QStringLiteral("VERSION")), QStringLiteral("1.2"));
        QCOMPARE(m_script.envAt[QStringLiteral("Deploy")].value(QStringLiteral("REL")), QStringLiteral("r42-1.2"));
        QCOMPARE(m_script.envAt[QStringLiteral("Deploy")].value(QStringLiteral("LAST")),
                 QStringLiteral("{\"data\":{\"version\":\"1.2\"}}"));
    }

    void whenFalseSkipsTheStep()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("A"));
        ExecutionCondition c;
        c.left = QStringLiteral("{{NOPE}}");
        c.op = QStringLiteral("exists");
        a.when << c;
        setup({a, stepOf(QStringLiteral("b"), QStringLiteral("B"))});
        runToEnd();
        QCOMPARE(m_script.calls, (QStringList{"B"}));
        QCOMPARE(m_runner->stepState(QStringLiteral("a")), KipStepState::Skipped);
    }

    void askCollectsAnswersAndMasksSecretsInLogs()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Login"));
        a.ask = QJsonArray{QJsonObject{{"name", "user"}, {"type", "text"}},
                           QJsonObject{{"name", "token"}, {"type", "secret"}}};
        a.params.insert(QStringLiteral("TOKEN"), QStringLiteral("{{ask.token}}"));
        setup({a});
        m_script.probes = {QStringLiteral("TOKEN"), QStringLiteral("ask.user")};
        m_script.logOutput = {QStringLiteral("using s3cret now\n")};
        QSignalSpy logs(m_runner.get(), &RunbookRunner::logLine);
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        QCOMPARE(m_runner->state(), KipSessionState::AwaitingInput);
        m_runner->setFieldValue(QStringLiteral("user"), QStringLiteral("ana"));
        m_runner->setFieldValue(QStringLiteral("token"), QStringLiteral("s3cret"));
        QVERIFY(m_runner->submit());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
        QCOMPARE(m_script.envAt[QStringLiteral("Login")].value(QStringLiteral("TOKEN")), QStringLiteral("s3cret"));
        QCOMPARE(m_script.envAt[QStringLiteral("Login")].value(QStringLiteral("ask.user")), QStringLiteral("ana"));
        QVERIFY(!logs.isEmpty());
        for (const QList<QVariant> &args : logs) {
            QVERIFY(!args.first().toString().contains(QStringLiteral("s3cret")));
        }
    }

    void tinySecretsDoNotShredTheLog()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Login"));
        a.ask = QJsonArray{QJsonObject{{"name", "token"}, {"type", "secret"}}};
        setup({a});
        m_script.logOutput = {QStringLiteral("Pipeline aborted: Process exited\n")};
        QSignalSpy logs(m_runner.get(), &RunbookRunner::logLine);
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        m_runner->setFieldValue(QStringLiteral("token"), QStringLiteral("a"));
        QVERIFY(m_runner->submit());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        bool found = false;
        for (const QList<QVariant> &args : logs) {
            const QString text = args.first().toString();
            QVERIFY(!text.contains(QString::fromUtf16(kai::core::kKipSecretMask)));
            found = found || text.contains(QStringLiteral("Pipeline aborted: Process exited"));
        }
        QVERIFY(found);
    }

    void reaskAfterAFailureAsksAgainAndKeepsNonSecretAnswers()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Login"));
        a.ask = QJsonArray{QJsonObject{{"name", "user"}, {"type", "text"}},
                           QJsonObject{{"name", "token"}, {"type", "secret"}}};
        a.retry.times = 1;
        a.retry.reask = true;
        setup({a});
        m_script.results[QStringLiteral("Login")] = {bad(), ok()};
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        m_runner->setFieldValue(QStringLiteral("user"), QStringLiteral("ana"));
        m_runner->setFieldValue(QStringLiteral("token"), QStringLiteral("wrong"));
        QVERIFY(m_runner->submit());
        // A segunda pergunta traz o usuário de volta, mas não o segredo.
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        QCOMPARE(m_runner->screen().openScreen()->values.value(QStringLiteral("user")).toString(),
                 QStringLiteral("ana"));
        QVERIFY(m_runner->screen().openScreen()->values.value(QStringLiteral("token")).toString().isEmpty());
        m_runner->setFieldValue(QStringLiteral("token"), QStringLiteral("right"));
        QVERIFY(m_runner->submit());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
        QCOMPARE(m_script.count(QStringLiteral("Login")), 2);
    }

    void decliningTheConfirmSkipsTheStep()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Danger"));
        a.confirm = QStringLiteral("Really?");
        setup({a, stepOf(QStringLiteral("b"), QStringLiteral("B"))});
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        QCOMPARE(m_runner->screen().openScreen()->kind, KipOpenScreen::Kind::Confirm);
        QVERIFY(m_runner->confirm(false));
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_script.calls, (QStringList{"B"}));
        QCOMPARE(m_runner->stepState(QStringLiteral("a")), KipStepState::Skipped);
    }

    void acceptingTheConfirmRunsTheStep()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Danger"));
        a.confirm = QStringLiteral("Really?");
        setup({a});
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        QVERIFY(m_runner->confirm(true));
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_script.calls, (QStringList{"Danger"}));
    }

    void onFailAskOffersRetrySkipAbort()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Build"));
        a.onFail = QStringLiteral("ask");
        setup({a, stepOf(QStringLiteral("b"), QStringLiteral("B"))});
        m_script.results[QStringLiteral("Build")] = {bad(), ok()};
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        m_runner->setFieldValue(QStringLiteral("choice"), QStringLiteral("retry"));
        QVERIFY(m_runner->submit());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_script.count(QStringLiteral("Build")), 2);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
    }

    void onFailAskSkipContinuesWithErrors()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Build"));
        a.onFail = QStringLiteral("ask");
        setup({a, stepOf(QStringLiteral("b"), QStringLiteral("B"))});
        m_script.results[QStringLiteral("Build")] = {bad()};
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        m_runner->setFieldValue(QStringLiteral("choice"), QStringLiteral("skip"));
        QVERIFY(m_runner->submit());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_script.count(QStringLiteral("B")), 1);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::CompletedWithErrors);
    }

    void onFailAskAbortFails()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Build"));
        a.onFail = QStringLiteral("ask");
        setup({a, stepOf(QStringLiteral("b"), QStringLiteral("B"))});
        m_script.results[QStringLiteral("Build")] = {bad()};
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        m_runner->setFieldValue(QStringLiteral("choice"), QStringLiteral("abort"));
        QVERIFY(m_runner->submit());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QCOMPARE(m_script.count(QStringLiteral("B")), 0);
    }

    void waitUntilPollsUntilReady()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Up"));
        a.waitUntil = {QStringLiteral("Health"), 2000, 1};
        setup({a});
        m_script.results[QStringLiteral("Health")] = {bad(), bad(), ok()};
        runToEnd();
        QCOMPARE(m_script.count(QStringLiteral("Health")), 3);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
    }

    void waitUntilTimesOut()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Up"));
        a.waitUntil = {QStringLiteral("Health"), 1, 1}; // 1 "segundo" = 1 ms nos testes
        setup({a});
        m_script.results[QStringLiteral("Health")] = {bad()};
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QVERIFY(m_script.count(QStringLiteral("Health")) >= 1);
    }

    void stopDuringARunCancelsAndStopsTheCommand()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("Long")), stepOf(QStringLiteral("b"), QStringLiteral("B"))});
        m_script.hold = {QStringLiteral("Long")};
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_script.held != nullptr, 3000);
        m_runner->stop();
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Cancelled);
        QCOMPARE(m_runner->state(), KipSessionState::Cancelled);
        QCOMPARE(m_script.count(QStringLiteral("B")), 0);
        QCOMPARE(m_runner->stepState(QStringLiteral("a")), KipStepState::Error);
    }

    void stopWhileAskingCancels()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("Login"));
        a.ask = QJsonArray{QJsonObject{{"name", "user"}, {"type", "text"}}};
        setup({a});
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        m_runner->cancel();
        QVERIFY(m_runner->isFinished());
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Cancelled);
        QCOMPARE(m_script.count(QStringLiteral("Login")), 0);
    }

    void anUnresolvedReferenceFailsTheStepWithoutCrashing()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("missing-one"))});
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QVERIFY(!m_runner->stepDetail(QStringLiteral("a")).isEmpty());
    }

    void aPipelineStepIsRefused()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("Inner"))});
        Command inner = cmd(QStringLiteral("Inner"));
        inner.type = CommandType::Pipeline;
        m_all.insert(inner.id, inner);
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QCOMPARE(m_script.count(QStringLiteral("Inner")), 0);
    }

    void aKipChildWithAnErrorDoneFailsTheStep()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("Wizard"))});
        StepExecResult r = ok();
        r.isKip = true;
        r.hasDone = true;
        r.doneLevel = KipLevel::Error;
        r.doneText = QStringLiteral("no go\nmore");
        m_script.results[QStringLiteral("Wizard")] = {r};
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QCOMPARE(m_runner->stepDetail(QStringLiteral("a")), QStringLiteral("no go"));
    }

    void aKipChildWithASuccessDoneShowsItsTextAsDetail()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("Wizard"))});
        StepExecResult r = ok();
        r.isKip = true;
        r.hasDone = true;
        r.doneText = QStringLiteral("target chosen");
        m_script.results[QStringLiteral("Wizard")] = {r};
        runToEnd();
        QCOMPARE(m_runner->stepDetail(QStringLiteral("a")), QStringLiteral("target chosen"));
    }

    void runFromRestartsAtTheStepKeepingVariables()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("A"));
        PipelineCapture cap;
        cap.name = QStringLiteral("KEEP");
        a.capture << cap;
        setup({a, stepOf(QStringLiteral("b"), QStringLiteral("B"))});
        m_script.results[QStringLiteral("A")] = {ok(QStringLiteral("kept"))};
        m_script.results[QStringLiteral("B")] = {bad(), ok()};
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QVERIFY(m_runner->runFrom(QStringLiteral("b")));
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
        QCOMPARE(m_script.count(QStringLiteral("A")), 1);
        QCOMPARE(m_script.count(QStringLiteral("B")), 2);
        QCOMPARE(m_runner->variable(QStringLiteral("KEEP")), QStringLiteral("kept"));
    }

    void theViewCanResumeFromAStepOnceTheRunIsOver()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("A")), stepOf(QStringLiteral("b"), QStringLiteral("B"))});
        m_script.results[QStringLiteral("B")] = {bad(), ok()};
        QSignalSpy newSession(m_runner.get(), &RunbookRunner::sessionStarted);
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        KipSession *first = m_runner->session();
        QVERIFY(first->stepActionsEnabled());
        // Durante a execução a ação não é oferecida.
        first->requestStepAction(QStringLiteral("b"), QStringLiteral("run_from"));
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished() && m_runner->result().outcome == RunbookResult::Outcome::Success
                                     && m_runner->session() != first, 3000);
        QCOMPARE(newSession.count(), 1);
        QCOMPARE(m_script.count(QStringLiteral("A")), 1);
        QCOMPARE(m_script.count(QStringLiteral("B")), 2);
    }

    void startFromSkipsEarlierSteps()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("A")), stepOf(QStringLiteral("b"), QStringLiteral("B"))});
        RunbookRunner::Options o;
        o.fromStep = QStringLiteral("b");
        QVERIFY(start(o));
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_script.calls, (QStringList{"B"}));
        QCOMPARE(m_runner->stepState(QStringLiteral("a")), KipStepState::Skipped);
    }

    void onlyRunsASingleStep()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("A")), stepOf(QStringLiteral("b"), QStringLiteral("B")),
               stepOf(QStringLiteral("c"), QStringLiteral("C"))});
        RunbookRunner::Options o;
        o.onlyStep = QStringLiteral("b");
        QVERIFY(start(o));
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QCOMPARE(m_script.calls, (QStringList{"B"}));
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
    }

    void anInvalidDefinitionFailsBeforeRunningAnything()
    {
        PipelineStep a = stepOf(QStringLiteral("a"), QStringLiteral("A"));
        a.onFail = QStringLiteral("goto:a"); // não é para a frente
        setup({a});
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QVERIFY(m_script.calls.isEmpty());
    }

    void emptyPipelineFails()
    {
        setup({});
        runToEnd();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
    }

    void nonPipelineCommandsAreRefused()
    {
        setup({stepOf(QStringLiteral("a"), QStringLiteral("A"))});
        Command plain = cmd(QStringLiteral("Plain"));
        QVERIFY(!m_runner->start(plain, m_all, {}, EnvironmentManager(), {}));
    }
};

QTEST_MAIN(TestRunbookRunner)
#include "test_runbook_runner.moc"
