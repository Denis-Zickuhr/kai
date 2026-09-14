#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <algorithm>

#include "core/config-manager.h"
#include "utils/translation-manager.h"
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
    // assets/manifesto/kai.schema.json): um export ENXUTO ainda vazava o id
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
        c.type = CommandType::Command;
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
        c.type = CommandType::Command;
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
    // segredos). Cobre o round-trip export -> import (sem o valor dos segredos).
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
        // O VALOR de uma variável secreta nunca sai do Kai: o nome e a marca de secreta voltam, o valor não.
        QVERIFY(!json.contains(QStringLiteral("shh")));
        QVERIFY(r.settings.environments.at(0).vars.contains(QStringLiteral("API_KEY")));
        QVERIFY(r.settings.environments.at(0).vars.value(QStringLiteral("API_KEY")).isEmpty());
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

    // ---- Ações de pasta / ações globais ------------------------------------
private:
    // "Repo" (com 'Pull' dentro) tem como ações o 'Pull' (no pacote) e o
    // 'Fetch', que mora FORA da pasta.
    static CommandsData actionsData()
    {
        Folder repo;
        repo.id = QStringLiteral("f_repo");
        repo.name = QStringLiteral("Repo");
        repo.actions = {QStringLiteral("c_fetch"), QStringLiteral("c_pull")};
        Folder tools;
        tools.id = QStringLiteral("f_tools");
        tools.name = QStringLiteral("Tools");
        Command fetch;
        fetch.id = QStringLiteral("c_fetch");
        fetch.name = QStringLiteral("Fetch");
        fetch.folderId = QStringLiteral("f_tools");
        fetch.command = QStringLiteral("git fetch");
        Command pull;
        pull.id = QStringLiteral("c_pull");
        pull.name = QStringLiteral("Pull");
        pull.folderId = QStringLiteral("f_repo");
        pull.command = QStringLiteral("git pull");
        return {{repo, tools}, {fetch, pull}};
    }
    static const Folder *folderById(const QVector<Folder> &folders, const QString &id)
    {
        for (const Folder &f : folders) if (f.id == id) return &f;
        return nullptr;
    }

private slots:
    void leanFolderExportCarriesActionsByNameAndResolvesWhatIsInThePackage()
    {
        const QString json = ConfigManager::exportFolder(QStringLiteral("f_repo"), actionsData());
        QVERIFY2(!json.contains(QStringLiteral("c_pull")) && !json.contains(QStringLiteral("c_fetch")),
                 "o formato enxuto não leva ids de comando");
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);

        // O próprio "Repo" volta como pasta (root_folder); 'Pull' é do pacote,
        // então já vem resolvido pro id novo; 'Fetch' mora fora: fica pendente por nome.
        const Folder *repo = nullptr;
        for (const Folder &f : r.folders) if (f.name == QStringLiteral("Repo")) repo = &f;
        QVERIFY(repo);
        QCOMPARE(r.commands.size(), 1);
        QCOMPARE(repo->actions, QStringList{r.commands.first().id});
        QCOMPARE(r.pendingFolderActions.value(repo->id), QStringList{QStringLiteral("Fetch")});
    }

    // A flag de EXPANSÃO viaja por nome junto com a ação: a do pacote já vem resolvida, a que
    // mora fora fica pendente e é marcada quando casa com um comando existente.
    void expansionFlagTravelsWithFolderActionsByName()
    {
        CommandsData data = actionsData();
        data.folders[0].expansionActions = {QStringLiteral("c_fetch"), QStringLiteral("c_pull")};
        const QString json = ConfigManager::exportFolder(QStringLiteral("f_repo"), data);
        QVERIFY2(!json.contains(QStringLiteral("c_fetch")) && !json.contains(QStringLiteral("c_pull")),
                 "o formato enxuto não leva ids de comando");
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        const Folder *repo = nullptr;
        for (const Folder &f : r.folders) if (f.name == QStringLiteral("Repo")) repo = &f;
        QVERIFY(repo);
        QCOMPARE(repo->expansionActions, QStringList{r.commands.first().id});
        QCOMPARE(r.pendingFolderExpansion.value(repo->id), QStringList{QStringLiteral("Fetch")});

        QVector<Folder> folders = r.folders;
        QVector<GlobalAction> global;
        QCOMPARE(ConfigManager::applyImportedActions(r, folders, actionsData().commands + r.commands, global), 0);
        for (const Folder &f : folders) {
            if (f.name != QStringLiteral("Repo")) continue;
            QCOMPARE(f.actions.size(), 2);
            QVERIFY(f.expansionActions.contains(QStringLiteral("c_fetch")));
            QVERIFY(f.expansionActions.contains(r.commands.first().id));
        }

        // Só 'Fetch' como expansão: 'Pull' continua um ícone comum.
        data.folders[0].expansionActions = {QStringLiteral("c_fetch")};
        const ConfigManager::ImportResult some =
            ConfigManager::importFromJson(ConfigManager::exportFolder(QStringLiteral("f_repo"), data));
        for (const Folder &f : some.folders) {
            if (f.name == QStringLiteral("Repo")) QVERIFY(f.expansionActions.isEmpty());
        }
        QCOMPARE(some.pendingFolderExpansion.size(), 1);

        // Também pelo caminho de "levar os comandos das ações" (action_only).
        const ConfigManager::ImportResult withCommands = ConfigManager::importFromJson(
            ConfigManager::exportFolder(QStringLiteral("f_repo"), data, {}, {}, true, true));
        QCOMPARE(withCommands.pendingFolderExpansion.size(), 1);
        QCOMPARE(withCommands.pendingFolderExpansion.constBegin().value(), QStringList{QStringLiteral("Fetch")});

        // Sem lean (ids): a lista de expansão atravessa como está.
        const ConfigManager::ImportResult raw = ConfigManager::importFromJson(
            ConfigManager::exportFolder(QStringLiteral("f_repo"), data, {}, {}, /*lean=*/false));
        const Folder *rawRepo = folderById(raw.folders, QStringLiteral("f_repo"));
        QVERIFY(rawRepo);
        QCOMPARE(rawRepo->expansionActions, QStringList{QStringLiteral("c_fetch")});
    }

    // O grupo (e o ícone dele) viaja com a ação, por nome do comando.
    void actionGroupsTravelWithFolderActionsByName()
    {
        CommandsData data = actionsData();
        data.folders[0].actionGroups = {{QStringLiteral("c_fetch"), QStringLiteral("Git")}, {QStringLiteral("c_pull"), QStringLiteral("Git")}};
        data.folders[0].groupIcons = {{QStringLiteral("Git"), QStringLiteral("git-branch")}};
        const QString json = ConfigManager::exportFolder(QStringLiteral("f_repo"), data);
        QVERIFY2(!json.contains(QStringLiteral("c_fetch")) && !json.contains(QStringLiteral("c_pull")), "sem ids no formato enxuto");
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        const Folder *repo = nullptr;
        for (const Folder &f : r.folders) if (f.name == QStringLiteral("Repo")) repo = &f;
        QVERIFY(repo);
        // 'Pull' está no pacote (já resolvido); 'Fetch' mora fora (pendente por nome).
        QCOMPARE(repo->actionGroups, (QMap<QString, QString>{{r.commands.first().id, QStringLiteral("Git")}}));
        QCOMPARE(repo->groupIcons.value(QStringLiteral("Git")), QStringLiteral("git-branch"));
        QCOMPARE(r.pendingFolderGroups.value(repo->id).value(QStringLiteral("Fetch")), QStringLiteral("Git"));

        QVector<Folder> folders = r.folders;
        QVector<GlobalAction> global;
        QCOMPARE(ConfigManager::applyImportedActions(r, folders, actionsData().commands + r.commands, global), 0);
        for (const Folder &f : folders) {
            if (f.name == QStringLiteral("Repo")) QCOMPARE(f.actionGroups.value(QStringLiteral("c_fetch")), QStringLiteral("Git"));
        }

        // Pelo caminho "levar os comandos das ações" também.
        const ConfigManager::ImportResult withCommands = ConfigManager::importFromJson(
            ConfigManager::exportFolder(QStringLiteral("f_repo"), data, {}, {}, true, true));
        QCOMPARE(withCommands.pendingFolderGroups.constBegin().value().value(QStringLiteral("Fetch")), QStringLiteral("Git"));

        // Sem lean (ids): atravessa como está.
        const ConfigManager::ImportResult raw = ConfigManager::importFromJson(
            ConfigManager::exportFolder(QStringLiteral("f_repo"), data, {}, {}, /*lean=*/false));
        const Folder *rawRepo = folderById(raw.folders, QStringLiteral("f_repo"));
        QVERIFY(rawRepo);
        QCOMPARE(rawRepo->actionGroups.value(QStringLiteral("c_fetch")), QStringLiteral("Git"));
    }

    // O VALOR de uma variável secreta nunca sai do Kai: nem na exportação de pasta (enxuta ou não), nem na de
    // pacotes de ambiente. O arquivo leva o nome; quem importa recebe a variável vazia.
    void secretValuesNeverLeaveInAnyExport()
    {
        CommandsData data = actionsData();
        data.folders[0].envVars = {{QStringLiteral("PORT"), QStringLiteral("8080")}, {QStringLiteral("TOKEN"), QStringLiteral("s3cr3t-value")}};
        data.folders[0].secretEnvKeys = {QStringLiteral("TOKEN")};
        for (bool lean : {true, false}) {
            const QString json = ConfigManager::exportFolder(QStringLiteral("f_repo"), data, {}, {}, lean);
            QVERIFY2(!json.contains(QStringLiteral("s3cr3t-value")), qPrintable(QString::number(lean)));
            QVERIFY(json.contains(QStringLiteral("TOKEN")));  // o nome vai
            QVERIFY(json.contains(QStringLiteral("8080")));   // a variável comum também
            const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
            const Folder *repo = nullptr;
            for (const Folder &f : r.folders) if (f.name == QStringLiteral("Repo") || f.id == QStringLiteral("f_repo")) repo = &f;
            QVERIFY(repo);
            QCOMPARE(repo->secretEnvKeys, QSet<QString>{QStringLiteral("TOKEN")});
            QVERIFY(repo->envVars.value(QStringLiteral("TOKEN")).isEmpty());
            QCOMPARE(repo->envVars.value(QStringLiteral("PORT")), QStringLiteral("8080"));
        }

        // Pacotes de ambiente (exportação global): mesmo critério.
        SettingsData settings;
        Environment env;
        env.id = QStringLiteral("e1");
        env.name = QStringLiteral("Dev");
        env.vars = {{QStringLiteral("HOST"), QStringLiteral("localhost")}, {QStringLiteral("KEY"), QStringLiteral("k-secret-value")}};
        env.secretKeys = {QStringLiteral("KEY")};
        settings.environments = {env};
        const QString global = ConfigManager::exportGlobal(settings, data);
        QVERIFY(!global.contains(QStringLiteral("k-secret-value")));
        const ConfigManager::ImportResult imported = ConfigManager::importFromJson(global);
        QCOMPARE(imported.settings.environments.size(), 1);
        QCOMPARE(imported.settings.environments.first().vars.value(QStringLiteral("HOST")), QStringLiteral("localhost"));
        QVERIFY(imported.settings.environments.first().vars.contains(QStringLiteral("KEY")));
        QVERIFY(imported.settings.environments.first().vars.value(QStringLiteral("KEY")).isEmpty());
    }

    void pendingActionsAreMatchedByUniqueNameAgainstExistingCommands()
    {
        const ConfigManager::ImportResult r =
            ConfigManager::importFromJson(ConfigManager::exportFolder(QStringLiteral("f_repo"), actionsData()));
        QVector<Folder> folders = r.folders;
        QVector<GlobalAction> global;

        // Destino com UM "Fetch": liga; a ordem de cadastro fica (Pull do pacote, depois Fetch).
        QVector<Command> existing = actionsData().commands;
        QVector<Command> all = existing + r.commands;
        QCOMPARE(ConfigManager::applyImportedActions(r, folders, all, global), 0);
        const Folder *repo = nullptr;
        for (const Folder &f : folders) if (f.name == QStringLiteral("Repo")) repo = &f;
        QVERIFY(repo);
        QCOMPARE(repo->actions.size(), 2);
        QCOMPARE(repo->actions.last(), QStringLiteral("c_fetch"));

        // Dois "Fetch": ambíguo, não liga nenhum e conta como não resolvido.
        QVector<Folder> again = r.folders;
        Command twin = existing.first();
        twin.id = QStringLiteral("c_fetch_2");
        QVector<Command> ambiguous = all + QVector<Command>{twin};
        QCOMPARE(ConfigManager::applyImportedActions(r, again, ambiguous, global), 1);
        for (const Folder &f : again) if (f.name == QStringLiteral("Repo")) QCOMPARE(f.actions.size(), 1);

        // Nenhum "Fetch": também não resolvido.
        QVector<Folder> none = r.folders;
        QCOMPARE(ConfigManager::applyImportedActions(r, none, r.commands, global), 1);
    }

    void fullFolderExportKeepsActionIdsStable()
    {
        const QString json = ConfigManager::exportFolder(QStringLiteral("f_repo"), actionsData(), {}, {}, /*lean=*/false);
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        const Folder *repo = folderById(r.folders, QStringLiteral("f_repo"));
        QVERIFY(repo);
        QCOMPARE(repo->actions, (QStringList{"c_fetch", "c_pull"}));
        QVERIFY(r.pendingFolderActions.isEmpty());
    }

    // ---- Comandos das ações de pasta levados no export ----------------------
    void folderExportCarriesTheCommandsOfItsActionsOnlyWhenAsked()
    {
        // Sem o pedido: como sempre, só a pasta (o 'Fetch' de fora fica por nome).
        const ConfigManager::ImportResult plain =
            ConfigManager::importFromJson(ConfigManager::exportFolder(QStringLiteral("f_repo"), actionsData()));
        QVERIFY(plain.actionCommands.isEmpty());

        const QString json = ConfigManager::exportFolder(QStringLiteral("f_repo"), actionsData(), {}, {}, true, true);
        QVERIFY(json.contains(QStringLiteral("action_only")));
        QVERIFY2(!json.contains(QStringLiteral("c_fetch")), "o formato enxuto continua sem ids");
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        // 'Pull' é da pasta; 'Fetch' vem à parte e NÃO entra como comando da pasta.
        QCOMPARE(r.commands.size(), 1);
        QCOMPARE(r.commands.first().name, QStringLiteral("Pull"));
        QCOMPARE(r.actionCommands.size(), 1);
        QCOMPARE(r.actionCommands.first().name, QStringLiteral("Fetch"));
        QCOMPARE(r.actionCommands.first().command, QStringLiteral("git fetch"));
        const Folder *repo = nullptr;
        for (const Folder &f : r.folders) if (f.name == QStringLiteral("Repo")) repo = &f;
        QVERIFY(repo);
        // O vínculo do 'Fetch' vira referência por nome (só liga se o comando existir).
        QCOMPARE(repo->actions, QStringList{r.commands.first().id});
        QCOMPARE(r.pendingFolderActions.value(repo->id), QStringList{QStringLiteral("Fetch")});

        // O que o dialog de exportar mostra: só o que mora fora da pasta.
        const QVector<Command> outside = ConfigManager::actionCommandsOutsideFolder(QStringLiteral("f_repo"), actionsData());
        QCOMPARE(outside.size(), 1);
        QCOMPARE(outside.first().id, QStringLiteral("c_fetch"));
    }

    void fullFolderExportAlsoCarriesActionCommandsSeparately()
    {
        const QString json = ConfigManager::exportFolder(QStringLiteral("f_repo"), actionsData(), {}, {}, /*lean=*/false, true);
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        QCOMPARE(r.commands.size(), 1);
        QCOMPARE(r.actionCommands.size(), 1);
        QCOMPARE(r.actionCommands.first().id, QStringLiteral("c_fetch"));
        const Folder *repo = folderById(r.folders, QStringLiteral("f_repo"));
        QVERIFY(repo);
        QCOMPARE(repo->actions, QStringList{QStringLiteral("c_pull")});
        QCOMPARE(r.pendingFolderActions.value(QStringLiteral("f_repo")), QStringList{QStringLiteral("Fetch")});
    }

    void onlyActionCommandsTheUserLacksAreImported()
    {
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(
            ConfigManager::exportFolder(QStringLiteral("f_repo"), actionsData(), {}, {}, true, true));
        // Quem não tem nenhum 'Fetch': importa.
        QCOMPARE(ConfigManager::actionCommandsToImport(r, {}).size(), 1);
        // Quem já tem (mesmo nome): nada a importar.
        QVector<Command> hasFetch;
        Command mine;
        mine.id = QStringLiteral("c_mine");
        mine.name = QStringLiteral("Fetch");
        hasFetch << mine;
        QVERIFY(ConfigManager::actionCommandsToImport(r, hasFetch).isEmpty());
    }

    void mergingAnImportBringsTheMissingActionCommandsAndLinksThem()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(
            ConfigManager::exportFolder(QStringLiteral("f_repo"), actionsData(), {}, {}, true, true));

        ConfigManager manager;
        QVERIFY(manager.mergeImportResult(r));
        const CommandsData after = manager.loadCommands();
        // 'Fetch' veio para a pasta "Ações importadas" e a pasta Repo ficou com as duas ações.
        const Command *fetch = nullptr;
        for (const Command &c : after.commands) if (c.name == QStringLiteral("Fetch")) fetch = &c;
        QVERIFY(fetch);
        const Folder *holder = folderById(after.folders, fetch->folderId);
        QVERIFY(holder);
        QCOMPARE(holder->name, kai::utils::tr(QStringLiteral("import.actions_folder")));
        QVERIFY(!holder->parentId.has_value());
        const Folder *repo = nullptr;
        for (const Folder &f : after.folders) if (f.name == QStringLiteral("Repo")) repo = &f;
        QVERIFY(repo);
        QCOMPARE(repo->actions.size(), 2);
        QVERIFY(repo->actions.contains(fetch->id));
    }

    void mergingWhenTheUserAlreadyHasTheCommandReusesItInsteadOfCopying()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        ConfigManager manager;
        CommandsData mine;
        Folder own;
        own.id = QStringLiteral("f_own");
        own.name = QStringLiteral("Own");
        Command fetch;
        fetch.id = QStringLiteral("c_user_fetch");
        fetch.name = QStringLiteral("Fetch");
        fetch.folderId = QStringLiteral("f_own");
        fetch.command = QStringLiteral("git fetch --all");
        mine.folders << own;
        mine.commands << fetch;
        QVERIFY(manager.saveCommands(mine));

        const ConfigManager::ImportResult r = ConfigManager::importFromJson(
            ConfigManager::exportFolder(QStringLiteral("f_repo"), actionsData(), {}, {}, true, true));
        QVERIFY(manager.mergeImportResult(r));
        const CommandsData after = manager.loadCommands();
        int fetches = 0;
        for (const Command &c : after.commands) if (c.name == QStringLiteral("Fetch")) ++fetches;
        QCOMPARE(fetches, 1); // só o do usuário
        for (const Folder &f : after.folders) {
            QVERIFY2(f.name != kai::utils::tr(QStringLiteral("import.actions_folder")), "não cria a pasta sem necessidade");
            if (f.name == QStringLiteral("Repo")) QVERIFY(f.actions.contains(QStringLiteral("c_user_fetch")));
        }
    }

    void globalActionsAreSavedAndLoadedInTheSettings()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        ConfigManager manager;
        SettingsData settings = manager.loadSettings();
        settings.globalActions = {{QStringLiteral("c_fetch"), true, true}, {QStringLiteral("c_status"), false, false}};
        QVERIFY(manager.saveSettings(settings));
        const QVector<GlobalAction> loaded = manager.loadSettings().globalActions;
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(0).commandId, QStringLiteral("c_fetch"));
        QVERIFY(loaded.at(0).onlyProjects);
        QVERIFY(!loaded.at(1).onlyProjects);
        QVERIFY(loaded.at(0).expansion);
        QVERIFY(!loaded.at(1).expansion);
        QVERIFY(loaded.at(0).group.isEmpty());
    }

    void globalActionGroupsAreSavedAndLoadedInTheSettings()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        ConfigManager manager;
        SettingsData settings = manager.loadSettings();
        settings.globalActions = {{QStringLiteral("c_fetch"), false, false, QStringLiteral("Git"), QStringLiteral("git-branch")},
                                  {QStringLiteral("c_status"), false, false, QString(), QStringLiteral("ignored")}};
        QVERIFY(manager.saveSettings(settings));
        const QVector<GlobalAction> loaded = manager.loadSettings().globalActions;
        QCOMPARE(loaded.at(0).group, QStringLiteral("Git"));
        QCOMPARE(loaded.at(0).groupIcon, QStringLiteral("git-branch"));
        QVERIFY(loaded.at(1).group.isEmpty());
        QVERIFY(loaded.at(1).groupIcon.isEmpty()); // ícone sem grupo não existe
    }

    void globalActionsTravelByNameAndRespectTheSelection()
    {
        SettingsData settings;
        settings.globalActions = {{QStringLiteral("c_fetch"), true, true}, {QStringLiteral("c_pull"), false, false, QStringLiteral("Git"), QStringLiteral("git-branch")}};
        ConfigManager::ExportSelection all;
        const QString lean = ConfigManager::exportSelective(all, settings, actionsData(), {}, /*lean=*/true);
        QVERIFY(!lean.contains(QStringLiteral("\"command_id\"")));
        QVERIFY(lean.contains(QStringLiteral("\"only_projects\"")));

        // Com os comandos no mesmo pacote: resolvidos pros ids novos.
        const ConfigManager::ImportResult withCommands = ConfigManager::importFromJson(lean);
        QVERIFY(withCommands.hasGlobalActions);
        QCOMPARE(withCommands.settings.globalActions.size(), 2);
        QVERIFY(withCommands.pendingGlobalActions.isEmpty());
        QVERIFY(withCommands.settings.globalActions.at(0).onlyProjects);
        QVERIFY(withCommands.settings.globalActions.at(0).expansion);
        QVERIFY(!withCommands.settings.globalActions.at(1).expansion);
        QCOMPARE(withCommands.settings.globalActions.at(1).group, QStringLiteral("Git"));
        QCOMPARE(withCommands.settings.globalActions.at(1).groupIcon, QStringLiteral("git-branch"));
        QSet<QString> importedIds;
        for (const Command &c : withCommands.commands) importedIds.insert(c.id);
        for (const GlobalAction &g : withCommands.settings.globalActions) QVERIFY(importedIds.contains(g.commandId));

        // Sem os comandos no pacote: ficam pendentes por nome (pra casar com os que já existem).
        ConfigManager::ExportSelection onlyActions;
        onlyActions.settings = false;
        onlyActions.commands = false;
        onlyActions.environments = false;
        onlyActions.collections = false;
        onlyActions.terminalProfiles = false;
        const ConfigManager::ImportResult pending =
            ConfigManager::importFromJson(ConfigManager::exportSelective(onlyActions, settings, actionsData(), {}, true));
        QVERIFY(pending.hasGlobalActions);
        QVERIFY(pending.settings.globalActions.isEmpty());
        QCOMPARE(pending.pendingGlobalActions.size(), 2);
        QCOMPARE(pending.pendingGlobalActions.at(0).name, QStringLiteral("Fetch"));
        QVERIFY(pending.pendingGlobalActions.at(0).onlyProjects);
        QVERIFY(pending.pendingGlobalActions.at(0).expansion);
        QVERIFY(!pending.pendingGlobalActions.at(1).expansion);
        QCOMPARE(pending.pendingGlobalActions.at(1).group, QStringLiteral("Git"));
        QVector<Folder> folders;
        QVector<GlobalAction> global;
        QCOMPARE(ConfigManager::applyImportedActions(pending, folders, actionsData().commands, global), 0);
        QCOMPARE(global.size(), 2);
        QCOMPARE(global.at(0).commandId, QStringLiteral("c_fetch"));
        QVERIFY(global.at(0).expansion);
        QCOMPARE(global.at(1).group, QStringLiteral("Git"));
        QCOMPARE(global.at(1).groupIcon, QStringLiteral("git-branch"));
        // Aplicar de novo não repete.
        QCOMPARE(ConfigManager::applyImportedActions(pending, folders, actionsData().commands, global), 0);
        QCOMPARE(global.size(), 2);

        // Desmarcado: a chave nem sai.
        ConfigManager::ExportSelection without;
        without.globalActions = false;
        QVERIFY(!ConfigManager::exportSelective(without, settings, actionsData(), {}, true)
                     .contains(QStringLiteral("global_actions")));
    }

    void mergingAnImportAppliesGlobalAndFolderActions()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        ConfigManager manager;
        // Destino: já tem o "Fetch" (fora do pacote que vai ser importado) e uma ação global antiga.
        CommandsData existing;
        existing.commands = {actionsData().commands.first()};
        QVERIFY(manager.saveCommands(existing));
        SettingsData settings = manager.loadSettings();
        settings.globalActions = {{QStringLiteral("c_old"), false}};
        QVERIFY(manager.saveSettings(settings));

        SettingsData incoming;
        incoming.globalActions = {{QStringLiteral("c_fetch"), true}};
        ConfigManager::ExportSelection sel;
        sel.settings = false;
        sel.environments = false;
        sel.terminalProfiles = false;
        sel.collections = false;
        const QString json = ConfigManager::exportSelective(sel, incoming, actionsData(), {}, true);
        const ConfigManager::ImportResult r = ConfigManager::importFromJson(json);
        QVERIFY(r.ok);
        QVERIFY(manager.mergeImportResult(r));

        const QVector<GlobalAction> merged = manager.loadSettings().globalActions;
        QCOMPARE(merged.size(), 2);
        QCOMPARE(merged.at(0).commandId, QStringLiteral("c_old"));      // o que já existia fica
        QVERIFY(merged.at(1).onlyProjects);                             // a importada entra, com a flag
        // Resolvida pro comando importado (o pacote levava os comandos).
        bool found = false;
        for (const Command &c : manager.loadCommands().commands) found |= (c.id == merged.at(1).commandId);
        QVERIFY(found);
    }

    // Estado local (últimos valores, histórico de parâmetros, respostas KIP) e o caminho de origem de uma
    // coleção ficam no app: nenhum export (pasta, comando, seletivo/global) os leva para o arquivo.
    void exportsNeverCarryLocalStateOrACollectionSourcePath()
    {
        CommandsData data = sampleData();
        data.commands[0].paramUsageHistory.insert(QStringLiteral("env"), QStringList{QStringLiteral("dev"), QStringLiteral("prod")});
        data.commands[0].kipLastValues = QJsonObject{{"p/f", "x"}};
        // Guarda que o app ainda persiste tudo isso (só o export é que corta).
        const QJsonObject stored = data.commands[0].toJson();
        QVERIFY(stored.contains(QStringLiteral("last_param_values")));
        QVERIFY(stored.contains(QStringLiteral("param_usage_history")));
        QVERIFY(stored.contains(QStringLiteral("kip_last_values")));

        Collection col;
        col.id = QStringLiteral("col1");
        col.folderId = QStringLiteral("f_root");
        col.name = QStringLiteral("Clientes");
        col.sourcePath = QStringLiteral("/home/eu/clientes.csv");
        QVERIFY(col.toJson().contains(QStringLiteral("source_path")));

        const QStringList exports = {
            ConfigManager::exportFolder(QStringLiteral("f_root"), data, {col}, {}, false),
            ConfigManager::exportFolder(QStringLiteral("f_root"), data, {col}, {}, true),
            ConfigManager::exportCommand(QStringLiteral("c1"), data, {col}, {}, false),
            ConfigManager::exportCommand(QStringLiteral("c1"), data, {col}, {}, true),
            ConfigManager::exportSelective(ConfigManager::ExportSelection{}, SettingsData{}, data, {col}, false),
            ConfigManager::exportSelective(ConfigManager::ExportSelection{}, SettingsData{}, data, {col}, true),
        };
        for (const QString &json : exports) {
            QVERIFY2(json.contains(QStringLiteral("Build")), "o comando foi exportado");
            QVERIFY2(!json.contains(QStringLiteral("last_param_values")), qPrintable(json));
            QVERIFY2(!json.contains(QStringLiteral("param_usage_history")), qPrintable(json));
            QVERIFY2(!json.contains(QStringLiteral("kip_last_values")), qPrintable(json));
            QVERIFY2(!json.contains(QStringLiteral("source_path")), qPrintable(json));
        }
        QVERIFY(exports.first().contains(QStringLiteral("Clientes")));
    }

    void linkedCollectionsOfAFolderAreTheOnesInsideItAndTheOnesItsParametersUse()
    {
        CommandsData data = sampleData();
        Parameter p;
        p.name = QStringLiteral("cliente");
        p.type = ParameterType::Select;
        p.collectionId = QStringLiteral("usada");
        data.commands[1].params << p; // comando da subpasta

        Collection inside;
        inside.id = QStringLiteral("dentro");
        inside.folderId = QStringLiteral("f_sub");
        Collection used;
        used.id = QStringLiteral("usada");
        used.folderId = QStringLiteral("fora");
        Collection other;
        other.id = QStringLiteral("outra");
        other.folderId = QStringLiteral("fora");

        const QVector<Collection> linked =
            ConfigManager::linkedCollectionsForFolder(QStringLiteral("f_root"), data, {inside, used, other});
        QStringList ids;
        for (const Collection &c : linked) ids << c.id;
        ids.sort();
        QCOMPARE(ids, (QStringList{QStringLiteral("dentro"), QStringLiteral("usada")}));
        QVERIFY(ConfigManager::linkedCollectionsForFolder(QStringLiteral("f_sub"), data, {other}).isEmpty());
    }
};

QTEST_MAIN(TestConfigImportExport)
#include "test_config_import_export.moc"
