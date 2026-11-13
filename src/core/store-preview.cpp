#include "core/store-preview.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QStandardPaths>

#include "core/config-manager.h"

namespace kai::core {

namespace {

QString interpreterFor(const QString &language)
{
    if (language == QLatin1String("python")) return QStringLiteral("python3");
    if (language == QLatin1String("node")) return QStringLiteral("node");
    if (language == QLatin1String("php")) return QStringLiteral("php");
    return {};
}

bool hooksObjectHasContent(const QJsonValue &value)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject o = value.toObject();
    for (const QString &key : {QStringLiteral("pre"), QStringLiteral("post"), QStringLiteral("cleanup")}) {
        if (!o.value(key).toArray().isEmpty()) {
            return true;
        }
    }
    return false;
}

} // namespace

bool storeToolFound(const QString &name)
{
    return !QStandardPaths::findExecutable(name).isEmpty();
}

StorePreview buildStorePreview(const QJsonObject &root)
{
    StorePreview p;
    p.isExportPackage = root.contains(QStringLiteral("kai_export"));
    if (p.isExportPackage) {
        for (const QString &key : {QStringLiteral("environments"), QStringLiteral("terminal_profiles"),
                                   QStringLiteral("settings"), QStringLiteral("collections"),
                                   QStringLiteral("global_actions")}) {
            if (root.contains(key)) p.exportCategories << key;
        }
    }

    if (hooksObjectHasContent(root.value(QStringLiteral("hooks")))) {
        ++p.hooksSkipped;
    }
    for (const QJsonValue &item : root.value(QStringLiteral("folders")).toArray()) {
        const QJsonObject folder = item.toObject();
        const QString path = folder.value(QStringLiteral("path")).toString();
        if (!path.isEmpty() && !p.folders.contains(path)) p.folders << path;
        if (hooksObjectHasContent(folder.value(QStringLiteral("hooks")))) ++p.hooksSkipped;
    }
    for (const QJsonValue &item : root.value(QStringLiteral("commands")).toArray()) {
        const QJsonObject c = item.toObject();
        StorePreviewCommand cmd;
        cmd.folder = c.value(QStringLiteral("folder")).toString();
        if (!cmd.folder.isEmpty() && !p.folders.contains(cmd.folder)) p.folders << cmd.folder;
        cmd.name = c.value(QStringLiteral("name")).toString();
        cmd.type = c.value(QStringLiteral("type")).toString(QStringLiteral("command"));
        cmd.language = c.value(QStringLiteral("language")).toString(QStringLiteral("native"));
        cmd.body = c.value(QStringLiteral("command")).toString();
        // Pipeline: o "código" são os passos (id, o que roda e a verificação).
        if (cmd.type == QLatin1String("pipeline")) {
            QStringList lines;
            for (const QJsonValue &stepValue : c.value(QStringLiteral("steps")).toArray()) {
                const QJsonObject step = stepValue.toObject();
                QString line = QStringLiteral("%1 → %2").arg(step.value(QStringLiteral("id")).toString(),
                                                              step.value(QStringLiteral("run")).toString());
                if (!step.value(QStringLiteral("check")).toString().isEmpty()) {
                    line += QStringLiteral("   (check: %1)").arg(step.value(QStringLiteral("check")).toString());
                }
                lines << line;
            }
            cmd.body = lines.join(QLatin1Char('\n'));
        }
        cmd.cron = c.value(QStringLiteral("cron_expression")).toString();
        cmd.autoRun = c.value(QStringLiteral("auto_run")).toBool(false);
        cmd.hasHooks = hooksObjectHasContent(c.value(QStringLiteral("hooks")));
        if (cmd.hasHooks) ++p.hooksSkipped;
        const QJsonObject http = c.value(QStringLiteral("http_config")).toObject();
        if (!http.isEmpty()) {
            cmd.url = http.value(QStringLiteral("url")).toString();
            if (!cmd.url.isEmpty() && !p.httpUrls.contains(cmd.url)) p.httpUrls << cmd.url;
        }
        const QString interpreter = interpreterFor(cmd.language);
        if (!interpreter.isEmpty() && !p.interpreters.contains(interpreter)) p.interpreters << interpreter;
        p.commands << cmd;
    }
    return p;
}

int applyStoreInstallRules(QVector<Folder> &folders, QVector<Command> &commands, const QString &rootFolderId,
                           const StoreInstallChoices &choices, const QStringList &secretNames)
{
    int removed = 0;
    const auto count = [&removed](Hooks &h) {
        if (!h.isEmpty()) {
            ++removed;
            h = Hooks();
        }
    };
    for (Folder &folder : folders) {
        count(folder.hooks);
    }
    for (Command &command : commands) {
        count(command.hooks);
        if (!command.cronExpression.isEmpty() && !choices.allowCron.contains(command.name)) {
            command.cronExpression.clear();
            command.cronNotifyOnRun = false;
        }
        if (command.autoRun && !choices.allowAutoRun.contains(command.name)) {
            command.autoRun = false;
            command.autoRunDelaySec = 0;
        }
    }
    for (Folder &folder : folders) {
        if (folder.id != rootFolderId) continue;
        for (const QString &name : secretNames) {
            if (name.isEmpty()) continue;
            if (!folder.envVars.contains(name)) folder.envVars.insert(name, QString());
            folder.secretEnvKeys.insert(name);
        }
    }
    return removed;
}

QJsonObject StoreInstallRequest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("provider"), providerId);
    o.insert(QStringLiteral("package"), packageId);
    o.insert(QStringLiteral("version"), version);
    o.insert(QStringLiteral("kind"), kind);
    o.insert(QStringLiteral("kai_file"), kaiFilePath);
    o.insert(QStringLiteral("clone_dir"), cloneDir);
    o.insert(QStringLiteral("parent"), parentFolderId);
    o.insert(QStringLiteral("replace"), replaceFolderId);
    o.insert(QStringLiteral("secrets"), QJsonArray::fromStringList(secrets));
    o.insert(QStringLiteral("allow_cron"), QJsonArray::fromStringList(allowCron));
    o.insert(QStringLiteral("allow_auto_run"), QJsonArray::fromStringList(allowAutoRun));
    o.insert(QStringLiteral("exclude"), QJsonArray::fromStringList(excludeCommands));
    return o;
}

static QStringList toStrings(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &item : v.toArray()) out << item.toString();
    return out;
}

StoreInstallRequest StoreInstallRequest::fromJson(const QJsonObject &o)
{
    StoreInstallRequest r;
    r.providerId = o.value(QStringLiteral("provider")).toString();
    r.packageId = o.value(QStringLiteral("package")).toString();
    r.version = o.value(QStringLiteral("version")).toString();
    r.kind = o.value(QStringLiteral("kind")).toString();
    r.kaiFilePath = o.value(QStringLiteral("kai_file")).toString();
    r.cloneDir = o.value(QStringLiteral("clone_dir")).toString();
    r.parentFolderId = o.value(QStringLiteral("parent")).toString();
    r.replaceFolderId = o.value(QStringLiteral("replace")).toString();
    r.secrets = toStrings(o.value(QStringLiteral("secrets")));
    r.allowCron = toStrings(o.value(QStringLiteral("allow_cron")));
    r.allowAutoRun = toStrings(o.value(QStringLiteral("allow_auto_run")));
    r.excludeCommands = toStrings(o.value(QStringLiteral("exclude")));
    return r;
}

StoreInstallChoices StoreInstallRequest::choices() const
{
    StoreInstallChoices c;
    for (const QString &n : allowCron) c.allowCron.insert(n);
    for (const QString &n : allowAutoRun) c.allowAutoRun.insert(n);
    return c;
}

int removeExcludedStoreCommands(QVector<Folder> &folders, QVector<Command> &commands, const QSet<QString> &names)
{
    if (names.isEmpty()) {
        return 0;
    }
    QSet<QString> removedIds;
    int removed = 0;
    for (auto it = commands.begin(); it != commands.end();) {
        if (names.contains(it->name)) {
            removedIds.insert(it->id);
            it = commands.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    for (Folder &f : folders) {
        f.actions.removeIf([&removedIds](const QString &id) { return removedIds.contains(id); });
        f.expansionActions.removeIf([&removedIds](const QString &id) { return removedIds.contains(id); });
        for (const QString &id : removedIds) {
            f.actionGroups.remove(id);
        }
    }
    return removed;
}

int remapCollidingStoreIds(QVector<Folder> &folders, QVector<Command> &commands, const QString &rootFolderId,
                           const QSet<QString> &existingIds)
{
    QSet<QString> taken = existingIds;
    for (const Folder &f : folders) taken.insert(f.id);
    for (const Command &c : commands) taken.insert(c.id);

    QMap<QString, QString> renamed;
    const auto fresh = [&taken](const QString &id) {
        QString candidate = id + QStringLiteral("_copy");
        for (int n = 2; taken.contains(candidate); ++n) {
            candidate = id + QStringLiteral("_copy") + QString::number(n);
        }
        taken.insert(candidate);
        return candidate;
    };
    for (Folder &f : folders) {
        if (f.id != rootFolderId && existingIds.contains(f.id)) {
            const QString next = fresh(f.id);
            renamed.insert(f.id, next);
            f.id = next;
        }
    }
    for (Command &c : commands) {
        if (existingIds.contains(c.id)) {
            const QString next = fresh(c.id);
            renamed.insert(c.id, next);
            c.id = next;
        }
    }
    if (renamed.isEmpty()) {
        return 0;
    }
    const auto fix = [&renamed](QString &id) {
        const auto it = renamed.constFind(id);
        if (it != renamed.constEnd()) id = it.value();
    };
    const auto fixList = [&fix](QStringList &list) {
        for (QString &id : list) fix(id);
    };
    for (Folder &f : folders) {
        if (f.parentId.has_value()) {
            QString parent = f.parentId.value();
            fix(parent);
            f.parentId = parent;
        }
        fixList(f.actions);
        fixList(f.expansionActions);
        QMap<QString, QString> groups;
        for (auto it = f.actionGroups.constBegin(); it != f.actionGroups.constEnd(); ++it) {
            QString key = it.key();
            fix(key);
            groups.insert(key, it.value());
        }
        f.actionGroups = groups;
        fixList(f.hooks.pre);
        fixList(f.hooks.post);
        fixList(f.hooks.cleanup);
    }
    for (Command &c : commands) {
        fix(c.folderId);
        fixList(c.hooks.pre);
        fixList(c.hooks.post);
        fixList(c.hooks.cleanup);
    }
    return static_cast<int>(renamed.size());
}

QString storeContentHash(const QString &folderId, const CommandsData &data)
{
    const QString text = ConfigManager::exportFolder(folderId, data);
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

} // namespace kai::core
