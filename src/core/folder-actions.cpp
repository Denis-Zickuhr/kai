#include "core/folder-actions.h"

#include <QSet>

#include <iterator>

namespace kai::core {

namespace {
const QString kPrefix = QStringLiteral("act:");
constexpr QChar kSeparator = QLatin1Char('|');
} // namespace

QString folderActionCommandId(const QString &commandId, const QString &folderId)
{
    return kPrefix + commandId + kSeparator + folderId;
}

bool isFolderActionCommandId(const QString &id)
{
    return parseFolderActionCommandId(id).has_value();
}

std::optional<FolderActionIds> parseFolderActionCommandId(const QString &id)
{
    if (!id.startsWith(kPrefix)) {
        return std::nullopt;
    }
    const int sep = id.indexOf(kSeparator, kPrefix.size());
    if (sep <= kPrefix.size() || sep == id.size() - 1) {
        return std::nullopt;
    }
    return FolderActionIds{id.mid(kPrefix.size(), sep - kPrefix.size()), id.mid(sep + 1)};
}

QVector<FolderAction> actionsForFolder(const Folder &folder, const QVector<GlobalAction> &globalActions,
                                       const QMap<QString, Command> &commandsById)
{
    QVector<FolderAction> result;
    QSet<QString> seen;
    auto add = [&](const QString &commandId, bool global, bool expansion, const QString &group = QString(),
                   const QString &groupIcon = QString()) {
        if (commandId.isEmpty() || seen.contains(commandId) || !commandsById.contains(commandId)) {
            return;
        }
        seen.insert(commandId);
        result.append({commandId, global, expansion, group.trimmed(), groupIcon.trimmed()});
    };
    for (const GlobalAction &g : globalActions) {
        if (!g.onlyProjects || folder.isProject) {
            add(g.commandId, true, g.expansion, g.group, g.groupIcon);
        }
    }
    for (const QString &commandId : folder.actions) {
        const QString group = folder.actionGroups.value(commandId).trimmed();
        add(commandId, false, folder.expansionActions.contains(commandId), group,
            group.isEmpty() ? QString() : folder.groupIcons.value(group));
    }
    // Grupos: uma grafia por grupo (a do primeiro membro) e um ícone por grupo (o primeiro informado).
    QMap<QString, QString> spelling; // minúsculas -> grafia
    QMap<QString, QString> icon;     // minúsculas -> ícone
    for (const FolderAction &a : std::as_const(result)) {
        if (a.group.isEmpty()) {
            continue;
        }
        const QString key = a.group.toLower();
        spelling.insert(key, spelling.value(key, a.group));
        if (!icon.contains(key) && !a.groupIcon.isEmpty()) {
            icon.insert(key, a.groupIcon);
        }
    }
    for (FolderAction &a : result) {
        if (!a.group.isEmpty()) {
            const QString key = a.group.toLower();
            a.group = spelling.value(key);
            a.groupIcon = icon.value(key);
            a.expansion = false;
        }
    }
    return result;
}

Command makeFolderActionCommand(const Command &base, const Folder &folder)
{
    Command c = base;
    c.id = folderActionCommandId(base.id, folder.id);
    c.folderId = folder.id;
    c.workingDirMode = WorkingDirMode::Inherit;
    c.workingDir.clear();
    c.cronExpression.clear();
    c.cronNotifyOnRun = false;
    c.autoRun = false;
    c.autoRunDelaySec = 0;
    c.cliPath.clear();
    c.order = -1;
    return c;
}

int removeCommandFromActions(const QString &commandId, QVector<Folder> &folders,
                             QVector<GlobalAction> &globalActions)
{
    int removed = 0;
    for (Folder &f : folders) {
        removed += static_cast<int>(f.actions.removeAll(commandId));
        f.expansionActions.removeAll(commandId);
        if (f.actionGroups.remove(commandId) > 0) {
            // O ícone de um grupo que ficou sem membros não serve mais.
            const QStringList stillUsed = f.actionGroups.values();
            for (auto it = f.groupIcons.begin(); it != f.groupIcons.end();) {
                it = stillUsed.contains(it.key()) ? std::next(it) : f.groupIcons.erase(it);
            }
        }
    }
    for (int i = static_cast<int>(globalActions.size()) - 1; i >= 0; --i) {
        if (globalActions.at(i).commandId == commandId) {
            globalActions.removeAt(i);
            ++removed;
        }
    }
    return removed;
}

} // namespace kai::core
