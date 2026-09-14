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

ProjectFormatContent convertProjectFormat(const QJsonObject &root, const QString &rootFolderId,
                                          const QString &projectName, const QString &sourcePath)
{
    ProjectFormatContent out;

    // Ícone de subpasta (pedido do usuário: "se eu quiser incluir um ícone
    // na subpasta de um projeto, dá?") — chave opcional "folders": [{
    // "path": "Marketplaces/Amazon", "icon": "box" }], mesmo padrão (path +
    // icon) do Export/Import Configuration id-free (ver
    // stripIdsFromExport/resolveIdFreeImport em config-manager.cpp). Uma
    // entrada aqui SÓ traz o ícone — a subpasta em si continua sendo
    // criada implicitamente por qualquer comando/coleção que declare
    // "folder": "esse mesmo path"; uma entrada sem nenhum comando
    // apontando pra ela é ignorada (mesma regra do "path" do
    // Export/Import Configuration: pasta intermediária vazia não aparece
    // sozinha).
    // Metadados de subpasta declarados explicitamente por path (hoje só
    // "icon" é lido) — chave "path" tolera vir COM o prefixo do nome do
    // projeto (como aparece na árvore) ou sem ele (ver
    // FolderPathResolver::stripRootPrefix e o comentário na classe: bug
    // real reportado, arquivo com "path": "Amazon Marketplace API/
    // Sincronizar" pra um comando com "folder": "Sincronizar" — o ícone
    // nunca batia, silenciosamente, até esta extração compartilhada).
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
    // Resolução de "folder"/"path" -> id, criando subpastas sob demanda —
    // COMPARTILHADA com o Export/Import Configuration id-free (ver
    // core::FolderPathResolver). O kai.yml pode trazer "folder":
    // "Marketplaces" ou "folder": "Callbacks/Shopee" (aninhado); cada
    // segmento vira uma Folder sob a pasta raiz do projeto, com ordem
    // preservada.
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
                // CLI PATH também pode vir pela mesma entrada "folders":
                // [{"path":..., "icon":..., "cli_path":...}] usada pra dar
                // ícone a uma subpasta criada implicitamente por "folder" —
                // sem isto, a única forma de marcar cli_path numa subpasta
                // seria via Export/Import Configuration completo (com id),
                // nunca num kai.yml de projeto escrito à mão.
                sub.cliPath = folderResolver.explicitMetadataFor(path).value(QStringLiteral("cli_path")).toString();
                sub.cliDescription = folderResolver.explicitMetadataFor(path).value(QStringLiteral("cli_description")).toString();
                sub.parentId = parentId;
                sub.isProject = false;
                sub.order = subFolderOrder++;
                out.subFolders << sub;
            });
    };

    // Hooks (chave "hooks": {"pre": [...], "post": [...], "cleanup": [...]})
    // referenciam outros comandos do MESMO kai.yml por NOME (o manifesto
    // não conhece os ids gerados por generateCommandId) — mesmo espírito da
    // resolução de "collection" nos parâmetros acima. Guardado à parte,
    // paralelo a out.commands pelo ÍNDICE (o loop abaixo insere
    // exatamente 1 comando por cmdVal, na mesma ordem), e resolvido pra id
    // DEPOIS que todos os comandos existirem — não dá pra resolver "pra
    // frente" (um comando pode referenciar um hook declarado mais adiante
    // no array).
    QVector<QJsonObject> rawHooksByIndex;
    // Parâmetros cuja `collectionId` ainda é um NOME (chave "collection"), como "<id do comando>#<índice>".
    // Quem já traz "collection_id" (um id real, ex.: resolvido pelo sync) não passa pela resolução por nome.
    QSet<QString> nameRefs;

    for (const QJsonValue &cmdVal : root.value("commands").toArray()) {
        const QJsonObject cmdObj = cmdVal.toObject();

        // Base = o MESMO parser usado pelo Export/Import Configuration
        // (core::Command::fromJson) — pedido do usuário ("tem que ser
        // GERAL aquela importação, ícone, nome e tudo, como na importação
        // de pasta"): um parser hand-rolled à parte, campo por campo, já
        // tinha ficado pra trás do struct real MAIS de uma vez (capture_env
        // antes, agora icon/formatted_output/declared_env_vars) — cada
        // campo novo em core::Command precisava ser copiado aqui à mão e
        // nunca era. Usar fromJson elimina essa classe inteira de bug:
        // qualquer campo que o formato do projeto grava com a MESMA chave
        // do Export/Import Configuration passa a chegar automaticamente.
        core::Command command = core::Command::fromJson(cmdObj);
        command.id = QStringLiteral("c_%1_%2").arg(rootFolderId).arg(index++);
        command.folderId = resolveFolder(cmdObj.value("folder").toString());
        // Sem "working_dir" o comando herda da pasta-projeto (a raiz do
        // projeto) — é o default do próprio modelo, nada a gravar aqui.

        // Referência de coleção por NOME nos parâmetros (chave "collection",
        // resolvida pra id mais abaixo, depois que as coleções existirem) —
        // fromJson só entende "collection_id" (id já resolvido), então essa
        // parte continua sendo lida à parte, pareada por índice com
        // command.params (mesmo array, mesma ordem).
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

    // Resolve hooks por NOME (ver comentário acima de rawHooksByIndex). Um
    // nome que não corresponda a nenhum comando deste MESMO kai.yml é
    // ignorado com aviso no log — nunca quebra a importação do projeto.
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

    // Coleções versionadas junto ao projeto (chave "collections"): cada
    // uma vira uma Collection associada à pasta do projeto. Geramos ids
    // determinísticos a partir do folderId + índice para evitar colisão e
    // permitir reimportação idempotente.
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

    // Resolve as referências de coleção por NOME nos params dos comandos
    // para o id gerado (o kai.yml não conhece o id determinístico). Params
    // cujo "collection" não casar com nenhuma coleção ficam sem fonte.
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
