#include <QtTest>

#include <QSignalSpy>

#include <memory>

#include "engine/pipeline-step-executor.h"
#include "engine/runbook-runner.h"

using namespace kai::core;
using namespace kai::engine;

// A pipeline de verdade: cada passo é um comando shell rodado pelo ExecutionPipeline normal.
class TestPipelineStepExecutor : public QObject {
    Q_OBJECT

private:
    QMap<QString, Command> m_all;
    std::unique_ptr<RunbookRunner> m_runner;

    static Command shell(const QString &name, const QString &script)
    {
        Command c;
        c.id = QStringLiteral("id_") + name;
        c.name = name;
        c.type = CommandType::Command;
        c.command = script;
        return c;
    }

    void build(const QVector<PipelineStep> &steps, const QList<Command> &commands)
    {
        m_all.clear();
        Command pipeline;
        pipeline.id = QStringLiteral("p");
        pipeline.name = QStringLiteral("Pipeline");
        pipeline.type = CommandType::Pipeline;
        pipeline.pipelineSteps = pipelineStepsToJson(steps);
        m_all.insert(pipeline.id, pipeline);
        for (const Command &c : commands) {
            m_all.insert(c.id, c);
        }
        m_runner = std::make_unique<RunbookRunner>([this](QObject *parent) -> StepExecutor * {
            return new PipelineStepExecutor(&m_all, nullptr, parent);
        });
        m_runner->setMillisPerSecond(10);
    }

    bool start()
    {
        return m_runner->start(m_all.value(QStringLiteral("p")), m_all, {}, EnvironmentManager(), {});
    }

    static PipelineStep step(const QString &id, const QString &run)
    {
        PipelineStep s;
        s.id = id;
        s.run = run;
        return s;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<kai::engine::RunbookResult>();
        qRegisterMetaType<kai::engine::StepExecResult>();
        qRegisterMetaType<kai::engine::PipelineResult>();
    }

    void realCommandsRunCaptureAndPassValuesOn()
    {
        PipelineStep first = step(QStringLiteral("make"), QStringLiteral("Make"));
        PipelineCapture cap;
        cap.name = QStringLiteral("REL");
        cap.from = QStringLiteral("stdout");
        cap.regex = QStringLiteral("release=(\\w+)");
        first.capture << cap;
        PipelineStep second = step(QStringLiteral("use"), QStringLiteral("Use"));
        second.params.insert(QStringLiteral("WHO"), QStringLiteral("{{REL}}"));
        PipelineCapture seen;
        seen.name = QStringLiteral("SEEN");
        seen.from = QStringLiteral("stdout");
        second.capture << seen;
        build({first, second}, {shell(QStringLiteral("Make"), QStringLiteral("echo release=r7")),
                                shell(QStringLiteral("Use"), QStringLiteral("echo got-{{WHO}}"))});
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 15000);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
        QCOMPARE(m_runner->variable(QStringLiteral("REL")), QStringLiteral("r7"));
        QCOMPARE(m_runner->variable(QStringLiteral("SEEN")), QStringLiteral("got-r7"));
    }

    void aFailingCommandFailsTheStepAndSkipsTheRest()
    {
        build({step(QStringLiteral("a"), QStringLiteral("Bad")), step(QStringLiteral("b"), QStringLiteral("Never"))},
              {shell(QStringLiteral("Bad"), QStringLiteral("echo oops; exit 3")),
               shell(QStringLiteral("Never"), QStringLiteral("echo never"))});
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 15000);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QCOMPARE(m_runner->result().failedStepId, QStringLiteral("a"));
        QCOMPARE(m_runner->stepState(QStringLiteral("b")), KipStepState::Pending);
        QCOMPARE(m_runner->variable(QStringLiteral("steps.a.ok")), QStringLiteral("false"));
    }

    void checkThatPassesSkipsTheRealRun()
    {
        PipelineStep s = step(QStringLiteral("a"), QStringLiteral("Do"));
        s.check = QStringLiteral("Done");
        build({s}, {shell(QStringLiteral("Do"), QStringLiteral("exit 9")), shell(QStringLiteral("Done"), QStringLiteral("exit 0"))});
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 15000);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
    }

    void stopKillsTheRunningCommand()
    {
        build({step(QStringLiteral("a"), QStringLiteral("Slow"))},
              {shell(QStringLiteral("Slow"), QStringLiteral("echo started; sleep 30"))});
        QSignalSpy logs(m_runner.get(), &RunbookRunner::logLine);
        QVERIFY(start());
        QTRY_VERIFY_WITH_TIMEOUT(!logs.isEmpty(), 15000);
        m_runner->stop();
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 15000);
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Cancelled);
    }

    void aPipelineCommandIsNeverRunByTheExecutionPipeline()
    {
        Command pipeline;
        pipeline.id = QStringLiteral("p2");
        pipeline.name = QStringLiteral("Nested");
        pipeline.type = CommandType::Pipeline;
        QMap<QString, Command> all{{pipeline.id, pipeline}};
        ExecutionPipeline ep;
        EnvironmentManager env;
        QSignalSpy finished(&ep, &ExecutionPipeline::pipelineFinished);
        ep.run(pipeline, all, env);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(!finished.first().first().value<PipelineResult>().success);
    }
};

QTEST_MAIN(TestPipelineStepExecutor)
#include "test_pipeline_step_executor.moc"
