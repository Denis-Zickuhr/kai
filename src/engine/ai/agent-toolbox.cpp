#include "engine/ai/agent-toolbox.h"
#include "engine/ai/agent-toolbox-internal.h"
#include "engine/ai/command-json.h"

#include "core/kai-file-validator.h"
#include "utils/translation-manager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>

namespace kai::engine {

using core::AiToolCall;
using core::Command;
using core::Folder;

using namespace toolutil;

QString AgentToolbox::limited(const QString &text, int maxChars)
{
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(maxChars) + QStringLiteral("\n[truncated: %1 more characters]").arg(text.size() - maxChars);
}

QString describeJsonDiff(const QJsonObject &before, const QJsonObject &after)
{
    QStringList lines;
    auto show = [](const QJsonValue &v) { return clip(compact(v).replace(QLatin1Char('\n'), QStringLiteral("⏎")), 160); };
    QStringList keys = before.keys() + after.keys();
    keys.removeDuplicates();
    keys.sort();
    for (const QString &k : keys) {
        const bool inBefore = before.contains(k);
        const bool inAfter = after.contains(k);
        if (inBefore && !inAfter) {
            lines << QStringLiteral("- %1: %2").arg(k, show(before.value(k)));
        } else if (!inBefore && inAfter) {
            lines << QStringLiteral("+ %1: %2").arg(k, show(after.value(k)));
        } else if (before.value(k) != after.value(k)) {
            lines << QStringLiteral("~ %1: %2 → %3").arg(k, show(before.value(k)), show(after.value(k)));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

AgentToolbox::AgentToolbox(AgentToolHost *host, QObject *parent)
    : QObject(parent)
    , m_host(host)
{
    buildTools();
}

AgentToolbox::~AgentToolbox() = default;

const AgentToolbox::Tool *AgentToolbox::toolNamed(const QString &name) const
{
    for (const Tool &t : m_tools) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

QVector<AiToolSpec> AgentToolbox::specs() const
{
    QVector<AiToolSpec> out;
    for (const Tool &t : m_tools) {
        out.append(AiToolSpec{t.name, t.description, t.schema});
    }
    return out;
}

// ------------------------------------------------------------------ helpers

const Folder *AgentToolbox::findFolder(const QString &id) const
{
    for (const Folder &f : m_host->commandsData().folders) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

QString AgentToolbox::folderPath(const QString &folderId) const
{
    QStringList names;
    QString cur = folderId;
    QSet<QString> seen;
    while (!cur.isEmpty() && !seen.contains(cur)) {
        seen.insert(cur);
        const Folder *f = findFolder(cur);
        if (!f) break;
        names.prepend(f->name);
        cur = f->parentId.value_or(QString());
    }
    return names.join(QLatin1Char('/'));
}

bool AgentToolbox::folderIsInside(const QString &candidateId, const QString &ancestorId) const
{
    QString cur = candidateId;
    QSet<QString> seen;
    while (!cur.isEmpty() && !seen.contains(cur)) {
        if (cur == ancestorId) return true;
        seen.insert(cur);
        const Folder *f = findFolder(cur);
        if (!f) return false;
        cur = f->parentId.value_or(QString());
    }
    return false;
}

QVector<QString> AgentToolbox::subtreeFolderIds(const QString &rootId) const
{
    QVector<QString> ids{rootId};
    for (int i = 0; i < ids.size(); ++i) {
        for (const Folder &f : m_host->commandsData().folders) {
            if (f.parentId.value_or(QString()) == ids.at(i) && !ids.contains(f.id)) {
                ids.append(f.id);
            }
        }
    }
    return ids;
}

const Command *AgentToolbox::findCommand(const QJsonObject &args, QString &error) const
{
    const auto &commands = m_host->commandsData().commands;
    const QString id = strArg(args, QStringLiteral("id"));
    if (!id.isEmpty()) {
        for (const Command &c : commands) {
            if (c.id == id) return &c;
        }
        error = QStringLiteral("No command with id '%1'. Use list_commands to find it.").arg(id);
        return nullptr;
    }
    const QString name = strArg(args, QStringLiteral("name"));
    if (name.isEmpty()) {
        error = QStringLiteral("Give the command 'id' (or its 'name').");
        return nullptr;
    }
    QVector<const Command *> hits;
    for (const Command &c : commands) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) hits << &c;
    }
    if (hits.isEmpty()) {
        error = QStringLiteral("No command named '%1'. Use list_commands to find it.").arg(name);
        return nullptr;
    }
    if (hits.size() > 1) {
        QStringList ids;
        for (const Command *c : hits) ids << QStringLiteral("%1 (%2)").arg(c->id, folderPath(c->folderId));
        error = QStringLiteral("Several commands are named '%1': %2. Use the id.").arg(name, ids.join(QStringLiteral("; ")));
        return nullptr;
    }
    return hits.first();
}

QString AgentToolbox::commandSummary(const Command &c) const
{
    if (c.type == core::CommandType::Http && c.httpConfig) {
        return QStringLiteral("%1 %2").arg(c.httpConfig->methodName(), clip(c.httpConfig->url, 100));
    }
    return clip(c.command.section(QLatin1Char('\n'), 0, 0), 100);
}

QString AgentToolbox::maskValue(const QString &value) const
{
    return m_hideSecrets ? QStringLiteral("***hidden***") : value;
}

QString AgentToolbox::uniqueCommandId(const QString &folderId, const QString &name) const
{
    const QString base = QStringLiteral("c_%1_%2").arg(folderId, slug(name));
    auto exists = [this](const QString &id) {
        for (const Command &c : m_host->commandsData().commands) if (c.id == id) return true;
        return false;
    };
    QString id = base;
    for (int n = 2; exists(id); ++n) id = QStringLiteral("%1_%2").arg(base).arg(n);
    return id;
}

QString AgentToolbox::uniqueFolderId(const QString &name) const
{
    const QString base = QStringLiteral("f_custom_%1").arg(slug(name));
    QString id = base;
    for (int n = 2; findFolder(id); ++n) id = QStringLiteral("%1_%2").arg(base).arg(n);
    return id;
}

QJsonObject AgentToolbox::commandView(const Command &c) const
{
    QJsonObject o = commandToAiJson(c, m_host->commandsData().commands);
    o[QStringLiteral("folder_path")] = folderPath(c.folderId);
    return o;
}

bool AgentToolbox::buildCommand(const QJsonObject &input, const QString &folderId, const Command *existing,
                                Command &out, QString &error, QStringList *warnings) const
{
    return commandFromAiJson(input, folderId, existing, m_host->commandsData().commands,
                             [this](const QString &f, const QString &n) { return uniqueCommandId(f, n); },
                             out, error, warnings);
}

// ----------------------------------------------------------------- changes

AgentToolbox::Snapshot AgentToolbox::snapshot() const
{
    Snapshot s;
    s.folders = m_host->commandsData().folders;
    s.commands = m_host->commandsData().commands;
    for (const core::Note &n : m_host->notes()) s.noteIds << n.id;
    s.environments = m_host->settings().environments;
    return s;
}

static bool sameEnvironments(const QVector<core::Environment> &a, const QVector<core::Environment> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i).id != b.at(i).id || a.at(i).name != b.at(i).name || a.at(i).vars != b.at(i).vars
            || a.at(i).secretKeys != b.at(i).secretKeys) {
            return false;
        }
    }
    return true;
}

QString AgentToolbox::commit(const Snapshot &before, const QString &label)
{
    AgentChange ch;
    const auto &folders = m_host->commandsData().folders;
    const auto &commands = m_host->commandsData().commands;

    for (const Folder &old : before.folders) {
        const auto it = std::find_if(folders.cbegin(), folders.cend(), [&old](const Folder &f) { return f.id == old.id; });
        if (it == folders.cend() || it->toJson() != old.toJson()) ch.beforeFolders << old;
    }
    for (const Folder &now : folders) {
        const bool existed = std::any_of(before.folders.cbegin(), before.folders.cend(),
                                         [&now](const Folder &f) { return f.id == now.id; });
        if (!existed) ch.createdFolderIds << now.id;
    }
    for (const Command &old : before.commands) {
        const auto it = std::find_if(commands.cbegin(), commands.cend(), [&old](const Command &c) { return c.id == old.id; });
        if (it == commands.cend() || it->toJson() != old.toJson()) ch.beforeCommands << old;
    }
    for (const Command &now : commands) {
        const bool existed = std::any_of(before.commands.cbegin(), before.commands.cend(),
                                         [&now](const Command &c) { return c.id == now.id; });
        if (!existed) ch.createdCommandIds << now.id;
    }
    for (const core::Note &n : m_host->notes()) {
        if (!before.noteIds.contains(n.id)) ch.createdNoteIds << n.id;
    }
    const QVector<core::Environment> envNow = m_host->settings().environments;
    if (!sameEnvironments(before.environments, envNow)) {
        ch.environmentsChanged = true;
        ch.beforeEnvironments = before.environments;
    }

    if (ch.beforeFolders.isEmpty() && ch.beforeCommands.isEmpty() && ch.createdFolderIds.isEmpty()
        && ch.createdCommandIds.isEmpty() && ch.createdNoteIds.isEmpty() && !ch.environmentsChanged) {
        return QString();
    }
    ch.id = QStringLiteral("chg_%1").arg(++m_changeCounter);
    ch.label = label;
    ch.at = QDateTime::currentDateTime();
    m_changes.append(ch);
    return ch.id;
}

bool AgentToolbox::undo(const QString &changeId, QString &error)
{
    auto it = std::find_if(m_changes.begin(), m_changes.end(), [&changeId](const AgentChange &c) { return c.id == changeId; });
    if (it == m_changes.end()) {
        error = QStringLiteral("Unknown change.");
        return false;
    }
    if (it->undone) {
        error = QStringLiteral("Already undone.");
        return false;
    }
    core::CommandsData &data = m_host->commandsData();
    data.commands.removeIf([&it](const Command &c) { return it->createdCommandIds.contains(c.id); });
    data.folders.removeIf([&it](const Folder &f) { return it->createdFolderIds.contains(f.id); });
    for (const Folder &old : it->beforeFolders) {
        auto f = std::find_if(data.folders.begin(), data.folders.end(), [&old](const Folder &x) { return x.id == old.id; });
        if (f != data.folders.end()) *f = old; else data.folders.append(old);
    }
    for (const Command &old : it->beforeCommands) {
        auto c = std::find_if(data.commands.begin(), data.commands.end(), [&old](const Command &x) { return x.id == old.id; });
        if (c != data.commands.end()) *c = old; else data.commands.append(old);
    }
    if (!it->createdNoteIds.isEmpty()) {
        m_host->notes().removeIf([&it](const core::Note &n) { return it->createdNoteIds.contains(n.id); });
        m_host->notesChanged();
    }
    if (it->environmentsChanged) {
        QString envError;
        if (!m_host->saveEnvironments(it->beforeEnvironments, envError)) {
            error = envError;
            return false;
        }
    }
    it->undone = true;
    m_host->commandsChanged();
    return true;
}

// --------------------------------------------------------------- execution

ToolPlan AgentToolbox::plan(const AiToolCall &call)
{
    const Tool *tool = toolNamed(call.name);
    if (!tool) {
        return invalid(QStringLiteral("Unknown tool '%1'.").arg(call.name));
    }
    if (call.arguments.contains(QStringLiteral("__parse_error"))) {
        return invalid(QStringLiteral("The arguments are not valid JSON."));
    }
    ToolPlan p;
    if (tool->planFn) {
        p = tool->planFn(call.arguments);
    } else {
        p.title = tool->name;
    }
    p.risk = tool->risk;
    return p;
}

void AgentToolbox::execute(const AiToolCall &call, std::function<void(const ToolOutcome &)> done)
{
    const Tool *tool = toolNamed(call.name);
    if (!tool) {
        done(fail(QStringLiteral("Unknown tool '%1'.").arg(call.name)));
        return;
    }
    if (call.name == QLatin1String("run_command")) {
        QString error;
        const Command *c = findCommand(call.arguments, error);
        if (!c) {
            done(fail(error));
            return;
        }
        QMap<QString, QString> params;
        const QJsonObject given = call.arguments.value(QStringLiteral("params")).toObject();
        for (auto it = given.constBegin(); it != given.constEnd(); ++it) {
            params.insert(it.key(), it.value().isString() ? it.value().toString() : compact(it.value()));
        }
        const QString commandId = c->id;
        const QString commandName = c->name;
        auto finished = std::make_shared<bool>(false);
        auto timer = new QTimer(this);
        timer->setSingleShot(true);
        auto finish = [finished, timer, done](const ToolOutcome &o) {
            if (*finished) return;
            *finished = true;
            timer->stop();
            timer->deleteLater();
            done(o);
        };
        connect(timer, &QTimer::timeout, this, [this, commandId, commandName, finish]() {
            QJsonObject o{{QStringLiteral("status"), QStringLiteral("still_running")},
                          {QStringLiteral("command"), commandName},
                          {QStringLiteral("output_so_far"), limited(m_host->storedOutput(commandId).right(8000))},
                          {QStringLiteral("hint"), QStringLiteral("It keeps running in Kai. Use get_command_output or list_processes later.")}};
            finish(okValue(o));
        });
        timer->start(m_runTimeoutMs);
        m_host->runCommand(commandId, params, [this, commandName, finish](const AgentToolHost::RunOutcome &r) {
            if (!r.started) {
                finish(fail(r.error.isEmpty() ? QStringLiteral("The command could not be started.") : r.error));
                return;
            }
            QJsonObject o{{QStringLiteral("status"), r.finished ? QStringLiteral("finished") : QStringLiteral("running_in_background")},
                          {QStringLiteral("command"), commandName},
                          {QStringLiteral("success"), r.success},
                          {QStringLiteral("exit_code"), r.exitCode},
                          {QStringLiteral("output"), limited(r.output.right(8000))}};
            if (!r.error.isEmpty()) o[QStringLiteral("error")] = r.error;
            finish({limited(compact(o)), !r.success && r.finished, {}});
        });
        return;
    }
    if (!tool->runFn) {
        done(fail(QStringLiteral("Tool '%1' cannot run here.").arg(call.name)));
        return;
    }
    done(tool->runFn(call.arguments));
}

} // namespace kai::engine
