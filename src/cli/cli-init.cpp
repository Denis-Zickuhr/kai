#include "cli/cli-init.h"

#include "core/cli-reserved-verbs.h"
#include "core/models.h"
#include "ui/features/collections/project-detection-strategy.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace kai::cli {

namespace {

constexpr int kMaxSlugLength = 32;

// Escalar YAML entre aspas duplas — o escape do JSON é válido em YAML.
QString yamlQuoted(const QString &value)
{
    const QByteArray arr = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(arr.mid(1, arr.size() - 2));
}

struct InitCommand {
    QString name;
    QString cliPath;
    QString command;
    bool background = false;
};

struct InitGroup {
    QString name;
    QString cliPath;
    QVector<InitCommand> commands;
};

// Único entre os irmãos e nunca um verbo reservado do kai na raiz.
QString uniqueSlug(const QString &base, QSet<QString> &taken, bool atRoot)
{
    QString slug = base.isEmpty() ? QStringLiteral("cmd") : base;
    if (atRoot && core::reservedCliVerbs().contains(slug)) {
        slug += QStringLiteral("-cmd");
    }
    QString candidate = slug;
    for (int n = 2; taken.contains(candidate); ++n) {
        candidate = QStringLiteral("%1-%2").arg(slug).arg(n);
    }
    taken.insert(candidate);
    return candidate;
}

QVector<InitGroup> detectGroups(const QDir &dir)
{
    QVector<InitGroup> groups;
    QSet<QString> rootSlugs;
    for (const auto &strategy : ui::allDetectionStrategies()) {
        if (!strategy->appliesTo(dir)) {
            continue;
        }
        const QVector<ui::DetectedCommand> detected = strategy->detect(dir);
        if (detected.isEmpty()) {
            continue;
        }
        InitGroup group;
        group.name = strategy->name();
        group.cliPath = uniqueSlug(cliSlug(group.name), rootSlugs, /*atRoot=*/true);
        QSet<QString> commandSlugs;
        for (const ui::DetectedCommand &d : detected) {
            group.commands << InitCommand{d.command.name,
                                          uniqueSlug(cliSlug(d.command.name), commandSlugs, false),
                                          d.command.command, d.command.isBackground};
        }
        groups << group;
    }
    return groups;
}

QString yamlManifest(const QString &projectName, const QVector<InitGroup> &groups)
{
    QString out = QStringLiteral("project_name: %1\n").arg(yamlQuoted(projectName));
    if (groups.isEmpty()) {
        out += QStringLiteral(
            "\ncommands:\n"
            "  - name: Hello\n"
            "    cli_path: hello\n"
            "    description: Example command - edit or replace it.\n"
            "    type: command\n"
            "    command: echo \"Hello from Kai\"\n");
        return out;
    }
    out += QStringLiteral("\nfolders:\n");
    for (const InitGroup &g : groups) {
        out += QStringLiteral("  - path: %1\n    cli_path: %2\n    cli_description: %3\n")
                   .arg(yamlQuoted(g.name), g.cliPath, yamlQuoted(g.name));
    }
    out += QStringLiteral("\ncommands:\n");
    for (const InitGroup &g : groups) {
        out += QStringLiteral("  # --- %1 ---\n").arg(g.name);
        for (const InitCommand &c : g.commands) {
            out += QStringLiteral("  - name: %1\n    folder: %2\n    cli_path: %3\n    type: command\n    command: %4\n")
                       .arg(yamlQuoted(c.name), yamlQuoted(g.name), c.cliPath, yamlQuoted(c.command));
            if (c.background) {
                out += QStringLiteral("    is_background: true\n");
            }
        }
    }
    return out;
}

QString jsonManifest(const QString &projectName, const QVector<InitGroup> &groups)
{
    QJsonObject root;
    root[QStringLiteral("project_name")] = projectName;
    QJsonArray folders;
    QJsonArray commands;
    for (const InitGroup &g : groups) {
        folders.append(QJsonObject{{QStringLiteral("path"), g.name},
                                   {QStringLiteral("cli_path"), g.cliPath},
                                   {QStringLiteral("cli_description"), g.name}});
        for (const InitCommand &c : g.commands) {
            QJsonObject cmd{{QStringLiteral("name"), c.name},
                            {QStringLiteral("folder"), g.name},
                            {QStringLiteral("cli_path"), c.cliPath},
                            {QStringLiteral("type"), QStringLiteral("command")},
                            {QStringLiteral("command"), c.command}};
            if (c.background) {
                cmd[QStringLiteral("is_background")] = true;
            }
            commands.append(cmd);
        }
    }
    if (groups.isEmpty()) {
        commands.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("Hello")},
                                    {QStringLiteral("cli_path"), QStringLiteral("hello")},
                                    {QStringLiteral("description"), QStringLiteral("Example command - edit or replace it.")},
                                    {QStringLiteral("type"), QStringLiteral("command")},
                                    {QStringLiteral("command"), QStringLiteral("echo \"Hello from Kai\"")}});
    } else {
        root[QStringLiteral("folders")] = folders;
    }
    root[QStringLiteral("commands")] = commands;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace

QString cliSlug(const QString &text)
{
    // Sem acentos: decompõe (NFD) e descarta as marcas combinantes.
    QString ascii;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing) {
            ascii += c;
        }
    }
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    QString slug = ascii.toLower().replace(nonAlnum, QStringLiteral("-"));
    slug = slug.mid(0, kMaxSlugLength);
    while (slug.startsWith(QLatin1Char('-'))) {
        slug.remove(0, 1);
    }
    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }
    return slug;
}

InitManifest buildInitManifest(const QString &directory, bool asJson)
{
    const QDir dir(directory);
    const QVector<InitGroup> groups = detectGroups(dir);
    const QString projectName = QFileInfo(dir.absolutePath()).fileName();
    InitManifest manifest;
    manifest.text = asJson ? jsonManifest(projectName, groups) : yamlManifest(projectName, groups);
    for (const InitGroup &g : groups) {
        manifest.ecosystems << g.name;
        manifest.commandCount += g.commands.size();
    }
    if (groups.isEmpty()) {
        manifest.commandCount = 1;
    }
    return manifest;
}

int runInitVerb(const QStringList &args)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    const bool asJson = args.contains(QStringLiteral("--json"));
    const bool force = args.contains(QStringLiteral("--force"));
    const QDir dir(QDir::currentPath());

    if (!force) {
        for (const char *name : {"kai.json", "kai.yml", "kai.yaml"}) {
            if (dir.exists(QString::fromLatin1(name))) {
                err << utils::tr(QStringLiteral("cli.init.exists")).arg(QString::fromLatin1(name)) << "\n";
                return 1;
            }
        }
    }

    const InitManifest manifest = buildInitManifest(dir.absolutePath(), asJson);
    const QString fileName = asJson ? QStringLiteral("kai.json") : QStringLiteral("kai.yml");
    QFile file(dir.filePath(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        err << utils::tr(QStringLiteral("cli.init.write_failed")).arg(fileName, file.errorString()) << "\n";
        return 1;
    }
    file.write(manifest.text.toUtf8());
    file.close();

    if (manifest.ecosystems.isEmpty()) {
        out << utils::tr(QStringLiteral("cli.init.created_example")).arg(fileName) << "\n";
    } else {
        out << utils::tr(QStringLiteral("cli.init.created"))
                   .arg(fileName).arg(manifest.commandCount).arg(manifest.ecosystems.join(QStringLiteral(", ")))
            << "\n";
    }
    return 0;
}

} // namespace kai::cli
