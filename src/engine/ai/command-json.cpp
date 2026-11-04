#include "engine/ai/command-json.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace kai::engine {

using core::Command;

namespace {

bool validIdentifier(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(name).hasMatch();
}

} // namespace

// Monta um Command a partir do objeto que o modelo mandou (chaves do kai.yml / Command::toJson).
bool commandFromAiJson(const QJsonObject &input, const QString &folderId, const Command *existing,
                       const QVector<Command> &allCommands,
                       const std::function<QString(const QString &, const QString &)> &makeId,
                       Command &out, QString &error, QStringList *warnings)
{
    QJsonObject obj = input;
    static const QSet<QString> known = {
        "id", "folder_id", "folder", "name", "description", "type", "icon", "command", "language", "interpreter",
        "working_dir", "is_background", "compact_output", "hide_on_run", "ignore_exit_code", "hidden", "capture_env",
        "declared_env_vars", "open_last_link", "interactive_terminal", "formatted_output", "kip", "kip_window",
        "kip_auto_close", "kip_auto_close_delay_sec", "terminal_target", "auto_run", "auto_run_delay_sec",
        "cron_expression", "cron_notify_on_run", "order", "http_config", "params", "hooks", "execution_conditions",
        "condition_combinator", "condition_skip_behavior", "responders", "last_param_values", "kip_last_values",
        "param_usage_history", "cli_path", "cli_working_dir"};
    QStringList unknown;
    for (const QString &k : obj.keys()) {
        if (!known.contains(k)) unknown << k;
    }
    if (!unknown.isEmpty()) {
        unknown.sort();
        error = QStringLiteral("Unknown command key(s): %1. Check the keys with read_manifesto (document 'kai', section '3.1').")
                    .arg(unknown.join(QStringLiteral(", ")));
        return false;
    }
    if (obj.contains(QStringLiteral("type"))) {
        const QString t = obj.value(QStringLiteral("type")).toString();
        if (t != QLatin1String("command") && t != QLatin1String("http") && t != QLatin1String("shell")) {
            error = QStringLiteral("Invalid type '%1' (use 'command' or 'http').").arg(t);
            return false;
        }
    }
    if (obj.contains(QStringLiteral("language"))) {
        const QString l = obj.value(QStringLiteral("language")).toString();
        static const QSet<QString> langs = {"native", "python", "node", "php"};
        if (!langs.contains(l)) {
            error = QStringLiteral("Invalid language '%1' (native, python, node or php).").arg(l);
            return false;
        }
    }

    // Hooks: nomes (ou ids) -> ids.
    if (obj.contains(QStringLiteral("hooks"))) {
        QJsonObject hooks = obj.value(QStringLiteral("hooks")).toObject();
        for (const QString &stage : {QStringLiteral("pre"), QStringLiteral("post"), QStringLiteral("cleanup")}) {
            if (!hooks.contains(stage)) continue;
            QJsonArray ids;
            for (const QJsonValue &v : hooks.value(stage).toArray()) {
                const QString ref = v.toString().trimmed();
                QString resolved;
                int hits = 0;
                for (const Command &c : allCommands) {
                    if (c.id == ref) { resolved = c.id; hits = 1; break; }
                }
                if (hits == 0) {
                    for (const Command &c : allCommands) {
                        if (c.name.compare(ref, Qt::CaseInsensitive) == 0) { resolved = c.id; ++hits; }
                    }
                }
                if (hits != 1) {
                    error = hits == 0 ? QStringLiteral("Hook '%1' is not an existing command.").arg(ref)
                                      : QStringLiteral("Hook '%1' matches several commands; use the id.").arg(ref);
                    return false;
                }
                if (existing && resolved == existing->id) {
                    error = QStringLiteral("A command cannot be its own hook.");
                    return false;
                }
                ids.append(resolved);
            }
            hooks[stage] = ids;
        }
        obj[QStringLiteral("hooks")] = hooks;
    }

    obj.remove(QStringLiteral("folder"));
    obj[QStringLiteral("folder_id")] = folderId;
    if (existing) {
        obj[QStringLiteral("id")] = existing->id;
    }
    Command c = Command::fromJson(obj);
    if (existing) {
        // Campos de uso que o modelo não vê (e que não podem sumir numa edição).
        if (!obj.contains(QStringLiteral("last_param_values"))) c.lastParamValues = existing->lastParamValues;
        if (!obj.contains(QStringLiteral("kip_last_values"))) c.kipLastValues = existing->kipLastValues;
        if (!obj.contains(QStringLiteral("param_usage_history"))) c.paramUsageHistory = existing->paramUsageHistory;
    } else {
        c.id = makeId ? makeId(folderId, c.name) : QString();
    }
    c.name = c.name.trimmed();

    if (c.name.isEmpty()) {
        error = QStringLiteral("The command needs a 'name'.");
        return false;
    }
    for (const Command &o : allCommands) {
        if (o.folderId == folderId && o.name.compare(c.name, Qt::CaseInsensitive) == 0 && (!existing || o.id != existing->id)) {
            error = QStringLiteral("A command named '%1' already exists in this folder.").arg(c.name);
            return false;
        }
    }
    if (c.type == core::CommandType::Http) {
        if (!c.httpConfig || c.httpConfig->url.trimmed().isEmpty()) {
            error = QStringLiteral("An http command needs http_config.url.");
            return false;
        }
    } else if (c.command.trimmed().isEmpty()) {
        error = QStringLiteral("The command needs a non-empty 'command'.");
        return false;
    }
    QSet<QString> names;
    for (const core::Parameter &p : c.params) {
        if (!validIdentifier(p.name)) {
            error = QStringLiteral("Invalid parameter name '%1' (letters, digits and _; start with a letter or _).").arg(p.name);
            return false;
        }
        if (names.contains(p.name)) {
            error = QStringLiteral("Duplicate parameter name '%1'.").arg(p.name);
            return false;
        }
        names.insert(p.name);
    }
    if (c.kip && (c.interactiveTerminal || c.formattedOutput || c.compactOutput || c.openLastLink || c.isBackground)) {
        if (warnings) *warnings << QStringLiteral("kip cannot be combined with interactive_terminal, formatted_output, compact_output, open_last_link or is_background.");
    }
    out = c;
    return true;
}

QJsonObject commandToAiJson(const Command &c, const QVector<Command> &allCommands)
{
    QJsonObject o = c.toJson();
    o.remove(QStringLiteral("last_param_values"));
    o.remove(QStringLiteral("kip_last_values"));
    o.remove(QStringLiteral("param_usage_history"));
    if (o.contains(QStringLiteral("hooks"))) {
        auto names = [&allCommands](const QStringList &ids) {
            QJsonArray arr;
            for (const QString &id : ids) {
                QString name = id;
                for (const Command &other : allCommands) {
                    if (other.id == id) name = other.name;
                }
                arr.append(name);
            }
            return arr;
        };
        QJsonObject hooks;
        if (!c.hooks.pre.isEmpty()) hooks[QStringLiteral("pre")] = names(c.hooks.pre);
        if (!c.hooks.post.isEmpty()) hooks[QStringLiteral("post")] = names(c.hooks.post);
        if (!c.hooks.cleanup.isEmpty()) hooks[QStringLiteral("cleanup")] = names(c.hooks.cleanup);
        o[QStringLiteral("hooks")] = hooks;
    }
    return o;
}

} // namespace kai::engine
