#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/config-manager.h"
#include "core/models.h"
#include "core/yaml-bridge.h"
#include "engine/project-sync-manager.h"

using namespace kai::core;
namespace core = kai::core;
using namespace kai::engine;
using Status = SyncPlan::Status;

namespace {

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// Escreve o arquivo do projeto: o fixture vem em JSON (legível no teste) e vai pro disco como YAML, o
// único formato do arquivo. Texto que não é JSON (um arquivo quebrado de propósito) vai como está.
void writeKaiFile(const QString &path, const QString &jsonText);

void writeAll(const QString &path, const QString &content)
{
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    f.write(content.toUtf8());
}

void writeKaiFile(const QString &path, const QString &jsonText)
{
    const bool isJson = !QJsonDocument::fromJson(jsonText.toUtf8()).isNull();
    writeAll(path, isJson ? core::jsonTextToYamlText(jsonText) : jsonText);
}

Command makeCommand(const QString &id, const QString &folderId, const QString &name)
{
    Command c;
    c.id = id;
    c.folderId = folderId;
    c.name = name;
    c.type = CommandType::Command;
    c.command = QStringLiteral("echo ") + name;
    return c;
}

// Uma pasta-projeto "proj" apontando pra `dir`, com uma subpasta e 2 comandos.
CommandsData sampleTree(const QString &dir)
{
    CommandsData data;
    Folder project;
    project.id = QStringLiteral("proj");
    project.name = QStringLiteral("Proj");
    project.isProject = true;
    project.workingDirMode = WorkingDirMode::Custom;
    project.workingDir = dir;
    Folder sub;
    sub.id = QStringLiteral("proj_sub");
    sub.name = QStringLiteral("Sub");
    sub.parentId = project.id;
    data.folders << project << sub;
    data.commands << makeCommand(QStringLiteral("c_root"), project.id, QStringLiteral("Raiz"))
                  << makeCommand(QStringLiteral("c_sub"), sub.id, QStringLiteral("Filha"));
    return data;
}

SyncPlan plan(SyncDirection direction, const CommandsData &data, const QString &folderId = QStringLiteral("proj"),
              const QString &lastFile = QString(), const QString &lastKai = QString())
{
    ProjectSyncManager::PlanInputs in;
    in.direction = direction;
    in.folderId = folderId;
    in.data = data;
    in.lastFileHash = lastFile;
    in.lastKaiHash = lastKai;
    return ProjectSyncManager::computePlan(in);
}

} // namespace

class TestProjectSyncManager : public QObject {
    Q_OBJECT

private:
    // Roda requestPlan e devolve o plano emitido (o cálculo é em outra thread).
    static SyncPlan awaitPlan(ProjectSyncManager &mgr, SyncDirection dir, const CommandsData &data,
                              const QString &folderId = QStringLiteral("proj"),
                              const QVector<Collection> &collections = {})
    {
        QSignalSpy spy(&mgr, &ProjectSyncManager::planReady);
        mgr.requestPlan(dir, folderId, data, collections);
        if (!spy.wait(5000)) {
            qWarning("planReady não chegou");
            return {};
        }
        return qvariant_cast<SyncPlan>(spy.first().first());
    }
    static QMap<QString, SyncDrift> awaitDrift(ProjectSyncManager &mgr, const CommandsData &data,
                                               const QVector<Collection> &collections = {})
    {
        QSignalSpy spy(&mgr, &ProjectSyncManager::driftChecked);
        mgr.requestDriftCheck(data, collections);
        if (spy.isEmpty() && !spy.wait(5000)) { // sem nada sincronizado o sinal sai na hora
            qWarning("driftChecked não chegou");
            return {};
        }
        return qvariant_cast<SyncDriftMap>(spy.first().first());
    }
    static Note makeNote(const QString &id, const QString &folderId, const QString &name, const QString &content, bool local)
    {
        Note n;
        n.id = id;
        n.folderId = folderId;
        n.name = name;
        n.content = content;
        n.local = local;
        return n;
    }

    static bool awaitWrite(ProjectSyncManager &mgr, const SyncPlan &p)
    {
        QSignalSpy spy(&mgr, &ProjectSyncManager::kaiToFileFinished);
        mgr.applyKaiToFile(p);
        return spy.wait(5000) && spy.first().at(1).toBool();
    }

private slots:
    // ---- canonicalHash ----------------------------------------------------
    void canonicalHashIgnoresFormattingAndKeyOrder()
    {
        QCOMPARE(ProjectSyncManager::canonicalHash(QStringLiteral(R"({"a":1,"b":[1,2]})")),
                 ProjectSyncManager::canonicalHash(QStringLiteral("{\n  \"b\": [1, 2],\n  \"a\": 1\n}")));
        QVERIFY(ProjectSyncManager::canonicalHash(QStringLiteral(R"({"a":1})"))
                != ProjectSyncManager::canonicalHash(QStringLiteral(R"({"a":2})")));
    }

    // ---- planos: erros de configuração -----------------------------------
    void failsWithAReadableReasonWhenTheFolderHasNoUsableDirectory()
    {
        QTemporaryDir dir;
        CommandsData data = sampleTree(dir.path());

        data.folders[0].workingDirMode = WorkingDirMode::Inherit; // sem diretório em lugar nenhum
        QCOMPARE(plan(SyncDirection::KaiToFile, data).errorKey, QStringLiteral("project_sync.error.no_directory"));

        data.folders[0].workingDirMode = WorkingDirMode::None;
        QCOMPARE(plan(SyncDirection::KaiToFile, data).errorKey, QStringLiteral("project_sync.error.no_directory"));

        data.folders[0].workingDirMode = WorkingDirMode::Custom;
        data.folders[0].workingDir = QStringLiteral("{{HOME}}/proj");
        QCOMPARE(plan(SyncDirection::KaiToFile, data).errorKey, QStringLiteral("project_sync.error.directory_has_variables"));

        data.folders[0].workingDir = QStringLiteral("relativo/proj");
        QCOMPARE(plan(SyncDirection::KaiToFile, data).errorKey, QStringLiteral("project_sync.error.directory_not_absolute"));

        data.folders[0].workingDir = dir.path() + QStringLiteral("/nao-existe");
        const SyncPlan missing = plan(SyncDirection::KaiToFile, data);
        QCOMPARE(missing.status, Status::Failed);
        QCOMPARE(missing.errorKey, QStringLiteral("project_sync.error.directory_missing"));
        QCOMPARE(missing.errorDetail, data.folders[0].workingDir);

        QCOMPARE(plan(SyncDirection::KaiToFile, data, QStringLiteral("fantasma")).errorKey,
                 QStringLiteral("project_sync.error.folder_not_found"));
    }

    // O diretório vem da herança: uma subpasta/pasta sem wd próprio usa o de cima.
    // Bug: no Windows, a pasta do projeto com caminho Linux (/home/...) "não existia" pro Kai, e o
    // sync só achava o conteúdo se o caminho fosse digitado como \\wsl.localhost\<distro>\...
    void aLinuxPathOfAWslFolderIsOpenedThroughTheWslShare()
    {
        const QString distro = QStringLiteral("Ubuntu");
        QCOMPARE(ProjectSyncManager::nativeDirFor(QStringLiteral("/home/u/proj"), distro),
                 QStringLiteral("\\\\wsl.localhost\\Ubuntu\\home\\u\\proj"));
        // Sem distro conhecida (terminal local): intacto.
        QCOMPARE(ProjectSyncManager::nativeDirFor(QStringLiteral("/home/u/proj"), QString()), QStringLiteral("/home/u/proj"));
        // Já digitado como UNC/Windows: intacto.
        QCOMPARE(ProjectSyncManager::nativeDirFor(QStringLiteral("\\\\wsl.localhost\\Ubuntu\\home\\u"), distro),
                 QStringLiteral("\\\\wsl.localhost\\Ubuntu\\home\\u"));
        QCOMPARE(ProjectSyncManager::nativeDirFor(QStringLiteral("C:\\proj"), distro), QStringLiteral("C:\\proj"));
        QCOMPARE(ProjectSyncManager::nativeDirFor(QStringLiteral("//server/share"), distro), QStringLiteral("//server/share"));
        // Um diretório que existe de verdade neste sistema não é traduzido (Kai rodando dentro do WSL).
        QTemporaryDir real;
        QCOMPARE(ProjectSyncManager::nativeDirFor(real.path(), distro), real.path());
    }

    void directoryIsInheritedFromTheFolderAbove()
    {
        QTemporaryDir dir;
        CommandsData data = sampleTree(dir.path());
        Folder inner;
        inner.id = QStringLiteral("inner");
        inner.name = QStringLiteral("Inner");
        inner.parentId = QStringLiteral("proj");
        inner.isProject = true;
        data.folders << inner;
        const SyncPlan p = plan(SyncDirection::KaiToFile, data, QStringLiteral("inner"));
        QCOMPARE(p.status, Status::Ready);
        QCOMPARE(p.filePath, dir.path() + QStringLiteral("/kai.yml"));
    }

    // ---- Kai -> Arquivo ---------------------------------------------------
    void kaiToFileWithoutAFileIsReadyAndCreatesYaml()
    {
        QTemporaryDir dir;
        const SyncPlan p = plan(SyncDirection::KaiToFile, sampleTree(dir.path()));
        QCOMPARE(p.status, Status::Ready);
        QVERIFY(!p.fileExists);
        QCOMPARE(p.filePath, dir.path() + QStringLiteral("/kai.yml"));
        QCOMPARE(p.kaiFolders, 1);   // só a subpasta: a raiz é o destino
        QCOMPARE(p.kaiCommands, 2);
    }

    void kaiToFileOverAnUnsyncedExistingFileAsksFirst()
    {
        QTemporaryDir dir;
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), QStringLiteral(R"({"commands": [], "folders": [], "note": "à mão"})"));
        const SyncPlan p = plan(SyncDirection::KaiToFile, sampleTree(dir.path()));
        QCOMPARE(p.status, Status::NeedsConfirmation); // nunca sincronizado: o conteúdo do arquivo seria perdido
        QVERIFY(p.fileExists);
        QCOMPARE(p.filePath, dir.filePath(QStringLiteral("kai.yml")));
    }

    void kaiToFileOverABrokenYamlAsksInsteadOfFailing()
    {
        QTemporaryDir dir;
        writeAll(dir.filePath(QStringLiteral("kai.yml")), QStringLiteral("isto não é um mapa yaml\n"));
        QCOMPARE(plan(SyncDirection::KaiToFile, sampleTree(dir.path())).status, Status::NeedsConfirmation);
    }

    // ---- Arquivo -> Kai ---------------------------------------------------
    void fileToKaiFailsClearlyWithoutAUsableFile()
    {
        QTemporaryDir dir;
        const CommandsData data = sampleTree(dir.path());
        const SyncPlan none = plan(SyncDirection::FileToKai, data);
        QCOMPARE(none.errorKey, QStringLiteral("project_sync.error.no_file"));
        QCOMPARE(none.errorDetail, dir.path() + QStringLiteral("/kai.yml"));

        writeAll(dir.filePath(QStringLiteral("kai.yml")), QStringLiteral("isto não é um mapa yaml\n"));
        QCOMPARE(plan(SyncDirection::FileToKai, data).errorKey, QStringLiteral("project_sync.error.invalid_file"));

        // Um arquivo que não é um mapa vira erro legível, não crash nem "vazio".
        QFile::remove(dir.filePath(QStringLiteral("kai.yml")));
        writeAll(dir.filePath(QStringLiteral("kai.yml")), QStringLiteral("- 1\n- 2\n"));
        QCOMPARE(plan(SyncDirection::FileToKai, data).errorKey, QStringLiteral("project_sync.error.import_failed"));
    }

    // O kai.yml ESCRITO À MÃO (formato de projeto: "folder" por caminho, hooks
    // por nome, sem ids e sem cabeçalho kai_export) é lido igual ao do export.
    void handWrittenProjectFormatIsReadWithDeterministicIds()
    {
        QTemporaryDir dir, state;
        const QString file = dir.filePath(QStringLiteral("kai.yml"));
        const QString handWritten = QStringLiteral(R"YAML(project_name: "Meu App"
folders:
  - path: "Ops"
    icon: "box"
collections:
  - name: "clientes"
    schema: []
    entries: []
commands:
  - name: "Build"
    type: "command"
    command: "make"
  - name: "Deploy"
    type: "command"
    command: "deploy"
    folder: "Ops/Prod"
    hooks:
      pre:
        - "Build"
)YAML");
        writeAll(file, handWritten);

        CommandsData data;
        data.folders << sampleTree(dir.path()).folders.first(); // pasta-projeto vazia
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QCOMPARE(p.status, Status::Ready); // pasta vazia: nada a perder
        QCOMPARE(p.fileCommands, 2);
        QCOMPARE(p.fileFolders, 2); // Ops e Ops/Prod (a lista "folders" do arquivo é só metadado)
        QCOMPARE(p.ignoredCollections, 1);
        QVERIFY(mgr.applyFileToKai(p, data));

        QCOMPARE(data.commands.size(), 2);
        const Command *build = nullptr, *deploy = nullptr;
        for (const Command &c : std::as_const(data.commands)) {
            (c.name == QStringLiteral("Build") ? build : deploy) = &c;
        }
        QVERIFY(build && deploy);
        QCOMPARE(build->folderId, QStringLiteral("proj"));
        QCOMPARE(deploy->hooks.pre, QStringList{build->id});

        // Ops/Prod: Prod sob Ops sob a pasta do projeto; o ícone declarado chegou em Ops.
        auto folderNamed = [&data](const QString &name) -> const Folder * {
            for (const Folder &f : data.folders) if (f.name == name) return &f;
            return nullptr;
        };
        const Folder *ops = folderNamed(QStringLiteral("Ops"));
        const Folder *prod = folderNamed(QStringLiteral("Prod"));
        QVERIFY(ops && prod);
        QCOMPARE(ops->parentId.value_or(QString()), QStringLiteral("proj"));
        QCOMPARE(ops->icon, QStringLiteral("box"));
        QCOMPARE(prod->parentId.value_or(QString()), ops->id);
        QCOMPARE(deploy->folderId, prod->id);

        // Ler o arquivo NUNCA o altera.
        QCOMPARE(readAll(file), handWritten);
        // Já sincronizado: de novo não há o que fazer, e um novo plano gera os mesmos ids.
        QCOMPARE(awaitPlan(mgr, SyncDirection::FileToKai, data).status, Status::NothingToDo);
        CommandsData fresh;
        fresh.folders << sampleTree(dir.path()).folders.first();
        const SyncPlan again = awaitPlan(mgr, SyncDirection::FileToKai, fresh);
        QCOMPARE(again.imported.commands.first().id, data.commands.first().id);
    }

    // Reaplicar o mesmo arquivo de projeto não duplica nada.
    void handWrittenProjectFormatAppliedTwiceDoesNotDuplicate()
    {
        QTemporaryDir dir, state;
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), QStringLiteral(R"JSON({
          "commands": [ {"name": "A", "type": "command", "command": "ls", "folder": "X/Y"},
                        {"name": "B", "type": "command", "command": "ls"} ]})JSON"));
        CommandsData data;
        data.folders << sampleTree(dir.path()).folders.first();
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(p, data));
        QVERIFY(mgr.applyFileToKai(p, data));
        QCOMPARE(data.commands.size(), 2);
        QCOMPARE(data.folders.size(), 3); // projeto + X + Y
    }

    void fileToKaiIntoAnEmptyFolderIsReadyButOverNeverSyncedContentAsks()
    {
        QTemporaryDir dir;
        CommandsData full = sampleTree(dir.path());
        const QString exported = ConfigManager::exportFolder(QStringLiteral("proj"), full, {}, {}, false);
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), exported);

        CommandsData empty;
        empty.folders << full.folders.first(); // só a pasta-projeto, sem nada dentro
        const SyncPlan seed = plan(SyncDirection::FileToKai, empty);
        QCOMPARE(seed.status, Status::Ready);
        QCOMPARE(seed.fileCommands, 2);

        CommandsData changed = full;
        changed.commands << makeCommand(QStringLiteral("c_new"), QStringLiteral("proj"), QStringLiteral("So no Kai"));
        QCOMPARE(plan(SyncDirection::FileToKai, changed).status, Status::NeedsConfirmation);
    }

    void identicalSidesAreNothingToDo()
    {
        QTemporaryDir dir;
        const CommandsData data = sampleTree(dir.path());
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")),
                 ConfigManager::exportFolder(QStringLiteral("proj"), data, {}, {}, true));
        QCOMPARE(plan(SyncDirection::FileToKai, data).status, Status::NothingToDo);
        QCOMPARE(plan(SyncDirection::KaiToFile, data).status, Status::NothingToDo);
    }

    // ---- ciclo completo com o manager -------------------------------------
    void fullCycleKaiToFileThenDetectsChangesOnEitherSide()
    {
        QTemporaryDir dir, state;
        const QString stateFile = state.filePath(QStringLiteral("state.json"));
        CommandsData data = sampleTree(dir.path());
        ProjectSyncManager mgr(stateFile);

        const SyncPlan first = awaitPlan(mgr, SyncDirection::KaiToFile, data);
        QCOMPARE(first.status, Status::Ready);
        QVERIFY(awaitWrite(mgr, first));
        const QString yml = dir.filePath(QStringLiteral("kai.yml"));
        QVERIFY(readAll(yml).contains(QStringLiteral("Filha")));
        QVERIFY(!QFile::exists(yml + QStringLiteral(".tmp")));

        // Nada mudou: nada a fazer, nos dois sentidos.
        QCOMPARE(awaitPlan(mgr, SyncDirection::KaiToFile, data).status, Status::NothingToDo);
        QCOMPARE(awaitPlan(mgr, SyncDirection::FileToKai, data).status, Status::NothingToDo);

        // O Kai mudou, o arquivo não: Kai -> Arquivo é seguro, Arquivo -> Kai pergunta.
        CommandsData kaiEdited = data;
        kaiEdited.commands << makeCommand(QStringLiteral("c_extra"), QStringLiteral("proj"), QStringLiteral("Extra"));
        QCOMPARE(awaitPlan(mgr, SyncDirection::KaiToFile, kaiEdited).status, Status::Ready);
        QCOMPARE(awaitPlan(mgr, SyncDirection::FileToKai, kaiEdited).status, Status::NeedsConfirmation);

        // O arquivo mudou por fora, o Kai não: Arquivo -> Kai é seguro, Kai -> Arquivo pergunta.
        QString yaml = readAll(yml);
        bool ok = false;
        QJsonObject fileObj = QJsonDocument::fromJson(yamlTextToJsonText(yaml, &ok, nullptr).toUtf8()).object();
        QVERIFY(ok);
        QJsonArray cmds = fileObj.value(QStringLiteral("commands")).toArray();
        QJsonObject added = cmds.first().toObject();
        added[QStringLiteral("id")] = QStringLiteral("c_ext");
        added[QStringLiteral("name")] = QStringLiteral("Veio do arquivo");
        cmds.append(added);
        fileObj[QStringLiteral("commands")] = cmds;
        writeAll(yml, jsonTextToYamlText(QString::fromUtf8(QJsonDocument(fileObj).toJson())));
        QCOMPARE(awaitPlan(mgr, SyncDirection::FileToKai, data).status, Status::Ready);
        QCOMPARE(awaitPlan(mgr, SyncDirection::KaiToFile, data).status, Status::NeedsConfirmation);

        // Os dois mudaram: nos dois sentidos pergunta.
        QCOMPARE(awaitPlan(mgr, SyncDirection::FileToKai, kaiEdited).status, Status::NeedsConfirmation);
        QCOMPARE(awaitPlan(mgr, SyncDirection::KaiToFile, kaiEdited).status, Status::NeedsConfirmation);
    }

    // ---- Destaque de pasta dessincronizada --------------------------------
    void driftIsDetectedOnEitherSideAndClearedByASync()
    {
        QTemporaryDir dir, state;
        CommandsData data = sampleTree(dir.path());
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));

        // Nunca sincronizada: nada a comparar, a pasta nem entra no resultado.
        QVERIFY(awaitDrift(mgr, data).isEmpty());

        QVERIFY(awaitWrite(mgr, awaitPlan(mgr, SyncDirection::KaiToFile, data)));
        QCOMPARE(awaitDrift(mgr, data).value(QStringLiteral("proj")), SyncDrift::InSync);

        // O Kai mudou (e não foi gravado no arquivo).
        CommandsData kaiEdited = data;
        kaiEdited.commands << makeCommand(QStringLiteral("c_extra"), QStringLiteral("proj"), QStringLiteral("Extra"));
        QCOMPARE(awaitDrift(mgr, kaiEdited).value(QStringLiteral("proj")), SyncDrift::KaiChanged);

        // O arquivo mudou por fora.
        const QString yml = dir.filePath(QStringLiteral("kai.yml"));
        const QString original = readAll(yml);
        bool ok = false;
        QJsonObject fileObj = QJsonDocument::fromJson(yamlTextToJsonText(original, &ok, nullptr).toUtf8()).object();
        QVERIFY(ok);
        QJsonArray cmds = fileObj.value(QStringLiteral("commands")).toArray();
        QJsonObject added = cmds.first().toObject();
        added[QStringLiteral("id")] = QStringLiteral("c_ext");
        added[QStringLiteral("name")] = QStringLiteral("Veio do arquivo");
        cmds.append(added);
        fileObj[QStringLiteral("commands")] = cmds;
        writeAll(yml, jsonTextToYamlText(QString::fromUtf8(QJsonDocument(fileObj).toJson())));
        QCOMPARE(awaitDrift(mgr, data).value(QStringLiteral("proj")), SyncDrift::FileChanged);
        // Os dois.
        QCOMPARE(awaitDrift(mgr, kaiEdited).value(QStringLiteral("proj")), SyncDrift::BothChanged);

        // Arquivo apagado conta como mudança do arquivo.
        QVERIFY(QFile::remove(yml));
        QCOMPARE(awaitDrift(mgr, data).value(QStringLiteral("proj")), SyncDrift::FileChanged);

        // Sincronizar de novo (Kai -> Arquivo, confirmado) zera o destaque.
        QVERIFY(awaitWrite(mgr, awaitPlan(mgr, SyncDirection::KaiToFile, kaiEdited)));
        QCOMPARE(awaitDrift(mgr, kaiEdited).value(QStringLiteral("proj")), SyncDrift::InSync);
    }

    // Rodar um comando (parâmetros / respostas KIP lembrados) não é mudança no projeto.
    void rememberedRunValuesAreNotADrift()
    {
        QTemporaryDir dir, state;
        CommandsData data = sampleTree(dir.path());
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        QVERIFY(awaitWrite(mgr, awaitPlan(mgr, SyncDirection::KaiToFile, data)));

        data.commands[0].lastParamValues.insert(QStringLiteral("env"), QStringLiteral("prod"));
        data.commands[1].kipLastValues.insert(QStringLiteral("a/b"), QStringLiteral("x"));
        QCOMPARE(awaitDrift(mgr, data).value(QStringLiteral("proj")), SyncDrift::InSync);
        // ...nem pede confirmação ao sincronizar.
        QCOMPARE(awaitPlan(mgr, SyncDirection::KaiToFile, data).status, Status::NothingToDo);
    }

    // ---- coleções e estado local no arquivo ---------------------------------
    static QVector<Collection> sampleCollections(CommandsData &data)
    {
        Collection inside;
        inside.id = QStringLiteral("col_inside");
        inside.folderId = QStringLiteral("proj_sub");
        inside.name = QStringLiteral("Guardada na pasta");
        inside.sourcePath = QStringLiteral("/home/eu/dados/guardada.csv");
        CollectionEntry entry;
        entry.id = QStringLiteral("e1");
        entry.values.insert(QStringLiteral("key"), QStringLiteral("k"));
        entry.values.insert(QStringLiteral("value"), QStringLiteral("v"));
        inside.entries << entry;

        Collection referenced;
        referenced.id = QStringLiteral("col_ref");
        referenced.folderId = QStringLiteral("outra_pasta");
        referenced.name = QStringLiteral("Usada por parametro");
        referenced.sourcePath = QStringLiteral("C:\\dados\\usada.json");
        referenced.entries << entry;

        Collection unrelated;
        unrelated.id = QStringLiteral("col_other");
        unrelated.folderId = QStringLiteral("outra_pasta");
        unrelated.name = QStringLiteral("Nao relacionada");

        Parameter param;
        param.name = QStringLiteral("alvo");
        param.type = ParameterType::Select;
        param.collectionId = referenced.id;
        data.commands[0].params << param;
        return {inside, referenced, unrelated};
    }

    // Kai -> Arquivo NUNCA leva o conteúdo de coleção (grande, às vezes pessoal): só a REFERÊNCIA por nome,
    // no parâmetro que a usa. Nem a coleção guardada na pasta, nem o caminho local de onde ela veio.
    void kaiToFileCarriesOnlyCollectionReferences()
    {
        QTemporaryDir dir, state;
        CommandsData data = sampleTree(dir.path());
        const QVector<Collection> collections = sampleCollections(data);
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));

        const SyncPlan p = awaitPlan(mgr, SyncDirection::KaiToFile, data, QStringLiteral("proj"), collections);
        QCOMPARE(p.status, Status::Ready);
        const QJsonObject root = QJsonDocument::fromJson(p.jsonToWrite.toUtf8()).object();
        QVERIFY2(!root.contains(QStringLiteral("collections")), "o arquivo não leva coleções");
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("Guardada na pasta")));
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("source_path")));
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("col_ref")));        // nem o id local
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("collection_id")));
        const QJsonObject param = root.value(QStringLiteral("commands")).toArray().at(0).toObject()
                                      .value(QStringLiteral("params")).toArray().at(0).toObject();
        QCOMPARE(param.value(QStringLiteral("collection")).toString(), QStringLiteral("Usada por parametro"));

        // Gravado no disco (yml) a pasta fica em sincronia.
        QVERIFY(awaitWrite(mgr, p));
        QVERIFY(!readAll(dir.filePath(QStringLiteral("kai.yml"))).contains(QStringLiteral("Guardada na pasta")));
        QCOMPARE(awaitDrift(mgr, data, collections).value(QStringLiteral("proj")), SyncDrift::InSync);

        // Renomear a coleção referenciada muda a referência (diferença); mexer nos DADOS dela, ou em outra
        // coleção (inclusive a que mora na pasta), não é diferença: nada disso está no arquivo.
        QVector<Collection> renamed = collections;
        renamed[1].name = QStringLiteral("Renomeada");
        QCOMPARE(awaitDrift(mgr, data, renamed).value(QStringLiteral("proj")), SyncDrift::KaiChanged);
        QVector<Collection> otherData = collections;
        otherData[1].entries.clear();
        otherData[0].name = QStringLiteral("Outra coisa");
        otherData[2].name = QStringLiteral("Outra ainda");
        QCOMPARE(awaitDrift(mgr, data, otherData).value(QStringLiteral("proj")), SyncDrift::InSync);
    }

    // Arquivo -> Kai liga a referência por nome à coleção que ESTE Kai tem (só se for única); o que não
    // acha é avisado e o parâmetro fica sem fonte. Vale pros dois formatos de arquivo.
    void fileToKaiResolvesCollectionReferencesAgainstTheLocalCollections()
    {
        QTemporaryDir dir, state;
        CommandsData source = sampleTree(dir.path());
        QVector<Collection> sourceCollections = sampleCollections(source);
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan kaiToFile = awaitPlan(mgr, SyncDirection::KaiToFile, source, QStringLiteral("proj"), sourceCollections);
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), kaiToFile.jsonToWrite);

        // Outra instalação: mesma coleção com OUTRO id.
        CommandsData data = sampleTree(dir.path());
        QVector<Collection> local;
        Collection mine;
        mine.id = QStringLiteral("col_local_1");
        mine.name = QStringLiteral("Usada por parametro");
        local << mine;
        SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data, QStringLiteral("proj"), local);
        QVERIFY(p.unresolvedCollections.isEmpty());
        QVERIFY(mgr.applyFileToKai(p, data, local));
        const Command *command = nullptr;
        for (const Command &c : std::as_const(data.commands)) if (c.id == QStringLiteral("c_root")) command = &c;
        QVERIFY(command && !command->params.isEmpty());
        QCOMPARE(command->params.first().collectionId, QStringLiteral("col_local_1"));

        // Sem a coleção por aqui (ou com o nome repetido): avisa e fica sem fonte.
        for (const QVector<Collection> &variant : {QVector<Collection>{}, QVector<Collection>{mine, mine}}) {
            CommandsData other = sampleTree(dir.path());
            p = awaitPlan(mgr, SyncDirection::FileToKai, other, QStringLiteral("proj"), variant);
            QCOMPARE(p.unresolvedCollections, QStringList{QStringLiteral("Usada por parametro")});
            QVERIFY(mgr.applyFileToKai(p, other, variant));
            for (const Command &c : std::as_const(other.commands)) {
                if (c.id == QStringLiteral("c_root")) {
                    QVERIFY(c.params.first().collectionId.isEmpty());
                    // a referência NÃO se perde: o parâmetro guarda o nome...
                    QCOMPARE(c.params.first().collectionName, QStringLiteral("Usada por parametro"));
                }
            }
            // ...e ele volta pro arquivo no próximo Kai -> Arquivo
            const SyncPlan again = awaitPlan(mgr, SyncDirection::KaiToFile, other, QStringLiteral("proj"), variant);
            const QJsonObject againRoot = QJsonDocument::fromJson(again.jsonToWrite.toUtf8()).object();
            QCOMPARE(againRoot.value(QStringLiteral("commands")).toArray().at(0).toObject().value(QStringLiteral("params"))
                         .toArray().at(0).toObject().value(QStringLiteral("collection")).toString(),
                     QStringLiteral("Usada por parametro"));
            // e quando uma coleção de mesmo nome aparece, vira o vínculo de verdade
            QVector<Collection> arrived{mine};
            QCOMPARE(bindPendingCollectionReferences(other.commands, arrived), 1);
            for (const Command &c : std::as_const(other.commands)) {
                if (c.id == QStringLiteral("c_root")) {
                    QCOMPARE(c.params.first().collectionId, QStringLiteral("col_local_1"));
                    QVERIFY(c.params.first().collectionName.isEmpty());
                }
            }
        }

        // Formato escrito à mão: `collection` por nome que o arquivo NÃO define resolve nas coleções do Kai.
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), QStringLiteral(
            R"({"project_name":"Proj","commands":[{"name":"A","command":"echo a","params":[{"name":"x","type":"select","collection":"Usada por parametro"}]}]})"));
        CommandsData hand = sampleTree(dir.path());
        p = awaitPlan(mgr, SyncDirection::FileToKai, hand, QStringLiteral("proj"), local);
        QVERIFY(p.unresolvedCollections.isEmpty());
        QVERIFY(mgr.applyFileToKai(p, hand, local));
        QCOMPARE(hand.commands.first().params.first().collectionId, QStringLiteral("col_local_1"));
    }

    // O arquivo não tem ids: a identidade de uma pasta/comando é CAMINHO + NOME. Carregar o arquivo mantém os
    // ids (e o que está ligado a eles) do que continua no mesmo lugar com o mesmo nome; renomear vira item novo.
    void identityIsPathAndNameSoLoadingKeepsTheIdsOfWhatStays()
    {
        QTemporaryDir dir, state;
        CommandsData data = sampleTree(dir.path());
        data.commands[0].lastParamValues.insert(QStringLiteral("env"), QStringLiteral("prod"));
        data.commands[1].hooks.pre = {QStringLiteral("c_root")};
        const SyncPlan written = plan(SyncDirection::KaiToFile, data);
        QJsonObject file = QJsonDocument::fromJson(written.jsonToWrite.toUtf8()).object();
        for (const char *id : {"c_root", "c_sub", "proj_sub"}) {
            QVERIFY2(!written.jsonToWrite.contains(QLatin1String(id)), id);
        }

        // Edita o arquivo: muda o corpo de "Raiz" e renomeia "Filha" -> "Filha 2".
        QJsonArray commands = file.value(QStringLiteral("commands")).toArray();
        for (int i = 0; i < commands.size(); ++i) {
            QJsonObject c = commands.at(i).toObject();
            if (c.value(QStringLiteral("name")).toString() == QStringLiteral("Raiz")) c[QStringLiteral("command")] = QStringLiteral("echo novo");
            if (c.value(QStringLiteral("name")).toString() == QStringLiteral("Filha")) c[QStringLiteral("name")] = QStringLiteral("Filha 2");
            commands.replace(i, c);
        }
        file[QStringLiteral("commands")] = commands;
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), QString::fromUtf8(QJsonDocument(file).toJson()));

        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan load = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(load, data));

        const Command *raiz = nullptr, *filha2 = nullptr;
        for (const Command &c : std::as_const(data.commands)) {
            if (c.name == QStringLiteral("Raiz")) raiz = &c;
            if (c.name == QStringLiteral("Filha 2")) filha2 = &c;
        }
        QVERIFY(raiz && filha2);
        QCOMPARE(raiz->id, QStringLiteral("c_root"));                    // mesmo lugar, mesmo nome: mesmo id
        QCOMPARE(raiz->command, QStringLiteral("echo novo"));            // com o conteúdo do arquivo
        QCOMPARE(raiz->lastParamValues.value(QStringLiteral("env")), QStringLiteral("prod")); // e o que era dele
        QVERIFY(filha2->id != QStringLiteral("c_sub"));                  // renomeado = item novo
        QCOMPARE(filha2->folderId, QStringLiteral("proj_sub"));          // mas a pasta continua a mesma
        QCOMPARE(filha2->hooks.pre, QStringList{QStringLiteral("c_root")}); // e o hook liga no id que ficou
        QCOMPARE(data.commands.size(), 2);                                // o antigo "Filha" saiu
        QCOMPARE(data.folders.size(), 2);
    }

    // Ações de PASTA vão pro arquivo (inclusive as da própria pasta raiz, com expansão e grupos); a que aponta
    // pra um comando de FORA do projeto vai por nome. Ações GLOBAIS nunca.
    void folderActionsTravelButGlobalActionsDoNot()
    {
        QTemporaryDir dir, state;
        auto build = [&](const QString &outsideId) {
            CommandsData data = sampleTree(dir.path());
            Folder other;
            other.id = QStringLiteral("outra_pasta");
            other.name = QStringLiteral("Outra");
            data.folders << other;
            data.commands << makeCommand(outsideId, other.id, QStringLiteral("Fora"));
            return data;
        };
        CommandsData data = build(QStringLiteral("c_fora"));
        Folder &root = data.folders[0];
        root.actions = {QStringLiteral("c_root"), QStringLiteral("c_fora")};
        root.expansionActions = {QStringLiteral("c_fora")};
        root.actionGroups = {{QStringLiteral("c_root"), QStringLiteral("Git")}, {QStringLiteral("c_fora"), QStringLiteral("Extras")}};
        root.groupIcons = {{QStringLiteral("Git"), QStringLiteral("git-branch")}};
        data.folders[1].actions = {QStringLiteral("c_sub")};

        const SyncPlan p = plan(SyncDirection::KaiToFile, data);
        const QJsonObject file = QJsonDocument::fromJson(p.jsonToWrite.toUtf8()).object();
        // O arquivo não tem ids: as ações são NOMES de comando, na ordem em que a pasta as lista.
        QCOMPARE(file.value(QStringLiteral("actions")).toArray(), (QJsonArray{QStringLiteral("Raiz"), QStringLiteral("Fora")}));
        QCOMPARE(file.value(QStringLiteral("expansion_actions")).toArray(), QJsonArray{QStringLiteral("Fora")});
        QCOMPARE(file.value(QStringLiteral("action_groups")).toObject().value(QStringLiteral("Raiz")).toString(), QStringLiteral("Git"));
        QCOMPARE(file.value(QStringLiteral("action_groups")).toObject().value(QStringLiteral("Fora")).toString(), QStringLiteral("Extras"));
        QCOMPARE(file.value(QStringLiteral("group_icons")).toObject().value(QStringLiteral("Git")).toString(), QStringLiteral("git-branch"));
        QCOMPARE(file.value(QStringLiteral("folders")).toArray().at(0).toObject().value(QStringLiteral("actions")).toArray(),
                 QJsonArray{QStringLiteral("Filha")});                       // a da subpasta também por nome
        QVERIFY(!file.contains(QStringLiteral("root_folder")));
        for (const char *id : {"c_fora", "c_root", "c_sub", "proj_sub", "f_"}) {
            QVERIFY2(!p.jsonToWrite.contains(QLatin1String(id)), id);        // nenhum id no arquivo
        }
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("global_actions")));

        // Carregar noutra instalação: o comando de fora tem OUTRO id, o casamento é por nome.
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), p.jsonToWrite);
        CommandsData there = build(QStringLiteral("c_fora_local"));
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan load = awaitPlan(mgr, SyncDirection::FileToKai, there);
        int unresolved = -1;
        QVERIFY(mgr.applyFileToKai(load, there, {}, &unresolved));
        QCOMPARE(unresolved, 0);
        const Folder &loaded = there.folders[0];
        QCOMPARE(loaded.actions, (QStringList{"c_root", "c_fora_local"}));
        QCOMPARE(loaded.expansionActions, QStringList{"c_fora_local"});
        QCOMPARE(loaded.actionGroups.value(QStringLiteral("c_fora_local")), QStringLiteral("Extras"));
        QCOMPARE(loaded.actionGroups.value(QStringLiteral("c_root")), QStringLiteral("Git"));
        QCOMPARE(loaded.groupIcons.value(QStringLiteral("Git")), QStringLiteral("git-branch"));
        for (const Folder &f : std::as_const(there.folders)) {
            if (f.id == QStringLiteral("proj_sub")) QCOMPARE(f.actions, QStringList{"c_sub"});
        }

        // Sem um comando "Fora" por aqui, a ação fica de fora e é contada.
        CommandsData bare = sampleTree(dir.path());
        const SyncPlan load2 = awaitPlan(mgr, SyncDirection::FileToKai, bare);
        QVERIFY(mgr.applyFileToKai(load2, bare, {}, &unresolved));
        QCOMPARE(unresolved, 1);
        QCOMPARE(bare.folders[0].actions, QStringList{"c_root"});
    }

    // O arquivo não leva últimos valores nem histórico de parâmetros, e isso não é diferença entre os lados.
    void filesNeverCarryRememberedParameterValues()
    {
        QTemporaryDir dir, state;
        CommandsData data = sampleTree(dir.path());
        data.commands[0].lastParamValues.insert(QStringLiteral("env"), QStringLiteral("prod"));
        data.commands[0].paramUsageHistory.insert(QStringLiteral("env"), QStringList{QStringLiteral("dev"), QStringLiteral("prod")});
        data.commands[1].kipLastValues.insert(QStringLiteral("a/b"), QStringLiteral("x"));
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::KaiToFile, data);
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("last_param_values")));
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("param_usage_history")));
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("kip_last_values")));
        QVERIFY(awaitWrite(mgr, p));
        const QString written = readAll(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(!written.contains(QStringLiteral("last_param_values")));
        QVERIFY(!written.contains(QStringLiteral("param_usage_history")));
    }

    // Arquivo -> Kai: o comando que já existe no Kai continua com o que lembrava (o arquivo não o traz).
    void applyKeepsTheRememberedValuesOfCommandsThatAlreadyExist()
    {
        QTemporaryDir dir, state;
        CommandsData data = sampleTree(dir.path());
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")),
                 ConfigManager::exportFolder(QStringLiteral("proj"), data, {}, {}, false));

        data.commands[0].lastParamValues.insert(QStringLiteral("env"), QStringLiteral("prod"));
        data.commands[0].paramUsageHistory.insert(QStringLiteral("env"), QStringList{QStringLiteral("prod")});
        data.commands[1].kipLastValues.insert(QStringLiteral("a/b"), QStringLiteral("x"));
        data.commands[0].command = QStringLiteral("echo alterado so no kai"); // força o plano a pedir aplicação

        const SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(p.status == Status::Ready || p.status == Status::NeedsConfirmation);
        QVERIFY(mgr.applyFileToKai(p, data));
        const auto root = std::find_if(data.commands.cbegin(), data.commands.cend(),
                                       [](const Command &c) { return c.id == QStringLiteral("c_root"); });
        const auto sub = std::find_if(data.commands.cbegin(), data.commands.cend(),
                                      [](const Command &c) { return c.id == QStringLiteral("c_sub"); });
        QVERIFY(root != data.commands.cend() && sub != data.commands.cend());
        QCOMPARE(root->command, QStringLiteral("echo Raiz")); // o conteúdo veio do arquivo
        QCOMPARE(root->lastParamValues.value(QStringLiteral("env")), QStringLiteral("prod"));
        QCOMPARE(root->paramUsageHistory.value(QStringLiteral("env")), QStringList{QStringLiteral("prod")});
        QCOMPARE(sub->kipLastValues.value(QStringLiteral("a/b")).toString(), QStringLiteral("x"));
    }

    // Estado gravado antes do hash ignorar esses valores não compara com o de hoje: é descartado.
    void legacySyncStateWithoutAVersionIsIgnored()
    {
        QTemporaryDir dir, state;
        const QString stateFile = state.filePath(QStringLiteral("state.json"));
        writeAll(stateFile, QStringLiteral(R"({"proj":{"file":"aaa","kai":"bbb"}})"));
        ProjectSyncManager mgr(stateFile);
        QVERIFY(awaitDrift(mgr, sampleTree(dir.path())).isEmpty());
    }

    void syncStateSurvivesARestart()
    {
        QTemporaryDir dir, state;
        const QString stateFile = state.filePath(QStringLiteral("state.json"));
        const CommandsData data = sampleTree(dir.path());
        {
            ProjectSyncManager mgr(stateFile);
            QVERIFY(awaitWrite(mgr, awaitPlan(mgr, SyncDirection::KaiToFile, data)));
        }
        QVERIFY(QFile::exists(stateFile));
        ProjectSyncManager restarted(stateFile);
        QCOMPARE(awaitPlan(restarted, SyncDirection::KaiToFile, data).status, Status::NothingToDo);
    }

    // O JSON deixou de ser formato de arquivo: um kai.json antigo não é lido nem tocado; o sync usa o kai.yml.
    void anOldKaiJsonIsIgnoredAndNeverOverwritten()
    {
        QTemporaryDir dir, state;
        const QString json = dir.filePath(QStringLiteral("kai.json"));
        const QString old = QStringLiteral(R"({"commands": [], "folders": []})");
        writeAll(json, old);
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::KaiToFile, sampleTree(dir.path()));
        QCOMPARE(p.status, Status::Ready);
        QVERIFY(!p.fileExists);
        QCOMPARE(p.filePath, dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(awaitWrite(mgr, p));
        QVERIFY(readAll(dir.filePath(QStringLiteral("kai.yml"))).contains(QStringLiteral("Filha")));
        QCOMPARE(readAll(json), old);
        QCOMPARE(awaitPlan(mgr, SyncDirection::FileToKai, sampleTree(dir.path())).status, Status::NothingToDo);
    }

    void writeFailureIsReportedAndNothingIsRecorded()
    {
        QTemporaryDir state;
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        SyncPlan p;
        p.direction = SyncDirection::KaiToFile;
        p.folderId = QStringLiteral("proj");
        p.filePath = state.filePath(QStringLiteral("pasta/inexistente/kai.yml"));
        p.jsonToWrite = QStringLiteral("{}");
        QSignalSpy spy(&mgr, &ProjectSyncManager::kaiToFileFinished);
        mgr.applyKaiToFile(p);
        QVERIFY(spy.wait(5000));
        QVERIFY(!spy.first().at(1).toBool());
        QCOMPARE(spy.first().at(2).toString(), p.filePath);
    }

    // ---- Notas -----------------------------------------------------------
    // Só as notas SINCRONIZÁVEIS vão para o arquivo (as locais nunca saem do Kai), cada uma com a pasta por caminho.
    void kaiToFileWritesOnlySyncedNotesWithTheirFolderPath()
    {
        QTemporaryDir dir, state;
        CommandsData data = sampleTree(dir.path());
        data.notes << makeNote(QStringLiteral("n_local"), QStringLiteral("proj"), QStringLiteral("Segredo"), QStringLiteral("so aqui"), true)
                   << makeNote(QStringLiteral("n_root"), QStringLiteral("proj"), QStringLiteral("Checklist"), QStringLiteral("1. a"), false)
                   << makeNote(QStringLiteral("n_sub"), QStringLiteral("proj_sub"), QStringLiteral("Da sub"), QStringLiteral("{}"), false);
        data.notes.last().type = QStringLiteral("json");
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::KaiToFile, data);
        QVERIFY(awaitWrite(mgr, p));

        bool ok = false;
        const QString json = core::yamlTextToJsonText(readAll(dir.filePath(QStringLiteral("kai.yml"))), &ok);
        QVERIFY(ok);
        const QJsonArray notes = QJsonDocument::fromJson(json.toUtf8()).object().value(QStringLiteral("notes")).toArray();
        QCOMPARE(notes.size(), 2);
        QStringList names;
        for (const QJsonValue &v : notes) {
            names << v.toObject().value(QStringLiteral("name")).toString();
            QVERIFY(!v.toObject().contains(QStringLiteral("id")));        // sem ids no arquivo
            QVERIFY(!v.toObject().contains(QStringLiteral("folder_id")));
        }
        names.sort();
        QCOMPARE(names, (QStringList{QStringLiteral("Checklist"), QStringLiteral("Da sub")}));
        for (const QJsonValue &v : notes) {
            if (v.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("Da sub")) {
                QCOMPARE(v.toObject().value(QStringLiteral("folder")).toString(), QStringLiteral("Sub"));
                QCOMPARE(v.toObject().value(QStringLiteral("type")).toString(), QStringLiteral("json"));
            }
        }
        QVERIFY(!readAll(dir.filePath(QStringLiteral("kai.yml"))).contains(QStringLiteral("so aqui"))); // a local não vazou
    }

    // Do arquivo para o Kai: as notas do arquivo viram sincronizáveis, as locais do Kai ficam, e aplicar de novo não duplica
    // nem perde o id (o conteúdo continua sendo "a mesma nota").
    void fileToKaiBringsNotesKeepsLocalOnesAndNeverDuplicates()
    {
        QTemporaryDir dir, state;
        CommandsData source = sampleTree(dir.path());
        source.notes << makeNote(QStringLiteral("a"), QStringLiteral("proj"), QStringLiteral("Checklist"), QStringLiteral("passo 1"), false)
                     << makeNote(QStringLiteral("b"), QStringLiteral("proj_sub"), QStringLiteral("Da sub"), QStringLiteral("texto"), false);
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), ConfigManager::exportFolder(QStringLiteral("proj"), source, {}, {}, true));

        CommandsData data = sampleTree(dir.path());
        data.notes << makeNote(QStringLiteral("minha"), QStringLiteral("proj"), QStringLiteral("Rascunho"), QStringLiteral("local"), true);
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(p, data));
        QCOMPARE(data.notes.size(), 3); // as 2 do arquivo + a local

        const Note *draft = nullptr, *checklist = nullptr, *fromSub = nullptr;
        for (const Note &n : std::as_const(data.notes)) {
            if (n.name == QStringLiteral("Rascunho")) draft = &n;
            if (n.name == QStringLiteral("Checklist")) checklist = &n;
            if (n.name == QStringLiteral("Da sub")) fromSub = &n;
        }
        QVERIFY(draft && checklist && fromSub);
        QVERIFY(draft->local);
        QCOMPARE(draft->content, QStringLiteral("local")); // intacta
        QVERIFY(!checklist->local);
        QCOMPARE(checklist->content, QStringLiteral("passo 1"));
        QCOMPARE(checklist->folderId, QStringLiteral("proj"));
        QVERIFY(!fromSub->local);
        const Folder *sub = nullptr;
        for (const Folder &f : std::as_const(data.folders)) {
            if (f.name == QStringLiteral("Sub")) sub = &f;
        }
        QVERIFY(sub);
        QCOMPARE(fromSub->folderId, sub->id); // a pasta por caminho voltou a ser a subpasta daqui

        // De novo: nada duplica, e o id da nota sincronizada continua o mesmo.
        const QString checklistId = checklist->id;
        const SyncPlan again = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(again, data));
        QCOMPARE(data.notes.size(), 3);
        bool sameId = false;
        for (const Note &n : std::as_const(data.notes)) {
            if (n.name == QStringLiteral("Checklist")) sameId = n.id == checklistId;
        }
        QVERIFY(sameId);

        // Uma nota sincronizável que saiu do arquivo some do Kai (o arquivo manda), a local fica.
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), ConfigManager::exportFolder(QStringLiteral("proj"), sampleTree(dir.path()), {}, {}, true));
        const SyncPlan removed = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(removed, data));
        QCOMPARE(data.notes.size(), 1);
        QCOMPARE(data.notes.first().name, QStringLiteral("Rascunho"));
    }

    // O kai.yml escrito à mão: `notes:` com name/content/type/icon/folder.
    void handWrittenProjectFileNotesAreImportedAsSyncedNotes()
    {
        QTemporaryDir dir, state;
        QJsonObject file;
        file[QStringLiteral("project_name")] = QStringLiteral("Proj");
        file[QStringLiteral("commands")] = QJsonArray{QJsonObject{{"name", "Build"}, {"type", "command"}, {"command", "make"}}};
        file[QStringLiteral("notes")] = QJsonArray{
            QJsonObject{{"name", "Release checklist"}, {"content", "1. Bump\n2. Tag"}, {"icon", "list-checks"}},
            QJsonObject{{"name", "Payload"}, {"type", "json"}, {"content", "{\"a\": 1}"}, {"folder", "Ops"}}};
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), QString::fromUtf8(QJsonDocument(file).toJson()));
        CommandsData data;
        data.folders << sampleTree(dir.path()).folders.first();
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(p, data));
        QCOMPARE(data.notes.size(), 2);
        for (const Note &n : std::as_const(data.notes)) {
            QVERIFY(!n.local);
            if (n.name == QStringLiteral("Release checklist")) {
                QCOMPARE(n.folderId, QStringLiteral("proj"));
                QCOMPARE(n.type, QStringLiteral("markdown"));
                QCOMPARE(n.icon, QStringLiteral("list-checks"));
                QCOMPARE(n.content, QStringLiteral("1. Bump\n2. Tag"));
            } else {
                QCOMPARE(n.type, QStringLiteral("json"));
                QVERIFY(n.folderId != QStringLiteral("proj")); // foi para a subpasta "Ops"
            }
        }
        // Reaplicar o arquivo à mão não duplica as notas.
        QVERIFY(mgr.applyFileToKai(p, data));
        QCOMPARE(data.notes.size(), 2);
    }

    // ---- Arquivo -> Kai: aplicar -----------------------------------------
    void applyReplacesTheSubtreeKeepsEverythingElseAndNeverDuplicates()
    {
        QTemporaryDir dir, state;
        CommandsData source = sampleTree(dir.path());
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")),
                 ConfigManager::exportFolder(QStringLiteral("proj"), source, {}, {}, false));

        // Estado do Kai: o projeto com conteúdo ANTIGO + uma pasta vizinha
        // do usuário que nunca pode ser tocada.
        CommandsData data;
        data.folders << source.folders.first();
        Folder neighbour;
        neighbour.id = QStringLiteral("projetos_2");
        neighbour.name = QStringLiteral("Projetos 2");
        data.folders << neighbour;
        data.commands << makeCommand(QStringLiteral("c_old"), QStringLiteral("proj"), QStringLiteral("Velho"))
                      << makeCommand(QStringLiteral("c_user"), neighbour.id, QStringLiteral("Do usuario"));

        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY2(p.status == Status::NeedsConfirmation, "tem conteúdo nunca sincronizado: pergunta");
        for (int round = 0; round < 2; ++round) { // 2x: reimportar o mesmo arquivo não duplica
            QVERIFY(mgr.applyFileToKai(p, data));
        }

        QStringList names;
        for (const Command &c : std::as_const(data.commands)) names << c.name;
        names.sort();
        QCOMPARE(names, (QStringList{QStringLiteral("Do usuario"), QStringLiteral("Filha"), QStringLiteral("Raiz")}));
        QStringList folderIds;
        for (const Folder &f : std::as_const(data.folders)) folderIds << f.id;
        folderIds.sort();
        QCOMPARE(folderIds, (QStringList{QStringLiteral("proj"), QStringLiteral("proj_sub"), QStringLiteral("projetos_2")}));

        // Depois de aplicar, os lados coincidem.
        QCOMPARE(awaitPlan(mgr, SyncDirection::FileToKai, data).status, Status::NothingToDo);
    }

    // Arquivo vindo de outra instalação (ids de pasta diferentes) ou sem pai:
    // o que não aponta pra uma pasta do próprio arquivo cai na pasta-projeto.
    void applyReparentsItemsThatPointOutsideTheFile()
    {
        QTemporaryDir dir, state;
        const QString foreign = QStringLiteral("f_da_outra_maquina");
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), QStringLiteral(R"JSON({
          "kai_export": {"scope": "folder", "version": 1},
          "folders": [ {"id": "sub1", "name": "Sub", "parent_id": "%1"} ],
          "commands": [
            {"id": "c1", "name": "NaRaiz", "type": "command", "command": "ls", "folder_id": "%1"},
            {"id": "c2", "name": "NaSub", "type": "command", "command": "ls", "folder_id": "sub1"},
            {"id": "c3", "name": "SemPasta", "type": "command", "command": "ls"}
          ]})JSON").arg(foreign));
        CommandsData data;
        data.folders << sampleTree(dir.path()).folders.first();
        data.folders[0].id = QStringLiteral("minha_pasta");

        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data, QStringLiteral("minha_pasta"));
        QCOMPARE(p.status, Status::Ready);
        QVERIFY(mgr.applyFileToKai(p, data));

        auto folderOf = [&data](const QString &name) {
            for (const Command &c : data.commands) if (c.name == name) return c.folderId;
            return QStringLiteral("<não achou>");
        };
        QCOMPARE(folderOf(QStringLiteral("NaRaiz")), QStringLiteral("minha_pasta"));
        QCOMPARE(folderOf(QStringLiteral("SemPasta")), QStringLiteral("minha_pasta"));
        QCOMPARE(folderOf(QStringLiteral("NaSub")), QStringLiteral("sub1"));
        for (const Folder &f : std::as_const(data.folders)) {
            if (f.id == QStringLiteral("sub1")) QCOMPARE(f.parentId.value_or(QString()), QStringLiteral("minha_pasta"));
        }
    }

    // Um export de pasta feito pela UI (enxuto) traz a própria pasta raiz (`root_folder`): ela é o
    // destino, não conteúdo — nunca pode virar uma pasta dentro de si mesma nem substituir a pasta real.
    void anExportThatContainsTheRootFolderDoesNotCorruptTheTree()
    {
        QTemporaryDir dir, state;
        const CommandsData source = sampleTree(dir.path());
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")),
                 ConfigManager::exportFolder(QStringLiteral("proj"), source, {}, {}, true)); // COM a raiz

        CommandsData data;
        data.folders << source.folders.first();
        data.folders[0].name = QStringLiteral("Nome local");
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QCOMPARE(p.status, Status::Ready);
        QVERIFY(mgr.applyFileToKai(p, data));

        int roots = 0;
        for (const Folder &f : std::as_const(data.folders)) {
            if (f.id == QStringLiteral("proj")) {
                ++roots;
                QVERIFY(!f.parentId.has_value());
                QCOMPARE(f.name, QStringLiteral("Nome local")); // a pasta real não foi trocada
                QVERIFY(f.isProject);
            }
        }
        QCOMPARE(roots, 1);
        QCOMPARE(data.folders.size(), 2);
        QCOMPARE(data.commands.size(), 2);
    }

    // Variável SECRETA: o arquivo leva só o nome. O valor que este Kai tem fica (o arquivo não o apaga nem o
    // sobrescreve), e quem carrega o arquivo noutro Kai ganha a variável vazia pra preencher.
    void secretVariablesTravelAsNamesOnly()
    {
        QTemporaryDir dir, state;
        CommandsData data = sampleTree(dir.path());
        data.folders[0].envVars = {{QStringLiteral("AWS_REGION"), QStringLiteral("us-east-1")},
                                   {QStringLiteral("AWS_TOKEN"), QStringLiteral("tok-123-secret")}};
        data.folders[0].secretEnvKeys = {QStringLiteral("AWS_TOKEN")};
        data.folders[1].envVars = {{QStringLiteral("SUB_PASS"), QStringLiteral("pw-456-secret")}};
        data.folders[1].secretEnvKeys = {QStringLiteral("SUB_PASS")};

        const SyncPlan p = plan(SyncDirection::KaiToFile, data);
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("tok-123-secret")));
        QVERIFY(!p.jsonToWrite.contains(QStringLiteral("pw-456-secret")));
        const QJsonObject file = QJsonDocument::fromJson(p.jsonToWrite.toUtf8()).object();
        QCOMPARE(file.value(QStringLiteral("secret_env_keys")).toArray(), QJsonArray{QStringLiteral("AWS_TOKEN")});
        QCOMPARE(file.value(QStringLiteral("env_vars")).toObject().keys(), QStringList{QStringLiteral("AWS_REGION")});
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), p.jsonToWrite);

        // Mudar só o valor secreto aqui não é diferença (ele não está no arquivo).
        data.folders[0].envVars[QStringLiteral("AWS_TOKEN")] = QStringLiteral("outro-valor");
        QCOMPARE(plan(SyncDirection::KaiToFile, data).jsonToWrite, p.jsonToWrite);

        // Carregar o arquivo no MESMO Kai: os valores secretos locais continuam.
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        SyncPlan load = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(load, data));
        QCOMPARE(data.folders[0].envVars.value(QStringLiteral("AWS_TOKEN")), QStringLiteral("outro-valor"));
        QCOMPARE(data.folders[0].secretEnvKeys, QSet<QString>{QStringLiteral("AWS_TOKEN")});
        QCOMPARE(data.folders[0].envVars.value(QStringLiteral("AWS_REGION")), QStringLiteral("us-east-1"));
        for (const Folder &f : std::as_const(data.folders)) {
            if (f.id == QStringLiteral("proj_sub")) QCOMPARE(f.envVars.value(QStringLiteral("SUB_PASS")), QStringLiteral("pw-456-secret"));
        }

        // Outro Kai (sem os valores): a variável existe, vazia, e é secreta.
        CommandsData other = sampleTree(dir.path());
        load = awaitPlan(mgr, SyncDirection::FileToKai, other);
        QVERIFY(mgr.applyFileToKai(load, other));
        QVERIFY(other.folders[0].envVars.contains(QStringLiteral("AWS_TOKEN")));
        QVERIFY(other.folders[0].envVars.value(QStringLiteral("AWS_TOKEN")).isEmpty());
        QCOMPARE(other.folders[0].secretEnvKeys, QSet<QString>{QStringLiteral("AWS_TOKEN")});
        for (const Folder &f : std::as_const(other.folders)) {
            if (f.name == QStringLiteral("Sub")) {
                QVERIFY(f.secretEnvKeys.contains(QStringLiteral("SUB_PASS")));
                QVERIFY(f.envVars.value(QStringLiteral("SUB_PASS")).isEmpty());
            }
        }

        // Um segredo que SÓ este Kai tem (o arquivo não o conhece) não é apagado pelo carregamento.
        data.folders[0].envVars[QStringLiteral("LOCAL_ONLY")] = QStringLiteral("so-aqui");
        data.folders[0].secretEnvKeys.insert(QStringLiteral("LOCAL_ONLY"));
        const SyncPlan again = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(again, data));
        QCOMPARE(data.folders[0].envVars.value(QStringLiteral("LOCAL_ONLY")), QStringLiteral("so-aqui"));
        QVERIFY(data.folders[0].secretEnvKeys.contains(QStringLiteral("LOCAL_ONLY")));
    }

    // O projeto (pasta raiz) é o destino, mas as VARIÁVEIS dele são conteúdo: sem elas no
    // arquivo, sincronizar perdia o env inteiro do projeto.
    void kaiToFileCarriesTheProjectEnvVars()
    {
        QTemporaryDir dir;
        CommandsData data = sampleTree(dir.path());
        data.folders[0].envVars = {{QStringLiteral("AWS_PROFILE"), QStringLiteral("dev")},
                                   {QStringLiteral("AWS_REGION"), QStringLiteral("us-east-1")}};
        const SyncPlan p = plan(SyncDirection::KaiToFile, data);
        QCOMPARE(p.status, Status::Ready);
        const QJsonObject written = QJsonDocument::fromJson(p.jsonToWrite.toUtf8()).object();
        QCOMPARE(written.value(QStringLiteral("env_vars")).toObject().value(QStringLiteral("AWS_PROFILE")).toString(),
                 QStringLiteral("dev"));
        QCOMPARE(written.value(QStringLiteral("env_vars")).toObject().size(), 2);
        QCOMPARE(written.value(QStringLiteral("folders")).toArray().size(), 1); // a raiz continua fora da lista

        // Mudar só o env no Kai é uma diferença: o sync não pode dizer "nada a fazer".
        QTemporaryDir state;
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        const SyncPlan first = awaitPlan(mgr, SyncDirection::KaiToFile, data);
        QSignalSpy spy(&mgr, &ProjectSyncManager::kaiToFileFinished);
        mgr.applyKaiToFile(first);
        QVERIFY(spy.wait(5000));
        QCOMPARE(awaitPlan(mgr, SyncDirection::KaiToFile, data).status, Status::NothingToDo);
        data.folders[0].envVars[QStringLiteral("AWS_PROFILE")] = QStringLiteral("prod");
        QCOMPARE(awaitPlan(mgr, SyncDirection::KaiToFile, data).status, Status::Ready);
    }

    void fileToKaiAppliesTheProjectEnvVarsInBothFormats()
    {
        QTemporaryDir dir, state;
        CommandsData source = sampleTree(dir.path());
        source.folders[0].envVars = {{QStringLiteral("AWS_PROFILE"), QStringLiteral("dev")}};
        const SyncPlan written = plan(SyncDirection::KaiToFile, source);

        CommandsData data = sampleTree(dir.path());
        data.folders[0].envVars = {{QStringLiteral("OLD"), QStringLiteral("x")}};
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));

        // Formato de export do Kai (o que o Kai -> Arquivo grava).
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")), written.jsonToWrite);
        SyncPlan p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(p, data));
        QCOMPARE(data.folders[0].envVars, (QMap<QString, QString>{{QStringLiteral("AWS_PROFILE"), QStringLiteral("dev")}}));

        // Formato de projeto escrito à mão: `env_vars` na raiz do arquivo.
        data.folders[0].envVars = {{QStringLiteral("OLD"), QStringLiteral("x")}};
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")),
                 QStringLiteral(R"({"project_name":"Proj","env_vars":{"PORT":"8080"},"commands":[{"name":"A","command":"echo a"}]})"));
        p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(p, data));
        QCOMPARE(data.folders[0].envVars, (QMap<QString, QString>{{QStringLiteral("PORT"), QStringLiteral("8080")}}));

        // Arquivo sem `env_vars`: o que o Kai já tem fica.
        data.folders[0].envVars = {{QStringLiteral("KEEP"), QStringLiteral("1")}};
        writeKaiFile(dir.filePath(QStringLiteral("kai.yml")),
                 QStringLiteral(R"({"project_name":"Proj","commands":[{"name":"A","command":"echo a"}]})"));
        p = awaitPlan(mgr, SyncDirection::FileToKai, data);
        QVERIFY(mgr.applyFileToKai(p, data));
        QCOMPARE(data.folders[0].envVars, (QMap<QString, QString>{{QStringLiteral("KEEP"), QStringLiteral("1")}}));
    }

    void applyRefusesAPlanThatIsNotFileToKai()
    {
        QTemporaryDir state;
        ProjectSyncManager mgr(state.filePath(QStringLiteral("state.json")));
        CommandsData data;
        SyncPlan wrongDirection;
        wrongDirection.direction = SyncDirection::KaiToFile;
        QVERIFY(!mgr.applyFileToKai(wrongDirection, data));
        SyncPlan notImported;
        notImported.direction = SyncDirection::FileToKai;
        QVERIFY(!mgr.applyFileToKai(notImported, data));
    }
};

QTEST_MAIN(TestProjectSyncManager)
#include "test_project_sync_manager.moc"
