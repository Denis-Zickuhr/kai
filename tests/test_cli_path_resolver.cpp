#include <QTest>

#include "core/cli-path-resolver.h"

using namespace kai::core;

// Cobre a resolução de CLI Paths (kai <cli_path...>): casa os tokens do
// argv contra a árvore de folders/commands, pulando pastas TRANSPARENTES
// (sem cli_path próprio) sem quebrar o alcance dos filhos delas — mesmo
// exemplo usado na conversa de design: "API Vendas/Zaphyr" com cli_path só
// em "Zaphyr" -> `kai zephyr env prod`, não `kai api-vendas zephyr env`.
class TestCliPathResolver : public QObject {
    Q_OBJECT

private:
    // Monta a árvore do exemplo: "API Vendas" (transparente) > "Zaphyr"
    // (cli_path "zephyr") > comandos "env" e "test".
    static void buildZaphyrTree(QVector<Folder> &folders, QVector<Command> &commands)
    {
        Folder apiVendas;
        apiVendas.id = QStringLiteral("f_api_vendas");
        apiVendas.name = QStringLiteral("API Vendas");
        // sem cli_path — transparente.

        Folder zaphyr;
        zaphyr.id = QStringLiteral("f_zaphyr");
        zaphyr.name = QStringLiteral("Zaphyr");
        zaphyr.parentId = apiVendas.id;
        zaphyr.cliPath = QStringLiteral("zephyr");

        folders = {apiVendas, zaphyr};

        Command env;
        env.id = QStringLiteral("c_env");
        env.name = QStringLiteral("Subir ambiente");
        env.description = QStringLiteral("Sobe o ambiente Zaphyr.");
        env.folderId = zaphyr.id;
        env.cliPath = QStringLiteral("env");

        Command test;
        test.id = QStringLiteral("c_test");
        test.name = QStringLiteral("Rodar testes");
        test.folderId = zaphyr.id;
        test.cliPath = QStringLiteral("test");

        commands = {env, test};
    }

private slots:
    void resolvesCommandThroughTransparentFolder()
    {
        QVector<Folder> folders;
        QVector<Command> commands;
        buildZaphyrTree(folders, commands);
        CliPathResolver resolver(folders, commands);

        const CliPathResolution res = resolver.resolve({QStringLiteral("zephyr"), QStringLiteral("env"), QStringLiteral("prod")});
        QCOMPARE(res.kind, CliPathResolution::Kind::Command);
        QCOMPARE(res.commandId, QStringLiteral("c_env"));
        QCOMPARE(res.remainingArgs, QStringList{QStringLiteral("prod")});
    }

    // Caminho incompleto (parou numa pasta): lista os filhos, pra uma tela
    // de ajuda/descoberta automática ("kai zephyr" sozinho).
    void incompletePathListsChildren()
    {
        QVector<Folder> folders;
        QVector<Command> commands;
        buildZaphyrTree(folders, commands);
        CliPathResolver resolver(folders, commands);

        const CliPathResolution res = resolver.resolve({QStringLiteral("zephyr")});
        QCOMPARE(res.kind, CliPathResolution::Kind::Folder);
        QCOMPARE(res.folderId, QStringLiteral("f_zaphyr"));
        QCOMPARE(res.children.size(), 2);

        QStringList cliPaths;
        for (const CliPathChildEntry &c : res.children) cliPaths << c.cliPath;
        cliPaths.sort();
        QCOMPARE(cliPaths, QStringList({QStringLiteral("env"), QStringLiteral("test")}));

        // Descrição do comando "env" preservada, pronta pro --help.
        for (const CliPathChildEntry &c : res.children) {
            if (c.cliPath == QStringLiteral("env")) {
                QCOMPARE(c.description, QStringLiteral("Sobe o ambiente Zaphyr."));
                QVERIFY(!c.isFolder);
            }
        }
    }

    // Sem NENHUM argumento: lista os filhos da raiz.
    void emptyArgsListsRootChildren()
    {
        QVector<Folder> folders;
        QVector<Command> commands;
        buildZaphyrTree(folders, commands);
        CliPathResolver resolver(folders, commands);

        const CliPathResolution res = resolver.resolve({});
        QCOMPARE(res.kind, CliPathResolution::Kind::Folder);
        QCOMPARE(res.folderId, QString()); // raiz
        QCOMPARE(res.children.size(), 1);
        QCOMPARE(res.children.first().cliPath, QStringLiteral("zephyr"));
        QVERIFY(res.children.first().isFolder);

        // rootChildren() é o mesmo resultado, sem precisar resolver args vazio.
        QCOMPARE(resolver.rootChildren().size(), 1);
    }

    // 1º token não bate com nada: NotFound, mas ainda devolve os filhos
    // válidos daquele escopo (raiz) pra sugerir opções.
    void unknownFirstTokenIsNotFoundWithSuggestions()
    {
        QVector<Folder> folders;
        QVector<Command> commands;
        buildZaphyrTree(folders, commands);
        CliPathResolver resolver(folders, commands);

        const CliPathResolution res = resolver.resolve({QStringLiteral("bogus")});
        QCOMPARE(res.kind, CliPathResolution::Kind::NotFound);
        QCOMPARE(res.children.size(), 1);
        QCOMPARE(res.children.first().cliPath, QStringLiteral("zephyr"));
    }

    // Token errado DEPOIS de um segmento válido: NotFound com os filhos
    // daquele nível específico (não da raiz).
    void unknownTokenAfterValidPrefixIsNotFoundWithScopedSuggestions()
    {
        QVector<Folder> folders;
        QVector<Command> commands;
        buildZaphyrTree(folders, commands);
        CliPathResolver resolver(folders, commands);

        const CliPathResolution res = resolver.resolve({QStringLiteral("zephyr"), QStringLiteral("bogus")});
        QCOMPARE(res.kind, CliPathResolution::Kind::NotFound);
        QCOMPARE(res.children.size(), 2); // env, test
    }

    // Um comando na RAIZ (folderId vazio) é alcançável direto, sem
    // qualquer segmento de pasta antes.
    void rootLevelCommandIsReachableDirectly()
    {
        Command deploy;
        deploy.id = QStringLiteral("c_deploy");
        deploy.name = QStringLiteral("Deploy");
        deploy.cliPath = QStringLiteral("deploy");
        // folderId vazio = raiz.

        CliPathResolver resolver({}, {deploy});
        const CliPathResolution res = resolver.resolve({QStringLiteral("deploy")});
        QCOMPARE(res.kind, CliPathResolution::Kind::Command);
        QCOMPARE(res.commandId, QStringLiteral("c_deploy"));
        QVERIFY(res.remainingArgs.isEmpty());
    }

    // Uma pasta com cli_path aninhada dentro de OUTRA pasta com cli_path —
    // cada nível consome exatamente um token, sem colapsar (diferente do
    // caso transparente).
    void twoLevelsOfOptedInFoldersEachConsumeOneToken()
    {
        Folder outer;
        outer.id = QStringLiteral("f_outer");
        outer.name = QStringLiteral("Outer");
        outer.cliPath = QStringLiteral("outer");

        Folder inner;
        inner.id = QStringLiteral("f_inner");
        inner.name = QStringLiteral("Inner");
        inner.parentId = outer.id;
        inner.cliPath = QStringLiteral("inner");

        Command leaf;
        leaf.id = QStringLiteral("c_leaf");
        leaf.name = QStringLiteral("Leaf");
        leaf.folderId = inner.id;
        leaf.cliPath = QStringLiteral("run");

        CliPathResolver resolver({outer, inner}, {leaf});

        // Pular direto pra "inner" sem passar por "outer" NÃO resolve —
        // "inner" só é filho do escopo "outer", não da raiz.
        QCOMPARE(resolver.resolve({QStringLiteral("inner"), QStringLiteral("run")}).kind,
            CliPathResolution::Kind::NotFound);

        const CliPathResolution res = resolver.resolve(
            {QStringLiteral("outer"), QStringLiteral("inner"), QStringLiteral("run")});
        QCOMPARE(res.kind, CliPathResolution::Kind::Command);
        QCOMPARE(res.commandId, QStringLiteral("c_leaf"));
    }
};

QTEST_MAIN(TestCliPathResolver)
#include "test_cli_path_resolver.moc"
