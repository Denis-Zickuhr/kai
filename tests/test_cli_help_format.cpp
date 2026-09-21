#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "cli/cli-help-format.h"
#include "cli/cli-local-executor.h"

using namespace kai;
using namespace kai::core;

// Ajuda do CLI (pedido do usuário: listar uma pasta tem que ajudar de
// verdade — parâmetros, opcionais, descrições) e as flags -g/-d.
class TestCliHelpFormat : public QObject {
    Q_OBJECT

private:
    static Command buildUpCommand()
    {
        Command up;
        up.id = QStringLiteral("c_up");
        up.name = QStringLiteral("Subir container");
        up.cliPath = QStringLiteral("up");
        up.description = QStringLiteral("Sobe o ambiente de dev");
        Parameter service;
        service.name = QStringLiteral("servico");
        service.type = ParameterType::Select;
        service.options = {QStringLiteral("API:api"), QStringLiteral("web")};
        service.description = QStringLiteral("Qual serviço subir");
        Parameter build;
        build.name = QStringLiteral("build");
        build.type = ParameterType::Bool;
        build.optional = true;
        build.defaultValue = QStringLiteral("false");
        build.description = QStringLiteral("Recompila as imagens antes");
        Parameter tag;
        tag.name = QStringLiteral("tag");
        tag.optional = true;
        up.params = {service, build, tag};
        return up;
    }

private slots:
    void usageShowsRequiredPositionalsAndOptionalFlags()
    {
        QCOMPARE(cli::formatCommandUsage(buildUpCommand(), {QStringLiteral("dev"), QStringLiteral("up")}),
                 QStringLiteral("kai dev up <servico> [--build=true|false] [--tag=<value>]"));
    }

    // `kai dev` lista cada comando com descrição, uso e CADA parâmetro
    // (obrigatório/opcional, opções, padrão e descrição).
    void folderListingDocumentsEveryParameter()
    {
        const Command up = buildUpCommand();
        CliPathChildEntry upEntry;
        upEntry.cliPath = up.cliPath;
        upEntry.label = up.name;
        upEntry.targetId = up.id;
        CliPathChildEntry tools;
        tools.cliPath = QStringLiteral("tools");
        tools.label = QStringLiteral("Ferramentas");
        tools.description = QStringLiteral("utilitários");
        tools.isFolder = true;

        const QString listing = cli::formatCliListing({tools, upEntry}, {up}, {QStringLiteral("dev")});
        QVERIFY2(listing.contains(QStringLiteral("tools     Ferramentas — utilitários")), qPrintable(listing));
        QVERIFY(listing.contains(QStringLiteral("Sobe o ambiente de dev")));
        QVERIFY(listing.contains(QStringLiteral("kai dev up <servico> [--build=true|false] [--tag=<value>]")));
        QVERIFY(listing.contains(QStringLiteral("<servico>")));
        QVERIFY(listing.contains(QStringLiteral("choices: api, web")));
        QVERIFY(listing.contains(QStringLiteral("Qual serviço subir")));
        QVERIFY(listing.contains(QStringLiteral("--build")));
        QVERIFY(listing.contains(QStringLiteral("default: false")));
        QVERIFY(listing.contains(QStringLiteral("Recompila as imagens antes")));
        // Pastas antes dos comandos.
        QVERIFY(listing.indexOf(QStringLiteral("tools")) < listing.indexOf(QStringLiteral("Subir container")));
    }

    void commandHelpHasUsageDescriptionAndParams()
    {
        const QString help = cli::formatCommandHelp(buildUpCommand(), {QStringLiteral("dev"), QStringLiteral("up")});
        QVERIFY(help.startsWith(QStringLiteral("Usage: kai dev up <servico>")));
        QVERIFY(help.contains(QStringLiteral("Sobe o ambiente de dev")));
        QVERIFY(help.contains(QStringLiteral("Recompila as imagens antes")));
    }

    void newFlagsParse()
    {
        cli::CliFlags a;
        cli::stripLeadingCliFlags({QStringLiteral("kai"), QStringLiteral("-gdn"), QStringLiteral("x")}, a);
        QVERIFY(a.global && a.detached && a.notify);
        cli::CliFlags b;
        const QStringList rest = cli::stripLeadingCliFlags(
            {QStringLiteral("kai"), QStringLiteral("--dry-run"), QStringLiteral("--json"), QStringLiteral("x")}, b);
        QVERIFY(b.dryRun && b.json && !b.global);
        QCOMPARE(rest, (QStringList{QStringLiteral("kai"), QStringLiteral("x")}));
    }

    // -w/--window: abre a saída numa janela do app; combina com as demais.
    void windowFlagParses()
    {
        const QString exe = QStringLiteral("kai");
        cli::CliFlags a;
        QCOMPARE(cli::stripLeadingCliFlags({exe, QStringLiteral("-gw"), QStringLiteral("x")}, a),
                 (QStringList{exe, QStringLiteral("x")}));
        QVERIFY(a.global && a.window && !a.detached);
        QVERIFY(a.any());

        cli::CliFlags b;
        cli::stripLeadingCliFlags({exe, QStringLiteral("--global"), QStringLiteral("--window"), QStringLiteral("x")}, b);
        QVERIFY(b.global && b.window);

        cli::CliFlags c;
        cli::stripLeadingCliFlags({exe, QStringLiteral("-gdnw"), QStringLiteral("x")}, c);
        QVERIFY(c.global && c.detached && c.notify && c.window);

        // Depois do caminho, -w é parâmetro do comando.
        cli::CliFlags d;
        cli::stripLeadingCliFlags({exe, QStringLiteral("x"), QStringLiteral("-w")}, d);
        QVERIFY(!d.window);
    }

    void dryRunShowsRenderedCommandTargetAndHooks()
    {
        cli::DryRunPreview p;
        p.name = QStringLiteral("Subir container");
        p.pathTokens = {QStringLiteral("dev"), QStringLiteral("up")};
        p.type = QStringLiteral("shell");
        p.command = QStringLiteral("docker compose up api");
        p.workingDir = QStringLiteral("/home/corin/projects/kai");
        p.target = QStringLiteral("WSL");
        p.preHooks = {QStringLiteral("Login")};
        const QString text = cli::formatDryRun(p);
        QVERIFY2(text.contains(QStringLiteral("Dry run")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("(kai dev up)")));
        QVERIFY(text.contains(QStringLiteral("WSL")));
        QVERIFY(text.contains(QStringLiteral("/home/corin/projects/kai")));
        QVERIFY(text.contains(QStringLiteral("Login")));
        QVERIFY(text.contains(QStringLiteral("  docker compose up api")));

        const QJsonObject json = QJsonDocument::fromJson(cli::formatDryRunJson(p).toUtf8()).object();
        QCOMPARE(json.value(QStringLiteral("command")).toString(), QStringLiteral("docker compose up api"));
        QCOMPARE(json.value(QStringLiteral("target")).toString(), QStringLiteral("WSL"));
    }

    void dryRunOfACodeCommandShowsTheCodeAndTheInterpreter()
    {
        cli::DryRunPreview p;
        p.name = QStringLiteral("Report");
        p.pathTokens = {QStringLiteral("report")};
        p.type = QStringLiteral("command");
        p.command = QStringLiteral("print(f\"{{x}}\")");
        p.language = QStringLiteral("python");
        p.interpreter = QStringLiteral("uv run python");
        const QString text = cli::formatDryRun(p);
        QVERIFY2(text.contains(QStringLiteral("python (uv run python)")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("  print(f\"{{x}}\")"))); // o código, sem interpolar
        const QJsonObject json = QJsonDocument::fromJson(cli::formatDryRunJson(p).toUtf8()).object();
        QCOMPARE(json.value(QStringLiteral("language")).toString(), QStringLiteral("python"));
        QCOMPARE(json.value(QStringLiteral("interpreter")).toString(), QStringLiteral("uv run python"));

        // Nativo: nenhuma linha de linguagem.
        p.language.clear();
        p.interpreter.clear();
        QVERIFY(!cli::formatDryRun(p).contains(QStringLiteral("python")));
        QVERIFY(!QJsonDocument::fromJson(cli::formatDryRunJson(p).toUtf8()).object().contains(QStringLiteral("language")));
    }

    void listingAsJsonCarriesParamsForScripts()
    {
        const Command up = buildUpCommand();
        CliPathChildEntry entry;
        entry.cliPath = up.cliPath;
        entry.label = up.name;
        entry.targetId = up.id;
        const QJsonArray items = QJsonDocument::fromJson(
            cli::formatCliListingJson({entry}, {up}, {QStringLiteral("dev")}).toUtf8()).array();
        QCOMPARE(items.size(), 1);
        const QJsonObject item = items.first().toObject();
        QCOMPARE(item.value(QStringLiteral("kind")).toString(), QStringLiteral("command"));
        QCOMPARE(item.value(QStringLiteral("path")).toArray(), (QJsonArray{QStringLiteral("dev"), QStringLiteral("up")}));
        const QJsonArray params = item.value(QStringLiteral("params")).toArray();
        QCOMPARE(params.size(), 3);
        QCOMPARE(params.at(0).toObject().value(QStringLiteral("choices")).toArray(),
                 (QJsonArray{QStringLiteral("api"), QStringLiteral("web")}));
        QVERIFY(params.at(0).toObject().value(QStringLiteral("required")).toBool());
    }

    void cliFlagsCombineAndStopAtThePath()
    {
        const QString exe = QStringLiteral("kai");
        cli::CliFlags a;
        QCOMPARE(cli::stripLeadingCliFlags({exe, QStringLiteral("-gd"), QStringLiteral("dev"), QStringLiteral("up")}, a),
                 (QStringList{exe, QStringLiteral("dev"), QStringLiteral("up")}));
        QVERIFY(a.global && a.detached);

        cli::CliFlags b;
        cli::stripLeadingCliFlags({exe, QStringLiteral("-dg"), QStringLiteral("x")}, b);
        QVERIFY(b.global && b.detached);

        cli::CliFlags c;
        cli::stripLeadingCliFlags({exe, QStringLiteral("--global"), QStringLiteral("--detached"), QStringLiteral("x")}, c);
        QVERIFY(c.global && c.detached);

        cli::CliFlags d;
        cli::stripLeadingCliFlags({exe, QStringLiteral("-d"), QStringLiteral("x")}, d);
        QVERIFY(!d.global && d.detached);

        // A palavra "global" não é mais flag; e flags depois do caminho são
        // do comando (parâmetros), não do kai.
        cli::CliFlags e;
        QCOMPARE(cli::stripLeadingCliFlags({exe, QStringLiteral("global"), QStringLiteral("x")}, e),
                 (QStringList{exe, QStringLiteral("global"), QStringLiteral("x")}));
        QVERIFY(!e.global && !e.detached);
        cli::CliFlags f;
        QCOMPARE(cli::stripLeadingCliFlags({exe, QStringLiteral("x"), QStringLiteral("-d")}, f),
                 (QStringList{exe, QStringLiteral("x"), QStringLiteral("-d")}));
        QVERIFY(!f.detached);
    }
};

QTEST_MAIN(TestCliHelpFormat)
#include "test_cli_help_format.moc"
