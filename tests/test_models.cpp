#include <QtTest>
#include "core/models.h"
#include "core/config-manager.h"

class TestModels : public QObject {
    Q_OBJECT
private slots:
    void testCommandCronRoundTrip() {
        kai::core::Command c;
        c.id = "c1";
        c.name = "Command1";
        c.cronExpression = "0 9 * * 1-5";
        c.cronNotifyOnRun = true;

        QJsonObject obj = c.toJson();
        kai::core::Command c2 = kai::core::Command::fromJson(obj);

        QCOMPARE(c2.cronExpression, c.cronExpression);
        QCOMPARE(c2.cronNotifyOnRun, c.cronNotifyOnRun);
    }

    // "Executar no diretório de invocação": ida e volta, e o default não
    // polui o arquivo (chave omitida).
    void testCliWorkingDirRoundTripAndDefaultIsOmitted() {
        kai::core::Command c;
        c.id = "g1";
        c.name = "Git";
        QVERIFY(!c.toJson().contains("cli_working_dir"));
        QCOMPARE(kai::core::Command::fromJson(c.toJson()).cliWorkingDir, kai::core::CliWorkingDir::Default);

        c.cliWorkingDir = kai::core::CliWorkingDir::Invocation;
        const QJsonObject obj = c.toJson();
        QCOMPARE(obj.value("cli_working_dir").toString(), QStringLiteral("invocation"));
        QCOMPARE(kai::core::Command::fromJson(obj).cliWorkingDir, kai::core::CliWorkingDir::Invocation);

        // Valor desconhecido (typo) cai no default em vez de mudar o cwd.
        QJsonObject typo = obj;
        typo["cli_working_dir"] = QStringLiteral("invokation");
        QCOMPARE(kai::core::Command::fromJson(typo).cliWorkingDir, kai::core::CliWorkingDir::Default);
    }

    // Ações da pasta: ida e volta preservando a ORDEM de cadastro; chave
    // omitida quando vazia; vazio e repetido não entram.
    void testFolderActionsRoundTripKeepOrderAndAreOmittedWhenEmpty() {
        kai::core::Folder f;
        f.id = "f1";
        f.name = "Repo";
        QVERIFY(!f.toJson().contains("actions"));
        QVERIFY(kai::core::Folder::fromJson(f.toJson()).actions.isEmpty());

        f.actions = {"c_pull", "c_fetch", "c_status"};
        const QJsonObject obj = f.toJson();
        QCOMPARE(obj.value("actions").toArray().size(), 3);
        QCOMPARE(kai::core::Folder::fromJson(obj).actions, (QStringList{"c_pull", "c_fetch", "c_status"}));

        QJsonObject dirty = obj;
        dirty["actions"] = QJsonArray{"a", "", "b", "a", "  "};
        QCOMPARE(kai::core::Folder::fromJson(dirty).actions, (QStringList{"a", "b"}));
    }

    // Ações de EXPANSÃO: lista própria (subconjunto das ações), omitida quando vazia.
    void testFolderExpansionActionsRoundTripAndAreOmittedWhenEmpty() {
        kai::core::Folder f;
        f.id = "f1";
        f.actions = {"c_pull", "c_fetch"};
        QVERIFY(!f.toJson().contains("expansion_actions"));
        f.expansionActions = {"c_fetch"};
        const QJsonObject obj = f.toJson();
        QCOMPARE(kai::core::Folder::fromJson(obj).expansionActions, QStringList{"c_fetch"});

        QJsonObject dirty = obj;
        dirty["expansion_actions"] = QJsonArray{"a", "", "a", "  "};
        QCOMPARE(kai::core::Folder::fromJson(dirty).expansionActions, QStringList{"a"});
    }

    // Variáveis SECRETAS da pasta: o nome vive em `secret_env_keys`; um nome sem valor (vindo de um arquivo)
    // continua existindo como variável vazia.
    void testFolderSecretEnvKeysRoundTrip() {
        kai::core::Folder f;
        f.id = "f1";
        f.envVars = {{"PORT", "8080"}, {"TOKEN", "s3cr3t"}};
        QVERIFY(!f.toJson().contains("secret_env_keys"));
        f.secretEnvKeys = {"TOKEN"};
        const kai::core::Folder back = kai::core::Folder::fromJson(f.toJson());
        QCOMPARE(back.secretEnvKeys, QSet<QString>{"TOKEN"});
        QCOMPARE(back.envVars.value("TOKEN"), QStringLiteral("s3cr3t")); // o armazenamento interno guarda o valor

        QJsonObject fromFile{{"id", "f2"}, {"env_vars", QJsonObject{{"PORT", "1"}}}, {"secret_env_keys", QJsonArray{"API_KEY", ""}}};
        const kai::core::Folder bare = kai::core::Folder::fromJson(fromFile);
        QCOMPARE(bare.secretEnvKeys, QSet<QString>{"API_KEY"});
        QVERIFY(bare.envVars.contains("API_KEY"));
        QVERIFY(bare.envVars.value("API_KEY").isEmpty());
    }

    // Referência por NOME a uma coleção que ainda não existe aqui: só vive enquanto não há `collection_id`.
    void testParameterKeepsAPendingCollectionNameUntilItIsBound() {
        kai::core::Parameter p;
        p.name = "alvo";
        p.collectionName = "Clientes";
        p.collectionDisplayField = "value";
        const QJsonObject json = p.toJson();
        QCOMPARE(json.value("collection_name").toString(), QStringLiteral("Clientes"));
        QVERIFY(!json.contains("collection_id"));
        QCOMPARE(json.value("collection_display_field").toString(), QStringLiteral("value"));
        const kai::core::Parameter back = kai::core::Parameter::fromJson(json);
        QCOMPARE(back.collectionName, QStringLiteral("Clientes"));
        QVERIFY(back.collectionId.isEmpty());

        // Com um id de verdade o nome não é gravado.
        p.collectionId = "col_1";
        QVERIFY(!p.toJson().contains("collection_name"));

        QVector<kai::core::Command> commands(1);
        commands[0].params << back;
        kai::core::Collection c;
        c.id = "col_9";
        c.name = "Clientes";
        QCOMPARE(kai::core::bindPendingCollectionReferences(commands, {c, c}), 0); // nome repetido: ambíguo
        QCOMPARE(kai::core::bindPendingCollectionReferences(commands, {c}), 1);
        QCOMPARE(commands[0].params[0].collectionId, QStringLiteral("col_9"));
        QVERIFY(commands[0].params[0].collectionName.isEmpty());
    }

    // GRUPOS de ações: id -> grupo e grupo -> ícone, omitidos quando vazios; vazio não entra.
    void testFolderActionGroupsRoundTripAndAreOmittedWhenEmpty() {
        kai::core::Folder f;
        f.id = "f1";
        f.actions = {"c_pull", "c_fetch"};
        QVERIFY(!f.toJson().contains("action_groups"));
        QVERIFY(!f.toJson().contains("group_icons"));
        f.actionGroups = {{"c_pull", "Git"}, {"c_fetch", "Git"}};
        f.groupIcons = {{"Git", "git-branch"}};
        const kai::core::Folder back = kai::core::Folder::fromJson(f.toJson());
        QCOMPARE(back.actionGroups, f.actionGroups);
        QCOMPARE(back.groupIcons, f.groupIcons);

        QJsonObject dirty = f.toJson();
        dirty["action_groups"] = QJsonObject{{"a", " Docker "}, {"b", ""}, {"", "X"}};
        dirty["group_icons"] = QJsonObject{{"Docker", ""}};
        const kai::core::Folder cleaned = kai::core::Folder::fromJson(dirty);
        QCOMPARE(cleaned.actionGroups, (QMap<QString, QString>{{"a", "Docker"}}));
        QVERIFY(cleaned.groupIcons.isEmpty());
    }

    // KIP (spec 11 §2/§7.2): flags e últimas respostas sobrevivem ao JSON, e o
    // estado default não polui o arquivo (chaves omitidas quando no default).
    void testKipFieldsRoundTrip() {
        kai::core::Command c;
        c.id = "k1";
        c.name = "Deploy";
        c.command = "deploy --kip";
        c.kip = true;
        c.kipOpenInWindow = true;
        c.kipLastValues.insert(QStringLiteral("env/target"), QStringLiteral("prod"));
        c.kipLastValues.insert(QStringLiteral("env/tags"), QJsonArray{QStringLiteral("a"), QStringLiteral("b")});
        c.kipLastValues.insert(QStringLiteral("opts/flags"), QJsonObject{{"force", true}, {"dry", false}});
        c.kipLastValues.insert(QStringLiteral("n/count"), 3.5);

        const QJsonObject obj = c.toJson();
        QCOMPARE(obj.value("kip").toBool(), true);
        QCOMPARE(obj.value("kip_window").toBool(), true);

        const kai::core::Command back = kai::core::Command::fromJson(obj);
        QVERIFY(back.kip);
        QVERIFY(back.kipOpenInWindow);
        QCOMPARE(back.kipLastValues, c.kipLastValues);
    }

    void testKipDefaultsAreOmittedAndLoadAsOff() {
        kai::core::Command c;
        c.id = "k2";
        c.name = "Plain";
        const QJsonObject obj = c.toJson();
        QVERIFY(!obj.contains("kip"));
        QVERIFY(!obj.contains("kip_window"));
        QVERIFY(!obj.contains("kip_last_values"));

        const kai::core::Command back = kai::core::Command::fromJson(obj);
        QVERIFY(!back.kip);
        QVERIFY(!back.kipOpenInWindow);
        QVERIFY(back.kipLastValues.isEmpty());
    }

    // kai.json mais velhos (sem as chaves KIP) continuam carregando.
    void testLegacyCommandJsonWithoutKipKeysLoads() {
        const QJsonObject legacy{{"id", "old"}, {"name", "Old"}, {"type", "shell"}, {"command", "ls"}};
        const kai::core::Command c = kai::core::Command::fromJson(legacy);
        QVERIFY(!c.kip);
        QVERIFY(c.kipLastValues.isEmpty());
    }

    void testSettingsDataRoundTrip() {
        // Since SettingsData is serialized in ConfigManager, we test the round-trip
        // by saving and loading using ConfigManager (which uses toJson/fromJson logic internally).
        kai::core::ConfigManager cm;
        kai::core::SettingsData s;
        s.activeTheme = "test-theme";
        s.gracefulStopTimeoutSec = 12;

        cm.saveSettings(s);
        kai::core::SettingsData s2 = cm.loadSettings();

        QCOMPARE(s2.gracefulStopTimeoutSec, s.gracefulStopTimeoutSec);
    }

    // Bug relatado: "o campo no param como exibido é key, mas ele tá
    // exibindo o valor na lista.. ta certo?" — uma escolha EXPLÍCITA de
    // campo de exibição precisa ser respeitada mesmo sendo um campo Key
    // (o usuário pode ter um motivo real pra isso). Uma versão anterior
    // deste resolver ignorava até escolhas explícitas de campos Key, o
    // que corrigia demais o bug original.
    void resolveCollectionDisplayFieldRespectsExplicitKeyChoice() {
        using namespace kai::core;
        Collection col;
        col.schema = Collection::defaultSchema(); // [Key, Value]
        QCOMPARE(resolveCollectionDisplayField(col, QStringLiteral("key")), QStringLiteral("key"));
    }

    // Sem NENHUMA escolha configurada, evita o campo Key por padrão
    // (prefere Value) — essa é a causa raiz de verdade do bug original
    // ("tava renderizando o id"): o editor pré-selecionava Key (1º campo
    // do schema padrão) sem o usuário perceber/escolher nada.
    void resolveCollectionDisplayFieldAvoidsKeyWhenUnconfigured() {
        using namespace kai::core;
        Collection col;
        col.schema = Collection::defaultSchema(); // [Key, Value]
        QCOMPARE(resolveCollectionDisplayField(col, QString()), QStringLiteral("value"));
    }

    // Campo configurado que não existe mais no schema (removido/renomeado
    // depois de salvo) cai no mesmo auto-fallback de "nada configurado",
    // não trava mostrando um campo inexistente.
    void resolveCollectionDisplayFieldFallsBackWhenConfiguredFieldNoLongerExists() {
        using namespace kai::core;
        Collection col;
        col.schema = Collection::defaultSchema(); // [Key, Value]
        QCOMPARE(resolveCollectionDisplayField(col, QStringLiteral("removido")), QStringLiteral("value"));
    }
};

QTEST_MAIN(TestModels)
#include "test_models.moc"
