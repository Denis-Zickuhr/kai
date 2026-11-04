

#include <QJsonArray>

#include "core/folder-path-resolver.h"
#include "core/config-id-free.h"

namespace kai::core::detail {

// Export sem ids: referências cruzadas viram nomes/caminhos (pasta "A/B",
// hooks pelo nome do comando, Select pelo nome da coleção). Reimportar o
// mesmo arquivo sempre ADICIONA cópias, não atualiza no lugar.
//
// Último passo de exportXxx, sobre o QJsonObject id-based. `exportRootFolderId`
// é a pasta exportada (topo implícito, path == "", fora da lista de pastas);
// vazio no export global/seletivo.
QJsonObject stripIdsFromExport(QJsonObject root, const QString &exportRootFolderId,
                               const QMap<QString, QString> &allCommandNames)
{
    const QJsonArray foldersArr = root.value(QStringLiteral("folders")).toArray();
    const QJsonArray commandsArr = root.value(QStringLiteral("commands")).toArray();
    const bool hasCollectionsKey = root.contains(QStringLiteral("collections"));
    const QJsonArray collectionsArr = root.value(QStringLiteral("collections")).toArray();

    QMap<QString, QJsonObject> foldersById;
    for (const QJsonValue &v : foldersArr) {
        const QJsonObject f = v.toObject();
        foldersById.insert(f.value(QStringLiteral("id")).toString(), f);
    }
    QMap<QString, QString> commandNameById;
    for (const QJsonValue &v : commandsArr) {
        const QJsonObject c = v.toObject();
        commandNameById.insert(c.value(QStringLiteral("id")).toString(), c.value(QStringLiteral("name")).toString());
    }
    QMap<QString, QString> collectionNameById;
    for (const QJsonValue &v : collectionsArr) {
        const QJsonObject c = v.toObject();
        collectionNameById.insert(c.value(QStringLiteral("id")).toString(), c.value(QStringLiteral("name")).toString());
    }

    // Caminho (nomes separados por "/") de `folderId` até — mas SEM
    // incluir — `exportRootFolderId`. Cadeia quebrada (parent_id fora do
    // pacote, dados corrompidos) simplesmente para onde a cadeia acaba;
    // nunca trava.
    std::function<QString(const QString &)> pathFor = [&](const QString &folderId) -> QString {
        if (folderId.isEmpty() || folderId == exportRootFolderId || !foldersById.contains(folderId)) {
            return QString();
        }
        QStringList parts;
        QString cur = folderId;
        QSet<QString> visited;
        while (foldersById.contains(cur) && cur != exportRootFolderId && !visited.contains(cur)) {
            visited.insert(cur);
            const QJsonObject &f = foldersById.value(cur);
            parts.prepend(f.value(QStringLiteral("name")).toString());
            const QJsonValue parentVal = f.value(QStringLiteral("parent_id"));
            if (!parentVal.isString()) {
                break;
            }
            cur = parentVal.toString();
        }
        return parts.join(QStringLiteral("/"));
    };

    QJsonArray newFolders;
    for (const QJsonValue &v : foldersArr) {
        QJsonObject f = v.toObject();
        const QString id = f.value(QStringLiteral("id")).toString();
        // Ações da pasta: ids -> NOMES. O comando pode estar fora do pacote
        // (ex.: um "git fetch" que mora em outra pasta), então o nome vem do
        // conjunto completo; id sem comando conhecido some.
        if (f.value(QStringLiteral("actions")).isArray()) {
            auto toNames = [&](const QJsonArray &ids) {
                QJsonArray names;
                for (const QJsonValue &av : ids) {
                    const QString commandId = av.toString();
                    const QString name = commandNameById.value(commandId, allCommandNames.value(commandId));
                    if (!name.isEmpty()) {
                        names.append(name);
                    }
                }
                return names;
            };
            const QJsonArray names = toNames(f.value(QStringLiteral("actions")).toArray());
            const QJsonArray expansionNames = toNames(f.value(QStringLiteral("expansion_actions")).toArray());
            f.remove(QStringLiteral("expansion_actions"));
            // Grupos: chave por NOME do comando (o id não viaja).
            QJsonObject groupsByName;
            const QJsonObject groupsById = f.value(QStringLiteral("action_groups")).toObject();
            for (auto git = groupsById.constBegin(); git != groupsById.constEnd(); ++git) {
                const QString name = commandNameById.value(git.key(), allCommandNames.value(git.key()));
                if (!name.isEmpty()) {
                    groupsByName[name] = git.value();
                }
            }
            f.remove(QStringLiteral("action_groups"));
            if (names.isEmpty()) {
                f.remove(QStringLiteral("actions"));
            } else {
                f.insert(QStringLiteral("actions"), names);
                if (!expansionNames.isEmpty()) {
                    f.insert(QStringLiteral("expansion_actions"), expansionNames);
                }
                if (!groupsByName.isEmpty()) {
                    f.insert(QStringLiteral("action_groups"), groupsByName);
                }
            }
        } else {
            f.remove(QStringLiteral("expansion_actions"));
            f.remove(QStringLiteral("action_groups"));
        }
        if (!exportRootFolderId.isEmpty() && id == exportRootFolderId) {
            // A pasta exportada não entra na lista de filhas, mas nome/ícone
            // vão em "root_folder"; sem isso um comando direto na raiz perdia
            // o folder_id na reimportação.
            QJsonObject rootMeta = f;
            rootMeta.remove(QStringLiteral("id"));
            rootMeta.remove(QStringLiteral("parent_id"));
            root.insert(QStringLiteral("root_folder"), rootMeta);
            continue;
        }
        const QString path = pathFor(id);
        f.remove(QStringLiteral("id"));
        f.remove(QStringLiteral("parent_id"));
        f.insert(QStringLiteral("path"), path);
        newFolders.append(f);
    }

    if (root.value(QStringLiteral("notes")).isArray()) {
        QJsonArray newNotes;
        for (const QJsonValue &v : root.value(QStringLiteral("notes")).toArray()) {
            QJsonObject n = v.toObject();
            const QString path = pathFor(n.value(QStringLiteral("folder_id")).toString());
            n.remove(QStringLiteral("id"));
            n.remove(QStringLiteral("folder_id"));
            if (!path.isEmpty()) {
                n.insert(QStringLiteral("folder"), path);
            }
            newNotes.append(n);
        }
        root.insert(QStringLiteral("notes"), newNotes);
    }

    QJsonArray newCommands;
    for (const QJsonValue &v : commandsArr) {
        QJsonObject c = v.toObject();
        const QString path = pathFor(c.value(QStringLiteral("folder_id")).toString());
        c.remove(QStringLiteral("id"));
        c.remove(QStringLiteral("folder_id"));
        if (!path.isEmpty()) {
            c.insert(QStringLiteral("folder"), path);
        }

        if (c.value(QStringLiteral("hooks")).isObject()) {
            QJsonObject hooksObj = c.value(QStringLiteral("hooks")).toObject();
            for (const char *key : {"pre", "post", "cleanup"}) {
                const QJsonArray idsArr = hooksObj.value(QLatin1String(key)).toArray();
                if (idsArr.isEmpty()) {
                    continue;
                }
                QJsonArray namesArr;
                for (const QJsonValue &idv : idsArr) {
                    namesArr.append(commandNameById.value(idv.toString(), idv.toString()));
                }
                hooksObj.insert(QLatin1String(key), namesArr);
            }
            c.insert(QStringLiteral("hooks"), hooksObj);
        }

        if (c.value(QStringLiteral("params")).isArray()) {
            QJsonArray newParams;
            for (const QJsonValue &pv : c.value(QStringLiteral("params")).toArray()) {
                QJsonObject p = pv.toObject();
                if (p.contains(QStringLiteral("collection_id"))) {
                    const QString colId = p.value(QStringLiteral("collection_id")).toString();
                    p.remove(QStringLiteral("collection_id"));
                    p.insert(QStringLiteral("collection"), collectionNameById.value(colId, colId));
                }
                // Referência por nome ainda sem coleção neste Kai: no arquivo enxuto é só `collection: <nome>`.
                if (p.contains(QStringLiteral("collection_name"))) {
                    const QString pending = p.value(QStringLiteral("collection_name")).toString();
                    p.remove(QStringLiteral("collection_name"));
                    if (!p.contains(QStringLiteral("collection"))) {
                        p.insert(QStringLiteral("collection"), pending);
                    }
                }
                newParams.append(p);
            }
            c.insert(QStringLiteral("params"), newParams);
        }
        newCommands.append(c);
    }

    QJsonArray newCollections;
    for (const QJsonValue &v : collectionsArr) {
        QJsonObject c = v.toObject();
        const QString path = pathFor(c.value(QStringLiteral("folder_id")).toString());
        c.remove(QStringLiteral("id"));
        c.remove(QStringLiteral("folder_id"));
        if (!path.isEmpty()) {
            c.insert(QStringLiteral("folder"), path);
        }
        // CollectionEntry::toJson sempre escreve "id"; Collection::fromJson
        // gera um novo na releitura.
        if (c.value(QStringLiteral("entries")).isArray()) {
            QJsonArray strippedEntries;
            for (const QJsonValue &ev : c.value(QStringLiteral("entries")).toArray()) {
                QJsonObject e = ev.toObject();
                e.remove(QStringLiteral("id"));
                strippedEntries.append(e);
            }
            c.insert(QStringLiteral("entries"), strippedEntries);
        }
        newCollections.append(c);
    }

    // Só reescreve a chave se já existia (export sem "commands" não ganha "commands": []).
    if (root.contains(QStringLiteral("folders"))) {
        root.insert(QStringLiteral("folders"), newFolders);
    }
    if (root.contains(QStringLiteral("commands"))) {
        root.insert(QStringLiteral("commands"), newCommands);
    }
    if (hasCollectionsKey) {
        root.insert(QStringLiteral("collections"), newCollections);
    }
    QJsonObject header = root.value(QStringLiteral("kai_export")).toObject();
    // Decide na importação qual caminho rodar (ver resolveIdFreeImport); a
    // ausência de "id" no primeiro item também vale, p/ kai.yml escrito à mão.
    header.insert(QStringLiteral("id_free"), true);
    root.insert(QStringLiteral("kai_export"), header);
    return root;
}

// Inverso de stripIdsFromExport: roda ANTES do parsing normal em
// importFromJson, devolvendo o formato id-based (ids novos GERADOS).
QJsonObject resolveIdFreeImport(QJsonObject root)
{
    const QJsonArray foldersArr = root.value(QStringLiteral("folders")).toArray();
    const QJsonArray commandsArr = root.value(QStringLiteral("commands")).toArray();
    const bool hasCollectionsKey = root.contains(QStringLiteral("collections"));
    const QJsonArray collectionsArr = root.value(QStringLiteral("collections")).toArray();

    QMap<QString, QJsonObject> explicitByPath;
    for (const QJsonValue &v : foldersArr) {
        const QJsonObject f = v.toObject();
        explicitByPath.insert(f.value(QStringLiteral("path")).toString(), f);
    }

    QJsonArray newFolders;
    auto generateFolderId = []() {
        return QStringLiteral("f_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    };

    // A própria pasta exportada (escopo Pasta): "root_folder" (ver
    // stripIdsFromExport) guarda a identidade; recria ANTES de qualquer
    // outra resolução, pra path=="" apontar pra ela.
    QString rootFolderId;
    if (root.value(QStringLiteral("root_folder")).isObject()) {
        QJsonObject rf = root.value(QStringLiteral("root_folder")).toObject();
        rootFolderId = generateFolderId();
        rf.insert(QStringLiteral("id"), rootFolderId);
        newFolders.append(rf);
    }

    // Resolução folder/path -> id, criando pastas intermediárias sob demanda;
    // compartilhada com o Import de Projeto (core::FolderPathResolver).
    core::FolderPathResolver resolver(rootFolderId, generateFolderId, explicitByPath);
    std::function<QString(const QString &)> ensurePath = [&](const QString &path) -> QString {
        return resolver.resolve(path, [&](const QString &id, const QString &name,
                                           const QString &parentId, const QString &p) {
            QJsonObject f = resolver.explicitMetadataFor(p); // pasta "implícita" (só do meio-do-caminho) fica com objeto vazio — ok, fromJson cai em defaults
            f.insert(QStringLiteral("id"), id);
            f.insert(QStringLiteral("name"), name);
            if (!parentId.isEmpty()) {
                f.insert(QStringLiteral("parent_id"), parentId);
            }
            f.remove(QStringLiteral("path"));
            newFolders.append(f);
        });
    };
    // Garante também pastas declaradas sem comando/coleção (só subpastas).
    for (const QString &path : resolver.explicitPaths()) {
        ensurePath(path);
    }

    // ids de comando/coleção pré-gerados numa passada única (hooks/params
    // referenciam por NOME e precisam resolver pra um id que já existe).
    QVector<QString> commandIds;
    QMap<QString, QString> commandIdByName;
    for (const QJsonValue &v : commandsArr) {
        const QString newId = QStringLiteral("c_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
        commandIds.append(newId);
        const QString name = v.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && !commandIdByName.contains(name)) {
            commandIdByName.insert(name, newId);
        }
    }
    QVector<QString> collectionIds;
    QMap<QString, QString> collectionIdByName;
    for (const QJsonValue &v : collectionsArr) {
        const QString newId = QStringLiteral("col_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
        collectionIds.append(newId);
        const QString name = v.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && !collectionIdByName.contains(name)) {
            collectionIdByName.insert(name, newId);
        }
    }

    QJsonArray newCommands;
    for (int i = 0; i < commandsArr.size(); ++i) {
        QJsonObject c = commandsArr.at(i).toObject();
        c.insert(QStringLiteral("id"), commandIds.at(i));
        const QString path = c.value(QStringLiteral("folder")).toString();
        c.remove(QStringLiteral("folder"));
        const QString folderId = ensurePath(path);
        if (!folderId.isEmpty()) {
            c.insert(QStringLiteral("folder_id"), folderId);
        }

        if (c.value(QStringLiteral("hooks")).isObject()) {
            QJsonObject hooksObj = c.value(QStringLiteral("hooks")).toObject();
            for (const char *key : {"pre", "post", "cleanup"}) {
                const QJsonArray namesArr = hooksObj.value(QLatin1String(key)).toArray();
                if (namesArr.isEmpty()) {
                    continue;
                }
                QJsonArray idsArr;
                for (const QJsonValue &nv : namesArr) {
                    // Nome sem comando correspondente NO MESMO pacote: ignorado
                    // (mesma regra do import de projeto — ver manifesto §9).
                    const QString resolved = commandIdByName.value(nv.toString());
                    if (!resolved.isEmpty()) {
                        idsArr.append(resolved);
                    }
                }
                hooksObj.insert(QLatin1String(key), idsArr);
            }
            c.insert(QStringLiteral("hooks"), hooksObj);
        }

        if (c.value(QStringLiteral("params")).isArray()) {
            QJsonArray newParams;
            for (const QJsonValue &pv : c.value(QStringLiteral("params")).toArray()) {
                QJsonObject p = pv.toObject();
                if (p.contains(QStringLiteral("collection"))) {
                    const QString colName = p.value(QStringLiteral("collection")).toString();
                    p.remove(QStringLiteral("collection"));
                    const QString resolved = collectionIdByName.value(colName);
                    if (!resolved.isEmpty()) {
                        p.insert(QStringLiteral("collection_id"), resolved);
                    }
                }
                newParams.append(p);
            }
            c.insert(QStringLiteral("params"), newParams);
        }
        newCommands.append(c);
    }

    QJsonArray newNotes;
    for (const QJsonValue &v : root.value(QStringLiteral("notes")).toArray()) {
        QJsonObject n = v.toObject();
        n.insert(QStringLiteral("id"), QStringLiteral("note_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
        const QString path = n.value(QStringLiteral("folder")).toString();
        n.remove(QStringLiteral("folder"));
        const QString folderId = ensurePath(path);
        if (!folderId.isEmpty()) {
            n.insert(QStringLiteral("folder_id"), folderId);
        }
        newNotes.append(n);
    }

    QJsonArray newCollections;
    for (int i = 0; i < collectionsArr.size(); ++i) {
        QJsonObject c = collectionsArr.at(i).toObject();
        c.insert(QStringLiteral("id"), collectionIds.at(i));
        const QString path = c.value(QStringLiteral("folder")).toString();
        c.remove(QStringLiteral("folder"));
        const QString folderId = ensurePath(path);
        if (!folderId.isEmpty()) {
            c.insert(QStringLiteral("folder_id"), folderId);
        }
        newCollections.append(c);
    }

    // Ações (pasta e globais): nomes -> ids dos comandos do PRÓPRIO pacote. O
    // que não casa fica em "actions_pending"/sem command_id, por NOME, pra
    // quem importa casar com os comandos que já existem (ver
    // ConfigManager::applyImportedActions).
    for (int i = 0; i < newFolders.size(); ++i) {
        QJsonObject f = newFolders.at(i).toObject();
        if (!f.value(QStringLiteral("actions")).isArray()) {
            continue;
        }
        QJsonArray ids, pending, expansionIds, expansionPending;
        QJsonObject groupsById, groupsPending;
        const QJsonObject groupsByName = f.value(QStringLiteral("action_groups")).toObject();
        QStringList expansionNames;
        for (const QJsonValue &nv : f.value(QStringLiteral("expansion_actions")).toArray()) {
            expansionNames << nv.toString();
        }
        for (const QJsonValue &nv : f.value(QStringLiteral("actions")).toArray()) {
            const QString resolved = commandIdByName.value(nv.toString());
            const bool expansion = expansionNames.contains(nv.toString());
            const QJsonValue group = groupsByName.value(nv.toString());
            if (!resolved.isEmpty()) {
                ids.append(resolved);
                if (group.isString()) {
                    groupsById[resolved] = group;
                }
                if (expansion) {
                    expansionIds.append(resolved);
                }
            } else if (!nv.toString().isEmpty()) {
                pending.append(nv.toString());
                if (group.isString()) {
                    groupsPending[nv.toString()] = group;
                }
                if (expansion) {
                    expansionPending.append(nv.toString());
                }
            }
        }
        f.insert(QStringLiteral("actions"), ids);
        f.insert(QStringLiteral("expansion_actions"), expansionIds);
        f.insert(QStringLiteral("action_groups"), groupsById);
        if (!groupsPending.isEmpty()) {
            f.insert(QStringLiteral("action_groups_pending"), groupsPending);
        }
        if (!pending.isEmpty()) {
            f.insert(QStringLiteral("actions_pending"), pending);
        }
        if (!expansionPending.isEmpty()) {
            f.insert(QStringLiteral("expansion_actions_pending"), expansionPending);
        }
        newFolders.replace(i, f);
    }
    if (root.value(QStringLiteral("settings")).isObject()) {
        QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
        if (settingsObj.value(QStringLiteral("global_actions")).isArray()) {
            QJsonArray resolvedActions;
            for (const QJsonValue &av : settingsObj.value(QStringLiteral("global_actions")).toArray()) {
                QJsonObject o = av.toObject();
                const QString resolved = commandIdByName.value(o.value(QStringLiteral("command")).toString());
                if (!resolved.isEmpty()) {
                    o.insert(QStringLiteral("command_id"), resolved);
                }
                resolvedActions.append(o);
            }
            settingsObj.insert(QStringLiteral("global_actions"), resolvedActions);
            root.insert(QStringLiteral("settings"), settingsObj);
        }
    }

    root.insert(QStringLiteral("folders"), newFolders);
    root.insert(QStringLiteral("commands"), newCommands);
    if (root.contains(QStringLiteral("notes"))) {
        root.insert(QStringLiteral("notes"), newNotes);
    }
    if (hasCollectionsKey) {
        root.insert(QStringLiteral("collections"), newCollections);
    }
    return root;
}

// Detecta um pacote SEM ids — a marca explícita (id_free no cabeçalho,
// ver stripIdsFromExport) OU, na ausência dela (kai.yml escrito à mão,
// nunca passou pelo export do Kai), a AUSÊNCIA estrutural de "id" no
// primeiro item de folders/commands.
bool looksIdFree(const QJsonObject &root)
{
    const QJsonObject header = root.value(QStringLiteral("kai_export")).toObject();
    if (header.value(QStringLiteral("id_free")).toBool(false)) {
        return true;
    }
    const QJsonArray foldersArr = root.value(QStringLiteral("folders")).toArray();
    if (!foldersArr.isEmpty()) {
        return !foldersArr.first().toObject().contains(QStringLiteral("id"));
    }
    const QJsonArray commandsArr = root.value(QStringLiteral("commands")).toArray();
    if (!commandsArr.isEmpty()) {
        return !commandsArr.first().toObject().contains(QStringLiteral("id"));
    }
    return false;
}
} // namespace kai::core::detail
