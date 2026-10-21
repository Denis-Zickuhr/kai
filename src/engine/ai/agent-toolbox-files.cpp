// Ferramentas de arquivo do agente: ler, listar e ESCREVER dentro das pastas-projeto do Kai. A raiz é o diretório
// de cada pasta marcada como projeto; links simbólicos e `..` são resolvidos antes de qualquer acesso e a E/S roda
// numa thread do pool (a GUI nunca espera o disco). Escrever passa pela aprovação como toda ferramenta Write.
#include "engine/ai/agent-file-sandbox.h"
#include "engine/ai/agent-toolbox-internal.h"
#include "engine/ai/agent-toolbox.h"

#include "utils/translation-manager.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QThreadPool>

namespace kai::engine {

using namespace toolutil;
namespace fs = filesandbox;

namespace {

QVector<fs::ProjectRoot> projectRoots(AgentToolHost *host)
{
    QVector<fs::ProjectRoot> roots;
    for (const core::Folder &f : host->commandsData().folders) {
        if (f.isProject && !f.workingDir.trimmed().isEmpty()) roots.append({f.id, f.name, f.workingDir});
    }
    return roots;
}

QJsonObject projectIdProp()
{
    return {{QStringLiteral("project_id"),
             prop(QStringLiteral("string"), QStringLiteral("Project folder id (is_project in list_folders). Optional with one project or an absolute path."))}};
}

QJsonObject merge(QJsonObject a, const QJsonObject &b)
{
    for (auto it = b.constBegin(); it != b.constEnd(); ++it) a.insert(it.key(), it.value());
    return a;
}

} // namespace

void AgentToolbox::buildFileTools()
{
    auto add = [this](const QString &name, const QString &description, const QJsonObject &schema, ToolRisk risk,
                      std::function<ToolPlan(const QJsonObject &)> planFn,
                      std::function<void(const QJsonObject &, std::function<void(const ToolOutcome &)>)> asyncFn) {
        Tool t;
        t.name = name;
        t.description = description;
        t.schema = schema;
        t.risk = risk;
        t.planFn = std::move(planFn);
        t.asyncFn = std::move(asyncFn);
        m_tools.append(std::move(t));
    };

    // Roda `work` no pool e entrega o resultado na thread do toolbox (se ele ainda existir).
    auto inBackground = [this](std::function<ToolOutcome()> work, std::function<void(const ToolOutcome &)> done) {
        QPointer<AgentToolbox> self(this);
        QThreadPool::globalInstance()->start([self, work = std::move(work), done = std::move(done)]() mutable {
            const ToolOutcome outcome = work();
            if (!self) return;
            QMetaObject::invokeMethod(self.data(), [done = std::move(done), outcome]() { done(outcome); }, Qt::QueuedConnection);
        });
    };

    const QString pathDescription = QStringLiteral("Path relative to the project folder, or absolute inside one.");

    add(QStringLiteral("list_project_files"),
        QStringLiteral("List the entries of a folder inside a Kai project folder (folders end with '/'). Empty path = the project root."),
        objectSchema(merge(projectIdProp(), {{QStringLiteral("path"), prop(QStringLiteral("string"), pathDescription)}})),
        ToolRisk::Read,
        [this](const QJsonObject &a) {
            const QString path = strArg(a, QStringLiteral("path"));
            const auto check = fs::checkLexical(projectRoots(m_host), strArg(a, QStringLiteral("project_id")), path.isEmpty() ? QStringLiteral(".") : path);
            if (!check.ok) return invalid(check.error);
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.list_files")).arg(check.relative);
            return p;
        },
        [this, inBackground](const QJsonObject &a, std::function<void(const ToolOutcome &)> done) {
            const auto roots = projectRoots(m_host);
            const QString project = strArg(a, QStringLiteral("project_id"));
            const QString path = strArg(a, QStringLiteral("path"));
            inBackground(
                [roots, project, path]() {
                    const auto r = fs::listDirectory(roots, project, path, 300);
                    if (!r.ok) return fail(r.error);
                    QJsonObject o{{QStringLiteral("entries"), QJsonArray::fromStringList(r.entries)}};
                    if (r.truncated) o[QStringLiteral("truncated")] = true;
                    return okValue(o);
                },
                std::move(done));
        });

    add(QStringLiteral("read_project_file"),
        QStringLiteral("Read a text file inside a Kai project folder (up to ~200 KB). Links that leave the project are refused."),
        objectSchema(merge(projectIdProp(), {{QStringLiteral("path"), prop(QStringLiteral("string"), pathDescription)}}), {QStringLiteral("path")}),
        ToolRisk::Read,
        [this](const QJsonObject &a) {
            const auto check = fs::checkLexical(projectRoots(m_host), strArg(a, QStringLiteral("project_id")), strArg(a, QStringLiteral("path")));
            if (!check.ok) return invalid(check.error);
            if (m_hideSecrets && QFileInfo(check.path).fileName().startsWith(QStringLiteral(".env"))) {
                return invalid(QStringLiteral("Secret-bearing files (.env) are hidden by the provider profile."));
            }
            ToolPlan p;
            p.title = utils::tr(QStringLiteral("ai.plan.read_file")).arg(check.relative);
            return p;
        },
        [this, inBackground](const QJsonObject &a, std::function<void(const ToolOutcome &)> done) {
            const auto roots = projectRoots(m_host);
            const QString project = strArg(a, QStringLiteral("project_id"));
            const QString path = strArg(a, QStringLiteral("path"));
            inBackground(
                [roots, project, path]() {
                    const auto r = fs::readFile(roots, project, path, fs::kMaxReadBytes);
                    if (!r.ok) return fail(r.error);
                    QJsonObject o{{QStringLiteral("path"), r.relative}, {QStringLiteral("size"), static_cast<double>(r.size)}, {QStringLiteral("content"), r.text}};
                    if (r.truncated) o[QStringLiteral("truncated")] = true;
                    return okValue(o);
                },
                std::move(done));
        });

    add(QStringLiteral("write_project_file"),
        QStringLiteral("Create or overwrite a text file inside a Kai project folder (missing folders are created; max 1 MB). "
                       "append=true adds to the end instead. The user approves each write. Not undoable by Kai: say what you changed."),
        objectSchema(merge(projectIdProp(), {{QStringLiteral("path"), prop(QStringLiteral("string"), pathDescription)},
                                              {QStringLiteral("content"), prop(QStringLiteral("string"), QStringLiteral("Full new content of the file."))},
                                              {QStringLiteral("append"), prop(QStringLiteral("boolean"), QStringLiteral("Append instead of overwrite. Default false."))}}),
                     {QStringLiteral("path"), QStringLiteral("content")}),
        ToolRisk::Write,
        [this](const QJsonObject &a) {
            if (!a.value(QStringLiteral("content")).isString()) return invalid(QStringLiteral("Give the 'content' as text."));
            const QString content = a.value(QStringLiteral("content")).toString();
            if (content.toUtf8().size() > fs::kMaxWriteBytes) return invalid(QStringLiteral("Content is too large (limit 1 MB)."));
            const auto check = fs::checkLexical(projectRoots(m_host), strArg(a, QStringLiteral("project_id")), strArg(a, QStringLiteral("path")));
            if (!check.ok) return invalid(check.error);
            ToolPlan p;
            const bool append = a.value(QStringLiteral("append")).toBool(false);
            p.title = (append ? utils::tr(QStringLiteral("ai.plan.append_file")) : utils::tr(QStringLiteral("ai.plan.write_file"))).arg(check.relative);
            p.detail = utils::tr(QStringLiteral("ai.plan.file_size")).arg(content.toUtf8().size()) + QStringLiteral("\n\n") + clip(content, 2000);
            return p;
        },
        [this, inBackground](const QJsonObject &a, std::function<void(const ToolOutcome &)> done) {
            const auto roots = projectRoots(m_host);
            const QString project = strArg(a, QStringLiteral("project_id"));
            const QString path = strArg(a, QStringLiteral("path"));
            const QString content = a.value(QStringLiteral("content")).toString();
            const bool append = a.value(QStringLiteral("append")).toBool(false);
            inBackground(
                [roots, project, path, content, append]() {
                    const auto r = fs::writeFile(roots, project, path, content, append);
                    if (!r.ok) return fail(r.error);
                    return okValue(QJsonObject{{QStringLiteral("path"), r.relative},
                                               {QStringLiteral("created"), r.created},
                                               {QStringLiteral("bytes_written"), static_cast<double>(r.bytes)}});
                },
                std::move(done));
        });
}

} // namespace kai::engine
