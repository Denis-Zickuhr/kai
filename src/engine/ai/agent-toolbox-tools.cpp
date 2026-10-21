// As ferramentas do agente: esquema, plano (validação + prévia) e execução de cada uma.
#include "engine/ai/agent-toolbox.h"
#include "engine/ai/agent-toolbox-internal.h"
#include "engine/ai/agent-toolbox-settings.h"

#include "core/kai-file-validator.h"
#include "utils/translation-manager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace kai::engine {

using core::AiToolCall;
using core::Command;
using core::Folder;

using namespace toolutil;

namespace {

QJsonObject idOrName()
{
    return {{QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Command id."))},
            {QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Command name, if the id is unknown."))}};
}

QJsonObject merged(QJsonObject a, const QJsonObject &b)
{
    for (auto it = b.constBegin(); it != b.constEnd(); ++it) a.insert(it.key(), it.value());
    return a;
}

// Aplica um "patch": chave presente substitui, valor null remove.
QJsonObject applyPatch(QJsonObject base, const QJsonObject &patch)
{
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
        if (it.value().isNull()) base.remove(it.key()); else base.insert(it.key(), it.value());
    }
    return base;
}

QString commandBody(const Command &c)
{
    if (c.type == core::CommandType::Http && c.httpConfig) {
        QString s = QStringLiteral("%1 %2").arg(c.httpConfig->methodName(), c.httpConfig->url);
        if (!c.httpConfig->body.isEmpty()) s += QStringLiteral("\n") + clip(c.httpConfig->body, 600);
        return s;
    }
    return clip(c.command, 1200);
}

} // namespace

void AgentToolbox::buildTools()
{
    auto add = [this](const QString &name, const QString &description, const QJsonObject &schema, ToolRisk risk,
                      std::function<ToolPlan(const QJsonObject &)> planFn,
                      std::function<ToolOutcome(const QJsonObject &)> runFn) {
        Tool t;
        t.name = name;
        t.description = description;
        t.schema = schema;
        t.risk = risk;
        t.planFn = std::move(planFn);
        t.runFn = std::move(runFn);
        m_tools.append(std::move(t));
    };
    auto titled = [](const QString &title) {
        return [title](const QJsonObject &) { ToolPlan p; p.title = title; return p; };
    };

    // ---------------------------------------------------------------- leitura
    add(QStringLiteral("list_folders"),
        QStringLiteral("List all folders with id, name, parent, full path and how many commands each holds."),
        objectSchema({}), ToolRisk::Read, titled(QStringLiteral("List folders")),
        [this](const QJsonObject &) {
            QJsonArray arr;
            const auto &data = m_host->commandsData();
            for (const Folder &f : data.folders) {
                int n = 0;
                for (const Command &c : data.commands) if (c.folderId == f.id) ++n;
                QJsonObject o{{QStringLiteral("id"), f.id},
                              {QStringLiteral("name"), f.name},
                              {QStringLiteral("path"), folderPath(f.id)},
                              {QStringLiteral("commands"), n}};
                if (f.parentId) o[QStringLiteral("parent_id")] = *f.parentId;
                if (f.isProject) o[QStringLiteral("is_project")] = true;
                if (f.hidden) o[QStringLiteral("hidden")] = true;
                arr.append(o);
            }
            return okValue(arr);
        });

    add(QStringLiteral("get_folder"), QStringLiteral("Full definition of one folder (variables, working dir, HTTP defaults, actions)."),
        objectSchema({{QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Folder id."))}}, {QStringLiteral("id")}),
        ToolRisk::Read, titled(QStringLiteral("Read folder")),
        [this](const QJsonObject &a) {
            const Folder *f = findFolder(strArg(a, QStringLiteral("id")));
            if (!f) return fail(QStringLiteral("No folder with that id. Use list_folders."));
            QJsonObject o = f->toJson();
            if (m_hideSecrets && o.contains(QStringLiteral("env_vars"))) {
                QJsonObject env = o.value(QStringLiteral("env_vars")).toObject();
                for (const QString &k : env.keys()) if (f->secretEnvKeys.contains(k)) env[k] = maskValue(env.value(k).toString());
                o[QStringLiteral("env_vars")] = env;
            }
            o[QStringLiteral("path")] = folderPath(f->id);
            return okValue(o);
        });

    add(QStringLiteral("list_commands"),
        QStringLiteral("List commands (id, name, type, folder, one-line summary). Filter by folder and/or a text query on the name."),
        objectSchema({{QStringLiteral("folder_id"), prop(QStringLiteral("string"), QStringLiteral("Only this folder (not its subfolders)."))},
                      {QStringLiteral("query"), prop(QStringLiteral("string"), QStringLiteral("Case-insensitive text in the name."))},
                      {QStringLiteral("include_hidden"), prop(QStringLiteral("boolean"), QStringLiteral("Include hidden commands. Default false."))},
                      {QStringLiteral("limit"), prop(QStringLiteral("integer"), QStringLiteral("Default 100, max 200."))},
                      {QStringLiteral("offset"), prop(QStringLiteral("integer"), QStringLiteral("Default 0."))}}),
        ToolRisk::Read, titled(QStringLiteral("List commands")),
        [this](const QJsonObject &a) {
            const QString folder = strArg(a, QStringLiteral("folder_id"));
            const QString query = strArg(a, QStringLiteral("query"));
            const bool hidden = a.value(QStringLiteral("include_hidden")).toBool(false);
            QVector<const Command *> hits;
            for (const Command &c : m_host->commandsData().commands) {
                if (!folder.isEmpty() && c.folderId != folder) continue;
                if (!query.isEmpty() && !c.name.contains(query, Qt::CaseInsensitive)) continue;
                if (c.hidden && !hidden) continue;
                hits << &c;
            }
            const int limit = intArg(a, QStringLiteral("limit"), 100, 1, 200);
            const int offset = intArg(a, QStringLiteral("offset"), 0, 0, 1000000);
            QJsonArray arr;
            for (int i = offset; i < hits.size() && i < offset + limit; ++i) {
                const Command &c = *hits.at(i);
                arr.append(QJsonObject{{QStringLiteral("id"), c.id},
                                       {QStringLiteral("name"), c.name},
                                       {QStringLiteral("type"), core::commandTypeToString(c.type)},
                                       {QStringLiteral("folder_id"), c.folderId},
                                       {QStringLiteral("folder_path"), folderPath(c.folderId)},
                                       {QStringLiteral("summary"), commandSummary(c)}});
            }
            return okValue(fitPage(arr, int(hits.size()), offset));
        });

    add(QStringLiteral("get_command"), QStringLiteral("Full definition of one command, in the kai.yml key format (hooks by name)."),
        objectSchema(idOrName()), ToolRisk::Read, titled(QStringLiteral("Read command")),
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            return c ? okValue(commandView(*c)) : fail(error);
        });

    add(QStringLiteral("list_collections"), QStringLiteral("List collections (tabular data sources): id, name, folder, columns and number of rows."),
        objectSchema({}), ToolRisk::Read, titled(QStringLiteral("List collections")),
        [this](const QJsonObject &) {
            QJsonArray arr;
            for (const core::Collection &c : m_host->collections()) {
                QJsonArray cols;
                for (const core::CollectionField &f : c.schema) cols.append(f.name);
                arr.append(QJsonObject{{QStringLiteral("id"), c.id},
                                       {QStringLiteral("name"), c.name},
                                       {QStringLiteral("folder_id"), c.folderId},
                                       {QStringLiteral("columns"), cols},
                                       {QStringLiteral("rows"), int(c.entries.size())}});
            }
            return okValue(arr);
        });

    add(QStringLiteral("get_collection"), QStringLiteral("Rows of one collection (paged). Secret columns are masked when secrets are hidden."),
        objectSchema({{QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Collection id or name."))},
                      {QStringLiteral("limit"), prop(QStringLiteral("integer"), QStringLiteral("Default 50, max 200."))},
                      {QStringLiteral("offset"), prop(QStringLiteral("integer"), QStringLiteral("Default 0."))}}, {QStringLiteral("id")}),
        ToolRisk::Read, titled(QStringLiteral("Read collection")),
        [this](const QJsonObject &a) {
            const QString ref = strArg(a, QStringLiteral("id"));
            const core::Collection *col = nullptr;
            for (const core::Collection &c : m_host->collections()) {
                if (c.id == ref || c.name.compare(ref, Qt::CaseInsensitive) == 0) { col = &c; break; }
            }
            if (!col) return fail(QStringLiteral("No such collection. Use list_collections."));
            const int limit = intArg(a, QStringLiteral("limit"), 50, 1, 200);
            const int offset = intArg(a, QStringLiteral("offset"), 0, 0, 1000000);
            QSet<QString> secret;
            for (const core::CollectionField &f : col->schema) if (f.secret) secret.insert(f.name);
            QJsonArray rows;
            for (int i = offset; i < col->entries.size() && i < offset + limit; ++i) {
                QJsonObject row;
                const auto &values = col->entries.at(i).values;
                for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
                    row[it.key()] = secret.contains(it.key()) ? maskValue(it.value()) : it.value();
                }
                rows.append(row);
            }
            QJsonObject page = fitPage(rows, int(col->entries.size()), offset);
            return okValue(QJsonObject{{QStringLiteral("name"), col->name},
                                       {QStringLiteral("total"), page.value(QStringLiteral("total"))},
                                       {QStringLiteral("next_offset"), page.value(QStringLiteral("next_offset"))},
                                       {QStringLiteral("rows"), page.value(QStringLiteral("items"))}});
        });

    add(QStringLiteral("list_environments"),
        QStringLiteral("Environments (variable sets) with their variables, and which one is active."),
        objectSchema({}), ToolRisk::Read, titled(QStringLiteral("List environments")),
        [this](const QJsonObject &) {
            const core::SettingsData s = m_host->settings();
            QJsonArray arr;
            for (const core::Environment &e : s.environments) {
                QJsonObject vars;
                for (auto it = e.vars.constBegin(); it != e.vars.constEnd(); ++it) {
                    vars[it.key()] = e.secretKeys.contains(it.key()) ? maskValue(it.value()) : it.value();
                }
                QStringList secrets(e.secretKeys.cbegin(), e.secretKeys.cend());
                secrets.sort();
                arr.append(QJsonObject{{QStringLiteral("id"), e.id},
                                       {QStringLiteral("name"), e.name},
                                       {QStringLiteral("active"), e.id == s.activeEnvironmentId},
                                       {QStringLiteral("variables"), vars},
                                       {QStringLiteral("secret_variables"), QJsonArray::fromStringList(secrets)}});
            }
            return okValue(arr);
        });

    add(QStringLiteral("list_notes"), QStringLiteral("List notes (id, name, folder, type)."),
        objectSchema({{QStringLiteral("folder_id"), prop(QStringLiteral("string"), QStringLiteral("Only this folder."))}}),
        ToolRisk::Read, titled(QStringLiteral("List notes")),
        [this](const QJsonObject &a) {
            const QString folder = strArg(a, QStringLiteral("folder_id"));
            QJsonArray arr;
            for (const core::Note &n : m_host->notes()) {
                if (!folder.isEmpty() && n.folderId != folder) continue;
                arr.append(QJsonObject{{QStringLiteral("id"), n.id}, {QStringLiteral("name"), n.name},
                                       {QStringLiteral("folder_id"), n.folderId}, {QStringLiteral("type"), n.type}});
            }
            return okValue(arr);
        });

    add(QStringLiteral("get_note"), QStringLiteral("Content of one note."),
        objectSchema({{QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Note id."))}}, {QStringLiteral("id")}),
        ToolRisk::Read, titled(QStringLiteral("Read note")),
        [this](const QJsonObject &a) {
            const QString id = strArg(a, QStringLiteral("id"));
            for (const core::Note &n : m_host->notes()) {
                if (n.id == id) return okValue(QJsonObject{{QStringLiteral("name"), n.name}, {QStringLiteral("type"), n.type},
                                                           {QStringLiteral("content"), n.content}});
            }
            return fail(QStringLiteral("No note with that id. Use list_notes."));
        });

    add(QStringLiteral("list_run_history"), QStringLiteral("Recent runs, newest first (no output; use get_run_output)."),
        objectSchema({{QStringLiteral("limit"), prop(QStringLiteral("integer"), QStringLiteral("Default 20, max 100."))},
                      {QStringLiteral("command_id"), prop(QStringLiteral("string"), QStringLiteral("Only runs of this command."))}}),
        ToolRisk::Read, titled(QStringLiteral("Read run history")),
        [this](const QJsonObject &a) {
            const int limit = intArg(a, QStringLiteral("limit"), 20, 1, 100);
            const QString only = strArg(a, QStringLiteral("command_id"));
            QJsonArray arr;
            for (const core::RunRecord &r : m_host->runHistory()) {
                if (!only.isEmpty() && r.commandId != only) continue;
                arr.append(QJsonObject{{QStringLiteral("run_id"), r.id},
                                       {QStringLiteral("command_id"), r.commandId},
                                       {QStringLiteral("command"), r.commandName},
                                       {QStringLiteral("type"), r.commandType},
                                       {QStringLiteral("started_at"), r.startedAt.toString(Qt::ISODate)},
                                       {QStringLiteral("duration_ms"), double(r.durationMs)},
                                       {QStringLiteral("success"), r.success},
                                       {QStringLiteral("output_chars"), int(r.output.size())}});
                if (arr.size() >= limit) break;
            }
            return okValue(arr);
        });

    add(QStringLiteral("get_run_output"), QStringLiteral("Output of one past run, paged by character."),
        objectSchema({{QStringLiteral("run_id"), prop(QStringLiteral("string"), QStringLiteral("From list_run_history."))},
                      {QStringLiteral("offset"), prop(QStringLiteral("integer"), QStringLiteral("Default 0."))},
                      {QStringLiteral("limit"), prop(QStringLiteral("integer"), QStringLiteral("Default 6000, max 12000."))}}, {QStringLiteral("run_id")}),
        ToolRisk::Read, titled(QStringLiteral("Read run output")),
        [this](const QJsonObject &a) {
            const QString id = strArg(a, QStringLiteral("run_id"));
            for (const core::RunRecord &r : m_host->runHistory()) {
                if (r.id != id) continue;
                const int offset = intArg(a, QStringLiteral("offset"), 0, 0, 100000000);
                const int limit = intArg(a, QStringLiteral("limit"), 6000, 1, kMaxResultChars);
                return okValue(QJsonObject{{QStringLiteral("total_chars"), int(r.output.size())},
                                           {QStringLiteral("offset"), offset},
                                           {QStringLiteral("text"), r.output.mid(offset, limit)}});
            }
            return fail(QStringLiteral("No run with that id."));
        });

    add(QStringLiteral("get_command_output"), QStringLiteral("Output kept in memory for the last run of a command (the tail)."),
        objectSchema(merged(idOrName(), {{QStringLiteral("tail_chars"), prop(QStringLiteral("integer"), QStringLiteral("Default 6000."))}})),
        ToolRisk::Read, titled(QStringLiteral("Read command output")),
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return fail(error);
            const QString out = m_host->storedOutput(c->id);
            const int tail = intArg(a, QStringLiteral("tail_chars"), 6000, 1, kMaxResultChars);
            return okValue(QJsonObject{{QStringLiteral("command"), c->name},
                                       {QStringLiteral("running"), m_host->isRunning(c->id)},
                                       {QStringLiteral("total_chars"), int(out.size())},
                                       {QStringLiteral("text"), out.right(tail)}});
        });

    add(QStringLiteral("list_processes"), QStringLiteral("Processes Kai is running now."),
        objectSchema({}), ToolRisk::Read, titled(QStringLiteral("List processes")),
        [this](const QJsonObject &) { return okValue(m_host->processes()); });

    add(QStringLiteral("read_logs"), QStringLiteral("Recent lines of Kai's own log."),
        objectSchema({{QStringLiteral("limit"), prop(QStringLiteral("integer"), QStringLiteral("Default 50, max 200."))},
                      {QStringLiteral("min_level"), prop(QStringLiteral("string"), QStringLiteral("debug, info, warning or error. Default info."))}}),
        ToolRisk::Read, titled(QStringLiteral("Read logs")),
        [this](const QJsonObject &a) {
            const int limit = intArg(a, QStringLiteral("limit"), 50, 1, 200);
            const QString lv = strArg(a, QStringLiteral("min_level")).toLower();
            const int minLevel = lv == QLatin1String("debug") ? 0 : lv == QLatin1String("warning") ? 2 : lv == QLatin1String("error") ? 3 : 1;
            QStringList lines;
            const auto logs = m_host->recentLogs();
            for (auto it = logs.crbegin(); it != logs.crend() && lines.size() < limit; ++it) {
                if (int(it->level) < minLevel) continue;
                static const char *names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
                lines.prepend(QStringLiteral("%1 %2 [%3] %4").arg(it->timestamp.toString(Qt::ISODate),
                              QLatin1String(names[std::clamp(int(it->level), 0, 3)]), it->tag, it->message));
            }
            return okText(lines.join(QLatin1Char('\n')));
        });

    add(QStringLiteral("list_notifications"), QStringLiteral("Recent notifications Kai generated."),
        objectSchema({{QStringLiteral("limit"), prop(QStringLiteral("integer"), QStringLiteral("Default 20, max 100."))}}),
        ToolRisk::Read, titled(QStringLiteral("Read notifications")),
        [this](const QJsonObject &a) {
            const int limit = intArg(a, QStringLiteral("limit"), 20, 1, 100);
            QJsonArray arr;
            for (const core::NotificationRecord &n : m_host->notifications()) {
                arr.append(QJsonObject{{QStringLiteral("at"), n.createdAt.toString(Qt::ISODate)}, {QStringLiteral("event"), n.eventKey},
                                       {QStringLiteral("title"), n.title}, {QStringLiteral("body"), clip(n.body, 400)},
                                       {QStringLiteral("command_id"), n.commandId}});
                if (arr.size() >= limit) break;
            }
            return okValue(arr);
        });

    add(QStringLiteral("get_settings"), QStringLiteral("A few non-secret settings: theme, language, terminal profiles, interpreters."),
        objectSchema({}), ToolRisk::Read, titled(QStringLiteral("Read settings")),
        [this](const QJsonObject &) {
            const core::SettingsData s = m_host->settings();
            QJsonArray terminals;
            for (const core::TerminalProfile &t : s.terminalProfiles) {
                terminals.append(QJsonObject{{QStringLiteral("name"), t.name}, {QStringLiteral("template"), t.commandTemplate},
                                             {QStringLiteral("default"), t.isDefault}});
            }
            return okValue(QJsonObject{{QStringLiteral("theme"), s.activeTheme},
                                       {QStringLiteral("language"), s.language},
                                       {QStringLiteral("terminal_profiles"), terminals},
                                       {QStringLiteral("interpreters"), QJsonObject{{QStringLiteral("python"), s.interpreters.python},
                                                                                  {QStringLiteral("node"), s.interpreters.node},
                                                                                  {QStringLiteral("php"), s.interpreters.php}}}});
        });

    add(QStringLiteral("read_manifesto"),
        QStringLiteral("Read the Kai reference by section. document: kai (the kai.yml format), kip (interactive protocol), "
                       "icons (valid icon names) or schema (JSON Schema definitions). Without 'section' it lists the sections."),
        objectSchema({{QStringLiteral("document"), prop(QStringLiteral("string"), QStringLiteral("kai, kip, icons or schema."))},
                      {QStringLiteral("section"), prop(QStringLiteral("string"), QStringLiteral("Section id from the list, e.g. 3.1."))},
                      {QStringLiteral("offset"), prop(QStringLiteral("integer"), QStringLiteral("Character offset for long sections."))}},
                     {QStringLiteral("document")}),
        ToolRisk::Read, titled(QStringLiteral("Read reference")),
        [this](const QJsonObject &a) {
            const QString doc = strArg(a, QStringLiteral("document")).toLower();
            if (!ManifestoLibrary::documents().contains(doc)) {
                return fail(QStringLiteral("Unknown document. Use one of: %1.").arg(ManifestoLibrary::documents().join(QStringLiteral(", "))));
            }
            const QString section = strArg(a, QStringLiteral("section"));
            if (section.isEmpty()) {
                const QString outline = m_manifesto.outline(doc);
                return outline.isEmpty() ? fail(QStringLiteral("The reference is not available.")) : okText(outline);
            }
            const QString text = m_manifesto.sectionText(doc, section);
            if (text.isEmpty()) return fail(QStringLiteral("No such section. Call without 'section' to list them."));
            const int offset = intArg(a, QStringLiteral("offset"), 0, 0, 100000000);
            const QString slice = text.mid(offset, kMaxResultChars);
            const int rest = text.size() - offset - slice.size();
            return okText(rest > 0 ? slice + QStringLiteral("\n[%1 more characters; call again with offset=%2]").arg(rest).arg(offset + slice.size())
                                   : slice);
        });

    add(QStringLiteral("validate_kai_yml"), QStringLiteral("Validate the text of a kai.yml and list errors/warnings."),
        objectSchema({{QStringLiteral("text"), prop(QStringLiteral("string"), QStringLiteral("The kai.yml content."))}}, {QStringLiteral("text")}),
        ToolRisk::Read, titled(QStringLiteral("Validate kai.yml")),
        [](const QJsonObject &a) {
            const core::ValidationResult r = core::validateKaiFileText(a.value(QStringLiteral("text")).toString());
            if (r.issues.isEmpty()) return okText(QStringLiteral("Valid: no errors or warnings."));
            QStringList lines;
            for (const core::ValidationIssue &i : r.issues) {
                lines << QStringLiteral("%1 %2: %3").arg(i.severity == core::ValidationSeverity::Error ? QStringLiteral("ERROR") : QStringLiteral("warning"),
                                                         i.path, i.message);
            }
            return okText(lines.join(QLatin1Char('\n')));
        });

    add(QStringLiteral("ask_user"),
        QStringLiteral("Ask the user a question and wait for the answer. Optionally offer short answer options."),
        objectSchema({{QStringLiteral("question"), prop(QStringLiteral("string"), QStringLiteral("The question."))},
                      {QStringLiteral("options"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                              {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                              {QStringLiteral("description"), QStringLiteral("Optional answer choices.")}}}},
                     {QStringLiteral("question")}),
        ToolRisk::Ask,
        [](const QJsonObject &a) {
            ToolPlan p;
            if (a.value(QStringLiteral("question")).toString().trimmed().isEmpty()) return invalid(QStringLiteral("Give the 'question'."));
            p.title = a.value(QStringLiteral("question")).toString();
            return p;
        },
        nullptr);

    // --------------------------------------------------------------- escrita
    add(QStringLiteral("create_folder"),
        QStringLiteral("Create a folder. parent_id empty = a top-level folder (a tab)."),
        objectSchema({{QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Folder name."))},
                      {QStringLiteral("parent_id"), prop(QStringLiteral("string"), QStringLiteral("Parent folder id (optional)."))},
                      {QStringLiteral("icon"), prop(QStringLiteral("string"), QStringLiteral("Icon name (see read_manifesto icons)."))},
                      {QStringLiteral("working_dir"), prop(QStringLiteral("string"), QStringLiteral("Working directory inherited by the commands."))},
                      {QStringLiteral("is_project"), prop(QStringLiteral("boolean"), QStringLiteral("Mark as a project folder."))},
                      {QStringLiteral("env_vars"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                               {QStringLiteral("description"), QStringLiteral("Folder variables, name -> text value.")}}}},
                     {QStringLiteral("name")}),
        ToolRisk::Write,
        [this](const QJsonObject &a) {
            const QString name = strArg(a, QStringLiteral("name"));
            if (name.isEmpty()) return invalid(QStringLiteral("The folder needs a 'name'."));
            const QString parent = strArg(a, QStringLiteral("parent_id"));
            if (!parent.isEmpty() && !findFolder(parent)) return invalid(QStringLiteral("parent_id does not exist."));
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.create_folder")).arg(name);
            p.detail = parent.isEmpty() ? utils::tr(QStringLiteral("ai.plan.top_level")) : utils::tr(QStringLiteral("ai.plan.inside")).arg(folderPath(parent));
            // O que a pasta passa aos comandos que ficam nela (diretório, variáveis) também se aprova.
            QJsonObject extra;
            for (const char *key : {"working_dir", "is_project", "icon"}) {
                if (a.contains(QString::fromLatin1(key))) extra.insert(QString::fromLatin1(key), a.value(QString::fromLatin1(key)));
            }
            const QJsonObject env = a.value(QStringLiteral("env_vars")).toObject();
            for (auto it = env.constBegin(); it != env.constEnd(); ++it) extra.insert(QStringLiteral("env.") + it.key(), it.value());
            const QString extras = describeJsonDiff({}, extra);
            if (!extras.isEmpty()) p.detail += QStringLiteral("\n\n") + extras;
            return p;
        },
        [this](const QJsonObject &a) {
            const Snapshot snap = snapshot();
            Folder f;
            f.name = strArg(a, QStringLiteral("name"));
            f.id = uniqueFolderId(f.name);
            const QString parent = strArg(a, QStringLiteral("parent_id"));
            if (!parent.isEmpty()) f.parentId = parent;
            f.icon = strArg(a, QStringLiteral("icon"));
            f.isProject = a.value(QStringLiteral("is_project")).toBool(false);
            const QString wd = strArg(a, QStringLiteral("working_dir"));
            if (!wd.isEmpty()) { f.workingDirMode = core::WorkingDirMode::Custom; f.workingDir = wd; }
            const QJsonObject env = a.value(QStringLiteral("env_vars")).toObject();
            for (auto it = env.constBegin(); it != env.constEnd(); ++it) f.envVars.insert(it.key(), compact(it.value()));
            m_host->commandsData().folders.append(f);
            m_host->commandsChanged();
            ToolOutcome o = okValue(QJsonObject{{QStringLiteral("id"), f.id}, {QStringLiteral("path"), folderPath(f.id)}});
            o.changeId = commit(snap, utils::tr(QStringLiteral("ai.change.created_folder")).arg(f.name));
            return o;
        });

    add(QStringLiteral("update_folder"),
        QStringLiteral("Change a folder. 'patch' holds the keys to set (as returned by get_folder); a key set to null is removed."),
        objectSchema({{QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Folder id."))},
                      {QStringLiteral("patch"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                            {QStringLiteral("description"), QStringLiteral("Keys to change.")}}}},
                     {QStringLiteral("id"), QStringLiteral("patch")}),
        ToolRisk::Write,
        [this](const QJsonObject &a) {
            const Folder *f = findFolder(strArg(a, QStringLiteral("id")));
            if (!f) return invalid(QStringLiteral("No folder with that id."));
            const QJsonObject patch = a.value(QStringLiteral("patch")).toObject();
            if (patch.isEmpty()) return invalid(QStringLiteral("The patch is empty."));
            if (patch.contains(QStringLiteral("id"))) return invalid(QStringLiteral("The id cannot be changed."));
            const QJsonObject before = f->toJson();
            const QJsonObject after = applyPatch(before, patch);
            if (after.contains(QStringLiteral("parent_id")) && folderIsInside(after.value(QStringLiteral("parent_id")).toString(), f->id)) {
                return invalid(QStringLiteral("A folder cannot be moved inside itself."));
            }
            if (Folder::fromJson(after).name.trimmed().isEmpty()) return invalid(QStringLiteral("The folder needs a name."));
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.edit_folder")).arg(f->name);
            p.detail = describeJsonDiff(before, after);
            return p;
        },
        [this](const QJsonObject &a) {
            const Snapshot snap = snapshot();
            auto &folders = m_host->commandsData().folders;
            auto it = std::find_if(folders.begin(), folders.end(), [&a](const Folder &f) { return f.id == strArg(a, QStringLiteral("id")); });
            if (it == folders.end()) return fail(QStringLiteral("No folder with that id."));
            QJsonObject merged2 = applyPatch(it->toJson(), a.value(QStringLiteral("patch")).toObject());
            merged2[QStringLiteral("id")] = it->id;
            *it = Folder::fromJson(merged2);
            m_host->commandsChanged();
            ToolOutcome o = okText(QStringLiteral("Folder updated."));
            o.changeId = commit(snap, utils::tr(QStringLiteral("ai.change.edited_folder")).arg(it->name));
            return o;
        });

    add(QStringLiteral("delete_folder"),
        QStringLiteral("Delete a folder. Refuses a folder that still has contents unless delete_contents is true (then subfolders and commands go too)."),
        objectSchema({{QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Folder id."))},
                      {QStringLiteral("delete_contents"), prop(QStringLiteral("boolean"), QStringLiteral("Also delete subfolders and commands. Default false."))}},
                     {QStringLiteral("id")}),
        ToolRisk::Danger,
        [this](const QJsonObject &a) {
            const Folder *f = findFolder(strArg(a, QStringLiteral("id")));
            if (!f) return invalid(QStringLiteral("No folder with that id."));
            const QVector<QString> ids = subtreeFolderIds(f->id);
            int cmds = 0;
            for (const Command &c : m_host->commandsData().commands) if (ids.contains(c.folderId)) ++cmds;
            const bool contents = a.value(QStringLiteral("delete_contents")).toBool(false);
            if (!contents && (ids.size() > 1 || cmds > 0)) {
                return invalid(QStringLiteral("The folder is not empty (%1 subfolder(s), %2 command(s)). Move the items first or pass delete_contents=true.")
                                   .arg(ids.size() - 1).arg(cmds));
            }
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.delete_folder")).arg(folderPath(f->id));
            p.detail = utils::tr(QStringLiteral("ai.plan.delete_folder.detail")).arg(ids.size() - 1).arg(cmds);
            return p;
        },
        [this](const QJsonObject &a) {
            const Snapshot snap = snapshot();
            const QString id = strArg(a, QStringLiteral("id"));
            const QVector<QString> ids = subtreeFolderIds(id);
            const QString label = utils::tr(QStringLiteral("ai.change.deleted_folder")).arg(folderPath(id));
            core::CommandsData &data = m_host->commandsData();
            data.commands.removeIf([&ids](const Command &c) { return ids.contains(c.folderId); });
            data.folders.removeIf([&ids](const Folder &f) { return ids.contains(f.id); });
            m_host->commandsChanged();
            ToolOutcome o = okText(QStringLiteral("Folder deleted."));
            o.changeId = commit(snap, label);
            return o;
        });

    add(QStringLiteral("create_command"),
        QStringLiteral("Create a command in a folder. 'command' is an object with the kai.yml command keys (name, type, command, "
                       "language, params, hooks by name, http_config...). Read the reference with read_manifesto (kai, 3.1) first when unsure."),
        objectSchema({{QStringLiteral("folder_id"), prop(QStringLiteral("string"), QStringLiteral("Destination folder id."))},
                      {QStringLiteral("command"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                              {QStringLiteral("description"), QStringLiteral("The command definition.")}}}},
                     {QStringLiteral("folder_id"), QStringLiteral("command")}),
        ToolRisk::Write,
        [this](const QJsonObject &a) {
            const QString folder = strArg(a, QStringLiteral("folder_id"));
            if (!findFolder(folder)) return invalid(QStringLiteral("folder_id does not exist. Use list_folders (or create_folder first)."));
            Command c;
            QString error;
            QStringList warnings;
            if (!buildCommand(a.value(QStringLiteral("command")).toObject(), folder, nullptr, c, error, &warnings)) return invalid(error);
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.create_command")).arg(c.name);
            p.detail = utils::tr(QStringLiteral("ai.plan.create_command.detail")).arg(folderPath(folder), core::commandTypeToString(c.type), commandBody(c));
            if (!c.params.isEmpty()) {
                QStringList names;
                for (const core::Parameter &pr : c.params) names << pr.name;
                p.detail += QStringLiteral("\n\n") + utils::tr(QStringLiteral("ai.plan.parameters")).arg(names.join(QStringLiteral(", ")));
            }
            // Tudo o mais que o comando traz além do texto (agendamento, auto-run, hooks, diretório, terminal...):
            // quem aprova precisa ver QUANDO e COMO ele vai rodar, não só o que diz.
            QJsonObject extra = commandView(c);
            for (const char *key : {"id", "folder_id", "name", "type", "command", "params", "http_config", "folder_path"}) {
                extra.remove(QString::fromLatin1(key));
            }
            const QString extras = describeJsonDiff({}, extra);
            if (!extras.isEmpty()) p.detail += QStringLiteral("\n\n") + extras;
            if (!warnings.isEmpty()) p.detail += QStringLiteral("\n\n! ") + warnings.join(QStringLiteral("\n! "));
            return p;
        },
        [this](const QJsonObject &a) {
            const Snapshot snap = snapshot();
            Command c;
            QString error;
            QStringList warnings;
            if (!buildCommand(a.value(QStringLiteral("command")).toObject(), strArg(a, QStringLiteral("folder_id")), nullptr, c, error, &warnings)) {
                return fail(error);
            }
            m_host->commandsData().commands.append(c);
            m_host->commandsChanged();
            QJsonObject res{{QStringLiteral("id"), c.id}, {QStringLiteral("name"), c.name}};
            if (!warnings.isEmpty()) res[QStringLiteral("warnings")] = QJsonArray::fromStringList(warnings);
            ToolOutcome o = okValue(res);
            o.changeId = commit(snap, utils::tr(QStringLiteral("ai.change.created_command")).arg(c.name));
            return o;
        });

    add(QStringLiteral("update_command"),
        QStringLiteral("Change a command. 'patch' holds the keys to set (the format of get_command; hooks by name); a key set to null is removed. "
                       "To change nested data (params, http_config.headers) send the whole new value of that key."),
        objectSchema(merged(idOrName(), {{QStringLiteral("patch"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                {QStringLiteral("description"), QStringLiteral("Keys to change.")}}}}),
                     {QStringLiteral("patch")}),
        ToolRisk::Write,
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return invalid(error);
            const QJsonObject patch = a.value(QStringLiteral("patch")).toObject();
            if (patch.isEmpty()) return invalid(QStringLiteral("The patch is empty."));
            if (patch.contains(QStringLiteral("id"))) return invalid(QStringLiteral("The id cannot be changed."));
            const QJsonObject before = commandView(*c);
            QJsonObject after = applyPatch(before, patch);
            after.remove(QStringLiteral("folder_path"));
            Command next;
            if (!buildCommand(after, c->folderId, c, next, error)) return invalid(error);
            QJsonObject beforeShown = before;
            beforeShown.remove(QStringLiteral("folder_path"));
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.edit_command")).arg(c->name);
            QJsonObject afterShown = commandView(next);
            afterShown.remove(QStringLiteral("folder_path"));
            p.detail = describeJsonDiff(beforeShown, afterShown);
            if (p.detail.isEmpty()) return invalid(QStringLiteral("The patch changes nothing."));
            return p;
        },
        [this](const QJsonObject &a) {
            QString error;
            const Command *found = findCommand(a, error);
            if (!found) return fail(error);
            const Snapshot snap = snapshot();
            const Command current = *found;
            QJsonObject after = applyPatch(commandView(current), a.value(QStringLiteral("patch")).toObject());
            after.remove(QStringLiteral("folder_path"));
            Command next;
            if (!buildCommand(after, current.folderId, &current, next, error)) return fail(error);
            auto &commands = m_host->commandsData().commands;
            auto it = std::find_if(commands.begin(), commands.end(), [&current](const Command &c) { return c.id == current.id; });
            if (it == commands.end()) return fail(QStringLiteral("The command disappeared."));
            *it = next;
            m_host->commandsChanged();
            ToolOutcome o = okText(QStringLiteral("Command updated."));
            o.changeId = commit(snap, utils::tr(QStringLiteral("ai.change.edited_command")).arg(next.name));
            return o;
        });

    add(QStringLiteral("delete_command"), QStringLiteral("Delete a command (it is also removed from other commands' hooks)."),
        objectSchema(idOrName()), ToolRisk::Danger,
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return invalid(error);
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.delete_command")).arg(c->name);
            p.detail = utils::tr(QStringLiteral("ai.plan.delete_command.detail")).arg(folderPath(c->folderId), commandBody(*c));
            return p;
        },
        [this](const QJsonObject &a) {
            QString error;
            const Command *found = findCommand(a, error);
            if (!found) return fail(error);
            const Snapshot snap = snapshot();
            const QString id = found->id;
            const QString label = utils::tr(QStringLiteral("ai.change.deleted_command")).arg(found->name);
            auto &commands = m_host->commandsData().commands;
            commands.removeIf([&id](const Command &c) { return c.id == id; });
            for (Command &c : commands) {
                c.hooks.pre.removeAll(id);
                c.hooks.post.removeAll(id);
                c.hooks.cleanup.removeAll(id);
            }
            m_host->commandsChanged();
            ToolOutcome o = okText(QStringLiteral("Command deleted."));
            o.changeId = commit(snap, label);
            return o;
        });

    add(QStringLiteral("move_command"), QStringLiteral("Move a command to another folder."),
        objectSchema(merged(idOrName(), {{QStringLiteral("folder_id"), prop(QStringLiteral("string"), QStringLiteral("Destination folder id."))}}),
                     {QStringLiteral("folder_id")}),
        ToolRisk::Write,
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return invalid(error);
            const QString dest = strArg(a, QStringLiteral("folder_id"));
            if (!findFolder(dest)) return invalid(QStringLiteral("folder_id does not exist."));
            if (dest == c->folderId) return invalid(QStringLiteral("The command is already in that folder."));
            for (const Command &o : m_host->commandsData().commands) {
                if (o.folderId == dest && o.name.compare(c->name, Qt::CaseInsensitive) == 0) {
                    return invalid(QStringLiteral("A command named '%1' already exists in the destination.").arg(c->name));
                }
            }
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.move_command")).arg(c->name);
            p.detail = QStringLiteral("%1 → %2").arg(folderPath(c->folderId), folderPath(dest));
            return p;
        },
        [this](const QJsonObject &a) {
            QString error;
            const Command *found = findCommand(a, error);
            if (!found) return fail(error);
            const Snapshot snap = snapshot();
            auto &commands = m_host->commandsData().commands;
            auto it = std::find_if(commands.begin(), commands.end(), [found](const Command &c) { return c.id == found->id; });
            it->folderId = strArg(a, QStringLiteral("folder_id"));
            it->order = -1;
            m_host->commandsChanged();
            ToolOutcome o = okText(QStringLiteral("Command moved."));
            o.changeId = commit(snap, utils::tr(QStringLiteral("ai.change.moved_command")).arg(it->name));
            return o;
        });

    add(QStringLiteral("duplicate_command"), QStringLiteral("Duplicate a command in the same folder (name gets ' (copy)')."),
        objectSchema(idOrName()), ToolRisk::Write,
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return invalid(error);
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.duplicate_command")).arg(c->name);
            return p;
        },
        [this](const QJsonObject &a) {
            QString error;
            const Command *found = findCommand(a, error);
            if (!found) return fail(error);
            const Snapshot snap = snapshot();
            Command copy = *found;
            QString name = found->name + QStringLiteral(" (copy)");
            auto nameTaken = [this, &copy](const QString &n) {
                for (const Command &o : m_host->commandsData().commands) {
                    if (o.folderId == copy.folderId && o.name.compare(n, Qt::CaseInsensitive) == 0) return true;
                }
                return false;
            };
            for (int n = 2; nameTaken(name); ++n) name = QStringLiteral("%1 (copy %2)").arg(found->name).arg(n);
            copy.name = name;
            copy.id = uniqueCommandId(copy.folderId, name);
            copy.order = -1;
            copy.lastParamValues.clear();
            m_host->commandsData().commands.append(copy);
            m_host->commandsChanged();
            ToolOutcome o = okValue(QJsonObject{{QStringLiteral("id"), copy.id}, {QStringLiteral("name"), copy.name}});
            o.changeId = commit(snap, utils::tr(QStringLiteral("ai.change.duplicated_command")).arg(found->name));
            return o;
        });

    // Ambientes
    auto findEnvironment = [this](const QJsonObject &a, const core::SettingsData &s, int &index, QString &error) {
        const QString ref = strArg(a, QStringLiteral("environment"));
        index = -1;
        if (ref.isEmpty()) {
            for (int i = 0; i < s.environments.size(); ++i) if (s.environments.at(i).id == s.activeEnvironmentId) index = i;
            if (index < 0) error = QStringLiteral("There is no active environment; name one in 'environment'.");
            return index >= 0;
        }
        for (int i = 0; i < s.environments.size(); ++i) {
            if (s.environments.at(i).id == ref || s.environments.at(i).name.compare(ref, Qt::CaseInsensitive) == 0) index = i;
        }
        if (index < 0) error = QStringLiteral("No environment '%1'. Use list_environments.").arg(ref);
        return index >= 0;
    };

    add(QStringLiteral("set_environment_variable"),
        QStringLiteral("Create or change a variable of an environment (default: the active one)."),
        objectSchema({{QStringLiteral("environment"), prop(QStringLiteral("string"), QStringLiteral("Environment name or id. Default: active."))},
                      {QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Variable name."))},
                      {QStringLiteral("value"), prop(QStringLiteral("string"), QStringLiteral("Text value."))},
                      {QStringLiteral("secret"), prop(QStringLiteral("boolean"), QStringLiteral("Mark as secret (masked on screen)."))}},
                     {QStringLiteral("name"), QStringLiteral("value")}),
        ToolRisk::Write,
        [this, findEnvironment](const QJsonObject &a) {
            const core::SettingsData s = m_host->settings();
            int idx = -1;
            QString error;
            if (!findEnvironment(a, s, idx, error)) return invalid(error);
            const QString name = strArg(a, QStringLiteral("name"));
            static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
            if (!re.match(name).hasMatch()) return invalid(QStringLiteral("Invalid variable name."));
            const core::Environment &e = s.environments.at(idx);
            const bool secret = a.value(QStringLiteral("secret")).toBool(e.secretKeys.contains(name));
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.set_variable")).arg(name, e.name);
            const QString value = secret ? utils::tr(QStringLiteral("ai.plan.secret_value")) : clip(a.value(QStringLiteral("value")).toString(), 200);
            p.detail = e.vars.contains(name) ? QStringLiteral("~ %1: %2 → %3").arg(name, e.secretKeys.contains(name) ? utils::tr(QStringLiteral("ai.plan.hidden")) : clip(e.vars.value(name), 200), value)
                                             : QStringLiteral("+ %1: %2").arg(name, value);
            return p;
        },
        [this, findEnvironment](const QJsonObject &a) {
            core::SettingsData s = m_host->settings();
            int idx = -1;
            QString error;
            if (!findEnvironment(a, s, idx, error)) return fail(error);
            const Snapshot snap = snapshot();
            const QString name = strArg(a, QStringLiteral("name"));
            core::Environment &e = s.environments[idx];
            e.vars.insert(name, a.value(QStringLiteral("value")).toString());
            if (a.contains(QStringLiteral("secret"))) {
                if (a.value(QStringLiteral("secret")).toBool()) e.secretKeys.insert(name); else e.secretKeys.remove(name);
            }
            if (!m_host->saveEnvironments(s.environments, error)) return fail(error);
            ToolOutcome o = okText(QStringLiteral("Variable saved."));
            o.changeId = commit(snap, utils::tr(QStringLiteral("ai.change.set_variable")).arg(name, e.name));
            return o;
        });

    add(QStringLiteral("delete_environment_variable"), QStringLiteral("Remove a variable from an environment (default: the active one)."),
        objectSchema({{QStringLiteral("environment"), prop(QStringLiteral("string"), QStringLiteral("Environment name or id. Default: active."))},
                      {QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Variable name."))}}, {QStringLiteral("name")}),
        ToolRisk::Danger,
        [this, findEnvironment](const QJsonObject &a) {
            const core::SettingsData s = m_host->settings();
            int idx = -1;
            QString error;
            if (!findEnvironment(a, s, idx, error)) return invalid(error);
            const QString name = strArg(a, QStringLiteral("name"));
            const core::Environment &e = s.environments.at(idx);
            if (!e.vars.contains(name)) return invalid(QStringLiteral("The environment has no variable '%1'.").arg(name));
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.delete_variable")).arg(name, e.name);
            return p;
        },
        [this, findEnvironment](const QJsonObject &a) {
            core::SettingsData s = m_host->settings();
            int idx = -1;
            QString error;
            if (!findEnvironment(a, s, idx, error)) return fail(error);
            const Snapshot snap = snapshot();
            const QString name = strArg(a, QStringLiteral("name"));
            core::Environment &e = s.environments[idx];
            e.vars.remove(name);
            e.secretKeys.remove(name);
            if (!m_host->saveEnvironments(s.environments, error)) return fail(error);
            ToolOutcome o = okText(QStringLiteral("Variable removed."));
            o.changeId = commit(snap, utils::tr(QStringLiteral("ai.change.deleted_variable")).arg(name, e.name));
            return o;
        });

    add(QStringLiteral("create_note"), QStringLiteral("Create a note in a folder (markdown by default)."),
        objectSchema({{QStringLiteral("folder_id"), prop(QStringLiteral("string"), QStringLiteral("Folder id."))},
                      {QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Note name."))},
                      {QStringLiteral("content"), prop(QStringLiteral("string"), QStringLiteral("Text."))},
                      {QStringLiteral("type"), prop(QStringLiteral("string"), QStringLiteral("markdown, text, json, yaml... Default markdown."))}},
                     {QStringLiteral("folder_id"), QStringLiteral("name"), QStringLiteral("content")}),
        ToolRisk::Write,
        [this](const QJsonObject &a) {
            if (!findFolder(strArg(a, QStringLiteral("folder_id")))) return invalid(QStringLiteral("folder_id does not exist."));
            if (strArg(a, QStringLiteral("name")).isEmpty()) return invalid(QStringLiteral("The note needs a 'name'."));
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.create_note")).arg(strArg(a, QStringLiteral("name")));
            p.detail = clip(a.value(QStringLiteral("content")).toString(), 800);
            return p;
        },
        [this](const QJsonObject &a) {
            const Snapshot snap = snapshot();
            core::Note n;
            n.name = strArg(a, QStringLiteral("name"));
            n.folderId = strArg(a, QStringLiteral("folder_id"));
            n.content = a.value(QStringLiteral("content")).toString();
            n.type = core::Note::normalizedType(strArg(a, QStringLiteral("type")));
            n.id = QStringLiteral("note_ai_%1").arg(QDateTime::currentMSecsSinceEpoch());
            n.local = false;
            m_host->notes().append(n);
            m_host->notesChanged();
            ToolOutcome o = okValue(QJsonObject{{QStringLiteral("id"), n.id}});
            o.changeId = commit(snap, utils::tr(QStringLiteral("ai.change.created_note")).arg(n.name));
            return o;
        });

    add(QStringLiteral("run_command"),
        QStringLiteral("Run a command in Kai and wait for its result (up to ~2 minutes; background commands return as soon as they start). "
                       "Pass 'params' as {name: value} for the command's parameters. Not available for interactive-terminal or KIP commands."),
        objectSchema(merged(idOrName(), {{QStringLiteral("params"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                                {QStringLiteral("description"), QStringLiteral("Parameter values, name -> text.")}}}})),
        ToolRisk::Write,
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return invalid(error);
            if (c->interactiveTerminal || c->kip) {
                return invalid(QStringLiteral("Interactive-terminal and KIP commands need the user at the keyboard: use run_in_app to open it in the Kai window."));
            }
            if (m_host->isRunning(c->id)) return invalid(QStringLiteral("That command is already running."));
            const QJsonObject given = a.value(QStringLiteral("params")).toObject();
            QStringList missing;
            for (const core::Parameter &p : c->params) {
                if (p.required && p.defaultValue.isEmpty() && !given.contains(p.name)) missing << p.name;
            }
            if (!missing.isEmpty()) {
                return invalid(QStringLiteral("Missing required parameter(s): %1.").arg(missing.join(QStringLiteral(", "))));
            }
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.run")).arg(c->name);
            p.detail = commandBody(*c);
            // Hooks são OUTROS comandos que rodam junto: quem aprova vê quais.
            auto names = [this](const QStringList &ids) {
                QStringList out;
                for (const QString &id : ids) {
                    const Command *h = nullptr;
                    for (const Command &o : m_host->commandsData().commands) if (o.id == id) h = &o;
                    out << (h ? h->name : id);
                }
                return out.join(QStringLiteral(", "));
            };
            for (const auto &stage : {std::make_pair(QStringLiteral("pre"), c->hooks.pre), std::make_pair(QStringLiteral("post"), c->hooks.post),
                                      std::make_pair(QStringLiteral("cleanup"), c->hooks.cleanup)}) {
                if (!stage.second.isEmpty()) p.detail += QStringLiteral("\n\n+ hooks.%1: %2").arg(stage.first, names(stage.second));
            }
            if (!given.isEmpty()) {
                QStringList lines;
                for (auto it = given.constBegin(); it != given.constEnd(); ++it) lines << QStringLiteral("%1 = %2").arg(it.key(), clip(compact(it.value()), 200));
                p.detail += QStringLiteral("\n\n") + utils::tr(QStringLiteral("ai.plan.run.parameters")) + QLatin1Char('\n') + lines.join(QLatin1Char('\n'));
            }
            return p;
        },
        nullptr);

    add(QStringLiteral("run_in_app"),
        QStringLiteral("Open a command in the Kai window as if the user had clicked it: the parameter form, interactive terminal or KIP screen appears there "
                       "and the user continues. Use it for commands run_command cannot run. It does not wait and you do not see the output "
                       "(read it later with get_command_output)."),
        objectSchema(idOrName()),
        ToolRisk::Write,
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return invalid(error);
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.run_in_app")).arg(c->name);
            p.detail = commandBody(*c);
            return p;
        },
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return fail(error);
            if (!m_host->openCommandInApp(c->id, error)) return fail(error);
            return okText(QStringLiteral("Opened in the Kai window. The user continues there; the output is not visible to you yet."));
        });

    add(QStringLiteral("kill_process"), QStringLiteral("Stop a running command (graceful stop, or force=true to kill immediately)."),
        objectSchema(merged(idOrName(), {{QStringLiteral("force"), prop(QStringLiteral("boolean"), QStringLiteral("Kill immediately. Default false."))}})),
        ToolRisk::Danger,
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return invalid(error);
            if (!m_host->isRunning(c->id)) return invalid(QStringLiteral("That command is not running."));
            ToolPlan p;
            p.title = (a.value(QStringLiteral("force")).toBool() ? utils::tr(QStringLiteral("ai.plan.force_kill")) : utils::tr(QStringLiteral("ai.plan.stop"))).arg(c->name);
            return p;
        },
        [this](const QJsonObject &a) {
            QString error;
            const Command *c = findCommand(a, error);
            if (!c) return fail(error);
            if (!m_host->killCommand(c->id, a.value(QStringLiteral("force")).toBool(false), error)) return fail(error);
            return okText(QStringLiteral("Stop requested."));
        });

    // Configuração (settings, temas, layout, perfis de execução): agent-toolbox-settings.cpp.
    for (ConfigToolDef &def : buildConfigTools(m_host)) {
        add(def.name, def.description, def.schema, def.risk, std::move(def.plan), std::move(def.run));
    }
}

} // namespace kai::engine
