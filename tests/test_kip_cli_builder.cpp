#include <QTest>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include "core/kip-cli-builder.h"
#include "core/kip-protocol.h"

using namespace kai::core;

namespace {

KipCliResult run(std::initializer_list<const char *> args)
{
    QStringList list;
    for (const char *a : args) list << QString::fromUtf8(a);
    return runKipCli(list);
}

QJsonObject jsonOf(const KipCliResult &r)
{
    return QJsonDocument::fromJson(r.output).object();
}

// A saída de todo verbo tem que ser UMA linha de protocolo válida.
void verifyIsOneValidProtocolLine(const KipCliResult &r)
{
    QCOMPARE(r.exitCode, 0);
    QVERIFY(r.error.isEmpty());
    QVERIFY(r.output.endsWith('\n'));
    QVERIFY(!r.output.contains("\r"));
    QCOMPARE(r.output.count('\n'), 1);
    const KipParseResult parsed = parseKipLine(QString::fromUtf8(r.output));
    QVERIFY2(parsed.kind == KipParseResult::Kind::Message, qPrintable(QString::fromUtf8(r.output)));
    QVERIFY2(parsed.diagnostics.isEmpty(), qPrintable(parsed.diagnostics.join(';')));
}

void verifyUsageError(const KipCliResult &r)
{
    QCOMPARE(r.exitCode, 2);
    QVERIFY(r.output.isEmpty()); // o stdout é o canal do protocolo: nunca recebe lixo
    QVERIFY(!r.error.isEmpty());
}

} // namespace

class TestKipCliBuilder : public QObject {
    Q_OBJECT

private slots:
    // ---- chips (§21) ----
    void promptWithChipsAndTheirModifiers()
    {
        const KipCliResult r = run({"prompt", "--id", "pick",
                                    "--field", "list", "branch", "--options", "a,b",
                                    "--chip", "ctx", "Context", "--chip-description", "Ticket", "--chip-icon", "info",
                                    "--chip-requires", "branch,other",
                                    "--chip", "nuke", "--chip-danger", "--chip-confirm-text", "Sure?",
                                    "--chip-confirm-label", "Delete", "--chip-cancel-label", "Keep",
                                    "--chip", "sync", "--chip-confirm"});
        verifyIsOneValidProtocolLine(r);
        const QJsonArray chips = jsonOf(r).value("chips").toArray();
        QCOMPARE(chips.size(), 3);
        QCOMPARE(chips.at(0).toObject().value("label").toString(), QStringLiteral("Context"));
        QCOMPARE(chips.at(0).toObject().value("icon").toString(), QStringLiteral("info"));
        QCOMPARE(chips.at(0).toObject().value("requires").toArray().size(), 2);
        QVERIFY(!chips.at(0).toObject().contains("confirm"));
        QVERIFY(chips.at(1).toObject().value("danger").toBool());
        const QJsonObject confirm = chips.at(1).toObject().value("confirm").toObject();
        QCOMPARE(confirm.value("text").toString(), QStringLiteral("Sure?"));
        QCOMPARE(confirm.value("confirm_label").toString(), QStringLiteral("Delete"));
        QCOMPARE(confirm.value("cancel_label").toString(), QStringLiteral("Keep"));
        QCOMPARE(chips.at(2).toObject().value("confirm").toBool(), true); // --chip-confirm sozinho
    }

    void chipModifierWithoutAChipIsAUsageError()
    {
        verifyUsageError(run({"prompt", "--id", "p", "--chip-danger"}));
        verifyUsageError(run({"prompt", "--id", "p", "--chip"}));
        verifyUsageError(run({"prompt", "--id", "p", "--chip", "x", "--chip-requires"}));
    }

    void patchReplacesOrClearsTheChips()
    {
        const KipCliResult set = run({"patch", "--id", "p", "--seq", "2", "--chip", "only", "Only"});
        verifyIsOneValidProtocolLine(set);
        QCOMPARE(jsonOf(set).value("chips").toArray().size(), 1);
        const KipCliResult cleared = run({"patch", "--id", "p", "--seq", "3", "--no-chips"});
        verifyIsOneValidProtocolLine(cleared);
        QVERIFY(jsonOf(cleared).contains("chips"));
        QVERIFY(jsonOf(cleared).value("chips").toArray().isEmpty());
        const KipCliResult untouched = run({"patch", "--id", "p", "--seq", "4"});
        QVERIFY(!jsonOf(untouched).contains("chips"));
    }

    void chipResultVerb()
    {
        const KipCliResult r = run({"chip-result", "ctx", "success", "line one", "line two", "--title", "Ticket"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("type").toString(), QStringLiteral("chip_result"));
        QCOMPARE(jsonOf(r).value("chip").toString(), QStringLiteral("ctx"));
        QCOMPARE(jsonOf(r).value("state").toString(), QStringLiteral("success"));
        QCOMPARE(jsonOf(r).value("title").toString(), QStringLiteral("Ticket"));
        QCOMPARE(jsonOf(r).value("text").toString(), QStringLiteral("line one line two"));
        const KipCliResult scoped = run({"chip-result", "ctx", "running", "--id", "pick"});
        verifyIsOneValidProtocolLine(scoped);
        QCOMPARE(jsonOf(scoped).value("id").toString(), QStringLiteral("pick"));
        verifyUsageError(run({"chip-result", "ctx"}));
        verifyUsageError(run({"chip-result", "ctx", "done"}));
        verifyUsageError(run({"chip-result", "ctx", "success", "--nope"}));
    }

    // ---- list/table: filtro e paginação ----
    void listSearchAndPageSizeOptions()
    {
        const KipCliResult r = run({"prompt", "--id", "p", "--field", "list", "branch", "Branch", "--options", "a,b,c",
                                    "--page-size", "10", "--search",
                                    "--field", "list", "other", "--options", "x", "--no-search"});
        verifyIsOneValidProtocolLine(r);
        const QJsonArray fields = jsonOf(r).value("fields").toArray();
        QCOMPARE(fields.at(0).toObject().value("page_size").toInt(), 10);
        QCOMPARE(fields.at(0).toObject().value("searchable").toBool(), true);
        QCOMPARE(fields.at(1).toObject().value("searchable").toBool(true), false);
        QVERIFY(!fields.at(1).toObject().contains("page_size"));
    }

    void pageSizeNeedsANumber()
    {
        verifyUsageError(run({"prompt", "--id", "p", "--field", "list", "l", "--page-size", "many"}));
    }

    // ---- hello / confirm / progress / ... ----
    void helloWithTitleAndVersion()
    {
        const KipCliResult r = run({"hello", "--title", "Deploy", "--version", "2.3.0"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("type").toString(), QStringLiteral("hello"));
        QCOMPARE(jsonOf(r).value("title").toString(), QStringLiteral("Deploy"));
        QCOMPARE(jsonOf(r).value("version").toString(), QStringLiteral("2.3.0"));
        QCOMPARE(jsonOf(r).value("kip").toInt(), 1);
    }

    void bareHelloIsValid()
    {
        verifyIsOneValidProtocolLine(run({"hello"}));
    }

    void confirmBuildsAllFlags()
    {
        const KipCliResult r = run({"confirm", "--id", "sure", "--title", "Deploy?", "--text", "Really?", "--danger",
                                    "--confirm-label", "Yes", "--cancel-label", "No", "--back", "--no-cancel"});
        verifyIsOneValidProtocolLine(r);
        const QJsonObject o = jsonOf(r);
        QCOMPARE(o.value("danger").toBool(), true);
        QCOMPARE(o.value("confirm_label").toString(), QStringLiteral("Yes"));
        QCOMPARE(o.value("cancel_label").toString(), QStringLiteral("No"));
        QCOMPARE(o.value("back").toBool(), true);
        QCOMPARE(o.value("cancellable").toBool(), false);
    }

    void confirmNeedsIdAndText()
    {
        verifyUsageError(run({"confirm", "--text", "x"}));
        verifyUsageError(run({"confirm", "--id", "x"}));
    }

    void progressValueLabelAndIndeterminate()
    {
        KipCliResult r = run({"progress", "30", "Uploading..."});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("value").toDouble(), 30.0);
        QCOMPARE(jsonOf(r).value("label").toString(), QStringLiteral("Uploading..."));
        r = run({"progress", "null", "Working", "--no-cancel"});
        verifyIsOneValidProtocolLine(r);
        QVERIFY(jsonOf(r).value("value").isNull());
        QCOMPARE(jsonOf(r).value("cancellable").toBool(), false);
        QCOMPARE(jsonOf(run({"progress", "250"})).value("value").toDouble(), 100.0);
        verifyUsageError(run({"progress", "abc"}));
        verifyUsageError(run({"progress"}));
    }

    void messageWithAndWithoutLevel()
    {
        KipCliResult r = run({"message", "warning", "Careful"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("level").toString(), QStringLiteral("warning"));
        QCOMPARE(jsonOf(r).value("text").toString(), QStringLiteral("Careful"));
        r = run({"message", "Plain text"});
        QCOMPARE(jsonOf(r).value("level").toString(), QStringLiteral("info"));
        r = run({"message", "--level", "error", "boom"});
        QCOMPARE(jsonOf(r).value("level").toString(), QStringLiteral("error"));
        verifyUsageError(run({"message", "--level", "fatal", "x"}));
        verifyUsageError(run({"message"}));
    }

    void markdownJoinsWordsAndReadsFiles()
    {
        verifyIsOneValidProtocolLine(run({"markdown", "# Title"}));
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("a.md")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# From file\n\nbody");
        f.close();
        const KipCliResult r = runKipCli({QStringLiteral("markdown"), QStringLiteral("--file"), f.fileName()});
        verifyIsOneValidProtocolLine(r);
        QVERIFY(jsonOf(r).value("text").toString().contains(QStringLiteral("From file")));
        verifyUsageError(runKipCli({QStringLiteral("markdown"), QStringLiteral("--file"), dir.filePath(QStringLiteral("missing.md"))}));
    }

    void stepsAndStep()
    {
        KipCliResult r = run({"steps", "--id", "setup", "--title", "Setup", "--item", "a", "Install", "--state", "running",
                              "--detail", "wait", "--item", "b"});
        verifyIsOneValidProtocolLine(r);
        const QJsonArray items = jsonOf(r).value("items").toArray();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).toObject().value("label").toString(), QStringLiteral("Install"));
        QCOMPARE(items.at(0).toObject().value("state").toString(), QStringLiteral("running"));
        QCOMPARE(items.at(0).toObject().value("detail").toString(), QStringLiteral("wait"));
        QCOMPARE(items.at(1).toObject().value("label").toString(), QStringLiteral("b")); // sem rótulo = id
        r = run({"step", "setup", "a", "success", "done in 3s"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("steps").toString(), QStringLiteral("setup"));
        QCOMPARE(jsonOf(r).value("detail").toString(), QStringLiteral("done in 3s"));
        verifyUsageError(run({"step", "setup", "a", "weird"}));
        verifyUsageError(run({"step", "setup"}));
        verifyUsageError(run({"steps", "--item", "a"})); // sem --id
        verifyUsageError(run({"steps", "--id", "s", "--state", "running"})); // --state sem --item
    }

    void tableWithColumnsAndRows()
    {
        const KipCliResult r = run({"table", "--id", "t", "--title", "Backups", "--column", "name:Name", "--column", "size",
                                    "--rows-json", "[{\"name\":\"a.sql\",\"size\":\"1MB\"}]"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("columns").toArray().size(), 2);
        QCOMPARE(jsonOf(r).value("rows").toArray().size(), 1);
        verifyUsageError(run({"table", "--rows-json", "[]"})); // sem colunas
        verifyUsageError(run({"table", "--column", "a", "--rows-json", "{\"a\":1}"}));
        verifyUsageError(run({"table", "--column", "a", "--rows-json", "not json"}));
    }

    void notifySetEnvAndInvalid()
    {
        KipCliResult r = run({"notify", "Done", "All set", "--level", "success"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("title").toString(), QStringLiteral("Done"));
        QCOMPARE(jsonOf(r).value("text").toString(), QStringLiteral("All set"));
        r = run({"set-env", "TOKEN", "abc 123"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("type").toString(), QStringLiteral("set_env"));
        QCOMPARE(jsonOf(r).value("value").toString(), QStringLiteral("abc 123"));
        r = run({"invalid", "--id", "p", "--error", "name=taken", "--error", "tag=bad=tag", "--message", "Fix it"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("errors").toObject().value("name").toString(), QStringLiteral("taken"));
        QCOMPARE(jsonOf(r).value("errors").toObject().value("tag").toString(), QStringLiteral("bad=tag"));
        verifyUsageError(run({"invalid", "--id", "p", "--error", "noequals"}));
    }

    void doneWithActionsKeepsColonsInTheValue()
    {
        const KipCliResult r = run({"done", "--title", "Deployed", "--level", "warning", "--action",
                                    "open_url:Open:https://example.com:8080/x", "--action", "copy:Copy ID:abc",
                                    "--action", "reveal:Show:C:\\out", "--path-format", "windows"});
        verifyIsOneValidProtocolLine(r);
        const QJsonArray actions = jsonOf(r).value("actions").toArray();
        QCOMPARE(actions.size(), 3);
        QCOMPARE(actions.at(0).toObject().value("url").toString(), QStringLiteral("https://example.com:8080/x"));
        QCOMPARE(actions.at(1).toObject().value("value").toString(), QStringLiteral("abc"));
        QCOMPARE(actions.at(2).toObject().value("path").toString(), QStringLiteral("C:\\out"));
        QCOMPARE(actions.at(2).toObject().value("path_format").toString(), QStringLiteral("windows"));
        verifyUsageError(run({"done", "--action", "run:Boom:rm -rf /"}));
        verifyUsageError(run({"done", "--action", "nolabel"}));
        verifyUsageError(run({"done", "--path-format", "posix"})); // sem --action antes
    }

    // ---- prompt / patch com campos ----
    void promptWithTheSpecExample()
    {
        const KipCliResult r = run({"prompt", "--id", "env", "--title", "Where to?",
                                    "--field", "select", "env", "Environment", "--options", "dev,staging,prod", "--required",
                                    "--field", "flags", "opts", "Options", "--flag", "force:Force", "--flag", "dry:Dry run",
                                    "--field", "folderpick", "dir", "Build folder"});
        verifyIsOneValidProtocolLine(r);
        const QJsonArray fields = jsonOf(r).value("fields").toArray();
        QCOMPARE(fields.size(), 3);
        const QJsonObject env = fields.at(0).toObject();
        QCOMPARE(env.value("name").toString(), QStringLiteral("env"));
        QCOMPARE(env.value("label").toString(), QStringLiteral("Environment"));
        QCOMPARE(env.value("required").toBool(), true);
        QCOMPARE(env.value("options").toArray().size(), 3);
        QCOMPARE(fields.at(1).toObject().value("options").toArray().size(), 2);
        QVERIFY(!fields.at(2).toObject().contains("required")); // modificador só vale para o campo anterior
    }

    void fieldModifiersApplyToTheLastField()
    {
        const KipCliResult r = run({"prompt", "--id", "p", "--description", "About this prompt", "--no-remember", "--back", "--submit-label", "Go",
                                    "--field", "number", "n", "Count", "--min", "1", "--max", "9", "--step", "2", "--decimals", "1",
                                    "--default", "3", "--placeholder", "how many", "--description", "field desc", "--group", "G", "--watch",
                                    "--no-remember",
                                    "--field", "date", "d", "--mode", "datetime", "--range",
                                    "--field", "list", "l", "--multiple", "--option", "a:Alpha:first one", "--option", "b", "--default", "a,b",
                                    "--field", "filepick", "f", "--filter", "*.sql", "--initial-dir", "/tmp", "--path-format", "posix"});
        verifyIsOneValidProtocolLine(r);
        const QJsonObject p = jsonOf(r);
        // Antes do 1º --field, --description/--no-remember são do PROMPT.
        QCOMPARE(p.value("description").toString(), QStringLiteral("About this prompt"));
        QCOMPARE(p.value("remember").toBool(true), false);
        QCOMPARE(p.value("back").toBool(), true);
        QCOMPARE(p.value("submit_label").toString(), QStringLiteral("Go"));
        const QJsonArray fields = p.value("fields").toArray();
        const QJsonObject n = fields.at(0).toObject();
        QCOMPARE(n.value("min").toDouble(), 1.0);
        QCOMPARE(n.value("max").toDouble(), 9.0);
        QCOMPARE(n.value("step").toDouble(), 2.0);
        QCOMPARE(n.value("decimals").toInt(), 1);
        QCOMPARE(n.value("default").toDouble(), 3.0);
        QCOMPARE(n.value("placeholder").toString(), QStringLiteral("how many"));
        QCOMPARE(n.value("description").toString(), QStringLiteral("field desc"));
        QCOMPARE(n.value("group").toString(), QStringLiteral("G"));
        QCOMPARE(n.value("watch").toBool(), true);
        QCOMPARE(n.value("remember").toBool(true), false);
        const QJsonObject d = fields.at(1).toObject();
        QCOMPARE(d.value("mode").toString(), QStringLiteral("datetime"));
        QCOMPARE(d.value("range").toBool(), true);
        QVERIFY(!d.contains("min"));
        const QJsonObject l = fields.at(2).toObject();
        QCOMPARE(l.value("multiple").toBool(), true);
        QCOMPARE(l.value("default").toArray().size(), 2);
        QCOMPARE(l.value("options").toArray().at(0).toObject().value("label").toString(), QStringLiteral("Alpha"));
        QCOMPARE(l.value("options").toArray().at(0).toObject().value("description").toString(), QStringLiteral("first one"));
        const QJsonObject f = fields.at(3).toObject();
        QCOMPARE(f.value("filter").toString(), QStringLiteral("*.sql"));
        QCOMPARE(f.value("initial_dir").toString(), QStringLiteral("/tmp"));
        QCOMPARE(f.value("path_format").toString(), QStringLiteral("posix"));
    }

    void tableFieldAndFlagDefaults()
    {
        const KipCliResult r = run({"prompt", "--id", "p",
                                    "--field", "table", "backup", "Backup", "--column", "name:Name", "--row-key", "name",
                                    "--rows-json", "[{\"name\":\"x\"},{\"name\":\"y\"}]", "--multiple", "--default", "x",
                                    "--field", "flags", "o", "--flag", "a:A:true", "--flag", "b:B:false", "--flag", "c:C", "--default", "b"});
        verifyIsOneValidProtocolLine(r);
        const QJsonArray fields = jsonOf(r).value("fields").toArray();
        const QJsonObject table = fields.at(0).toObject();
        QCOMPARE(table.value("row_key").toString(), QStringLiteral("name"));
        QCOMPARE(table.value("rows").toArray().size(), 2);
        QCOMPARE(table.value("multiple").toBool(), true);
        const QJsonObject flags = fields.at(1).toObject();
        // O default da própria flag (a:A:true) continua lá; `--default b` acrescenta
        // o mapa do CAMPO (só b ligada), que sobrepõe os defaults das flags.
        QCOMPARE(flags.value("options").toArray().at(0).toObject().value("default").toBool(), true);
        QCOMPARE(flags.value("options").toArray().size(), 3);
        QCOMPARE(flags.value("default").toObject().value("b").toBool(), true);
        QCOMPARE(flags.value("default").toObject().value("a").toBool(), false);
    }

    void promptWithoutFieldsIsAnAcknowledgeScreen()
    {
        const KipCliResult r = run({"prompt", "--id", "ack", "--title", "Ready?", "--submit-label", "Start"});
        verifyIsOneValidProtocolLine(r);
        QVERIFY(jsonOf(r).value("fields").toArray().isEmpty());
    }

    void patchBuildsFieldsAndRemovals()
    {
        const KipCliResult r = run({"patch", "--id", "k8s", "--seq", "7", "--field", "select", "pod", "Pod", "--options", "p1,p2",
                                    "--remove", "ns,old"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("seq").toInt(), 7);
        QCOMPARE(jsonOf(r).value("fields").toArray().size(), 1);
        QCOMPARE(jsonOf(r).value("remove").toArray().size(), 2);
        // Sem --seq o patch é espontâneo (um chip repintando a tabela): sem a chave seq na mensagem.
        const KipCliResult spontaneous = run({"patch", "--id", "k8s", "--field", "list", "pod", "--options", "p1"});
        verifyIsOneValidProtocolLine(spontaneous);
        QVERIFY(!jsonOf(spontaneous).contains("seq"));
        QCOMPARE(jsonOf(spontaneous).value("fields").toArray().size(), 1);
        verifyUsageError(run({"patch", "--seq", "1"}));  // sem --id
    }

    // `message success` sozinho é o TEXTO "success" (a palavra só vira nível se vier texto depois).
    void aLoneLevelWordIsTheTextAndKeepsTheDefaultLevel()
    {
        const KipCliResult alone = run({"message", "success"});
        verifyIsOneValidProtocolLine(alone);
        QCOMPARE(jsonOf(alone).value("text").toString(), QStringLiteral("success"));
        QCOMPARE(jsonOf(alone).value("level").toString(), QStringLiteral("info"));
        const KipCliResult withText = run({"message", "success", "all good"});
        QCOMPARE(jsonOf(withText).value("level").toString(), QStringLiteral("success"));
        QCOMPARE(jsonOf(withText).value("text").toString(), QStringLiteral("all good"));
    }

    // ---- uso inválido: nada no stdout, exit 2 ----
    void invalidUsageNeverWritesToStdout()
    {
        verifyUsageError(run({"prompt"}));                                           // sem --id
        verifyUsageError(run({"prompt", "--id", "p", "--field", "hologram", "x"})); // tipo desconhecido
        verifyUsageError(run({"prompt", "--id", "p", "--field", "text"}));          // sem nome
        verifyUsageError(run({"prompt", "--id", "p", "--options", "a,b"}));         // modificador sem --field
        verifyUsageError(run({"prompt", "--id", "p", "--field", "text", "t", "--bogus"}));
        verifyUsageError(run({"prompt", "--id", "p", "--field", "number", "n", "--min", "abc"}));
        verifyUsageError(run({"prompt", "--id", "p", "--field", "text", "t", "--default"})); // falta valor
        verifyUsageError(run({"prompt", "--id", "p", "--field", "select", "s", "--option", ":nolabel"}));
        verifyUsageError(run({"nope"}));
        verifyUsageError(runKipCli({}));
    }

    void errorMessageNamesTheProblem()
    {
        QVERIFY(run({"prompt", "--id", "p", "--field", "hologram", "x"}).error.contains(QStringLiteral("hologram")));
        QVERIFY(run({"prompt", "--id", "p", "--options", "a"}).error.contains(QStringLiteral("--options")));
        QVERIFY(run({"nope"}).error.contains(QStringLiteral("nope")));
    }

    // ---- raw ----
    void rawValidatesAndNormalizes()
    {
        KipCliResult r = run({"raw", "{ \"type\" : \"message\", \"kip\":1, \"level\":\"info\", \"text\":\"hi\" }"});
        verifyIsOneValidProtocolLine(r);
        QCOMPARE(jsonOf(r).value("text").toString(), QStringLiteral("hi"));
        verifyUsageError(run({"raw", "not json"}));
        verifyUsageError(run({"raw", "{\"type\":\"message\"}"}));                 // sem "kip"
        verifyUsageError(run({"raw", "{\"kip\":1,\"type\":\"teleport\"}"}));      // tipo desconhecido
        verifyUsageError(run({"raw", "{\"kip\":1,\"type\":\"prompt\"}"}));        // prompt sem id
        verifyUsageError(run({"raw"}));
    }

    // ---- get ----
    void getPrintsStringsRawAndOtherTypesAsLiterals()
    {
        const QString resp = QStringLiteral(
            "{\"kip\":1,\"type\":\"response\",\"id\":\"env\",\"values\":{\"env\":\"prod\",\"n\":3.5,\"count\":7,"
            "\"flag\":true,\"nothing\":null,\"tags\":[\"a\",\"b c\"],\"opts\":{\"force\":true,\"dry\":false},\"empty\":[],"
            "\"range\":{\"start\":\"2024-01-01\",\"end\":\"2024-01-31\"},\"text\":\"line1\\nline2\",\"acc\":\"ação\"}}");
        const auto get = [&resp](const QString &path) { return kipCliGet(resp, path); };

        QCOMPARE(get(QStringLiteral("type")).output, QByteArray("response\n"));
        QCOMPARE(get(QStringLiteral("values.env")).output, QByteArray("prod\n"));
        QCOMPARE(get(QStringLiteral("values.n")).output, QByteArray("3.5\n"));
        QCOMPARE(get(QStringLiteral("values.count")).output, QByteArray("7\n"));
        QCOMPARE(get(QStringLiteral("values.flag")).output, QByteArray("true\n"));
        QCOMPARE(get(QStringLiteral("values.opts.dry")).output, QByteArray("false\n"));
        QCOMPARE(get(QStringLiteral("values.nothing")).output, QByteArray("null\n"));
        QCOMPARE(get(QStringLiteral("values.tags")).output, QByteArray("a\nb c\n")); // um item por linha
        QCOMPARE(get(QStringLiteral("values.tags.1")).output, QByteArray("b c\n"));
        QCOMPARE(get(QStringLiteral("values.empty")).output, QByteArray());          // array vazio: nada
        QCOMPARE(get(QStringLiteral("values.empty")).exitCode, 0);
        QCOMPARE(get(QStringLiteral("values.opts")).output, QByteArray("{\"dry\":false,\"force\":true}\n"));
        QCOMPARE(get(QStringLiteral("values.range.end")).output, QByteArray("2024-01-31\n"));
        QCOMPARE(get(QStringLiteral("values.text")).output, QByteArray("line1\nline2\n"));
        QCOMPARE(get(QStringLiteral("values.acc")).output, QString::fromUtf8("ação\n").toUtf8());
    }

    void getMissingPathPrintsNothingAndExitsOne()
    {
        const QString resp = QStringLiteral("{\"kip\":1,\"type\":\"response\",\"values\":{\"a\":\"x\",\"l\":[1]}}");
        for (const char *path : {"values.zzz", "nope.deeper", "values.a.b", "values.l.5", "values.l.x"}) {
            const KipCliResult r = kipCliGet(resp, QString::fromLatin1(path));
            QVERIFY2(r.output.isEmpty(), path);
            QCOMPARE(r.exitCode, 1);
        }
    }

    void getRejectsInvalidJsonAsUsageError()
    {
        verifyUsageError(kipCliGet(QStringLiteral("not json"), QStringLiteral("a")));
        verifyUsageError(run({"get", "onlyjson"}));
        verifyUsageError(run({"get"}));
    }

    void getGivesTheProgramARealisticShellPipeline()
    {
        // O `resp` que o script lê é a linha que o Kai escreve no stdin.
        const QByteArray line = kipSerializeResponse(QStringLiteral("env"),
            QJsonObject{{"env", "prod"}, {"opts", QJsonObject{{"dry", true}, {"force", false}}}});
        const QString resp = QString::fromUtf8(line).trimmed();
        QCOMPARE(kipCliGet(resp, QStringLiteral("values.env")).output, QByteArray("prod\n"));
        QCOMPARE(kipCliGet(resp, QStringLiteral("values.opts.dry")).output, QByteArray("true\n"));
        QCOMPARE(kipCliGet(resp, QStringLiteral("type")).output, QByteArray("response\n"));
    }

    void helpListsEveryVerb()
    {
        const KipCliResult r = run({"--help"});
        QCOMPARE(r.exitCode, 0);
        const QString text = QString::fromUtf8(r.output);
        for (const char *verb : {"hello", "prompt", "confirm", "patch", "invalid", "message", "markdown", "progress",
                                 "steps", "step", "table", "notify", "set-env", "done", "raw", "get"}) {
            QVERIFY2(text.contains(QString::fromLatin1(verb)), verb);
        }
        QCOMPARE(run({"help"}).output, r.output);
        QCOMPARE(run({"-h"}).output, r.output);
    }

    // ---- ponta a ponta com o binário `kai` (offline, sem app, sem config) ----
    void theRealBinaryIsPureOfflineAndWritesOnlyNewlines()
    {
        const QString kai = QCoreApplication::applicationDirPath() + QStringLiteral("/kai");
        if (!QFile::exists(kai)) {
            QSKIP("binário kai não encontrado ao lado do teste");
        }
        QTemporaryDir emptyHome; // nenhuma config, nenhum app rodando
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HOME"), emptyHome.path());
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), emptyHome.path());
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), emptyHome.path());
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));

        const auto exec = [&](const QStringList &args, QByteArray *out, QByteArray *err) {
            QProcess p;
            p.setProcessEnvironment(env);
            p.start(kai, args);
            if (!p.waitForFinished(15000)) return -1;
            *out = p.readAllStandardOutput();
            *err = p.readAllStandardError();
            return p.exitCode();
        };

        QByteArray out, err;
        QCOMPARE(exec({QStringLiteral("kip"), QStringLiteral("hello"), QStringLiteral("--title"), QStringLiteral("Deploy")}, &out, &err), 0);
        QCOMPARE(QJsonDocument::fromJson(out).object().value("title").toString(), QStringLiteral("Deploy"));
        QVERIFY(out.endsWith('\n'));
        QVERIFY(!out.contains('\r'));
        QVERIFY(err.isEmpty());
        // Nada criado na config vazia: o helper não lê nem escreve configuração.
        QVERIFY(QDir(emptyHome.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());

        QCOMPARE(exec({QStringLiteral("kip"), QStringLiteral("prompt")}, &out, &err), 2);
        QVERIFY(out.isEmpty());
        QVERIFY(!err.isEmpty());

        QCOMPARE(exec({QStringLiteral("kip"), QStringLiteral("get"), QStringLiteral("{\"values\":{\"a\":\"x\"}}"),
                       QStringLiteral("values.a")}, &out, &err), 0);
        QCOMPARE(out, QByteArray("x\n"));
        QCOMPARE(exec({QStringLiteral("kip"), QStringLiteral("get"), QStringLiteral("{\"values\":{}}"),
                       QStringLiteral("values.zzz")}, &out, &err), 1);
        QVERIFY(out.isEmpty());
    }
};

QTEST_MAIN(TestKipCliBuilder)
#include "test_kip_cli_builder.moc"
