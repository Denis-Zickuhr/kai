#include <QTest>
#include <QTemporaryDir>
#include <QFile>

#include "ui/project-selector.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Cobre o versionamento de coleções por projeto (definição original da feature
// "Coleções"): um kai.json pode trazer uma chave "collections", e a
// importação do projeto deve materializar cada uma como Collection
// associada à pasta do projeto (folderId), utilizável como fonte de dados.
class TestProjectCollections : public QObject {
    Q_OBJECT

private slots:
    void importParsesCollectionsFromKaiJson()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "API Clientes",
            "icon": "database",
            "commands": [{"name": "Build", "type": "shell", "command": "echo ok"}],
            "collections": [
                {
                    "name": "Clientes",
                    "icon": "database",
                    "schema": [
                        {"name": "key", "label": "ID", "type": "key"},
                        {"name": "value", "label": "Nome", "type": "value"},
                        {"name": "email", "label": "E-mail", "type": "email"}
                    ],
                    "entries": [
                        {"values": {"key": "1", "value": "Alice", "email": "a@x.com"}, "tags": ["vip"]},
                        {"values": {"key": "2", "value": "Bob", "email": "b@x.com"}}
                    ]
                }
            ]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY(result.success);
        QCOMPARE(result.commands.size(), 1);

        // A coleção do projeto foi materializada e associada à pasta.
        QCOMPARE(result.collections.size(), 1);
        const Collection &col = result.collections.at(0);
        QCOMPARE(col.name, QStringLiteral("Clientes"));
        QCOMPARE(col.folderId, result.folder.id);
        QVERIFY(!col.id.isEmpty());
        QCOMPARE(col.schema.size(), 3);
        QCOMPARE(col.entries.size(), 2);
        QCOMPARE(col.entries.at(0).values.value(QStringLiteral("value")), QStringLiteral("Alice"));
        // sourcePath aponta para o kai.json de origem (versionamento).
        QVERIFY(col.sourcePath.endsWith(QStringLiteral("kai.json")));
    }

    // feat (pedido do usuário: "tu atualizou o manifesto com as novas
    // feats + kai.yml?") — Import Project agora também aceita kai.yml/
    // kai.yaml quando não há kai.json, convertendo via o mesmo bridge
    // YAML<->JSON já usado por Exportar/Importar Configuração.
    void importAcceptsKaiYmlWhenKaiJsonIsAbsent()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QFile kaiYml(directory.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(R"yaml(
project_name: "API Clientes YAML"
icon: "database"
commands:
  - name: "Build"
    type: "shell"
    command: "echo ok"
)yaml");
        kaiYml.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.folder.name, QStringLiteral("API Clientes YAML"));
        QCOMPARE(result.commands.size(), 1);
        QCOMPARE(result.commands.at(0).name, QStringLiteral("Build"));
    }

    // Bug reportado: um kai.yml escrito na FORMA do Export/Import
    // Configuration ("folders": [{"name", "icon", "is_project": true}],
    // sem "project_name"/"icon" no topo) sendo importado via Importar
    // Projeto ficava com o nome do DIRETÓRIO e ícone vazio — a chave
    // "folders" não existe no formato de projeto, então era só ignorada
    // silenciosamente. Import Project agora cai pra essa entrada quando
    // project_name/icon estão ausentes no topo.
    void importFallsBackToIsProjectFolderEntryForNameAndIcon()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QFile kaiYml(directory.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(R"yaml(
commands:
  - name: "Sync"
    type: "shell"
    command: "npm run sync"
    icon: "refresh-ccw"
folders:
  - icon: "box"
    is_project: true
    name: "Amazon Marketplace API"
)yaml");
        kaiYml.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.folder.name, QStringLiteral("Amazon Marketplace API"));
        QCOMPARE(result.folder.icon, QStringLiteral("box"));
        QCOMPARE(result.commands.size(), 1);
        QCOMPARE(result.commands.at(0).icon, QStringLiteral("refresh-ccw"));
    }

    void paramReferencesCollectionByName()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "API",
            "commands": [{
                "name": "Saudar",
                "type": "shell",
                "command": "echo {{cliente.value}}",
                "params": [{
                    "name": "cliente", "label": "Cliente", "type": "select",
                    "collection": "Clientes", "collection_display_field": "value"
                }]
            }],
            "collections": [{
                "name": "Clientes",
                "schema": [{"name": "value", "label": "Nome", "type": "value"}],
                "entries": [{"values": {"value": "Alice"}}]
            }]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY(result.success);
        QCOMPARE(result.commands.size(), 1);
        QCOMPARE(result.commands.at(0).params.size(), 1);
        QCOMPARE(result.collections.size(), 1);

        // O param "cliente" deve estar ligado ao id gerado da coleção
        // "Clientes" (resolvido por nome durante o import).
        const Parameter &p = result.commands.at(0).params.at(0);
        QCOMPARE(p.collectionId, result.collections.at(0).id);
        QCOMPARE(p.collectionDisplayField, QStringLiteral("value"));
    }

    void importWithoutCollectionsYieldsNone()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "Sem Coleções",
            "commands": [{"name": "Run", "type": "shell", "command": "ls"}]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY(result.success);
        QCOMPARE(result.collections.size(), 0);
    }

    // Suporte a PASTAS em projetos (feedback do usuário: era base da spec).
    // Um comando pode declarar "folder": "Nome" ou "A/B" (aninhado); cada
    // segmento vira uma subpasta sob a pasta raiz do projeto.
    void importCreatesSubFoldersFromFolderKey()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "API",
            "commands": [
                {"name": "Raiz", "type": "shell", "command": "echo raiz"},
                {"name": "Health", "type": "shell", "command": "echo h", "folder": "Health"},
                {"name": "Shopee", "type": "shell", "command": "echo s", "folder": "Marketplaces/Shopee"},
                {"name": "Olist", "type": "shell", "command": "echo o", "folder": "Marketplaces/Olist"}
            ]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY(result.success);
        QCOMPARE(result.commands.size(), 4);

        // Subpastas: Health, Marketplaces, Shopee, Olist (Marketplaces é
        // criada uma vez e reusada por Shopee e Olist).
        auto folderIdByName = [&](const QString &name) -> QString {
            for (const Folder &f : result.subFolders) if (f.name == name) return f.id;
            return QString();
        };
        QCOMPARE(result.subFolders.size(), 4);
        const QString healthId = folderIdByName(QStringLiteral("Health"));
        const QString mktId = folderIdByName(QStringLiteral("Marketplaces"));
        const QString shopeeId = folderIdByName(QStringLiteral("Shopee"));
        const QString olistId = folderIdByName(QStringLiteral("Olist"));
        QVERIFY(!healthId.isEmpty() && !mktId.isEmpty() && !shopeeId.isEmpty() && !olistId.isEmpty());

        // "Raiz" fica na pasta do projeto; "Health" na subpasta Health;
        // Shopee/Olist aninhados sob Marketplaces.
        auto cmdFolder = [&](const QString &cmdName) -> QString {
            for (const Command &c : result.commands) if (c.name == cmdName) return c.folderId;
            return QString();
        };
        QCOMPARE(cmdFolder(QStringLiteral("Raiz")), result.folder.id);
        QCOMPARE(cmdFolder(QStringLiteral("Health")), healthId);
        QCOMPARE(cmdFolder(QStringLiteral("Shopee")), shopeeId);
        QCOMPARE(cmdFolder(QStringLiteral("Olist")), olistId);

        // Marketplaces é filha da raiz; Shopee/Olist filhas de Marketplaces.
        for (const Folder &f : result.subFolders) {
            if (f.id == mktId) QCOMPARE(f.parentId.value_or(QString()), result.folder.id);
            if (f.id == shopeeId) QCOMPARE(f.parentId.value_or(QString()), mktId);
            if (f.id == olistId) QCOMPARE(f.parentId.value_or(QString()), mktId);
        }
    }

    // Pedido do usuário ("se eu quiser incluir um ícone na subpasta de um
    // projeto, dá?"): um "folders" array opcional com {"path", "icon"} dá
    // ícone a uma subpasta específica, sem CRIAR a pasta sozinho — a pasta
    // continua nascendo de "folder" nos comandos, como sempre.
    void importAppliesSubFolderIconFromFoldersArrayByPath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "API",
            "commands": [
                {"name": "API dev", "type": "shell", "command": "go run", "folder": "Backend/API"}
            ],
            "folders": [
                {"path": "Backend", "icon": "server"},
                {"path": "Backend/API", "icon": "webhook"},
                {"path": "Nao Usada", "icon": "trash"}
            ]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.subFolders.size(), 2);

        auto folderByName = [&](const QString &name) -> const Folder * {
            for (const Folder &f : result.subFolders) if (f.name == name) return &f;
            return nullptr;
        };
        const Folder *backend = folderByName(QStringLiteral("Backend"));
        const Folder *api = folderByName(QStringLiteral("API"));
        QVERIFY(backend != nullptr && api != nullptr);
        QCOMPARE(backend->icon, QStringLiteral("server"));
        QCOMPARE(api->icon, QStringLiteral("webhook"));
        // Uma entrada cujo path nenhum comando/coleção referencia não cria
        // pasta nenhuma sozinha.
        QVERIFY(folderByName(QStringLiteral("Nao Usada")) == nullptr);
    }

    // CLI Paths: mesmo mecanismo de "folders": [{"path", "icon"}] usado
    // acima também carrega "cli_path" pra uma subpasta implícita, e o
    // top-level "cli_path" vira o cli_path da RAIZ do projeto (mesma
    // convenção de project_name/icon).
    void importAppliesCliPathFromTopLevelAndFoldersArray()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "API",
            "cli_path": "api",
            "commands": [
                {"name": "API dev", "type": "shell", "command": "go run", "folder": "Backend/Zaphyr", "cli_path": "env"}
            ],
            "folders": [
                {"path": "Backend/Zaphyr", "cli_path": "zephyr"}
            ]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.folder.cliPath, QStringLiteral("api"));

        const Folder *zaphyr = nullptr;
        for (const Folder &f : result.subFolders) {
            if (f.name == QStringLiteral("Zaphyr")) { zaphyr = &f; break; }
        }
        QVERIFY(zaphyr != nullptr);
        QCOMPARE(zaphyr->cliPath, QStringLiteral("zephyr"));
        QCOMPARE(result.commands.first().cliPath, QStringLiteral("env"));
    }

    // Bug real reportado (arquivo real anexado): "path" escrito INCLUINDO
    // o nome do projeto como prefixo ("Amazon Marketplace API/Sincronizar"
    // pra um comando com "folder": "Sincronizar") — o ícone nunca batia
    // silenciosamente, já que o path de um comando NUNCA inclui a raiz.
    // Tolerado: o prefixo "<project_name>/" é removido se presente.
    void importAppliesSubFolderIconWhenPathIncludesProjectNamePrefix()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "Amazon Marketplace API",
            "icon": "box",
            "commands": [
                {"name": "Geral", "type": "shell", "command": "npm run sync", "folder": "Sincronizar", "icon": "refresh-ccw"}
            ],
            "folders": [
                {"path": "Amazon Marketplace API/Sincronizar", "icon": "refresh-ccw"}
            ]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.subFolders.size(), 1);
        QCOMPARE(result.subFolders.first().name, QStringLiteral("Sincronizar"));
        QCOMPARE(result.subFolders.first().icon, QStringLiteral("refresh-ccw"));
    }

    // AUDITORIA de import/export (revisão geral pedida pelo usuário): o
    // parser de kai.json de projeto lia só um subconjunto pequeno dos
    // campos de Command (o resto era descartado silenciosamente mesmo
    // usando as MESMAS chaves de Command::toJson). Cobre os campos simples
    // adicionados: description/compact_output/hide_on_run/hidden/
    // capture_env/open_last_link/interactive_terminal/terminal_target/
    // auto_run/auto_run_delay_sec, responders e a nova Condição de Execução.
    void importParsesPreviouslyMissingCommandFields()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "Projeto Completo",
            "commands": [
                {
                    "name": "Login",
                    "type": "shell",
                    "command": "gh auth login",
                    "description": "Faz login",
                    "icon": "key-round",
                    "formatted_output": true,
                    "compact_output": true,
                    "hide_on_run": true,
                    "hidden": true,
                    "capture_env": true,
                    "declared_env_vars": [
                        {"name": "TOKEN", "persist": true, "scope": "global"}
                    ],
                    "open_last_link": true,
                    "interactive_terminal": true,
                    "terminal_target": "WSL",
                    "auto_run": true,
                    "auto_run_delay_sec": 5,
                    "params": [
                        {"name": "scope", "label": "Scope", "type": "text", "optional": true}
                    ],
                    "responders": [
                        {"name": "Confirmar", "pattern": "\\[y/N\\]", "response": "y"}
                    ],
                    "execution_conditions": [
                        {"left": "{{TOKEN}}", "op": "not_exists", "right": ""}
                    ],
                    "condition_combinator": "or",
                    "condition_skip_behavior": "failure"
                }
            ]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY(result.success);
        QCOMPARE(result.commands.size(), 1);

        const Command &c = result.commands.at(0);
        QCOMPARE(c.description, QStringLiteral("Faz login"));
        // icon/formatted_output/declared_env_vars e Parameter::optional:
        // bug reportado ("nome e icone não parece importar" / "tem que ser
        // GERAL aquela importação, ícone, nome e tudo, como na importação
        // de pasta") — o parser hand-rolled desta função nunca lia estes
        // campos; agora reusa core::Command::fromJson (mesmo parser do
        // Export/Import Configuration), então qualquer campo novo chega
        // automaticamente.
        QCOMPARE(c.icon, QStringLiteral("key-round"));
        QCOMPARE(c.formattedOutput, true);
        QCOMPARE(c.compactOutput, true);
        QCOMPARE(c.hideOnRun, true);
        QCOMPARE(c.hidden, true);
        QCOMPARE(c.captureEnv, true);
        QCOMPARE(c.declaredEnvVars.size(), 1);
        if (!c.declaredEnvVars.isEmpty()) {
            QCOMPARE(c.declaredEnvVars.first().name, QStringLiteral("TOKEN"));
            QCOMPARE(c.declaredEnvVars.first().persist, true);
        }
        QCOMPARE(c.openLastLink, true);
        QCOMPARE(c.interactiveTerminal, true);
        QCOMPARE(c.terminalTarget, QStringLiteral("WSL"));
        QCOMPARE(c.autoRun, true);
        QCOMPARE(c.autoRunDelaySec, 5);
        QCOMPARE(c.params.size(), 1);
        if (!c.params.isEmpty()) {
            QCOMPARE(c.params.first().optional, true);
        }
        // working_dir sem a chave continua defaultando para
        // {{PROJECT_PATH}} (default de PROJETO, diferente do default vazio
        // que core::Command::fromJson usa sozinho) — não pode se perder ao
        // reusar fromJson como base.
        QCOMPARE(c.workingDir, QStringLiteral("{{PROJECT_PATH}}"));
        QCOMPARE(c.responders.size(), 1);
        QCOMPARE(c.responders.at(0).name, QStringLiteral("Confirmar"));
        QCOMPARE(c.executionConditions.size(), 1);
        QCOMPARE(c.executionConditions.at(0).left, QStringLiteral("{{TOKEN}}"));
        QCOMPARE(c.executionConditions.at(0).op, QStringLiteral("not_exists"));
        QCOMPARE(c.conditionCombinator, QStringLiteral("or"));
        QCOMPARE(c.conditionSkipBehavior, QStringLiteral("failure"));
    }

    // Hooks no kai.json de projeto referenciam outros comandos do MESMO
    // manifesto por NOME (o autor não conhece os ids gerados) — resolvidos
    // para os ids reais depois que todos os comandos existem.
    void importResolvesHooksByName()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "Projeto Hooks",
            "commands": [
                {
                    "name": "Deploy",
                    "type": "shell",
                    "command": "make deploy",
                    "hooks": {"pre": ["Login"], "post": ["Notificar"], "cleanup": ["Nao Existe"]}
                },
                {"name": "Login", "type": "shell", "command": "gh auth login"},
                {"name": "Notificar", "type": "shell", "command": "echo done"}
            ]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY(result.success);
        QCOMPARE(result.commands.size(), 3);

        auto idOf = [&](const QString &name) -> QString {
            for (const Command &c : result.commands) {
                if (c.name == name) return c.id;
            }
            return QString();
        };

        const Command &deploy = result.commands.at(0);
        QCOMPARE(deploy.name, QStringLiteral("Deploy"));
        QCOMPARE(deploy.hooks.pre.size(), 1);
        QCOMPARE(deploy.hooks.pre.at(0), idOf(QStringLiteral("Login")));
        QCOMPARE(deploy.hooks.post.size(), 1);
        QCOMPARE(deploy.hooks.post.at(0), idOf(QStringLiteral("Notificar")));
        // Nome sem correspondência é ignorado, não quebra a importação.
        QVERIFY(deploy.hooks.cleanup.isEmpty());
    }

    // AUDITORIA (bug relatado): o path usado pra LER o kai.json e o valor
    // gravado como PROJECT_PATH precisam ser INDEPENDENTES — editar um não
    // pode quebrar o outro. Aqui o "projectPathOverride" (2º argumento) é
    // deliberadamente um path que NÃO existe no disco (simula o usuário
    // simplificando à mão, ex: tirando o prefixo do WSL) — a leitura do
    // kai.json deve continuar funcionando normalmente porque ela usa
    // `directoryPath` (o path real), nunca o override.
    void importUsesRealDirectoryToReadButOverrideToSaveProjectPath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "Projeto",
            "commands": [{"name": "Build", "type": "shell", "command": "make"}]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const QString fakeSimplifiedPath = QStringLiteral("/home/user/projects/dotfiles");
        const ProjectImportResult result = selector.importFromDirectory(directory.path(), fakeSimplifiedPath);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commands.size(), 1); // achou o kai.json normalmente
        QCOMPARE(result.folder.projectPath.value_or(QString()), fakeSimplifiedPath); // mas salvou o override
        QCOMPARE(result.folder.envVars.value(QStringLiteral("PROJECT_PATH")), fakeSimplifiedPath);
    }

    // Sem override (string vazia, o padrão): mantém o comportamento antigo
    // — PROJECT_PATH cai pro directoryPath literal.
    void importFallsBackToDirectoryPathWhenNoOverrideGiven()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({"project_name": "Projeto", "commands": []})json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY(result.success);
        QCOMPARE(result.folder.projectPath.value_or(QString()), directory.path());
    }
};

QTEST_MAIN(TestProjectCollections)
#include "test_project_collections.moc"
