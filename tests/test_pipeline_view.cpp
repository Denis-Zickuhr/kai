#include <QtTest>

#include <QApplication>
#include <QTimer>

#include <memory>

#include "engine/runbook-runner.h"
#include "ui/app-stylesheet.h"
#include "ui/features/kip/kip-view.h"
#include "ui/features/kip/kip-result-card.h"

using namespace kai::core;
using namespace kai::engine;
using namespace kai::ui;

namespace {

// Termina cada comando na hora (sucesso, ou falha para os nomes em `failing`), emitindo uma linha de log.
class InstantExecutor : public StepExecutor {
public:
    InstantExecutor(const QSet<QString> *failing, QObject *parent) : StepExecutor(parent), m_failing(failing) {}
    void run(const Command &command, EnvironmentManager &) override
    {
        emit logLine(QStringLiteral("running %1\n").arg(command.name));
        StepExecResult r;
        r.success = !m_failing->contains(command.name);
        r.exitCode = r.success ? 0 : 2;
        r.errorMessage = r.success ? QString() : QStringLiteral("exit 2");
        QTimer::singleShot(0, this, [this, r]() { emit finished(r); });
    }
    void stop() override {}

private:
    const QSet<QString> *m_failing;
};

} // namespace

// A pipeline se mostra na MESMA view da KIP: checklist com log, pergunta no meio, cartão final.
class TestPipelineView : public QObject {
    Q_OBJECT

    QSet<QString> m_failing;
    QMap<QString, Command> m_all;
    std::unique_ptr<RunbookRunner> m_runner;
    Command m_pipeline;

    void setup(const QVector<PipelineStep> &steps)
    {
        m_all.clear();
        m_pipeline = Command();
        m_pipeline.id = QStringLiteral("p");
        m_pipeline.name = QStringLiteral("Ship to staging");
        m_pipeline.type = CommandType::Pipeline;
        m_pipeline.pipelineSteps = pipelineStepsToJson(steps);
        for (const PipelineStep &s : steps) {
            Command c;
            c.id = QStringLiteral("id_") + s.run;
            c.name = s.run;
            c.command = QStringLiteral("true");
            m_all.insert(c.id, c);
        }
        m_runner = std::make_unique<RunbookRunner>([this](QObject *parent) -> StepExecutor * {
            return new InstantExecutor(&m_failing, parent);
        });
    }

    static PipelineStep step(const QString &id, const QString &label, const QString &run)
    {
        PipelineStep s;
        s.id = id;
        s.label = label;
        s.run = run;
        return s;
    }

    static void shot(QWidget &w, const QString &name)
    {
        if (const QByteArray dir = qgetenv("KAI_TEST_SCREENSHOT_DIR"); !dir.isEmpty()) {
            w.grab().save(QString::fromLocal8Bit(dir) + QLatin1Char('/') + name);
        }
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<kai::engine::RunbookResult>();
        qRegisterMetaType<kai::engine::StepExecResult>();
        qApp->setStyleSheet(buildModernStylesheet());
    }

    void theViewShowsTheChecklistThenAQuestionThenTheResult()
    {
        PipelineStep deploy = step(QStringLiteral("deploy"), QStringLiteral("Deploy"), QStringLiteral("Deploy"));
        deploy.ask = QJsonArray{QJsonObject{{"name", "token"}, {"type", "secret"}, {"label", "Deploy token"},
                                            {"required", true}}};
        setup({step(QStringLiteral("tests"), QStringLiteral("Tests"), QStringLiteral("Tests")),
               step(QStringLiteral("build"), QStringLiteral("Build"), QStringLiteral("Build")), deploy,
               step(QStringLiteral("health"), QStringLiteral("Health check"), QStringLiteral("Health"))});
        KipView view;
        view.setCommandName(m_pipeline.name);
        view.setSession(m_runner->session());
        view.resize(560, 520);
        view.show();
        QVERIFY(m_runner->start(m_pipeline, m_all, {}, EnvironmentManager(), {}));
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->screen().awaitingInput(), 3000);
        QTest::qWait(60);
        view.flushRefresh();
        QVERIFY2(view.blockCount() >= 1, "o checklist (steps) deve estar na tela");
        QVERIFY(view.editorFor(QStringLiteral("token")));
        QCOMPARE(m_runner->stepState(QStringLiteral("build")), KipStepState::Success);
        shot(view, QStringLiteral("pipeline-asking.png"));

        m_runner->setFieldValue(QStringLiteral("token"), QStringLiteral("letmein"));
        QVERIFY(m_runner->submit());
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QTest::qWait(60);
        view.flushRefresh();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Success);
        QVERIFY(view.resultCard() && view.resultCard()->isVisibleTo(&view));
        shot(view, QStringLiteral("pipeline-done.png"));
    }

    void aFailedRunShowsTheErrorCardAndTheFailedStep()
    {
        PipelineStep health = step(QStringLiteral("health"), QStringLiteral("Health check"), QStringLiteral("Health"));
        health.onFail = QStringLiteral("goto:rollback");
        setup({step(QStringLiteral("tests"), QStringLiteral("Tests"), QStringLiteral("Tests")), health,
               step(QStringLiteral("announce"), QStringLiteral("Announce"), QStringLiteral("Announce")),
               step(QStringLiteral("rollback"), QStringLiteral("Rollback"), QStringLiteral("Rollback"))});
        m_failing = {QStringLiteral("Health")};
        KipView view;
        view.setCommandName(m_pipeline.name);
        view.setSession(m_runner->session());
        view.resize(560, 520);
        view.show();
        QVERIFY(m_runner->start(m_pipeline, m_all, {}, EnvironmentManager(), {}));
        QTRY_VERIFY_WITH_TIMEOUT(m_runner->isFinished(), 3000);
        QTest::qWait(60);
        view.flushRefresh();
        QCOMPARE(m_runner->result().outcome, RunbookResult::Outcome::Failed);
        QCOMPARE(m_runner->stepState(QStringLiteral("announce")), KipStepState::Skipped);
        QCOMPARE(m_runner->stepState(QStringLiteral("rollback")), KipStepState::Success);
        QVERIFY(view.resultCard() && view.resultCard()->isVisibleTo(&view));
        shot(view, QStringLiteral("pipeline-failed.png"));
    }
};

QTEST_MAIN(TestPipelineView)
#include "test_pipeline_view.moc"
