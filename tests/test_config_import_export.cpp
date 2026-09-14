#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <algorithm>

#include "core/config-manager.h"
#include "core/models.h"

using namespace kai::core;

// Cobre as novas funcionalidades de dados: import/export de configuração
// (global/pasta/comando) e a serialização de terminalTarget/lastParamValues
// no Command e de TerminalProfile nas Settings.
class TestConfigImportExport : public QObject {
    Q_OBJECT

private:
    static CommandsData sampleData()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Root");

        Folder sub;
        sub.id = QStringLiteral("f_sub");
        sub.name = QStringLiteral("Sub");
        sub.parentId = QStringLiteral("f_root");

        Command c1;
        c1.id = QStringLiteral("c1");
        c1.name = QStringLiteral("Build");
        c1.folderId = QStringLiteral("f_root");
        c1.command = QStringLiteral("make");
        c1.terminalTarget = QStringLiteral("WSL");
        c1.lastParamValues.insert(QStringLiteral("env"), QStringLiteral("prod"));

        Command c2;
        c2.id = QStringLiteral("c2");
        c2.name = QStringLiteral("Test");
        c2.folderId = QStringLiteral("f_sub");
        c2.command = QStringLiteral("ctest");

        return {{root, sub}, {c1, c2}};
    }

private slots:
    void commandSerializesTerminalProfileAndLastParams()
    {
        Command c = sampleData().commands.at(0);
        const Command roundTrip = Command::fromJson(c.toJson());
        QCOMPARE(roundTrip.terminalTarget, QStringLiteral("WSL"));
        QCOMPARE(roundTrip.lastParamValues.value(QStringLiteral("env")), QStringLiteral("prod"));
    }

    void exportCommandContainsOnlyThatCommand()
    {
        const QString json = ConfigManager::exportCommand(QStringLiteral("c1"), sampleData());
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        QCOMPARE(r.scope, QStringLiteral("command"));
        QCOMPARE(r.commands.size(), 1);
        // Formato ENXUTO (padrão): o id de origem "c1" NÃO sobrevive ao
        // export/import — o app gera um id novo a cada import (pedido do
        // usuário: "IDs tbm não devem ter no export/import, visto que o
        // APP deve gerar em runtime"). O NOME é o que precisa sobreviver.
        QCOMPARE(r.commands.at(0).name, QStringLiteral("Build"));
        QVERIFY(!r.commands.at(0).id.isEmpty()); // um id novo foi gerado, só não é "c1"
        QCOMPARE(r.folders.size(), 0);
    }

    void exportFolderIncludesSubtreeAndCommands()
    {
        const QString json = ConfigManager::exportFolder(QStringLiteral("f_root"), sampleData());
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        QCOMPARE(r.scope, QStringLiteral("folder"));
        // A pasta RAIZ exportada ("f_root") não aparece na lista "folders"
        // do ARQUIVO (é o topo implícito do pacote, ver "root_folder" no
        // envelope) — mas SUA IDENTIDADE (nome) é preservada e ela volta
        // como uma pasta de verdade na IMPORTAÇÃO, junto da subpasta
        // ("Sub") — ver leanFolderExportPreservesRootFolderIdentityForDirectChildren
        // pro caso que isso corrigiu (comando direto na raiz ficando com
        // folder_id vazio).
        QCOMPARE(r.folders.size(), 2);
        QVERIFY(std::any_of(r.folders.constBegin(), r.folders.constEnd(),
            [](const Folder &f) { return f.name == QStringLiteral("Root"); }));
        QVERIFY(std::any_of(r.folders.constBegin(), r.folders.constEnd(),
            [](const Folder &f) { return f.name == QStringLiteral("Sub"); }));
        // E os dois comandos (um na raiz, um na subpasta).
        QCOMPARE(r.commands.size(), 2);
        // O comando da subpasta deve apontar pro id GERADO da subpasta
        // reimportada (path "Sub" resolvido de volta corretamente).
        const auto testCmd = std::find_if(r.commands.constBegin(), r.commands.constEnd(),
            [](const Command &c) { return c.name == QStringLiteral("Test"); });
        QVERIFY(testCmd != r.commands.constEnd());
        const auto subFolder = std::find_if(r.folders.constBegin(), r.folders.constEnd(),
            [](const Folder &f) { return f.name == QStringLiteral("Sub"); });
        QVERIFY(subFolder != r.folders.constEnd());
        QCOMPARE(testCmd->folderId, subFolder->id);
    }

    // feat (pedido do usuário, reforçado depois: "pode botar na rotina de
    // exportação UMA flag pra exportar completo, oq iria trazer os dados
    // completos no export se o user quiser"). lean=false preserva o
    // comportamento antigo (ids estáveis) — útil pra quem quer reimportar
    // ATUALIZANDO no lugar em vez de sempre adicionar cópias novas.
    void completeExportPreservesStableIds()
    {
        const QString json = ConfigManager::exportCommand(
            QStringLiteral("c1"), sampleData(), {}, {}, /*lean=*/false);
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        QCOMPARE(r.commands.size(), 1);
        QCOMPARE(r.commands.at(0).id, QStringLiteral("c1"));
    }

    // Hooks referenciam o comando pelo NOME no formato enxuto (não pelo
    // id) - prova que o pipeline completo (export -> import) resolve isso
    // de volta corretamente, incluindo pra um comando que vive numa
    // SUBPASTA reimportada com um id novo.
    void leanExportResolvesHooksByNameAndParamsByCollectionName()
    {
        CommandsData data = sampleData();
        Command login;
        login.id = QStringLiteral("c_login");
        login.name = QStringLiteral("Login");
        login.folderId = QStringLiteral("f_root");
        login.command = QStringLiteral("gh auth login");
        data.commands.append(login);

        // c1 ("Build") ganha um pre-hook referenciando "Login" por id
        // internamente - o export deve convertê-lo pro NOME.
        for (Command &c : data.commands) {
            if (c.id == QStringLiteral("c1")) {
                c.hooks.pre << QStringLiteral("c_login");
            }
        }

        Collection col;
        col.id = QStringLiteral("col1");
        col.folderId = QStringLiteral("f_root");
        col.name = QStringLiteral("Usuarios");
        CollectionField field;
        field.name = QStringLiteral("value");
        col.schema << field;

        Parameter p;
        p.name = QStringLiteral("target");
        p.type = ParameterType::Select;
        p.collectionId = QStringLiteral("col1");
        for (Command &c : data.commands) {
            if (c.id == QStringLiteral("c1")) {
                c.params << p;
            }
        }

        const QString json = ConfigManager::exportFolder(
            QStringLiteral("f_root"), data, {col}, {});
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);

        const auto buildCmd = std::find_if(r.commands.constBegin(), r.commands.constEnd(),
            [](const Command &c) { return c.name == QStringLiteral("Build"); });
        QVERIFY(buildCmd != r.commands.constEnd());
        const auto loginCmd = std::find_if(r.commands.constBegin(), r.commands.constEnd(),
            [](const Command &c) { return c.name == QStringLiteral("Login"); });
        QVERIFY(loginCmd != r.commands.constEnd());

        // O hook foi resolvido de volta pro id (novo) do comando "Login".
        QCOMPARE(buildCmd->hooks.pre.size(), 1);
        QCOMPARE(buildCmd->hooks.pre.first(), loginCmd->id);

        // O parâmetro foi resolvido de volta pro id (novo) da coleção "Usuarios".
        QCOMPARE(r.collections.size(), 1);
        QCOMPARE(buildCmd->params.size(), 1);
        QCOMPARE(buildCmd->params.first().collectionId, r.collections.at(0).id);
    }

    // Parâmetro tipo Date (pedido do usuário) sobrevive ao ciclo completo
    // export -> reimport (lean, por pasta) — confirma que "date_mode"/
    // "date_range"/"date_format"/"date_format_custom" não são um caso
    // especial tratado à parte no export/import (ao contrário de
    // "collection_id"/hooks-por-nome): passam intactos pelo pipeline
    // genérico de Parameter::toJson/fromJson.
    void dateParameterSurvivesExportAndReimport()
    {
        CommandsData data = sampleData();
        Parameter p;
        p.name = QStringLiteral("janela");
        p.type = ParameterType::Date;
        p.dateMode = QStringLiteral("datetime");
        p.dateRange = true;
        p.dateFormat = QStringLiteral("custom");
        p.dateFormatCustom = QStringLiteral("dd.MM.yy HH:mm");
        for (Command &c : data.commands) {
            if (c.id == QStringLiteral("c1")) {
                c.params << p;
            }
        }

        const QString json = ConfigManager::exportFolder(QStringLiteral("f_root"), data, {}, {});
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);

        const auto buildCmd = std::find_if(r.commands.constBegin(), r.commands.constEnd(),
            [](const Command &c) { return c.name == QStringLiteral("Build"); });
        QVERIFY(buildCmd != r.commands.constEnd());
        QCOMPARE(buildCmd->params.size(), 1);
        const Parameter &back = buildCmd->params.first();
        QCOMPARE(back.type, ParameterType::Date);
        QCOMPARE(back.dateMode, QStringLiteral("datetime"));
        QCOMPARE(back.dateRange, true);
        QCOMPARE(back.dateFormat, QStringLiteral("custom"));
        QCOMPARE(back.dateFormatCustom, QStringLiteral("dd.MM.yy HH:mm"));
    }

    // REGRESSÃO (achado auditando o schema/JSON de um export real, ver
    // docs/manifesto/kai.schema.json): um export ENXUTO ainda vazava o id
    // interno de cada ENTRY de coleção — CollectionEntry::toJson escreve
    // "id" incondicionalmente, e stripIdsFromExport nunca olhava dentro
    // de "entries" pra removê-lo (só tratava id/folder_id no nível da
    // COLEÇÃO). Contradizia o próprio propósito do "Lean file".
    void leanExportStripsCollectionEntryIds()
    {
        CommandsData data = sampleData();
        Collection col;
        col.id = QStringLiteral("col1");
        col.folderId = QStringLiteral("f_root");
        col.name = QStringLiteral("Usuarios");
        CollectionField field;
        field.name = QStringLiteral("value");
        col.schema << field;
        CollectionEntry entry;
        entry.id = QStringLiteral("entry-id-interno-nao-deveria-vazar");
        entry.values[QStringLiteral("value")] = QStringLiteral("Alice");
        col.entries << entry;

        const QString json = ConfigManager::exportFolder(
            QStringLiteral("f_root"), data, {col}, {});
        QVERIFY2(!json.contains(QStringLiteral("entry-id-interno-nao-deveria-vazar")),
            "id interno da entry vazou no export enxuto");

        // E a reimportação continua funcionando (fromJson gera um id novo
        // pra entry sem "id" — nunca fica vazio/colidindo).
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        QCOMPARE(r.collections.size(), 1);
        QCOMPARE(r.collections.first().entries.size(), 1);
        QVERIFY(!r.collections.first().entries.first().id.isEmpty());
        QCOMPARE(r.collections.first().entries.first().values.value(QStringLiteral("value")),
            QStringLiteral("Alice"));
    }

    // "secret" (feedback do usuário sobre coleções e a superfície sensível
    // de export/import): um campo marcado secret nunca sai no export, nem
    // no export global/seletivo (exportSelective) nem no export por pasta
    // (exportFolder) — mesmo com "incluir dados das entries" LIGADO. Campo
    // não-secreto do mesmo schema continua saindo normalmente.
    void exportSelectiveRedactsSecretCollectionFields()
    {
        Collection col;
        col.id = QStringLiteral("col1");
        col.name = QStringLiteral("Credenciais");
        CollectionField secretField;
        secretField.name = QStringLiteral("token");
        secretField.secret = true;
        CollectionField plainField;
        plainField.name = QStringLiteral("label");
        col.schema = {secretField, plainField};
        CollectionEntry entry;
        entry.values[QStringLiteral("token")] = QStringLiteral("segredo-nao-deveria-vazar-12345");
        entry.values[QStringLiteral("label")] = QStringLiteral("api-producao");
        col.entries << entry;

        ConfigManager::ExportSelection selection;
        selection.collectionEntries = true;
        const QString json = ConfigManager::exportSelective(selection, SettingsData(), sampleData(), {col});
        QVERIFY2(!json.contains(QStringLiteral("segredo-nao-deveria-vazar-12345")),
            "valor de campo secret vazou no export global");
        QVERIFY(json.contains(QStringLiteral("api-producao"))); // campo normal continua saindo
    }

    void exportFolderRedactsSecretCollectionFields()
    {
        CommandsData data = sampleData();
        Collection col;
        col.id = QStringLiteral("col1");
        col.folderId = QStringLiteral("f_root");
        col.name = QStringLiteral("Credenciais");
        CollectionField secretField;
        secretField.name = QStringLiteral("token");
        secretField.secret = true;
        col.schema << secretField;
        CollectionEntry entry;
        entry.values[QStringLiteral("token")] = QStringLiteral("segredo-nao-deveria-vazar-67890");
        col.entries << entry;

        const QString json = ConfigManager::exportFolder(
            QStringLiteral("f_root"), data, {col}, {});
        QVERIFY2(!json.contains(QStringLiteral("segredo-nao-deveria-vazar-67890")),
            "valor de campo secret vazou no export por pasta");
    }

    // CLI Paths: cli_path (Folder/Command) e description (Parameter)
    // precisam sobreviver ao round-trip JSON — mesmo contrato de qualquer
    // outro campo opcional destes modelos.
    void cliPathAndParameterDescriptionRoundTrip()
    {
        Folder f;
        f.id = QStringLiteral("f1");
        f.name = QStringLiteral("Zaphyr");
        f.cliPath = QStringLiteral("zephyr");
        const Folder backF = Folder::fromJson(f.toJson());
        QCOMPARE(backF.cliPath, QStringLiteral("zephyr"));

        Parameter p;
        p.name = QStringLiteral("ambiente");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("prod")};
        p.description = QStringLiteral("Ambiente alvo.");

        Command c;
        c.id = QStringLiteral("c1");
        c.name = QStringLiteral("Subir ambiente");
        c.type = CommandType::Shell;
        c.command = QStringLiteral("up.sh");
        c.cliPath = QStringLiteral("env");
        c.params << p;

        const Command backC = Command::fromJson(c.toJson());
        QCOMPARE(backC.cliPath, QStringLiteral("env"));
        QCOMPARE(backC.params.size(), 1);
        QCOMPARE(backC.params.first().description, QStringLiteral("Ambiente alvo."));

        // Ausente (padrão) continua vazio — retrocompat com commands.json antigos.
        Command bare;
        bare.name = QStringLiteral("Sem cli_path");
        QVERIFY(Command::fromJson(bare.toJson()).cliPath.isEmpty());
    }

    // REGRESSÃO (achado real, testando o próprio formato enxuto: "revise
    // as outra cfg, todas devem ter importar se ter" — um comando ligado
    // DIRETAMENTE na pasta raiz exportada [não numa subpasta] perdia o
    // folder_id por completo na reimportação: a raiz exportada nunca
    // virava um item na lista de pastas — de propósito, é o "topo
    // implícito" do pacote — mas seu NOME também não ia pra lugar
    // nenhum, então path=="" resolvia pra "nenhuma pasta" em vez de
    // recriar a raiz. Agora "root_folder" (no envelope do export)
    // preserva essa identidade, e path=="" volta a apontar pra ela.
    void leanFolderExportPreservesRootFolderIdentityForDirectChildren()
    {
        CommandsData data = sampleData();
        const QString json = ConfigManager::exportFolder(QStringLiteral("f_root"), data);
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);

        // A raiz exportada ("Root") volta como uma pasta de verdade —
        // com o NOME preservado — mesmo não tendo sido listada
        // explicitamente em "folders" no arquivo (ela é o root_folder).
        const auto rootFolder = std::find_if(r.folders.constBegin(), r.folders.constEnd(),
            [](const Folder &f) { return f.name == QStringLiteral("Root"); });
        QVERIFY2(rootFolder != r.folders.constEnd(), "a pasta raiz exportada não foi recriada na importação");

        // "Build" (c1) pertencia DIRETAMENTE à raiz exportada ("f_root")
        // — precisa apontar pro id NOVO dessa pasta recriada, nunca ficar
        // com folder_id vazio.
        const auto buildCmd = std::find_if(r.commands.constBegin(), r.commands.constEnd(),
            [](const Command &c) { return c.name == QStringLiteral("Build"); });
        QVERIFY(buildCmd != r.commands.constEnd());
        QVERIFY2(!buildCmd->folderId.isEmpty(), "comando direto na raiz exportada ficou com folder_id vazio");
        QCOMPARE(buildCmd->folderId, rootFolder->id);
    }

    void exportGlobalIncludesSettings()
    {
        SettingsData settings;
        settings.language = QStringLiteral("pt");
        settings.globalEnvVars.insert(QStringLiteral("PORT"), QStringLiteral("8080"));
        TerminalProfile wsl;
        wsl.name = QStringLiteral("WSL");
        wsl.commandTemplate = QStringLiteral("wsl -d Ubuntu -- bash -lc '{{command}}'");
        settings.terminalProfiles.append(wsl);

        const QString json = ConfigManager::exportGlobal(settings, sampleData());
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        QCOMPARE(r.scope, QStringLiteral("global"));
        QVERIFY(r.hasSettings);
        QCOMPARE(r.settings.language, QStringLiteral("pt"));
        QCOMPARE(r.settings.globalEnvVars.value(QStringLiteral("PORT")), QStringLiteral("8080"));
        QCOMPARE(r.settings.terminalProfiles.size(), 1);
        QCOMPARE(r.settings.terminalProfiles.at(0).commandTemplate,
                 QStringLiteral("wsl -d Ubuntu -- bash -lc '{{command}}'"));
    }

    void importRejectsNonKaiJson()
    {
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(QStringLiteral("{\"foo\": 1}"));
        QVERIFY(!r.ok);
        QVERIFY(!r.errorMessage.isEmpty());
    }

    void importRejectsInvalidJson()
    {
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(QStringLiteral("not json at all"));
        QVERIFY(!r.ok);
    }

    // EXPORTAÇÃO SELETIVA (pedido do usuário): o form com checkboxes decide o
    // que sai. E COLEÇÕES passam a ser incluídas — antes o export "global"
    // simplesmente as ignorava, então nenhum backup era completo.
    void selectiveExportIncludesOnlyCheckedSections()
    {
        SettingsData settings;
        settings.activeTheme = QStringLiteral("kai-dark");

        CommandsData data;
        Folder f;
        f.id = QStringLiteral("f1");
        f.name = QStringLiteral("Pasta");
        data.folders.append(f);
        Command c;
        c.id = QStringLiteral("c1");
        c.name = QStringLiteral("Cmd");
        c.type = CommandType::Shell;
        c.command = QStringLiteral("echo oi");
        data.commands.append(c);

        Collection col;
        col.id = QStringLiteral("col1");
        col.name = QStringLiteral("Clientes");
        CollectionEntry e;
        e.values.insert(QStringLiteral("key"), QStringLiteral("acme-corp"));
        col.entries.append(e);
        QVector<Collection> collections{col};

        // (a) TUDO marcado: coleções e registros presentes.
        ConfigManager::ExportSelection all;
        const QJsonObject full = QJsonDocument::fromJson(
            ConfigManager::exportSelective(all, settings, data, collections).toUtf8()).object();
        QVERIFY(full.contains(QStringLiteral("settings")));
        QVERIFY(full.contains(QStringLiteral("commands")));
        QVERIFY2(full.contains(QStringLiteral("collections")),
                 "coleções deveriam estar no export completo");
        QCOMPARE(full.value(QStringLiteral("collections")).toArray().size(), 1);

        // (b) só coleções SEM registros: estrutura sai, entries vazias.
        ConfigManager::ExportSelection structureOnly;
        structureOnly.settings = false;
        structureOnly.commands = false;
        structureOnly.environments = false;
        structureOnly.collections = true;
        structureOnly.collectionEntries = false;
        const QJsonObject partial = QJsonDocument::fromJson(
            ConfigManager::exportSelective(structureOnly, settings, data, collections).toUtf8()).object();
        QVERIFY2(!partial.contains(QStringLiteral("commands")), "comandos não deveriam sair");
        QVERIFY(partial.contains(QStringLiteral("collections")));
        const QJsonObject exportedCol =
            partial.value(QStringLiteral("collections")).toArray().at(0).toObject();
        QCOMPARE(exportedCol.value(QStringLiteral("name")).toString(), QStringLiteral("Clientes"));
        QVERIFY2(exportedCol.value(QStringLiteral("entries")).toArray().isEmpty(),
                 "registros NÃO deveriam sair quando 'dados' está desmarcado");

        // (c) round-trip: a importação enxerga as coleções.
        const ConfigManager::ImportResult imported =
            ConfigManager::importFromJson(
                ConfigManager::exportSelective(all, settings, data, collections));
        QVERIFY(imported.ok);
        QVERIFY2(imported.hasCollections, "importação deveria trazer as coleções");
        QCOMPARE(imported.collections.size(), 1);
        QCOMPARE(imported.collections.at(0).name, QStringLiteral("Clientes"));
        QCOMPARE(imported.collections.at(0).entries.size(), 1);
    }

    // AUDITORIA (achado real): exportGlobal() nunca escrevia Environments —
    // um "backup completo" perdia todos os pacotes de ambiente (nomes/vars/
    // segredos). Cobre o round-trip export -> import.
    void exportGlobalRoundTripsEnvironmentsAndSecrets()
    {
        SettingsData settings;
        Environment env;
        env.id = QStringLiteral("env_prod");
        env.name = QStringLiteral("Produção");
        env.vars.insert(QStringLiteral("API_KEY"), QStringLiteral("shh"));
        env.secretKeys.insert(QStringLiteral("API_KEY"));
        settings.environments = {env};
        settings.activeEnvironmentId = QStringLiteral("env_prod");

        const QString json = ConfigManager::exportGlobal(settings, sampleData());
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        QVERIFY2(r.hasEnvironments, "pacote com Environments deveria marcar hasEnvironments");
        QCOMPARE(r.settings.environments.size(), 1);
        QCOMPARE(r.settings.environments.at(0).id, QStringLiteral("env_prod"));
        QCOMPARE(r.settings.environments.at(0).vars.value(QStringLiteral("API_KEY")), QStringLiteral("shh"));
        QVERIFY(r.settings.environments.at(0).secretKeys.contains(QStringLiteral("API_KEY")));
        QCOMPARE(r.settings.activeEnvironmentId, QStringLiteral("env_prod"));
    }

    // AUDITORIA: exportGlobal gravava use_pty/is_default/shell/icon do
    // TerminalProfile, mas a importação só lia name/command_template — os
    // outros 4 campos eram silenciosamente descartados num reimport.
    void terminalProfileRoundTripsAllFields()
    {
        SettingsData settings;
        TerminalProfile wsl;
        wsl.name = QStringLiteral("WSL");
        wsl.commandTemplate = QStringLiteral("wsl -- {{command}}");
        wsl.usePty = false;
        wsl.isDefault = true;
        wsl.shell = ShellFlavor::Posix;
        wsl.icon = QStringLiteral("terminal");
        settings.terminalProfiles.append(wsl);

        const ConfigManager::ImportResult r = ConfigManager::importFromJson(
            ConfigManager::exportGlobal(settings, sampleData()));
        QVERIFY(r.ok);
        QCOMPARE(r.settings.terminalProfiles.size(), 1);
        const TerminalProfile &t = r.settings.terminalProfiles.at(0);
        QCOMPARE(t.usePty, false);
        QCOMPARE(t.isDefault, true);
        QCOMPARE(t.shell, ShellFlavor::Posix);
        QCOMPARE(t.icon, QStringLiteral("terminal"));
    }

    // AUDITORIA (achado real, o mais grave): mergeImportResult() só aplicava
    // activeTheme + terminalProfiles do pacote importado — todo o resto das
    // preferências gerais (densidade, posicionamento, janela, efeitos
    // visuais...) era descartado antes de chegar em saveSettings(). Precisa
    // de um ConfigManager de verdade (isola em XDG_CONFIG_HOME temporário).
    void mergeImportResultAppliesFullSettingsSnapshot()
    {
        auto tempDir = std::make_unique<QTemporaryDir>();
        QVERIFY(tempDir->isValid());
        qputenv("XDG_CONFIG_HOME", tempDir->path().toUtf8());

        ConfigManager manager;
        SettingsData before = manager.loadSettings();
        before.uiDensity = QStringLiteral("compact");
        QVERIFY(manager.saveSettings(before));

        SettingsData imported;
        imported.activeTheme = QStringLiteral("kai-light");
        imported.uiDensity = QStringLiteral("comfortable");
        imported.outputCompact = true;
        imported.windowMode = QStringLiteral("maximized");

        ConfigManager::ImportResult result;
        result.ok = true;
        result.hasSettings = true;
        result.settings = imported;

        QVERIFY(manager.mergeImportResult(result));

        const SettingsData after = manager.loadSettings();
        QCOMPARE(after.activeTheme, QStringLiteral("kai-light"));
        QCOMPARE(after.uiDensity, QStringLiteral("comfortable"));
        QCOMPARE(after.outputCompact, true);
        QCOMPARE(after.windowMode, QStringLiteral("maximized"));
    }

    // AUDITORIA: Environments deve ser MESCLADO por id (reimportar um backup
    // não pode apagar pacotes criados localmente depois daquele backup).
    void mergeImportResultMergesEnvironmentsById()
    {
        auto tempDir = std::make_unique<QTemporaryDir>();
        QVERIFY(tempDir->isValid());
        qputenv("XDG_CONFIG_HOME", tempDir->path().toUtf8());

        ConfigManager manager;
        SettingsData before = manager.loadSettings();
        Environment local;
        local.id = QStringLiteral("env_local");
        local.name = QStringLiteral("Local (criado depois do backup)");
        before.environments.append(local);
        QVERIFY(manager.saveSettings(before));

        Environment imported;
        imported.id = QStringLiteral("env_prod");
        imported.name = QStringLiteral("Produção");
        imported.vars.insert(QStringLiteral("HOST"), QStringLiteral("prod.example.com"));

        ConfigManager::ImportResult result;
        result.ok = true;
        result.hasEnvironments = true;
        result.settings.environments = {imported};

        QVERIFY(manager.mergeImportResult(result));

        const SettingsData after = manager.loadSettings();
        // Os dois devem coexistir: o local (preservado) + o importado (novo).
        bool hasLocal = false;
        bool hasImported = false;
        for (const Environment &e : after.environments) {
            if (e.id == QStringLiteral("env_local")) hasLocal = true;
            if (e.id == QStringLiteral("env_prod")) hasImported = true;
        }
        QVERIFY2(hasLocal, "Environment local não deveria ser apagado pela importação");
        QVERIFY2(hasImported, "Environment importado deveria ter sido adicionado");
    }
};

QTEST_MAIN(TestConfigImportExport)
#include "test_config_import_export.moc"
