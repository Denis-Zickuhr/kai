#include "core/tree-view.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace kai::core {

namespace {

constexpr int kChainGuard = 128;

// Pai de qualquer item da árvore: pasta -> parentId; comando, coleção e nota -> folderId (que pode ser pasta ou, nos
// comandos, outro comando).
class TreeIndex {
public:
    explicit TreeIndex(const TreeData &data)
    {
        for (const Folder &f : data.folders) {
            m_parent.insert(f.id, f.parentId.value_or(QString()));
            m_isFolder.insert(f.id);
        }
        for (const Command &c : data.commands) {
            m_parent.insert(c.id, c.folderId);
        }
        for (const Collection &c : data.collections) {
            m_parent.insert(c.id, c.folderId);
        }
        for (const Note &n : data.notes) {
            m_parent.insert(n.id, n.folderId);
        }
    }

    bool has(const QString &id) const { return m_parent.contains(id); }
    QString parentOf(const QString &id) const { return m_parent.value(id); }
    bool isFolder(const QString &id) const { return m_isFolder.contains(id); }

    // `id` está dentro de `target` (ou é ele): sobe a cadeia de pais.
    bool chainContains(const QString &id, const QString &target) const
    {
        QString cur = id;
        QSet<QString> seen;
        for (int i = 0; i < kChainGuard && !cur.isEmpty() && !seen.contains(cur); ++i) {
            if (cur == target) {
                return true;
            }
            seen.insert(cur);
            cur = parentOf(cur);
        }
        return false;
    }

    // A cadeia chega a alguma pasta? Senão o item é órfão (vai para a aba padrão).
    bool reachesFolder(const QString &id) const
    {
        QString cur = id;
        QSet<QString> seen;
        for (int i = 0; i < kChainGuard && !cur.isEmpty() && !seen.contains(cur); ++i) {
            if (isFolder(cur)) {
                return true;
            }
            seen.insert(cur);
            cur = parentOf(cur);
        }
        return false;
    }

private:
    QHash<QString, QString> m_parent;
    QSet<QString> m_isFolder;
};

bool typeAllowed(const QStringList &types, const QString &type)
{
    return types.isEmpty() || types.contains(type);
}

QString commandTypeName(CommandType type)
{
    switch (type) {
    case CommandType::Http: return QStringLiteral("http");
    case CommandType::Pipeline: return QStringLiteral("pipeline");
    case CommandType::Command: break;
    }
    return QStringLiteral("command");
}

} // namespace

QStringList TreeViewDef::allTypes()
{
    return {QStringLiteral("command"), QStringLiteral("http"), QStringLiteral("pipeline"),
            QStringLiteral("collection"), QStringLiteral("note")};
}

QJsonObject TreeViewDef::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["icon"] = icon;
    obj["folder_ids"] = QJsonArray::fromStringList(folderIds);
    obj["types"] = QJsonArray::fromStringList(types);
    obj["running_only"] = runningOnly;
    obj["show_hidden"] = showHidden;
    obj["query"] = query;
    return obj;
}

TreeViewDef TreeViewDef::fromJson(const QJsonObject &obj)
{
    TreeViewDef view;
    view.id = obj.value("id").toString();
    view.name = obj.value("name").toString();
    view.icon = obj.value("icon").toString();
    for (const QJsonValue &v : obj.value("folder_ids").toArray()) {
        if (!v.toString().isEmpty()) {
            view.folderIds << v.toString();
        }
    }
    const QStringList known = allTypes();
    for (const QJsonValue &v : obj.value("types").toArray()) {
        if (known.contains(v.toString()) && !view.types.contains(v.toString())) {
            view.types << v.toString();
        }
    }
    view.runningOnly = obj.value("running_only").toBool(false);
    view.showHidden = obj.value("show_hidden").toBool(false);
    view.query = obj.value("query").toString();
    return view;
}

TreeData filterTreeData(const TreeViewDef &view, const TreeData &raw)
{
    const TreeIndex index(raw);
    const QSet<QString> chosen(view.folderIds.cbegin(), view.folderIds.cend());

    auto inChosenFolder = [&](const QString &id) {
        if (chosen.isEmpty()) {
            return true;
        }
        QString cur = id;
        QSet<QString> seen;
        for (int i = 0; i < kChainGuard && !cur.isEmpty() && !seen.contains(cur); ++i) {
            if (chosen.contains(cur)) {
                return true;
            }
            seen.insert(cur);
            cur = index.parentOf(cur);
        }
        return false;
    };

    QSet<QString> keptItems;     // comandos, coleções e notas que passam nos filtros
    QSet<QString> structure;     // pastas e comandos-pai que os contêm (e as pastas escolhidas)
    auto keepWithAncestors = [&](const QString &id) {
        QString cur = index.parentOf(id);
        QSet<QString> seen;
        for (int i = 0; i < kChainGuard && !cur.isEmpty() && !seen.contains(cur) && index.has(cur); ++i) {
            structure.insert(cur);
            if (chosen.contains(cur)) {
                break; // a pasta escolhida mais próxima é a raiz desta árvore
            }
            seen.insert(cur);
            cur = index.parentOf(cur);
        }
    };

    for (const Command &c : raw.commands) {
        if (typeAllowed(view.types, commandTypeName(c.type)) && inChosenFolder(c.id)) {
            keptItems.insert(c.id);
            keepWithAncestors(c.id);
        }
    }
    for (const Collection &c : raw.collections) {
        if (typeAllowed(view.types, QStringLiteral("collection")) && inChosenFolder(c.id)) {
            keptItems.insert(c.id);
            keepWithAncestors(c.id);
        }
    }
    for (const Note &n : raw.notes) {
        if (typeAllowed(view.types, QStringLiteral("note")) && inChosenFolder(n.id)) {
            keptItems.insert(n.id);
            keepWithAncestors(n.id);
        }
    }
    for (const QString &folderId : chosen) {
        if (index.isFolder(folderId)) {
            structure.insert(folderId);
        }
    }

    TreeData out;
    for (const Folder &f : raw.folders) {
        if (structure.contains(f.id)) {
            out.folders << f;
        }
    }
    for (const Command &c : raw.commands) {
        if (keptItems.contains(c.id) || structure.contains(c.id)) {
            out.commands << c;
        }
    }
    for (const Collection &c : raw.collections) {
        if (keptItems.contains(c.id)) {
            out.collections << c;
        }
    }
    for (const Note &n : raw.notes) {
        if (keptItems.contains(n.id)) {
            out.notes << n;
        }
    }
    return out;
}

QVector<TreePlacement> remapFilteredReorder(const TreeData &raw, const QVector<TreePlacement> &visual, const QString &rootId)
{
    const TreeIndex index(raw);
    const bool orphanRoot = rootId == QLatin1String(kTreeDefaultRootId);

    // Todos os itens da raiz, na ordem de exibição da árvore (a mesma de createTreeForRoot): `order` manual (>= 0) primeiro,
    // depois os sem ordem na ordem de inserção (pastas, comandos, coleções, notas).
    struct Ref {
        QString id;
        TreePlacement::Kind kind;
        int order;
        int tie;
    };
    QVector<Ref> refs;
    int tie = 0;
    auto belongs = [&](const QString &id) {
        if (id == rootId) {
            return false;
        }
        return orphanRoot ? !index.reachesFolder(id) : index.chainContains(id, rootId);
    };
    for (const Folder &f : raw.folders) {
        if (belongs(f.id)) refs.append({f.id, TreePlacement::Kind::Folder, f.order, tie});
        ++tie;
    }
    for (const Command &c : raw.commands) {
        if (belongs(c.id)) refs.append({c.id, TreePlacement::Kind::Command, c.order, tie});
        ++tie;
    }
    for (const Collection &c : raw.collections) {
        if (belongs(c.id)) refs.append({c.id, TreePlacement::Kind::Collection, c.order, tie});
        ++tie;
    }
    for (const Note &n : raw.notes) {
        if (belongs(n.id)) refs.append({n.id, TreePlacement::Kind::Note, n.order, tie});
        ++tie;
    }
    std::stable_sort(refs.begin(), refs.end(), [](const Ref &a, const Ref &b) {
        const bool aManual = a.order >= 0;
        const bool bManual = b.order >= 0;
        if (aManual != bManual) return aManual;
        if (aManual && a.order != b.order) return a.order < b.order;
        return a.tie < b.tie;
    });

    QSet<QString> visibleIds;
    for (const TreePlacement &p : visual) {
        visibleIds.insert(p.id);
    }
    QVector<int> visibleSlots;
    for (int i = 0; i < refs.size(); ++i) {
        if (visibleIds.contains(refs.at(i).id)) {
            visibleSlots.append(i);
        }
    }
    if (visibleIds.size() != visual.size() || visibleSlots.size() != visual.size()) {
        return {}; // a árvore mostra algo que não é da raiz (ou repete): melhor não gravar nada
    }

    // Os visíveis, na nova ordem visual, ocupam as posições que os visíveis já ocupavam; os ocultos não se movem.
    QHash<QString, int> newSlot;
    QHash<QString, QString> newParent;
    for (int i = 0; i < visual.size(); ++i) {
        newSlot.insert(visual.at(i).id, visibleSlots.at(i));
        newParent.insert(visual.at(i).id, visual.at(i).parentId);
    }
    QVector<TreePlacement> out;
    out.reserve(refs.size());
    for (int i = 0; i < refs.size(); ++i) {
        const Ref &ref = refs.at(i);
        TreePlacement placement;
        placement.id = ref.id;
        placement.kind = ref.kind;
        placement.parentId = newParent.contains(ref.id) ? newParent.value(ref.id) : index.parentOf(ref.id);
        placement.order = newSlot.contains(ref.id) ? newSlot.value(ref.id) : i;
        out.append(placement);
    }
    return out;
}

} // namespace kai::core
