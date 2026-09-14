#include "cli/cli-help-format.h"

#include "core/cli-param-binder.h"
#include "utils/translation-manager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace kai::cli {

namespace {

constexpr int kMaxInlineChoices = 6;

QString spaces(int n)
{
    return QString(qMax(0, n), QLatin1Char(' '));
}

QString padded(const QString &text, int width)
{
    return text + spaces(width - text.size());
}

// Texto em várias linhas, cada uma com o mesmo recuo.
QString indentedLines(const QString &text, int indent)
{
    QString out;
    for (const QString &line : text.trimmed().split(QLatin1Char('\n'))) {
        out += spaces(indent) + line.trimmed() + QLatin1Char('\n');
    }
    return out;
}

// O que vai depois de "--nome=" (opcional) na linha de uso.
QString valueHint(const core::Parameter &p)
{
    if (p.type == core::ParameterType::Bool) {
        return QStringLiteral("true|false");
    }
    // Sem o valor vazio: numa opção "Perguntar:" (valor vazio) ele é o
    // "não informado", não uma escolha a digitar.
    QStringList choices = core::selectOptionValues(p);
    choices.removeAll(QString());
    if (p.type == core::ParameterType::Select && !choices.isEmpty()) {
        if (choices.size() > kMaxInlineChoices) {
            return choices.mid(0, kMaxInlineChoices).join(QLatin1Char('|')) + QStringLiteral("|…");
        }
        return choices.join(QLatin1Char('|'));
    }
    if (p.type == core::ParameterType::Number) {
        return QStringLiteral("<%1>").arg(utils::tr(QStringLiteral("cli_local.help.number")));
    }
    return QStringLiteral("<%1>").arg(utils::tr(QStringLiteral("cli_local.help.value")));
}

QString paramDisplayName(const core::Parameter &p)
{
    return p.optional ? QStringLiteral("--%1").arg(p.name) : QStringLiteral("<%1>").arg(p.name);
}

QString paramDescription(const core::Parameter &p)
{
    if (!p.description.trimmed().isEmpty()) {
        return p.description;
    }
    if (!p.label.trimmed().isEmpty() && p.label != p.name) {
        return p.label;
    }
    return QString();
}

// Um bloco por parâmetro: "<nome>  obrigatório · opções: a, b · padrão: x"
// e, na linha de baixo, a descrição.
QString formatParamDetails(const core::Command &command, int indent)
{
    int width = 0;
    for (const core::Parameter &p : command.params) {
        width = qMax(width, static_cast<int>(paramDisplayName(p).size()));
    }
    QString out;
    for (const core::Parameter &p : command.params) {
        QStringList tags;
        tags << (p.optional ? utils::tr(QStringLiteral("cli_local.help.optional"))
                            : utils::tr(QStringLiteral("cli_local.help.required")));
        // Sem o valor vazio: numa opção "Perguntar:" (valor vazio) ele é o
    // "não informado", não uma escolha a digitar.
    QStringList choices = core::selectOptionValues(p);
    choices.removeAll(QString());
        if (p.type == core::ParameterType::Select && !choices.isEmpty()) {
            tags << utils::tr(QStringLiteral("cli_local.help.choices")).arg(choices.join(QStringLiteral(", ")));
        } else if (p.type == core::ParameterType::Bool) {
            tags << QStringLiteral("true|false");
        } else if (p.type != core::ParameterType::Text) {
            tags << core::parameterTypeToString(p.type);
        }
        if (!p.defaultValue.isEmpty()) {
            tags << utils::tr(QStringLiteral("cli_local.help.default")).arg(p.defaultValue);
        }
        out += spaces(indent) + padded(paramDisplayName(p), width) + QStringLiteral("  ")
            + tags.join(QStringLiteral(" · ")) + QLatin1Char('\n');
        const QString description = paramDescription(p);
        if (!description.isEmpty()) {
            out += indentedLines(description, indent + width + 2);
        }
    }
    return out;
}

const core::Command *findCommand(const QVector<core::Command> &commands, const QString &id)
{
    for (const core::Command &c : commands) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

} // namespace

QString formatCommandUsage(const core::Command &command, const QStringList &pathTokens)
{
    QString usage = QStringLiteral("kai");
    if (!pathTokens.isEmpty()) {
        usage += QLatin1Char(' ') + pathTokens.join(QLatin1Char(' '));
    }
    for (const core::Parameter &p : command.params) {
        if (!p.optional) {
            usage += QStringLiteral(" <%1>").arg(p.name);
        }
    }
    for (const core::Parameter &p : command.params) {
        if (p.optional) {
            usage += QStringLiteral(" [--%1=%2]").arg(p.name, valueHint(p));
        }
    }
    return usage;
}

QString formatCliListing(const QVector<core::CliPathChildEntry> &children,
                         const QVector<core::Command> &commands,
                         const QStringList &pathTokens)
{
    QVector<core::CliPathChildEntry> folders;
    QVector<core::CliPathChildEntry> commandEntries;
    int width = 8;
    for (const core::CliPathChildEntry &c : children) {
        (c.isFolder ? folders : commandEntries) << c;
        width = qMax(width, static_cast<int>(c.cliPath.size()));
    }

    // Sem cabeçalhos "Pastas:"/"Comandos:" (pedido do usuário: feios) —
    // pastas primeiro, uma linha cada; os comandos, com o detalhe, depois de
    // uma linha em branco.
    QString out;
    if (!folders.isEmpty()) {
        for (const core::CliPathChildEntry &f : folders) {
            out += QStringLiteral("  ") + padded(f.cliPath, width) + QStringLiteral("  ") + f.label;
            if (!f.description.trimmed().isEmpty()) {
                out += QStringLiteral(" — ") + f.description.trimmed();
            }
            out += QLatin1Char('\n');
        }
    }

    if (!commandEntries.isEmpty()) {
        if (!folders.isEmpty()) {
            out += QLatin1Char('\n');
        }
        const int detailIndent = 2 + width + 2;
        for (int i = 0; i < commandEntries.size(); ++i) {
            const core::CliPathChildEntry &entry = commandEntries.at(i);
            out += QStringLiteral("  ") + padded(entry.cliPath, width) + QStringLiteral("  ") + entry.label
                + QLatin1Char('\n');
            const core::Command *command = findCommand(commands, entry.targetId);
            if (!command) {
                continue;
            }
            if (!command->description.trimmed().isEmpty()) {
                out += indentedLines(command->description, detailIndent);
            }
            out += spaces(detailIndent) + formatCommandUsage(*command, QStringList(pathTokens) << entry.cliPath)
                + QLatin1Char('\n');
            out += formatParamDetails(*command, detailIndent + 2);
            if (i + 1 < commandEntries.size()) {
                out += QLatin1Char('\n');
            }
        }
    }
    return out;
}

QString formatCommandHelp(const core::Command &command, const QStringList &pathTokens)
{
    QString out = utils::tr(QStringLiteral("cli_local.help.usage")) + QLatin1Char(' ')
        + formatCommandUsage(command, pathTokens) + QStringLiteral("\n");
    if (!command.description.trimmed().isEmpty()) {
        out += QLatin1Char('\n') + indentedLines(command.description, 0);
    }
    if (!command.params.isEmpty()) {
        out += QLatin1Char('\n') + formatParamDetails(command, 2);
    }
    return out;
}

namespace {

QJsonObject paramToJson(const core::Parameter &p)
{
    QJsonObject o;
    o[QStringLiteral("name")] = p.name;
    o[QStringLiteral("required")] = !p.optional;
    o[QStringLiteral("type")] = core::parameterTypeToString(p.type);
    QStringList choices = core::selectOptionValues(p);
    choices.removeAll(QString());
    if (!choices.isEmpty()) {
        o[QStringLiteral("choices")] = QJsonArray::fromStringList(choices);
    }
    if (!p.defaultValue.isEmpty()) {
        o[QStringLiteral("default")] = p.defaultValue;
    }
    const QString description = paramDescription(p);
    if (!description.isEmpty()) {
        o[QStringLiteral("description")] = description;
    }
    return o;
}

} // namespace

QString formatCliListingJson(const QVector<core::CliPathChildEntry> &children,
                             const QVector<core::Command> &commands,
                             const QStringList &pathTokens)
{
    QJsonArray items;
    for (const core::CliPathChildEntry &c : children) {
        QJsonObject item;
        item[QStringLiteral("cli_path")] = c.cliPath;
        item[QStringLiteral("path")] = QJsonArray::fromStringList(QStringList(pathTokens) << c.cliPath);
        item[QStringLiteral("kind")] = c.isFolder ? QStringLiteral("folder") : QStringLiteral("command");
        item[QStringLiteral("name")] = c.label;
        if (!c.description.isEmpty()) {
            item[QStringLiteral("description")] = c.description;
        }
        if (!c.isFolder) {
            if (const core::Command *command = findCommand(commands, c.targetId)) {
                item[QStringLiteral("usage")] = formatCommandUsage(*command, QStringList(pathTokens) << c.cliPath);
                QJsonArray params;
                for (const core::Parameter &p : command->params) {
                    params.append(paramToJson(p));
                }
                item[QStringLiteral("params")] = params;
            }
        }
        items.append(item);
    }
    return QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Indented));
}

QString formatDryRun(const DryRunPreview &preview)
{
    auto label = [](const char *key) { return utils::tr(QString::fromLatin1(key)); };
    QString out = label("cli.dry_run.header") + QStringLiteral("\n\n");
    out += label("cli.dry_run.command") + QLatin1Char(' ') + preview.name
        + QStringLiteral("  (kai %1)\n").arg(preview.pathTokens.join(QLatin1Char(' ')));
    out += label("cli.dry_run.target") + QLatin1Char(' ')
        + (preview.target.isEmpty() ? label("cli.dry_run.local") : preview.target) + QLatin1Char('\n');
    if (preview.type != QStringLiteral("http") && !preview.interpreter.isEmpty()) {
        out += label("cli.dry_run.language") + QLatin1Char(' ') + preview.language
            + QStringLiteral(" (%1)\n").arg(preview.interpreter);
    }
    if (preview.type != QStringLiteral("http")) {
        out += label("cli.dry_run.working_dir") + QLatin1Char(' ')
            + (preview.workingDir.isEmpty() ? label("cli.dry_run.default_dir") : preview.workingDir) + QLatin1Char('\n');
    }
    if (!preview.preHooks.isEmpty()) {
        out += label("cli.dry_run.pre_hooks") + QLatin1Char(' ') + preview.preHooks.join(QStringLiteral(", ")) + QLatin1Char('\n');
    }
    if (!preview.postHooks.isEmpty()) {
        out += label("cli.dry_run.post_hooks") + QLatin1Char(' ') + preview.postHooks.join(QStringLiteral(", ")) + QLatin1Char('\n');
    }
    out += QLatin1Char('\n') + label("cli.dry_run.would_run") + QLatin1Char('\n');
    if (preview.type == QStringLiteral("http")) {
        out += QStringLiteral("  %1 %2\n").arg(preview.httpMethod, preview.httpUrl);
        if (!preview.httpBody.trimmed().isEmpty()) {
            out += indentedLines(preview.httpBody, 2);
        }
    } else {
        out += indentedLines(preview.command, 2);
    }
    return out;
}

QString formatDryRunJson(const DryRunPreview &preview)
{
    QJsonObject o;
    o[QStringLiteral("name")] = preview.name;
    o[QStringLiteral("path")] = QJsonArray::fromStringList(preview.pathTokens);
    o[QStringLiteral("type")] = preview.type;
    o[QStringLiteral("target")] = preview.target;
    if (preview.type == QStringLiteral("http")) {
        o[QStringLiteral("method")] = preview.httpMethod;
        o[QStringLiteral("url")] = preview.httpUrl;
        o[QStringLiteral("body")] = preview.httpBody;
    } else {
        o[QStringLiteral("command")] = preview.command;
        if (!preview.interpreter.isEmpty()) {
            o[QStringLiteral("language")] = preview.language;
            o[QStringLiteral("interpreter")] = preview.interpreter;
        }
        o[QStringLiteral("working_dir")] = preview.workingDir;
    }
    o[QStringLiteral("pre_hooks")] = QJsonArray::fromStringList(preview.preHooks);
    o[QStringLiteral("post_hooks")] = QJsonArray::fromStringList(preview.postHooks);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

} // namespace kai::cli
