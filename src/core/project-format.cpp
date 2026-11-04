#include "core/project-format.h"

#include "core/folder-path-resolver.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <functional>

namespace kai::core {

namespace {
constexpr const char *kLogTag = "ProjectFormat";
}

bool isKaiExportFormat(const QJsonObject &root)
{
    return root.value(QStringLiteral("kai_export")).isObject();
}

bool isFolderScopeExport(const QJsonObject &root)
{
    return isKaiExportFormat(root)
        && root.value(QStringLiteral("kai_export")).toObject().value(QStringLiteral("scope")).toString()
            == QLatin1String("folder");
}

ProjectFormatContent convertProjectFormat(const QJsonObject &root, const QString &rootFolderId,
                                          const QString &projectName, const QString &sourcePath)
{
    ProjectFormatContent out;

    // Metadados de subpasta por path ("folders": [{"path": "Marketplaces/Amazon", "icon": "box"}]; hoje
    // só "icon"), mesmo padrão do Export/Import Configuration id-free. A entrada só traz o ícone: a
    // subpasta nasce de um comando/coleção que declare esse "folder"; sem nenhum apontando, é ignorada.
    // "path" tolera o prefixo do nome do projeto (ver FolderPathResolver::stripRootPrefix).
    QMap<QString, QJsonObject> explicitFolderMetaByPath;
    for (const QJsonValue &v : root.value(QStringLiteral("folders")).toArray()) {
        const QJsonObject f = v.toObject();
        const QString path = core::FolderPathResolver::stripRootPrefix(
            f.value(QStringLiteral("path")).toString(), projectName);
        if (!path.isEmpty()) {
            explicitFolderMetaByPath.insert(path, f);
        }
    }

    int index = 0;
    // "folder"/"path" -> id, criando subpastas sob demanda (compartilhado com o Export/Import
    // Configuration id-free, core::FolderPathResolver); cada segmento vira uma Folder sob a raiz.
    int subFolderOrder = 0;
    int subFolderCounter = 0;
    core::FolderPathResolver folderResolver(rootFolderId,
        [&rootFolderId, &subFolderCounter]() {
            return QStringLiteral("%1_sf_%2").arg(rootFolderId).arg(subFolderCounter++);
        },
        explicitFolderMetaByPath);
    std::function<QString(const QString &)> resolveFolder =
        [&](const QString &folderPath) -> QString {
        return folderResolver.resolve(folderPath,
            [&](const QString &id, const QString &name, const QString &parentId, const QString &path) {
                core::Folder sub;
                sub.id = id;
                sub.name = name;
                sub.icon = folderResolver.explicitMetadataFor(path).value(QStringLiteral("icon")).toString();
                // "folders" também pode trazer "cli_path" de uma subpasta criada implicitamente.
                sub.cliPath = folderResolver.explicitMetadataFor(path).value(QStringLiteral("cli_path")).toString();
                sub.cliDescription = folderResolver.explicitMetadataFor(path).value(QStringLiteral("cli_description")).toString();
                sub.httpDefaults = HttpDefaults::fromJson(
                    folderResolver.explicitMetadataFor(path).value(QStringLiteral("http_defaults")).toObject());
                sub.parentId = parentId;
                sub.isProject = false;
                sub.order = subFolderOrder++;
                out.subFolders << sub;
            });
    };

    // Hooks ("hooks": {"pre","post","cleanup"}) referenciam outros comandos do MESMO kai.yml por NOME.
    // Guardados paralelos a out.commands pelo ÍNDICE (1 comando por cmdVal) e resolvidos pra id DEPOIS
    // que todos existirem (um hook pode ser declarado mais adiante).
    QVector<QJsonObject> rawHooksByIndex;
    // Parâmetros cuja `collectionId` ainda é um NOME (chave "collection"), como "<id do comando>#<índice>".
    // Quem já traz "collection_id" (um id real, ex.: resolvido pelo sync) não passa pela resolução por nome.
    QSet<QString> nameRefs;

    for (const QJsonValue &cmdVal : root.value("commands").toArray()) {
        const QJsonObject cmdObj = cmdVal.toObject();

        // Base = o MESMO parser do Export/Import Configuration (core::Command::fromJson): um parser à
        // parte ficava pra trás do struct a cada campo novo.
        core::Command command = core::Command::fromJson(cmdObj);
        command.id = QStringLiteral("c_%1_%2").arg(rootFolderId).arg(index++);
        command.folderId = resolveFolder(cmdObj.value("folder").toString());
        // Sem "working_dir" herda da pasta-projeto (default do modelo).

        // Referência de coleção por NOME ("collection"): fromJson só entende "collection_id", então é
        // lida à parte, pareada por índice com command.params e resolvida depois.
        const QJsonArray paramsArr = cmdObj.value(QStringLiteral("params")).toArray();
        for (int i = 0; i < paramsArr.size() && i < command.params.size(); ++i) {
            const QJsonObject pObj = paramsArr.at(i).toObject();
            if (pObj.contains(QStringLiteral("collection"))) {
                command.params[i].collectionId = pObj.value(QStringLiteral("collection")).toString();
                nameRefs.insert(QStringLiteral("%1#%2").arg(command.id).arg(i));
            }
        }

        rawHooksByIndex << cmdObj.value("hooks").toObject();
        out.commands << command;
    }

    // Hooks por NOME; um nome sem comando neste kai.yml é ignorado com aviso no log.
    auto resolveHookNames = [&out](const QJsonArray &namesArr) {
        QStringList ids;
        for (const QJsonValue &nv : namesArr) {
            const QString name = nv.toString();
            const auto it = std::find_if(out.commands.constBegin(), out.commands.constEnd(),
                [&name](const core::Command &c) { return c.name == name; });
            if (it != out.commands.constEnd()) {
                ids << it->id;
            } else {
                utils::Logger::warning(kLogTag,
                    QStringLiteral("Hook '%1' referenciado no kai.yml não corresponde a nenhum comando importado; ignorado.").arg(name));
            }
        }
        return ids;
    };
    for (int i = 0; i < out.commands.size(); ++i) {
        const QJsonObject &hooksObj = rawHooksByIndex.at(i);
        if (hooksObj.isEmpty()) {
            continue;
        }
        core::Hooks hooks;
        hooks.pre = resolveHookNames(hooksObj.value("pre").toArray());
        hooks.post = resolveHookNames(hooksObj.value("post").toArray());
        hooks.cleanup = resolveHookNames(hooksObj.value("cleanup").toArray());
        out.commands[i].hooks = hooks;
    }

    // Coleções versionadas ("collections"), associadas à pasta do projeto, com ids determinísticos
    // (folderId + índice) para reimportação idempotente.
    int colIndex = 0;
    for (const QJsonValue &colVal : root.value("collections").toArray()) {
        const QJsonObject colObj = colVal.toObject();
        core::Collection collection = core::Collection::fromJson(colObj);
        collection.id = QStringLiteral("col_%1_%2").arg(rootFolderId).arg(colIndex++);
        // Coleção também pode declarar "folder" para ir numa subpasta.
        collection.folderId = resolveFolder(colObj.value("folder").toString());
        collection.sourcePath = sourcePath;
        if (collection.name.isEmpty()) {
            collection.name = utils::tr(QStringLiteral("project_selector.collection.default_name")).arg(colIndex);
        }
        out.collections << collection;
    }

    // Notas versionadas junto ao projeto (chave "notes"): `name`, `content` e, opcionalmente, `type`, `icon` e `folder`
    // (caminho sob a pasta do projeto). Ids determinísticos pela posição no arquivo, como as coleções.
    int noteIndex = 0;
    for (const QJsonValue &noteVal : root.value("notes").toArray()) {
        const QJsonObject noteObj = noteVal.toObject();
        core::Note note = core::Note::fromJson(noteObj);
        note.id = QStringLiteral("note_%1_%2").arg(rootFolderId).arg(noteIndex++);
        note.folderId = resolveFolder(noteObj.value("folder").toString());
        note.local = false;
        if (note.name.isEmpty()) {
            note.name = utils::tr(QStringLiteral("note.default_name"));
        }
        out.notes << note;
    }

    // Resolve "collection" por NOME nos params para o id gerado; sem coleção correspondente, fica sem fonte.
    for (core::Command &command : out.commands) {
        for (int p = 0; p < command.params.size(); ++p) {
            core::Parameter &param = command.params[p];
            if (param.collectionId.isEmpty() || !nameRefs.contains(QStringLiteral("%1#%2").arg(command.id).arg(p))) {
                continue;
            }
            const QString byName = param.collectionId;
            const auto it = std::find_if(out.collections.constBegin(), out.collections.constEnd(),
                [&byName](const core::Collection &c) { return c.name == byName; });
            param.collectionId = (it != out.collections.constEnd()) ? it->id : QString();
        }
    }

    out.nextSubFolderCounter = subFolderCounter;
    out.nextSubFolderOrder = subFolderOrder;
    return out;
}

} // namespace kai::core
