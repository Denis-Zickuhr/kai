// As ferramentas do agente sobre um host em memória: leitura, validação, prévia (diff), execução,
// diário de mudanças com desfazer, esconder segredos e run_command.

#include <QTest>
#include "utils/translation-manager.h"
#include <QJsonArray>
#include <QJsonDocument>

#include "ai-fake-host.h"
#include "engine/ai/agent-toolbox.h"

using namespace kai::core;
using namespace kai::engine;

namespace {

AiToolCall call(const QString &name, const QJsonObject &args = {})
{
    return {QStringLiteral("id1"), name, args};
}

QJsonValue parse(const QString &text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    return doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
}

} // namespace

class TestAgentToolbox : public QObject {
    Q_OBJECT

    FakeAgentHost host;
    std::unique_ptr<AgentToolbox> box;

    ToolOutcome run(const QString &name, const QJsonObject &args = {})
    {
        ToolOutcome out;
        bool called = false;
        box->execute(call(name, args), [&](const ToolOutcome &o) { out = o; called = true; });
        if (!called) QTest::qWait(10);
        return out;
    }

private slots:
    void initTestCase() { kai::utils::TranslationManager::instance().loadLanguage(QStringLiteral("en")); }

    void init()
    {
        host = FakeAgentHost();
        host.addFolder(QStringLiteral("f_root"), QStringLiteral("Root"));
        host.addFolder(QStringLiteral("f_sub"), QStringLiteral("Sub"), QStringLiteral("f_root"));
        host.addCommand(QStringLiteral("c_a"), QStringLiteral("f_root"), QStringLiteral("Deploy"), QStringLiteral("make deploy"));
        host.addCommand(QStringLiteral("c_b"), QStringLiteral("f_sub"), QStringLiteral("Build"), QStringLiteral("make build"));
        Environment env;
        env.id = QStringLiteral("e1");
        env.name = QStringLiteral("Dev");
        env.vars = {{QStringLiteral("HOST"), QStringLiteral("localhost")}, {QStringLiteral("TOKEN"), QStringLiteral("s3cr3t")}};
        env.secretKeys = {QStringLiteral("TOKEN")};
        host.settingsData.environments << env;
        host.settingsData.activeEnvironmentId = QStringLiteral("e1");
        box = std::make_unique<AgentToolbox>(&host);
        box->setManifestoDirectory(QStringLiteral("assets/manifesto"));
        box->setSampleDirectory(QStringLiteral("sample"));
    }

    void everyToolHasANameDescriptionAndObjectSchema()
    {
        QSet<QString> names;
        for (const AiToolSpec &s : box->specs()) {
            QVERIFY2(!names.contains(s.name), qPrintable(s.name));
            names.insert(s.name);
            QVERIFY(!s.description.isEmpty());
            QCOMPARE(s.inputSchema.value(QStringLiteral("type")).toString(), QStringLiteral("object"));
        }
        for (const char *expected : {"list_folders", "list_commands", "get_command", "create_command", "update_command",
                                     "delete_command", "run_command", "kill_process", "ask_user", "read_manifesto", "read_samples",
                                     "set_environment_variable", "create_folder", "delete_folder", "move_command", "run_in_app"}) {
            QVERIFY2(names.contains(QString::fromLatin1(expected)), expected);
        }
    }

    void riskLevelsMatchWhatEachToolDoes()
    {
        QCOMPARE(box->plan(call(QStringLiteral("list_commands"))).risk, ToolRisk::Read);
        QCOMPARE(box->plan(call(QStringLiteral("ask_user"), {{QStringLiteral("question"), QStringLiteral("?")}})).risk, ToolRisk::Ask);
        QCOMPARE(box->plan(call(QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("X")}})).risk, ToolRisk::Write);
        QCOMPARE(box->plan(call(QStringLiteral("delete_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}})).risk, ToolRisk::Danger);
        QCOMPARE(box->plan(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}})).risk, ToolRisk::Write);
    }

    void unknownToolsAndBrokenArgumentsAreInvalid()
    {
        QVERIFY(!box->plan(call(QStringLiteral("format_disk"))).valid);
        QVERIFY(!box->plan(call(QStringLiteral("list_commands"), {{QStringLiteral("__parse_error"), QStringLiteral("{")}})).valid);
        QVERIFY(run(QStringLiteral("format_disk")).isError);
    }

    void listsFoldersWithPathsAndCounts()
    {
        const QJsonArray arr = parse(run(QStringLiteral("list_folders")).content).toArray();
        QCOMPARE(arr.size(), 2);
        QCOMPARE(arr.at(1).toObject().value(QStringLiteral("path")).toString(), QStringLiteral("Root/Sub"));
        QCOMPARE(arr.at(0).toObject().value(QStringLiteral("commands")).toInt(), 1);
    }

    void listCommandsFiltersAndPages()
    {
        QJsonObject all = parse(run(QStringLiteral("list_commands")).content).toObject();
        QCOMPARE(all.value(QStringLiteral("total")).toInt(), 2);
        QJsonObject byFolder = parse(run(QStringLiteral("list_commands"), {{QStringLiteral("folder_id"), QStringLiteral("f_sub")}}).content).toObject();
        QCOMPARE(byFolder.value(QStringLiteral("items")).toArray().first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Build"));
        QJsonObject byQuery = parse(run(QStringLiteral("list_commands"), {{QStringLiteral("query"), QStringLiteral("DEP")}}).content).toObject();
        QCOMPARE(byQuery.value(QStringLiteral("total")).toInt(), 1);
        QJsonObject paged = parse(run(QStringLiteral("list_commands"), {{QStringLiteral("limit"), 1}, {QStringLiteral("offset"), 1}}).content).toObject();
        QCOMPARE(paged.value(QStringLiteral("items")).toArray().size(), 1);
        QCOMPARE(paged.value(QStringLiteral("total")).toInt(), 2);
    }

    void listCommandsAlwaysFitsTheResultLimitAndSaysWhereToContinue()
    {
        // 150 comandos com resumo comprido: o JSON não pode ser cortado no meio e o total tem de aparecer.
        for (int i = 0; i < 150; ++i) {
            host.addCommand(QStringLiteral("many_%1").arg(i), QStringLiteral("f_root"), QStringLiteral("Comando número %1 com nome bem comprido para ocupar espaço").arg(i),
                            QStringLiteral("echo ").repeated(30));
        }
        QSet<QString> seen;
        int offset = 0;
        for (int guard = 0; guard < 20; ++guard) {
            const ToolOutcome out = run(QStringLiteral("list_commands"), {{QStringLiteral("limit"), 200}, {QStringLiteral("offset"), offset}});
            QVERIFY2(!out.content.contains(QStringLiteral("[truncated")), qPrintable(out.content.right(80)));
            const QJsonObject page = parse(out.content).toObject();
            QCOMPARE(page.value(QStringLiteral("total")).toInt(), 152);
            const QJsonArray items = page.value(QStringLiteral("items")).toArray();
            QVERIFY(!items.isEmpty());
            for (const QJsonValue &v : items) seen.insert(v.toObject().value(QStringLiteral("id")).toString());
            if (!page.contains(QStringLiteral("next_offset"))) break;
            QCOMPARE(page.value(QStringLiteral("next_offset")).toInt(), offset + items.size());
            offset = page.value(QStringLiteral("next_offset")).toInt();
        }
        QCOMPARE(seen.size(), 152);
    }

    void hiddenCommandsStayHiddenUnlessAsked()
    {
        host.data.commands[0].hidden = true;
        QCOMPARE(parse(run(QStringLiteral("list_commands")).content).toObject().value(QStringLiteral("total")).toInt(), 1);
        QCOMPARE(parse(run(QStringLiteral("list_commands"), {{QStringLiteral("include_hidden"), true}}).content).toObject().value(QStringLiteral("total")).toInt(), 2);
    }

    void getCommandResolvesByIdOrUniqueNameAndExplainsAmbiguity()
    {
        QVERIFY(!run(QStringLiteral("get_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}}).isError);
        QVERIFY(!run(QStringLiteral("get_command"), {{QStringLiteral("name"), QStringLiteral("deploy")}}).isError);
        QVERIFY(run(QStringLiteral("get_command"), {{QStringLiteral("id"), QStringLiteral("nope")}}).isError);
        host.addCommand(QStringLiteral("c_a2"), QStringLiteral("f_sub"), QStringLiteral("Deploy"));
        const ToolOutcome amb = run(QStringLiteral("get_command"), {{QStringLiteral("name"), QStringLiteral("Deploy")}});
        QVERIFY(amb.isError);
        QVERIFY(amb.content.contains(QStringLiteral("c_a2")));
    }

    void getCommandShowsHooksByNameAndHidesUsageNoise()
    {
        host.data.commands[0].hooks.pre = {QStringLiteral("c_b")};
        host.data.commands[0].lastParamValues = {{QStringLiteral("x"), QStringLiteral("1")}};
        const QJsonObject o = parse(run(QStringLiteral("get_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}}).content).toObject();
        QCOMPARE(o.value(QStringLiteral("hooks")).toObject().value(QStringLiteral("pre")).toArray().first().toString(), QStringLiteral("Build"));
        QVERIFY(!o.contains(QStringLiteral("last_param_values")));
        QCOMPARE(o.value(QStringLiteral("folder_path")).toString(), QStringLiteral("Root"));
    }

    void environmentsMaskSecretsOnlyWhenAsked()
    {
        QJsonArray envs = parse(run(QStringLiteral("list_environments")).content).toArray();
        QCOMPARE(envs.first().toObject().value(QStringLiteral("variables")).toObject().value(QStringLiteral("TOKEN")).toString(), QStringLiteral("s3cr3t"));
        box->setHideSecretVariables(true);
        envs = parse(run(QStringLiteral("list_environments")).content).toArray();
        const QJsonObject vars = envs.first().toObject().value(QStringLiteral("variables")).toObject();
        QCOMPARE(vars.value(QStringLiteral("TOKEN")).toString(), QStringLiteral("***hidden***"));
        QCOMPARE(vars.value(QStringLiteral("HOST")).toString(), QStringLiteral("localhost"));
    }

    void folderAndCollectionSecretsAreMasked()
    {
        host.data.folders[0].envVars = {{QStringLiteral("PW"), QStringLiteral("pw1")}, {QStringLiteral("ok"), QStringLiteral("v")}};
        host.data.folders[0].secretEnvKeys = {QStringLiteral("PW")};
        Collection col;
        col.id = QStringLiteral("col1");
        col.name = QStringLiteral("Users");
        col.schema = {{QStringLiteral("name"), QStringLiteral("Name"), CollectionFieldType::Text, true, false},
                      {QStringLiteral("pass"), QStringLiteral("Pass"), CollectionFieldType::Text, true, true}};
        CollectionEntry e;
        e.values = {{QStringLiteral("name"), QStringLiteral("ana")}, {QStringLiteral("pass"), QStringLiteral("hunter2")}};
        col.entries << e;
        host.collectionList << col;

        QVERIFY(run(QStringLiteral("get_folder"), {{QStringLiteral("id"), QStringLiteral("f_root")}}).content.contains(QStringLiteral("pw1")));
        QVERIFY(run(QStringLiteral("get_collection"), {{QStringLiteral("id"), QStringLiteral("Users")}}).content.contains(QStringLiteral("hunter2")));
        box->setHideSecretVariables(true);
        const QString f = run(QStringLiteral("get_folder"), {{QStringLiteral("id"), QStringLiteral("f_root")}}).content;
        QVERIFY(!f.contains(QStringLiteral("pw1")));
        QVERIFY(f.contains(QStringLiteral("***hidden***")));
        const QString c = run(QStringLiteral("get_collection"), {{QStringLiteral("id"), QStringLiteral("col1")}}).content;
        QVERIFY(!c.contains(QStringLiteral("hunter2")));
        QVERIFY(c.contains(QStringLiteral("ana")));
    }

    void runHistoryAndOutputsArePaged()
    {
        RunRecord r;
        r.id = QStringLiteral("r1");
        r.commandId = QStringLiteral("c_a");
        r.commandName = QStringLiteral("Deploy");
        r.output = QString(100, QLatin1Char('x')) + QStringLiteral("END");
        host.runs << r;
        const QJsonArray list = parse(run(QStringLiteral("list_run_history")).content).toArray();
        QCOMPARE(list.size(), 1);
        QVERIFY(!list.first().toObject().contains(QStringLiteral("output")));
        const QJsonObject page = parse(run(QStringLiteral("get_run_output"), {{QStringLiteral("run_id"), QStringLiteral("r1")}, {QStringLiteral("offset"), 98}}).content).toObject();
        QCOMPARE(page.value(QStringLiteral("text")).toString(), QStringLiteral("xxEND"));
        QCOMPARE(page.value(QStringLiteral("total_chars")).toInt(), 103);
        QVERIFY(run(QStringLiteral("get_run_output"), {{QStringLiteral("run_id"), QStringLiteral("zz")}}).isError);

        host.outputs[QStringLiteral("c_a")] = QStringLiteral("0123456789");
        const QJsonObject tail = parse(run(QStringLiteral("get_command_output"), {{QStringLiteral("id"), QStringLiteral("c_a")}, {QStringLiteral("tail_chars"), 4}}).content).toObject();
        QCOMPARE(tail.value(QStringLiteral("text")).toString(), QStringLiteral("6789"));
    }

    void logsAreFilteredByLevelNewestLast()
    {
        for (auto level : {kai::utils::LogLevel::Debug, kai::utils::LogLevel::Info, kai::utils::LogLevel::Error}) {
            kai::utils::LogEntry e;
            e.level = level;
            e.tag = QStringLiteral("T");
            e.message = QStringLiteral("msg%1").arg(int(level));
            e.timestamp = QDateTime::currentDateTime();
            host.logs << e;
        }
        const QString text = run(QStringLiteral("read_logs"), {{QStringLiteral("min_level"), QStringLiteral("info")}}).content;
        QVERIFY(!text.contains(QStringLiteral("msg0")));
        QVERIFY(text.indexOf(QStringLiteral("msg1")) < text.indexOf(QStringLiteral("msg3")));
    }

    void manifestoOutlineAndSections()
    {
        const QString outline = run(QStringLiteral("read_manifesto"), {{QStringLiteral("document"), QStringLiteral("kai")}}).content;
        QVERIFY(outline.contains(QStringLiteral("3.1")));
        const ToolOutcome sec = run(QStringLiteral("read_manifesto"), {{QStringLiteral("document"), QStringLiteral("kai")}, {QStringLiteral("section"), QStringLiteral("3.1")}});
        QVERIFY(!sec.isError);
        QVERIFY(sec.content.contains(QStringLiteral("complete command model")));
        QVERIFY(run(QStringLiteral("read_manifesto"), {{QStringLiteral("document"), QStringLiteral("nope")}}).isError);
        QVERIFY(run(QStringLiteral("read_manifesto"), {{QStringLiteral("document"), QStringLiteral("kai")}, {QStringLiteral("section"), QStringLiteral("99.9")}}).isError);
        const QString schema = run(QStringLiteral("read_manifesto"), {{QStringLiteral("document"), QStringLiteral("schema")}}).content;
        QVERIFY(schema.contains(QStringLiteral("command")));
    }

    void samplesAreListedAndReadable()
    {
        const QString list = run(QStringLiteral("read_samples"), {}).content;
        QVERIFY(list.contains(QStringLiteral("http/kai.yml")));
        const ToolOutcome file = run(QStringLiteral("read_samples"), {{QStringLiteral("path"), QStringLiteral("http/kai.yml")}});
        QVERIFY(!file.isError);
        QVERIFY(file.content.contains(QStringLiteral("commands:")));
        QVERIFY(run(QStringLiteral("read_samples"), {{QStringLiteral("path"), QStringLiteral("nope.yml")}}).isError);
        QVERIFY(run(QStringLiteral("read_samples"), {{QStringLiteral("path"), QStringLiteral("../CMakeLists.txt")}}).isError);
    }

    void validateKaiYmlReportsIssues()
    {
        QVERIFY(!run(QStringLiteral("validate_kai_yml"), {{QStringLiteral("text"), QStringLiteral("commands:\n  - name: \"A\"\n    command: \"echo\"\n")}}).isError);
        const QString bad = run(QStringLiteral("validate_kai_yml"), {{QStringLiteral("text"), QStringLiteral("commands:\n  - nme: \"A\"\n")}}).content;
        QVERIFY(bad.contains(QStringLiteral("ERROR")) || bad.contains(QStringLiteral("warning")));
    }

    void limitedTruncatesWithAMarker()
    {
        const QString t = AgentToolbox::limited(QString(50, QLatin1Char('a')), 10);
        QVERIFY(t.startsWith(QString(10, QLatin1Char('a'))));
        QVERIFY(t.contains(QStringLiteral("truncated: 40")));
        QCOMPARE(AgentToolbox::limited(QStringLiteral("short"), 10), QStringLiteral("short"));
    }

    void createCommandAddsItAndRecordsAChange()
    {
        const QJsonObject args{{QStringLiteral("folder_id"), QStringLiteral("f_sub")},
                               {QStringLiteral("command"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Lint")},
                                                                         {QStringLiteral("command"), QStringLiteral("npm run lint")}}}};
        const ToolPlan plan = box->plan(call(QStringLiteral("create_command"), args));
        QVERIFY2(plan.valid, qPrintable(plan.error));
        QVERIFY(plan.detail.contains(QStringLiteral("npm run lint")));
        QVERIFY(plan.detail.contains(QStringLiteral("Root/Sub")));
        QCOMPARE(host.data.commands.size(), 2); // o plano não muda nada

        const ToolOutcome out = run(QStringLiteral("create_command"), args);
        QVERIFY2(!out.isError, qPrintable(out.content));
        QCOMPARE(host.data.commands.size(), 3);
        QVERIFY(!out.changeId.isEmpty());
        QCOMPARE(host.commandsChangedCount, 1);
        const QString id = parse(out.content).toObject().value(QStringLiteral("id")).toString();
        QVERIFY(host.commandById(id));
        QCOMPARE(host.commandById(id)->folderId, QStringLiteral("f_sub"));
    }

    // Quem aprova precisa ver QUANDO o comando vai rodar sozinho, não só o texto dele.
    void createCommandPreviewShowsSchedulingAutoRunAndHooks()
    {
        const QJsonObject args{{QStringLiteral("folder_id"), QStringLiteral("f_root")},
                               {QStringLiteral("command"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Nightly")},
                                                                         {QStringLiteral("command"), QStringLiteral("make backup")},
                                                                         {QStringLiteral("cron_expression"), QStringLiteral("0 3 * * *")},
                                                                         {QStringLiteral("auto_run"), true},
                                                                         {QStringLiteral("is_background"), true},
                                                                         {QStringLiteral("hooks"), QJsonObject{{QStringLiteral("post"), QJsonArray{QStringLiteral("Build")}}}}}}};
        const ToolPlan plan = box->plan(call(QStringLiteral("create_command"), args));
        QVERIFY2(plan.valid, qPrintable(plan.error));
        QVERIFY(plan.detail.contains(QStringLiteral("+ cron_expression: 0 3 * * *")));
        QVERIFY(plan.detail.contains(QStringLiteral("+ auto_run: true")));
        QVERIFY(plan.detail.contains(QStringLiteral("+ is_background: true")));
        QVERIFY(plan.detail.contains(QStringLiteral("hooks")));
        QVERIFY(plan.detail.contains(QStringLiteral("Build")));
    }

    void createCommandRejectsBadInput()
    {
        auto plan = [this](const QJsonObject &cmd, const QString &folder = QStringLiteral("f_root")) {
            return box->plan(call(QStringLiteral("create_command"), {{QStringLiteral("folder_id"), folder}, {QStringLiteral("command"), cmd}}));
        };
        QVERIFY(!plan({{QStringLiteral("command"), QStringLiteral("x")}}).valid);                                     // sem nome
        QVERIFY(!plan({{QStringLiteral("name"), QStringLiteral("N")}}).valid);                                        // sem comando
        QVERIFY(!plan({{QStringLiteral("name"), QStringLiteral("Deploy")}, {QStringLiteral("command"), QStringLiteral("x")}}).valid); // nome repetido na pasta
        QVERIFY(plan({{QStringLiteral("name"), QStringLiteral("Deploy")}, {QStringLiteral("command"), QStringLiteral("x")}}, QStringLiteral("f_sub")).valid);
        const ToolPlan typo = plan({{QStringLiteral("name"), QStringLiteral("N")}, {QStringLiteral("comand"), QStringLiteral("x")}});
        QVERIFY(!typo.valid);
        QVERIFY(typo.error.contains(QStringLiteral("comand")));
        QVERIFY(!plan({{QStringLiteral("name"), QStringLiteral("N")}, {QStringLiteral("command"), QStringLiteral("x")}, {QStringLiteral("type"), QStringLiteral("grpc")}}).valid);
        QVERIFY(!plan({{QStringLiteral("name"), QStringLiteral("N")}, {QStringLiteral("command"), QStringLiteral("x")}, {QStringLiteral("language"), QStringLiteral("cobol")}}).valid);
        QVERIFY(!plan({{QStringLiteral("name"), QStringLiteral("N")}, {QStringLiteral("type"), QStringLiteral("http")}}).valid);          // http sem url
        QVERIFY(plan({{QStringLiteral("name"), QStringLiteral("N")}, {QStringLiteral("type"), QStringLiteral("http")},
                      {QStringLiteral("http_config"), QJsonObject{{QStringLiteral("url"), QStringLiteral("https://x.test")}, {QStringLiteral("method"), QStringLiteral("POST")}}}}).valid);
        QVERIFY(!plan({{QStringLiteral("name"), QStringLiteral("N")}, {QStringLiteral("command"), QStringLiteral("x")},
                       {QStringLiteral("params"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("bad name")}, {QStringLiteral("type"), QStringLiteral("text")}}}}}).valid);
        QVERIFY(!plan({{QStringLiteral("name"), QStringLiteral("N")}, {QStringLiteral("command"), QStringLiteral("x")},
                       {QStringLiteral("hooks"), QJsonObject{{QStringLiteral("pre"), QJsonArray{QStringLiteral("Missing")}}}}}).valid);
        QVERIFY(!plan({{QStringLiteral("name"), QStringLiteral("N")}, {QStringLiteral("command"), QStringLiteral("x")}}, QStringLiteral("f_nope")).valid);
    }

    void createCommandResolvesHooksByName()
    {
        const ToolOutcome out = run(QStringLiteral("create_command"),
            {{QStringLiteral("folder_id"), QStringLiteral("f_root")},
             {QStringLiteral("command"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Release")},
                                                       {QStringLiteral("command"), QStringLiteral("make release")},
                                                       {QStringLiteral("hooks"), QJsonObject{{QStringLiteral("pre"), QJsonArray{QStringLiteral("build")}}}}}}});
        QVERIFY2(!out.isError, qPrintable(out.content));
        const QString id = parse(out.content).toObject().value(QStringLiteral("id")).toString();
        QCOMPARE(host.commandById(id)->hooks.pre, QStringList{QStringLiteral("c_b")});
    }

    void createCommandGeneratesUniqueIds()
    {
        QSet<QString> ids;
        for (int i = 0; i < 3; ++i) {
            const ToolOutcome out = run(QStringLiteral("create_command"),
                {{QStringLiteral("folder_id"), i % 2 ? QStringLiteral("f_root") : QStringLiteral("f_sub")},
                 {QStringLiteral("command"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Same%1").arg(i / 2)}, {QStringLiteral("command"), QStringLiteral("x")}}}});
            QVERIFY(!out.isError);
            ids.insert(parse(out.content).toObject().value(QStringLiteral("id")).toString());
        }
        QCOMPARE(ids.size(), 3);
    }

    void updateCommandMergesAPatchAndKeepsUsageState()
    {
        host.data.commands[0].lastParamValues = {{QStringLiteral("p"), QStringLiteral("v")}};
        host.data.commands[0].order = 5;
        const QJsonObject args{{QStringLiteral("id"), QStringLiteral("c_a")},
                               {QStringLiteral("patch"), QJsonObject{{QStringLiteral("command"), QStringLiteral("make ship")},
                                                                      {QStringLiteral("description"), QStringLiteral("ships it")}}}};
        const ToolPlan plan = box->plan(call(QStringLiteral("update_command"), args));
        QVERIFY2(plan.valid, qPrintable(plan.error));
        QVERIFY(plan.detail.contains(QStringLiteral("~ command: make deploy → make ship")));
        QVERIFY(plan.detail.contains(QStringLiteral("+ description")));
        QCOMPARE(host.data.commands[0].command, QStringLiteral("make deploy"));

        const ToolOutcome out = run(QStringLiteral("update_command"), args);
        QVERIFY2(!out.isError, qPrintable(out.content));
        const Command *c = host.commandById(QStringLiteral("c_a"));
        QCOMPARE(c->command, QStringLiteral("make ship"));
        QCOMPARE(c->description, QStringLiteral("ships it"));
        QCOMPARE(c->lastParamValues.value(QStringLiteral("p")), QStringLiteral("v"));
        QCOMPARE(c->order, 5);
        QCOMPARE(c->folderId, QStringLiteral("f_root"));
    }

    void updateCommandNullRemovesAKeyAndBadPatchesAreRefused()
    {
        host.data.commands[0].description = QStringLiteral("old");
        QVERIFY(!run(QStringLiteral("update_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}, {QStringLiteral("patch"), QJsonObject{{QStringLiteral("description"), QJsonValue::Null}}}}).isError);
        QVERIFY(host.commandById(QStringLiteral("c_a"))->description.isEmpty());

        auto plan = [this](const QJsonObject &patch) {
            return box->plan(call(QStringLiteral("update_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}, {QStringLiteral("patch"), patch}}));
        };
        QVERIFY(!plan({}).valid);
        QVERIFY(!plan({{QStringLiteral("id"), QStringLiteral("other")}}).valid);
        QVERIFY(!plan({{QStringLiteral("name"), QString()}}).valid);
        QVERIFY(!plan({{QStringLiteral("nme"), QStringLiteral("x")}}).valid);
        QVERIFY(!plan({{QStringLiteral("command"), QStringLiteral("make deploy")}}).valid); // não muda nada
        host.addCommand(QStringLiteral("c_c"), QStringLiteral("f_root"), QStringLiteral("Other"));
        QVERIFY(!plan({{QStringLiteral("name"), QStringLiteral("other")}}).valid); // nome repetido
        QVERIFY(!plan({{QStringLiteral("hooks"), QJsonObject{{QStringLiteral("pre"), QJsonArray{QStringLiteral("Deploy")}}}}}).valid); // hook de si mesmo
    }

    void deleteCommandRemovesItFromOtherHooksAndCanBeUndone()
    {
        host.data.commands[0].hooks.pre = {QStringLiteral("c_b")};
        const ToolOutcome out = run(QStringLiteral("delete_command"), {{QStringLiteral("id"), QStringLiteral("c_b")}});
        QVERIFY(!out.isError);
        QVERIFY(!host.commandById(QStringLiteral("c_b")));
        QVERIFY(host.commandById(QStringLiteral("c_a"))->hooks.pre.isEmpty());

        QString error;
        QVERIFY2(box->undo(out.changeId, error), qPrintable(error));
        QVERIFY(host.commandById(QStringLiteral("c_b")));
        QCOMPARE(host.commandById(QStringLiteral("c_a"))->hooks.pre, QStringList{QStringLiteral("c_b")});
        QVERIFY(!box->undo(out.changeId, error)); // só uma vez
    }

    void undoOfACreationRemovesOnlyThatAndKeepsLaterUserEdits()
    {
        const ToolOutcome made = run(QStringLiteral("create_command"),
            {{QStringLiteral("folder_id"), QStringLiteral("f_root")},
             {QStringLiteral("command"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Temp")}, {QStringLiteral("command"), QStringLiteral("x")}}}});
        host.data.commands[0].command = QStringLiteral("edited by the user afterwards");
        QString error;
        QVERIFY(box->undo(made.changeId, error));
        QCOMPARE(host.data.commands.size(), 2);
        QCOMPARE(host.commandById(QStringLiteral("c_a"))->command, QStringLiteral("edited by the user afterwards"));
    }

    void undoOfAnEditRestoresTheOldValue()
    {
        const ToolOutcome out = run(QStringLiteral("update_command"),
            {{QStringLiteral("id"), QStringLiteral("c_a")}, {QStringLiteral("patch"), QJsonObject{{QStringLiteral("command"), QStringLiteral("changed")}}}});
        QCOMPARE(host.commandById(QStringLiteral("c_a"))->command, QStringLiteral("changed"));
        QString error;
        QVERIFY(box->undo(out.changeId, error));
        QCOMPARE(host.commandById(QStringLiteral("c_a"))->command, QStringLiteral("make deploy"));
        QVERIFY(box->changes().first().undone);
    }

    void moveAndDuplicateCommands()
    {
        QVERIFY(!box->plan(call(QStringLiteral("move_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}, {QStringLiteral("folder_id"), QStringLiteral("f_root")}})).valid); // já está lá
        QVERIFY(!run(QStringLiteral("move_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}, {QStringLiteral("folder_id"), QStringLiteral("f_sub")}}).isError);
        QCOMPARE(host.commandById(QStringLiteral("c_a"))->folderId, QStringLiteral("f_sub"));

        const ToolOutcome dup = run(QStringLiteral("duplicate_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}});
        QVERIFY(!dup.isError);
        QCOMPARE(host.data.commands.size(), 3);
        QCOMPARE(parse(dup.content).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Deploy (copy)"));
        run(QStringLiteral("duplicate_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}});
        QCOMPARE(host.data.commands.last().name, QStringLiteral("Deploy (copy 2)"));
    }

    void folderAndRunPreviewsShowWhatElseWouldHappen()
    {
        const ToolPlan folder = box->plan(call(QStringLiteral("create_folder"),
            {{QStringLiteral("name"), QStringLiteral("Ops")}, {QStringLiteral("working_dir"), QStringLiteral("/srv/app")},
             {QStringLiteral("env_vars"), QJsonObject{{QStringLiteral("PORT"), 8080}}}}));
        QVERIFY(folder.detail.contains(QStringLiteral("+ working_dir: /srv/app")));
        QVERIFY(folder.detail.contains(QStringLiteral("+ env.PORT: 8080")));

        host.data.commands[0].hooks.pre = {QStringLiteral("c_b")};
        host.data.commands[0].hooks.post = {QStringLiteral("c_b")};
        const ToolPlan run = box->plan(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}}));
        QVERIFY(run.detail.contains(QStringLiteral("make deploy")));
        QVERIFY(run.detail.contains(QStringLiteral("hooks.pre: Build")));
        QVERIFY(run.detail.contains(QStringLiteral("hooks.post: Build")));
    }

    void createFolderAndNestedPath()
    {
        const ToolOutcome out = run(QStringLiteral("create_folder"),
            {{QStringLiteral("name"), QStringLiteral("Ops")}, {QStringLiteral("parent_id"), QStringLiteral("f_sub")},
             {QStringLiteral("env_vars"), QJsonObject{{QStringLiteral("PORT"), 8080}}}, {QStringLiteral("working_dir"), QStringLiteral("/srv")}});
        QVERIFY2(!out.isError, qPrintable(out.content));
        QCOMPARE(parse(out.content).toObject().value(QStringLiteral("path")).toString(), QStringLiteral("Root/Sub/Ops"));
        const Folder &f = host.data.folders.last();
        QCOMPARE(f.envVars.value(QStringLiteral("PORT")), QStringLiteral("8080"));
        QCOMPARE(f.workingDir, QStringLiteral("/srv"));
        QVERIFY(!box->plan(call(QStringLiteral("create_folder"), {{QStringLiteral("name"), QStringLiteral("X")}, {QStringLiteral("parent_id"), QStringLiteral("zz")}})).valid);
        QVERIFY(!box->plan(call(QStringLiteral("create_folder"), {{QStringLiteral("name"), QString()}})).valid);
    }

    void updateFolderRefusesCyclesAndEmptyNames()
    {
        auto plan = [this](const QJsonObject &patch) {
            return box->plan(call(QStringLiteral("update_folder"), {{QStringLiteral("id"), QStringLiteral("f_root")}, {QStringLiteral("patch"), patch}}));
        };
        QVERIFY(!plan({{QStringLiteral("parent_id"), QStringLiteral("f_sub")}}).valid); // dentro do próprio filho
        QVERIFY(!plan({{QStringLiteral("parent_id"), QStringLiteral("f_root")}}).valid); // dentro de si mesma
        QVERIFY(!plan({{QStringLiteral("name"), QString()}}).valid);
        const ToolPlan ok = plan({{QStringLiteral("name"), QStringLiteral("Renamed")}});
        QVERIFY(ok.valid);
        QVERIFY(ok.detail.contains(QStringLiteral("~ name: Root → Renamed")));
        QVERIFY(!run(QStringLiteral("update_folder"), {{QStringLiteral("id"), QStringLiteral("f_root")}, {QStringLiteral("patch"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Renamed")}}}}).isError);
        QCOMPARE(host.data.folders[0].name, QStringLiteral("Renamed"));
    }

    void deleteFolderNeedsExplicitPermissionToTakeTheContents()
    {
        const ToolPlan refused = box->plan(call(QStringLiteral("delete_folder"), {{QStringLiteral("id"), QStringLiteral("f_root")}}));
        QVERIFY(!refused.valid);
        QVERIFY(refused.error.contains(QStringLiteral("delete_contents")));

        const QJsonObject args{{QStringLiteral("id"), QStringLiteral("f_root")}, {QStringLiteral("delete_contents"), true}};
        const ToolPlan plan = box->plan(call(QStringLiteral("delete_folder"), args));
        QVERIFY(plan.valid);
        QVERIFY(plan.detail.contains(QStringLiteral("1 subfolder")));
        QVERIFY(plan.detail.contains(QStringLiteral("2 command")));
        const ToolOutcome out = run(QStringLiteral("delete_folder"), args);
        QVERIFY(!out.isError);
        QVERIFY(host.data.folders.isEmpty());
        QVERIFY(host.data.commands.isEmpty());

        QString error;
        QVERIFY2(box->undo(out.changeId, error), qPrintable(error));
        QCOMPARE(host.data.folders.size(), 2);
        QCOMPARE(host.data.commands.size(), 2);
    }

    void emptyFoldersCanBeDeletedDirectly()
    {
        host.addFolder(QStringLiteral("f_empty"), QStringLiteral("Empty"));
        QVERIFY(box->plan(call(QStringLiteral("delete_folder"), {{QStringLiteral("id"), QStringLiteral("f_empty")}})).valid);
    }

    void environmentVariablesCanBeSetAndRemovedWithUndo()
    {
        const ToolOutcome set = run(QStringLiteral("set_environment_variable"),
            {{QStringLiteral("name"), QStringLiteral("PORT")}, {QStringLiteral("value"), QStringLiteral("9000")}});
        QVERIFY2(!set.isError, qPrintable(set.content));
        QCOMPARE(host.settingsData.environments[0].vars.value(QStringLiteral("PORT")), QStringLiteral("9000"));
        QVERIFY(!set.changeId.isEmpty());

        const ToolOutcome secret = run(QStringLiteral("set_environment_variable"),
            {{QStringLiteral("environment"), QStringLiteral("dev")}, {QStringLiteral("name"), QStringLiteral("API_KEY")},
             {QStringLiteral("value"), QStringLiteral("k")}, {QStringLiteral("secret"), true}});
        QVERIFY(host.settingsData.environments[0].secretKeys.contains(QStringLiteral("API_KEY")));

        const ToolPlan plan = box->plan(call(QStringLiteral("set_environment_variable"),
            {{QStringLiteral("name"), QStringLiteral("TOKEN")}, {QStringLiteral("value"), QStringLiteral("newvalue")}}));
        QVERIFY(!plan.detail.contains(QStringLiteral("newvalue")));
        QVERIFY(!plan.detail.contains(QStringLiteral("s3cr3t")));

        const ToolOutcome del = run(QStringLiteral("delete_environment_variable"), {{QStringLiteral("name"), QStringLiteral("PORT")}});
        QVERIFY(!del.isError);
        QVERIFY(!host.settingsData.environments[0].vars.contains(QStringLiteral("PORT")));
        QString error;
        QVERIFY(box->undo(del.changeId, error));
        QVERIFY(host.settingsData.environments[0].vars.contains(QStringLiteral("PORT")));

        QVERIFY(!box->plan(call(QStringLiteral("set_environment_variable"), {{QStringLiteral("name"), QStringLiteral("1bad")}, {QStringLiteral("value"), QStringLiteral("x")}})).valid);
        QVERIFY(!box->plan(call(QStringLiteral("set_environment_variable"), {{QStringLiteral("environment"), QStringLiteral("zzz")}, {QStringLiteral("name"), QStringLiteral("A")}, {QStringLiteral("value"), QStringLiteral("x")}})).valid);
        QVERIFY(!box->plan(call(QStringLiteral("delete_environment_variable"), {{QStringLiteral("name"), QStringLiteral("NOPE")}})).valid);
        Q_UNUSED(secret)
    }

    void createNote()
    {
        const ToolOutcome out = run(QStringLiteral("create_note"),
            {{QStringLiteral("folder_id"), QStringLiteral("f_root")}, {QStringLiteral("name"), QStringLiteral("Runbook")}, {QStringLiteral("content"), QStringLiteral("# hi")}});
        QVERIFY(!out.isError);
        QCOMPARE(host.noteList.size(), 1);
        QCOMPARE(host.notesChangedCount, 1);
        QString error;
        QVERIFY(box->undo(out.changeId, error));
        QVERIFY(host.noteList.isEmpty());
    }

    void runInAppOpensWhatRunCommandCannotRun()
    {
        host.data.commands[1].kip = true;
        const ToolPlan refused = box->plan(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_b")}}));
        QVERIFY(!refused.valid);
        QVERIFY(refused.error.contains(QStringLiteral("run_in_app")));
        const ToolPlan plan = box->plan(call(QStringLiteral("run_in_app"), {{QStringLiteral("id"), QStringLiteral("c_b")}}));
        QVERIFY(plan.valid);
        QCOMPARE(plan.risk, ToolRisk::Write);
        QVERIFY(plan.title.contains(QStringLiteral("Build")));
        const ToolOutcome out = run(QStringLiteral("run_in_app"), {{QStringLiteral("id"), QStringLiteral("c_b")}});
        QVERIFY(!out.isError);
        QCOMPARE(host.openedInApp, QStringList{QStringLiteral("c_b")});
        QVERIFY(host.runRequests.isEmpty()); // não passou pelo run_command
        QVERIFY(run(QStringLiteral("run_in_app"), {{QStringLiteral("id"), QStringLiteral("nope")}}).isError);
    }

    void runCommandPlanChecksTheCommand()
    {
        host.data.commands[1].kip = true;
        QVERIFY(!box->plan(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_b")}})).valid);
        host.data.commands[1].kip = false;
        host.data.commands[1].interactiveTerminal = true;
        QVERIFY(!box->plan(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_b")}})).valid);
        host.data.commands[1].interactiveTerminal = false;

        Parameter p;
        p.name = QStringLiteral("ENV");
        p.required = true;
        host.data.commands[0].params << p;
        const ToolPlan missing = box->plan(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}}));
        QVERIFY(!missing.valid);
        QVERIFY(missing.error.contains(QStringLiteral("ENV")));
        const ToolPlan ok = box->plan(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("ENV"), QStringLiteral("prod")}}}}));
        QVERIFY(ok.valid);
        QVERIFY(ok.detail.contains(QStringLiteral("make deploy")));
        QVERIFY(ok.detail.contains(QStringLiteral("ENV = prod")));

        host.running.insert(QStringLiteral("c_b"));
        QVERIFY(!box->plan(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_b")}})).valid);
    }

    void runCommandReturnsTheResult()
    {
        host.nextOutcome.output = QStringLiteral("built ok\n");
        const ToolOutcome out = run(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}, {QStringLiteral("params"), QJsonObject{{QStringLiteral("N"), 3}}}});
        QVERIFY(!out.isError);
        const QJsonObject o = parse(out.content).toObject();
        QCOMPARE(o.value(QStringLiteral("status")).toString(), QStringLiteral("finished"));
        QVERIFY(o.value(QStringLiteral("success")).toBool());
        QCOMPARE(o.value(QStringLiteral("output")).toString(), QStringLiteral("built ok\n"));
        QCOMPARE(host.runRequests, QStringList{QStringLiteral("c_a")});
        QCOMPARE(host.lastRunParams.value(QStringLiteral("N")), QStringLiteral("3"));
    }

    void failedRunsAreErrorsWithTheirOutput()
    {
        host.nextOutcome.success = false;
        host.nextOutcome.exitCode = 2;
        host.nextOutcome.output = QStringLiteral("boom");
        const ToolOutcome out = run(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}});
        QVERIFY(out.isError);
        QCOMPARE(parse(out.content).toObject().value(QStringLiteral("exit_code")).toInt(), 2);
    }

    void aCommandThatCannotStartIsAnError()
    {
        host.nextOutcome.started = false;
        host.nextOutcome.error = QStringLiteral("no shell");
        const ToolOutcome out = run(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}});
        QVERIFY(out.isError);
        QVERIFY(out.content.contains(QStringLiteral("no shell")));
    }

    void longRunsReturnStillRunningAfterTheTimeout()
    {
        host.deferRun = true;
        host.outputs[QStringLiteral("c_a")] = QStringLiteral("partial");
        box->setRunTimeoutMs(50);
        ToolOutcome out;
        bool called = false;
        box->execute(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}}), [&](const ToolOutcome &o) { out = o; called = true; });
        QVERIFY(!called);
        QTRY_VERIFY(called);
        const QJsonObject o = parse(out.content).toObject();
        QCOMPARE(o.value(QStringLiteral("status")).toString(), QStringLiteral("still_running"));
        QCOMPARE(o.value(QStringLiteral("output_so_far")).toString(), QStringLiteral("partial"));
        // O fim tardio não chama o callback de novo.
        host.pendingDone(host.nextOutcome);
        QTest::qWait(20);
    }

    void asyncRunFinishingBeforeTheTimeoutCallsBackOnce()
    {
        host.deferRun = true;
        int calls = 0;
        ToolOutcome out;
        box->execute(call(QStringLiteral("run_command"), {{QStringLiteral("id"), QStringLiteral("c_a")}}), [&](const ToolOutcome &o) { out = o; ++calls; });
        QCOMPARE(calls, 0);
        host.pendingDone(host.nextOutcome);
        QCOMPARE(calls, 1);
        QVERIFY(!out.isError);
        QTest::qWait(30);
        QCOMPARE(calls, 1);
    }

    void killProcessStopsARunningCommand()
    {
        QVERIFY(!box->plan(call(QStringLiteral("kill_process"), {{QStringLiteral("id"), QStringLiteral("c_a")}})).valid); // não está rodando
        host.running.insert(QStringLiteral("c_a"));
        const ToolPlan plan = box->plan(call(QStringLiteral("kill_process"), {{QStringLiteral("id"), QStringLiteral("c_a")}, {QStringLiteral("force"), true}}));
        QVERIFY(plan.valid);
        QVERIFY(plan.title.contains(QStringLiteral("Force-kill")));
        QCOMPARE(plan.risk, ToolRisk::Danger);
        QVERIFY(!run(QStringLiteral("kill_process"), {{QStringLiteral("id"), QStringLiteral("c_a")}}).isError);
        QCOMPARE(host.killed, QStringList{QStringLiteral("c_a")});
    }

    void diffDescribesAddedRemovedAndChangedKeys()
    {
        const QString d = describeJsonDiff(QJsonObject{{QStringLiteral("a"), 1}, {QStringLiteral("b"), 2}, {QStringLiteral("c"), 3}},
                                           QJsonObject{{QStringLiteral("a"), 1}, {QStringLiteral("b"), 5}, {QStringLiteral("d"), 4}});
        QCOMPARE(d, QStringLiteral("~ b: 2 → 5\n- c: 3\n+ d: 4"));
        QVERIFY(describeJsonDiff({}, {}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAgentToolbox)
#include "test_agent_toolbox.moc"
