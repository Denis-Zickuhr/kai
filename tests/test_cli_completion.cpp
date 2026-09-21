#include <QTest>

#include "cli/cli-completion.h"

using namespace kai;
using namespace kai::core;

// Autocomplete (Tab) do CLI: candidatos por posição contra a árvore de
// pastas/comandos — mesma resolução do CliPathResolver.
class TestCliCompletion : public QObject {
    Q_OBJECT

private:
    static void buildTree(QVector<Folder> &folders, QVector<Command> &commands)
    {
        Folder docker;
        docker.id = QStringLiteral("f_docker");
        docker.name = QStringLiteral("Docker");
        docker.cliPath = QStringLiteral("dev");
        Folder release;
        release.id = QStringLiteral("f_release");
        release.name = QStringLiteral("Release");
        release.cliPath = QStringLiteral("release");
        folders = {docker, release};

        Command up;
        up.id = QStringLiteral("c_up");
        up.folderId = docker.id;
        up.cliPath = QStringLiteral("up");
        Parameter service;
        service.name = QStringLiteral("servico");
        service.type = ParameterType::Select;
        service.options = {QStringLiteral("API:api"), QStringLiteral("web")};
        Parameter build;
        build.name = QStringLiteral("build");
        build.type = ParameterType::Bool;
        build.optional = true;
        Parameter tag;
        tag.name = QStringLiteral("tag");
        tag.optional = true;
        up.params = {service, build, tag};

        Command down;
        down.id = QStringLiteral("c_down");
        down.folderId = docker.id;
        down.cliPath = QStringLiteral("down");

        Command publish;
        publish.id = QStringLiteral("c_publish");
        publish.folderId = release.id;
        publish.cliPath = QStringLiteral("publish");
        commands = {up, down, publish};
    }

private slots:
    void rootListsSegmentsAndVerbs()
    {
        QVector<Folder> folders; QVector<Command> commands; buildTree(folders, commands);
        const QStringList all = cli::completionCandidates(folders, commands, {}, QString(), true);
        QVERIFY(all.contains(QStringLiteral("dev")));
        QVERIFY(all.contains(QStringLiteral("release")));
        QVERIFY(all.contains(QStringLiteral("run")));
        QVERIFY(!all.contains(QStringLiteral("__complete")));

        QCOMPARE(cli::completionCandidates(folders, commands, {}, QStringLiteral("re"), true),
                 QStringList{QStringLiteral("release")});
        // Sem verbos (ex: depois de `kai -g`): só os caminhos.
        QVERIFY(!cli::completionCandidates(folders, commands, {}, QString(), false).contains(QStringLiteral("run")));
    }

    void folderListsItsChildren()
    {
        QVector<Folder> folders; QVector<Command> commands; buildTree(folders, commands);
        QCOMPARE(cli::completionCandidates(folders, commands, {QStringLiteral("dev")}, QString(), true),
                 (QStringList{QStringLiteral("up"), QStringLiteral("down")}));
        QCOMPARE(cli::completionCandidates(folders, commands, {QStringLiteral("dev")}, QStringLiteral("d"), true),
                 QStringList{QStringLiteral("down")});
    }

    void commandOffersSelectValuesFlagsAndHelp()
    {
        QVector<Folder> folders; QVector<Command> commands; buildTree(folders, commands);
        const QStringList atCommand = cli::completionCandidates(
            folders, commands, {QStringLiteral("dev"), QStringLiteral("up")}, QString(), true);
        // 1º obrigatório é um select: valores canônicos ("API:api" -> "api").
        QVERIFY(atCommand.contains(QStringLiteral("api")));
        QVERIFY(atCommand.contains(QStringLiteral("web")));
        QVERIFY(atCommand.contains(QStringLiteral("--build=")));
        QVERIFY(atCommand.contains(QStringLiteral("--tag=")));
        QVERIFY(atCommand.contains(QStringLiteral("--help")));

        // Obrigatório já informado e --build usado: sobra --tag.
        const QStringList after = cli::completionCandidates(folders, commands,
            {QStringLiteral("dev"), QStringLiteral("up"), QStringLiteral("api"), QStringLiteral("--build=true")},
            QStringLiteral("--"), true);
        QCOMPARE(after, (QStringList{QStringLiteral("--tag="), QStringLiteral("--help")}));
    }

    void optionValueIsCompletedAfterTheEqualsSign()
    {
        QVector<Folder> folders; QVector<Command> commands; buildTree(folders, commands);
        QCOMPARE(cli::completionCandidates(folders, commands,
                     {QStringLiteral("dev"), QStringLiteral("up")}, QStringLiteral("--build=t"), true),
                 QStringList{QStringLiteral("--build=true")});
    }

    void wrongPathSuggestsNothing()
    {
        QVector<Folder> folders; QVector<Command> commands; buildTree(folders, commands);
        QVERIFY(cli::completionCandidates(folders, commands, {QStringLiteral("nada")}, QString(), true).isEmpty());
    }

    void scriptsExistForSupportedShellsOnly()
    {
        QVERIFY(cli::completionScript(QStringLiteral("bash")).contains(QStringLiteral("__complete")));
        QVERIFY(cli::completionScript(QStringLiteral("zsh")).contains(QStringLiteral("compdef")));
        QVERIFY(cli::completionScript(QStringLiteral("powershell")).contains(QStringLiteral("Register-ArgumentCompleter")));
        QVERIFY(cli::completionScript(QStringLiteral("fish")).isEmpty());
    }
    // `kai kip <TAB>`: o helper do KIP é um verbo reservado com completar próprio.
    void kipIsATopLevelVerbAndCompletesItsOwnVerbsAndOptions()
    {
        QVector<Folder> folders;
        QVector<Command> commands;
        QVERIFY(cli::completionCandidates(folders, commands, {}, QStringLiteral("ki"), true).contains(QStringLiteral("kip")));

        const QStringList verbs = cli::completionCandidates(folders, commands, {QStringLiteral("kip")}, QString(), true);
        for (const char *v : {"hello", "prompt", "confirm", "patch", "invalid", "message", "markdown", "progress", "steps",
                              "step", "table", "notify", "set-env", "done", "raw", "get"}) {
            QVERIFY2(verbs.contains(QString::fromLatin1(v)), v);
        }
        QCOMPARE(cli::completionCandidates(folders, commands, {QStringLiteral("kip")}, QStringLiteral("pr"), true),
                 (QStringList{QStringLiteral("prompt"), QStringLiteral("progress")}));

        // Depois do verbo, as opções dele; depois de --field, os tipos.
        const QStringList promptOptions = cli::completionCandidates(
            folders, commands, {QStringLiteral("kip"), QStringLiteral("prompt")}, QStringLiteral("--"), true);
        QVERIFY(promptOptions.contains(QStringLiteral("--field")));
        QVERIFY(promptOptions.contains(QStringLiteral("--id")));
        QVERIFY(promptOptions.contains(QStringLiteral("--required")));
        const QStringList types = cli::completionCandidates(
            folders, commands, {QStringLiteral("kip"), QStringLiteral("prompt"), QStringLiteral("--field")}, QString(), true);
        QVERIFY(types.contains(QStringLiteral("select")));
        QVERIFY(types.contains(QStringLiteral("flags")));
    }
};

QTEST_MAIN(TestCliCompletion)
#include "test_cli_completion.moc"
