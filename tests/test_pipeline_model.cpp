#include <QtTest>

#include <QFile>
#include <QJsonDocument>

#include "core/kai-file-validator.h"
#include "core/models.h"
#include "core/pipeline-model.h"
#include "core/project-format.h"
#include "core/yaml-bridge.h"
#include "utils/translation-manager.h"

using namespace kai::core;

class TestPipelineModel : public QObject {
    Q_OBJECT

private:
    static PipelineStep makeStep(const QString &id, const QString &run)
    {
        PipelineStep s;
        s.id = id;
        s.run = run;
        return s;
    }

private slots:
    void stepRoundTripsThroughJson()
    {
        PipelineStep s = makeStep(QStringLiteral("deploy"), QStringLiteral("steps/deploy"));
        s.label = QStringLiteral("Deploy");
        s.check = QStringLiteral("steps/is-deployed");
        s.confirm = QStringLiteral("Sure?");
        s.ask = QJsonArray{QJsonObject{{"name", "token"}, {"type", "secret"}}};
        s.params.insert(QStringLiteral("TARGET"), QStringLiteral("{{ask.target}}"));
        ExecutionCondition c;
        c.left = QStringLiteral("{{TARGET}}");
        c.op = QStringLiteral("eq");
        c.right = QStringLiteral("prod");
        s.when << c;
        s.whenCombinator = QStringLiteral("or");
        s.onFail = QStringLiteral("goto:rollback");
        s.retry = {3, 2, true};
        s.waitUntil = {QStringLiteral("steps/health"), 60, 5};
        PipelineCapture cap;
        cap.name = QStringLiteral("RELEASE_ID");
        cap.from = QStringLiteral("json");
        cap.path = QStringLiteral("data.id");
        cap.secret = true;
        s.capture << cap;

        const PipelineStep back = PipelineStep::fromJson(s.toJson());
        QCOMPARE(back.id, s.id);
        QCOMPARE(back.label, s.label);
        QCOMPARE(back.run, s.run);
        QCOMPARE(back.check, s.check);
        QCOMPARE(back.confirm, s.confirm);
        QCOMPARE(back.ask, s.ask);
        QCOMPARE(back.params, s.params);
        QCOMPARE(back.when.size(), 1);
        QCOMPARE(back.whenCombinator, QStringLiteral("or"));
        QCOMPARE(back.onFail, s.onFail);
        QCOMPARE(back.gotoTarget(), QStringLiteral("rollback"));
        QCOMPARE(back.retry.times, 3);
        QCOMPARE(back.retry.delaySec, 2);
        QVERIFY(back.retry.reask);
        QCOMPARE(back.waitUntil.check, s.waitUntil.check);
        QCOMPARE(back.waitUntil.timeoutSec, 60);
        QCOMPARE(back.waitUntil.intervalSec, 5);
        QCOMPARE(back.capture.size(), 1);
        QCOMPARE(back.capture.first().path, QStringLiteral("data.id"));
        QVERIFY(back.capture.first().secret);
    }

    void defaultsAreOmittedFromJson()
    {
        const QJsonObject obj = makeStep(QStringLiteral("a"), QStringLiteral("x")).toJson();
        QCOMPARE(obj.keys().size(), 2); // id + run
    }

    void commandPersistsStepsOnlyForPipelines()
    {
        Command c;
        c.id = QStringLiteral("p");
        c.name = QStringLiteral("Setup");
        c.type = CommandType::Pipeline;
        c.pipelineSteps = pipelineStepsToJson({makeStep(QStringLiteral("a"), QStringLiteral("x"))});
        const QJsonObject json = c.toJson();
        QCOMPARE(json.value("type").toString(), QStringLiteral("pipeline"));
        const Command back = Command::fromJson(json);
        QCOMPARE(back.type, CommandType::Pipeline);
        QCOMPARE(back.pipelineSteps.size(), 1);

        Command plain;
        plain.name = QStringLiteral("P");
        QVERIFY(!plain.toJson().contains("steps"));
    }

    void validationFindsStructuralProblems()
    {
        QVector<PipelineStep> steps;
        steps << makeStep(QStringLiteral("a"), QStringLiteral("x"));
        steps << makeStep(QStringLiteral("a"), QStringLiteral("x"));     // duplicate
        steps << makeStep(QStringLiteral("Bad Id"), QStringLiteral("x")); // invalid id
        steps << makeStep(QStringLiteral("d"), QString());                // no run
        PipelineStep back = makeStep(QStringLiteral("e"), QStringLiteral("x"));
        back.onFail = QStringLiteral("goto:a");
        steps << back;
        PipelineStep unknown = makeStep(QStringLiteral("f"), QStringLiteral("x"));
        unknown.onFail = QStringLiteral("goto:zzz");
        steps << unknown;
        PipelineStep weird = makeStep(QStringLiteral("g"), QStringLiteral("x"));
        weird.onFail = QStringLiteral("explode");
        steps << weird;
        PipelineStep cap = makeStep(QStringLiteral("h"), QStringLiteral("x"));
        PipelineCapture noPath;
        noPath.name = QStringLiteral("V");
        noPath.from = QStringLiteral("json");
        cap.capture << noPath;
        steps << cap;

        QStringList codes;
        for (const PipelineIssue &i : validatePipelineSteps(steps)) {
            codes << i.code;
        }
        for (const char *expected : {"duplicate_id", "invalid_id", "missing_run", "goto_backward", "goto_unknown",
                                     "bad_on_fail", "bad_capture"}) {
            QVERIFY2(codes.contains(QLatin1String(expected)), expected);
        }
    }

    void forwardGotoIsValid()
    {
        PipelineStep a = makeStep(QStringLiteral("a"), QStringLiteral("x"));
        a.onFail = QStringLiteral("goto:b");
        QVERIFY(validatePipelineSteps({a, makeStep(QStringLiteral("b"), QStringLiteral("y"))}).isEmpty());
    }

    void referencesResolveByNameAndPath()
    {
        Folder root;
        root.id = QStringLiteral("r");
        root.name = QStringLiteral("Proj");
        Folder steps;
        steps.id = QStringLiteral("s");
        steps.name = QStringLiteral("steps");
        steps.parentId = root.id;
        Folder other;
        other.id = QStringLiteral("o");
        other.name = QStringLiteral("other");
        other.parentId = root.id;
        const QVector<Folder> folders{root, steps, other};

        auto cmd = [](const QString &id, const QString &name, const QString &folder) {
            Command c;
            c.id = id;
            c.name = name;
            c.folderId = folder;
            return c;
        };
        QMap<QString, Command> all;
        for (const Command &c : {cmd(QStringLiteral("p"), QStringLiteral("Setup"), QStringLiteral("r")),
                                 cmd(QStringLiteral("1"), QStringLiteral("build"), QStringLiteral("s")),
                                 cmd(QStringLiteral("2"), QStringLiteral("test"), QStringLiteral("s")),
                                 cmd(QStringLiteral("3"), QStringLiteral("test"), QStringLiteral("o")),
                                 cmd(QStringLiteral("4"), QStringLiteral("root-only"), QStringLiteral("r"))}) {
            all.insert(c.id, c);
        }
        const Command &pipeline = all[QStringLiteral("p")];
        QString error;

        QCOMPARE(resolveCommandRef(QStringLiteral("steps/build"), pipeline, all, folders)->id, QStringLiteral("1"));
        QCOMPARE(resolveCommandRef(QStringLiteral("Proj/steps/build"), pipeline, all, folders)->id, QStringLiteral("1"));
        QCOMPARE(resolveCommandRef(QStringLiteral("build"), pipeline, all, folders)->id, QStringLiteral("1")); // único
        QCOMPARE(resolveCommandRef(QStringLiteral("root-only"), pipeline, all, folders)->id, QStringLiteral("4"));
        // "test" existe em duas pastas: nome simples é ambíguo, caminho resolve.
        QVERIFY(!resolveCommandRef(QStringLiteral("test"), pipeline, all, folders, &error));
        QCOMPARE(error, QStringLiteral("ambiguous"));
        QCOMPARE(resolveCommandRef(QStringLiteral("other/test"), pipeline, all, folders)->id, QStringLiteral("3"));
        QVERIFY(!resolveCommandRef(QStringLiteral("nope"), pipeline, all, folders, &error));
        QCOMPARE(error, QStringLiteral("not_found"));
        QVERIFY(!resolveCommandRef(QString(), pipeline, all, folders));
    }

    void listFieldsRoundTripThroughTheirTextForms()
    {
        QMap<QString, QString> params{{QStringLiteral("A"), QStringLiteral("1")},
                                      {QStringLiteral("URL"), QStringLiteral("http://x/?a=b")}};
        QCOMPARE(pipelineParamsFromText(pipelineParamsToText(params)), params);
        QVERIFY(pipelineParamsFromText(QStringLiteral("no equals\n=novalue")).isEmpty());

        ExecutionCondition c;
        c.left = QStringLiteral("{{steps.a.ok}}");
        c.op = QStringLiteral("eq");
        c.right = QStringLiteral("a | b");
        const QVector<ExecutionCondition> when = pipelineWhenFromText(pipelineWhenToText({c}));
        QCOMPARE(when.size(), 1);
        QCOMPARE(when.first().left, c.left);
        QCOMPARE(when.first().op, c.op);
        QCOMPARE(when.first().right, c.right);

        PipelineCapture json;
        json.name = QStringLiteral("ID");
        json.from = QStringLiteral("json");
        json.path = QStringLiteral("data.id");
        json.secret = true;
        PipelineCapture re;
        re.name = QStringLiteral("TOKEN");
        re.from = QStringLiteral("stdout");
        re.regex = QStringLiteral("token=(\\S+)");
        PipelineCapture exit;
        exit.name = QStringLiteral("CODE");
        exit.from = QStringLiteral("exit");
        const QVector<PipelineCapture> back = pipelineCaptureFromText(pipelineCaptureToText({json, re, exit}));
        QCOMPARE(back.size(), 3);
        QCOMPARE(back[0].path, json.path);
        QVERIFY(back[0].secret);
        QCOMPARE(back[1].regex, re.regex);
        QVERIFY(!back[1].secret);
        QCOMPARE(back[2].from, QStringLiteral("exit"));
    }

    // O exemplo com passos KIP: valida e toda pipeline aponta para comandos reais (incluindo os KIP, que exigem kip: true).
    void theKipSamplePipelinesValidateAndTheirReferencesResolve()
    {
        QFile file(QStringLiteral("sample/pipeline-kip/kai.yml"));
        if (!file.open(QIODevice::ReadOnly)) QSKIP("run from the repository root (needs sample/pipeline-kip/kai.yml)");
        const QString text = QString::fromUtf8(file.readAll());
        const ValidationResult result = validateKaiFileText(text);
        QString detail;
        for (const ValidationIssue &i : result.issues) detail += i.path + QStringLiteral(": ") + i.message + QLatin1Char('\n');
        QVERIFY2(!result.hasErrors(), qPrintable(detail));

        const QJsonDocument doc = QJsonDocument::fromJson(yamlTextToJsonText(text).toUtf8());
        QVERIFY(doc.isObject());
        const ProjectFormatContent content = convertProjectFormat(doc.object(), QStringLiteral("root"),
                                                                    QStringLiteral("Pipeline + KIP Sample"), QString());
        QVector<Folder> folders = content.subFolders;
        Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Pipeline + KIP Sample");
        folders.prepend(root);
        QMap<QString, Command> all;
        for (const Command &c : content.commands) {
            all.insert(c.id, c);
        }
        int pipelines = 0;
        int kipSteps = 0;
        for (const Command &c : all) {
            if (c.type != CommandType::Pipeline) continue;
            ++pipelines;
            const QVector<PipelineStep> steps = pipelineStepsFromJson(c.pipelineSteps);
            QVERIFY(validatePipelineSteps(steps).isEmpty());
            for (const PipelineStep &s : steps) {
                for (const QString &ref : {s.run, s.check, s.waitUntil.check}) {
                    if (ref.isEmpty()) continue;
                    QString error;
                    const Command *target = resolveCommandRef(ref, c, all, folders, &error);
                    QVERIFY2(target, qPrintable(ref + QStringLiteral(": ") + error));
                    if (ref == s.run && target->kip) ++kipSteps;
                }
            }
        }
        QCOMPARE(pipelines, 3);
        QVERIFY(kipSteps >= 6);
    }

    // O projeto de exemplo é o cenário de aceitação: valida, e toda referência de passo aponta para um comando real.
    void theSamplePipelineValidatesAndItsReferencesResolve()
    {
        QFile file(QStringLiteral("sample/pipeline/kai.yml"));
        if (!file.open(QIODevice::ReadOnly)) QSKIP("run from the repository root (needs sample/pipeline/kai.yml)");
        const QString text = QString::fromUtf8(file.readAll());
        const ValidationResult result = validateKaiFileText(text);
        QString detail;
        for (const ValidationIssue &i : result.issues) detail += i.path + QStringLiteral(": ") + i.message + QLatin1Char('\n');
        QVERIFY2(!result.hasErrors(), qPrintable(detail));

        const QJsonDocument doc = QJsonDocument::fromJson(yamlTextToJsonText(text).toUtf8());
        QVERIFY(doc.isObject());
        const ProjectFormatContent content = convertProjectFormat(doc.object(), QStringLiteral("root"),
                                                                    QStringLiteral("Pipeline Sample"), QString());
        QVector<Folder> folders = content.subFolders;
        Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Pipeline Sample");
        folders.prepend(root);
        QMap<QString, Command> all;
        const Command *pipeline = nullptr;
        for (const Command &c : content.commands) {
            all.insert(c.id, c);
        }
        for (const Command &c : all) {
            if (c.type == CommandType::Pipeline) pipeline = &c;
        }
        QVERIFY(pipeline);
        const QVector<PipelineStep> steps = pipelineStepsFromJson(pipeline->pipelineSteps);
        QVERIFY(steps.size() >= 8);
        QVERIFY(validatePipelineSteps(steps).isEmpty());
        for (const PipelineStep &s : steps) {
            for (const QString &ref : {s.run, s.check, s.waitUntil.check}) {
                if (ref.isEmpty()) continue;
                QString error;
                QVERIFY2(resolveCommandRef(ref, *pipeline, all, folders, &error), qPrintable(ref + QStringLiteral(": ") + error));
            }
        }
    }

    void fileValidatorAcceptsAPipelineAndFlagsBrokenOnes()
    {
        const QString good = QStringLiteral(
            "commands:\n"
            "  - name: Setup\n"
            "    type: pipeline\n"
            "    steps:\n"
            "      - id: a\n"
            "        run: One\n"
            "        on_fail: goto:b\n"
            "      - id: b\n"
            "        run: Two\n"
            "  - name: One\n"
            "    type: command\n"
            "    command: echo 1\n"
            "  - name: Two\n"
            "    type: command\n"
            "    command: echo 2\n");
        const ValidationResult ok = validateKaiFileText(good);
        QVERIFY2(!ok.hasErrors(), qPrintable(ok.issues.isEmpty() ? QString() : ok.issues.first().message));

        const QString bad = QStringLiteral(
            "commands:\n"
            "  - name: Setup\n"
            "    type: pipeline\n"
            "    steps:\n"
            "      - id: a\n"
            "        on_fail: goto:nowhere\n");
        const ValidationResult broken = validateKaiFileText(bad);
        QVERIFY(broken.hasErrors());

        const QString noSteps = QStringLiteral("commands:\n  - name: Setup\n    type: pipeline\n");
        QVERIFY(validateKaiFileText(noSteps).hasErrors());
    }
};

QTEST_MAIN(TestPipelineModel)
#include "test_pipeline_model.moc"
