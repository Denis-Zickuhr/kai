#include "core/pipeline-model.h"

#include <QHash>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <functional>

namespace kai::core {

namespace {

const QSet<QString> &captureSources()
{
    static const QSet<QString> sources = {QStringLiteral("stdout"), QStringLiteral("json"), QStringLiteral("exit"),
                                          QStringLiteral("status")};
    return sources;
}

bool validId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9_]+$"));
    return re.match(id).hasMatch();
}

bool validOnFail(const QString &onFail)
{
    return onFail == QLatin1String("abort") || onFail == QLatin1String("continue") || onFail == QLatin1String("ask")
        || (onFail.startsWith(QLatin1String("goto:")) && onFail.size() > 5);
}

QString paramString(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool: return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: return QString::number(v.toDouble(), 'g', 15);
    case QJsonValue::String: return v.toString();
    default: return QString();
    }
}

} // namespace

QJsonObject PipelineCapture::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("from")] = from;
    if (!regex.isEmpty()) obj[QStringLiteral("regex")] = regex;
    if (!path.isEmpty()) obj[QStringLiteral("path")] = path;
    if (secret) obj[QStringLiteral("secret")] = true;
    return obj;
}

PipelineCapture PipelineCapture::fromJson(const QString &name, const QJsonObject &obj)
{
    PipelineCapture c;
    c.name = name;
    c.from = obj.value(QStringLiteral("from")).toString(QStringLiteral("stdout"));
    c.regex = obj.value(QStringLiteral("regex")).toString();
    c.path = obj.value(QStringLiteral("path")).toString();
    c.secret = obj.value(QStringLiteral("secret")).toBool(false);
    return c;
}

QString PipelineStep::gotoTarget() const
{
    return onFail.startsWith(QLatin1String("goto:")) ? onFail.mid(5) : QString();
}

QJsonObject PipelineStep::toJson() const
{
    // Omite o que está no padrão, como Command::toJson: o arquivo fica enxuto para quem versiona à mão.
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    if (!label.isEmpty()) obj[QStringLiteral("label")] = label;
    obj[QStringLiteral("run")] = run;
    if (!check.isEmpty()) obj[QStringLiteral("check")] = check;
    if (!confirm.isEmpty()) obj[QStringLiteral("confirm")] = confirm;
    if (!ask.isEmpty()) obj[QStringLiteral("ask")] = ask;
    if (!params.isEmpty()) {
        QJsonObject p;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            p[it.key()] = it.value();
        }
        obj[QStringLiteral("params")] = p;
    }
    if (!when.isEmpty()) {
        QJsonArray arr;
        for (const ExecutionCondition &c : when) {
            arr.append(c.toJson());
        }
        obj[QStringLiteral("when")] = arr;
        if (whenCombinator != QLatin1String("and")) obj[QStringLiteral("when_combinator")] = whenCombinator;
    }
    if (onFail != QLatin1String("abort")) obj[QStringLiteral("on_fail")] = onFail;
    if (!retry.isDefault()) {
        QJsonObject r;
        r[QStringLiteral("times")] = retry.times;
        if (retry.delaySec > 0) r[QStringLiteral("delay")] = retry.delaySec;
        if (retry.reask) r[QStringLiteral("reask")] = true;
        obj[QStringLiteral("retry")] = r;
    }
    if (waitUntil.isSet()) {
        QJsonObject w;
        w[QStringLiteral("check")] = waitUntil.check;
        w[QStringLiteral("timeout")] = waitUntil.timeoutSec;
        w[QStringLiteral("interval")] = waitUntil.intervalSec;
        obj[QStringLiteral("wait_until")] = w;
    }
    if (!capture.isEmpty()) {
        QJsonObject c;
        for (const PipelineCapture &cap : capture) {
            c[cap.name] = cap.toJson();
        }
        obj[QStringLiteral("capture")] = c;
    }
    return obj;
}

PipelineStep PipelineStep::fromJson(const QJsonObject &obj)
{
    PipelineStep s;
    s.id = obj.value(QStringLiteral("id")).toString();
    s.label = obj.value(QStringLiteral("label")).toString();
    s.run = obj.value(QStringLiteral("run")).toString();
    s.check = obj.value(QStringLiteral("check")).toString();
    s.confirm = obj.value(QStringLiteral("confirm")).toString();
    s.ask = obj.value(QStringLiteral("ask")).toArray();
    const QJsonObject params = obj.value(QStringLiteral("params")).toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        s.params.insert(it.key(), paramString(it.value()));
    }
    for (const QJsonValue &v : obj.value(QStringLiteral("when")).toArray()) {
        s.when << ExecutionCondition::fromJson(v.toObject());
    }
    s.whenCombinator = obj.value(QStringLiteral("when_combinator")).toString(QStringLiteral("and"));
    s.onFail = obj.value(QStringLiteral("on_fail")).toString(QStringLiteral("abort"));
    const QJsonObject retry = obj.value(QStringLiteral("retry")).toObject();
    s.retry.times = qMax(0, retry.value(QStringLiteral("times")).toInt(0));
    s.retry.delaySec = qMax(0, retry.value(QStringLiteral("delay")).toInt(0));
    s.retry.reask = retry.value(QStringLiteral("reask")).toBool(false);
    const QJsonObject wait = obj.value(QStringLiteral("wait_until")).toObject();
    s.waitUntil.check = wait.value(QStringLiteral("check")).toString();
    s.waitUntil.timeoutSec = qMax(1, wait.value(QStringLiteral("timeout")).toInt(120));
    s.waitUntil.intervalSec = qMax(0, wait.value(QStringLiteral("interval")).toInt(3));
    const QJsonObject capture = obj.value(QStringLiteral("capture")).toObject();
    for (auto it = capture.constBegin(); it != capture.constEnd(); ++it) {
        s.capture << PipelineCapture::fromJson(it.key(), it.value().toObject());
    }
    return s;
}

QJsonArray pipelineStepsToJson(const QVector<PipelineStep> &steps)
{
    QJsonArray arr;
    for (const PipelineStep &s : steps) {
        arr.append(s.toJson());
    }
    return arr;
}

QVector<PipelineStep> pipelineStepsFromJson(const QJsonArray &array)
{
    QVector<PipelineStep> steps;
    for (const QJsonValue &v : array) {
        if (v.isObject()) {
            steps << PipelineStep::fromJson(v.toObject());
        }
    }
    return steps;
}

QString pipelineParamsToText(const QMap<QString, QString> &params)
{
    QStringList lines;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        lines << it.key() + QLatin1Char('=') + it.value();
    }
    return lines.join(QLatin1Char('\n'));
}

QMap<QString, QString> pipelineParamsFromText(const QString &text)
{
    QMap<QString, QString> params;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const int eq = line.indexOf(QLatin1Char('='));
        const QString key = (eq > 0 ? line.left(eq) : QString()).trimmed();
        if (!key.isEmpty()) {
            params.insert(key, line.mid(eq + 1).trimmed());
        }
    }
    return params;
}

QString pipelineWhenToText(const QVector<ExecutionCondition> &when)
{
    QStringList lines;
    for (const ExecutionCondition &c : when) {
        lines << QStringLiteral("%1 | %2 | %3").arg(c.left, c.op, c.right);
    }
    return lines.join(QLatin1Char('\n'));
}

QVector<ExecutionCondition> pipelineWhenFromText(const QString &text)
{
    QVector<ExecutionCondition> when;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList parts = line.split(QLatin1Char('|'));
        ExecutionCondition c;
        c.left = parts.value(0).trimmed();
        c.op = parts.value(1).trimmed().isEmpty() ? QStringLiteral("exists") : parts.value(1).trimmed();
        c.right = parts.mid(2).join(QLatin1Char('|')).trimmed();
        if (!c.left.isEmpty()) {
            when << c;
        }
    }
    return when;
}

QString pipelineCaptureToText(const QVector<PipelineCapture> &capture)
{
    QStringList lines;
    for (const PipelineCapture &c : capture) {
        QString source = c.from;
        if (c.from == QLatin1String("json")) {
            source += QLatin1Char(':') + c.path;
        } else if (c.from == QLatin1String("stdout") && !c.regex.isEmpty()) {
            source += QLatin1Char(':') + c.regex;
        }
        lines << c.name + QStringLiteral(": ") + source + (c.secret ? QStringLiteral(" | secret") : QString());
    }
    return lines.join(QLatin1Char('\n'));
}

QVector<PipelineCapture> pipelineCaptureFromText(const QString &text)
{
    QVector<PipelineCapture> capture;
    for (QString line : text.split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }
        PipelineCapture c;
        c.name = line.left(colon).trimmed();
        QString rest = line.mid(colon + 1).trimmed();
        if (rest.endsWith(QStringLiteral("| secret"))) {
            c.secret = true;
            rest = rest.left(rest.size() - 8).trimmed();
        }
        const int sep = rest.indexOf(QLatin1Char(':'));
        c.from = (sep >= 0 ? rest.left(sep) : rest).trimmed();
        const QString arg = sep >= 0 ? rest.mid(sep + 1) : QString();
        if (c.from == QLatin1String("json")) {
            c.path = arg.trimmed();
        } else if (c.from == QLatin1String("stdout")) {
            c.regex = arg;
        }
        if (!c.name.isEmpty()) {
            capture << c;
        }
    }
    return capture;
}

QVector<PipelineIssue> validatePipelineSteps(const QVector<PipelineStep> &steps)
{
    QVector<PipelineIssue> issues;
    QHash<QString, int> indexById;
    for (int i = 0; i < steps.size(); ++i) {
        const PipelineStep &s = steps.at(i);
        if (!validId(s.id)) {
            issues.append({i, QStringLiteral("invalid_id"), s.id});
        } else if (indexById.contains(s.id)) {
            issues.append({i, QStringLiteral("duplicate_id"), s.id});
        } else {
            indexById.insert(s.id, i);
        }
    }
    for (int i = 0; i < steps.size(); ++i) {
        const PipelineStep &s = steps.at(i);
        if (s.run.trimmed().isEmpty()) {
            issues.append({i, QStringLiteral("missing_run"), s.id});
        }
        if (!validOnFail(s.onFail)) {
            issues.append({i, QStringLiteral("bad_on_fail"), s.onFail});
        } else if (!s.gotoTarget().isEmpty()) {
            const auto it = indexById.constFind(s.gotoTarget());
            if (it == indexById.constEnd()) {
                issues.append({i, QStringLiteral("goto_unknown"), s.gotoTarget()});
            } else if (it.value() <= i) {
                issues.append({i, QStringLiteral("goto_backward"), s.gotoTarget()});
            }
        }
        for (const PipelineCapture &c : s.capture) {
            const bool needsPath = c.from == QLatin1String("json");
            if (c.name.isEmpty() || !captureSources().contains(c.from) || (needsPath && c.path.isEmpty())) {
                issues.append({i, QStringLiteral("bad_capture"), c.name});
            }
        }
        for (const QJsonValue &f : s.ask) {
            if (!f.isObject() || f.toObject().value(QStringLiteral("name")).toString().isEmpty()) {
                issues.append({i, QStringLiteral("bad_ask"), s.id});
                break;
            }
        }
    }
    return issues;
}

QString folderPathOf(const QString &folderId, const QVector<Folder> &folders)
{
    QMap<QString, const Folder *> byId;
    for (const Folder &f : folders) {
        byId.insert(f.id, &f);
    }
    QStringList parts;
    QString current = folderId;
    QSet<QString> seen;
    while (!current.isEmpty() && byId.contains(current) && !seen.contains(current)) {
        seen.insert(current);
        const Folder *f = byId.value(current);
        parts.prepend(f->name);
        current = f->parentId.value_or(QString());
    }
    return parts.join(QLatin1Char('/'));
}

const Command *resolveCommandRef(const QString &ref, const Command &from, const QMap<QString, Command> &allCommands,
                                 const QVector<Folder> &folders, QString *error)
{
    auto fail = [&](const QString &why) -> const Command * {
        if (error) *error = why;
        return nullptr;
    };
    const QString wanted = ref.trimmed();
    if (wanted.isEmpty()) {
        return fail(QStringLiteral("empty"));
    }
    const int slash = wanted.lastIndexOf(QLatin1Char('/'));
    const QString wantedFolder = slash >= 0 ? wanted.left(slash) : QString();
    const QString wantedName = slash >= 0 ? wanted.mid(slash + 1) : wanted;

    struct Candidate {
        const Command *command;
        QString folderPath;
    };
    QVector<Candidate> byName;
    for (auto it = allCommands.constBegin(); it != allCommands.constEnd(); ++it) {
        if (it.value().name == wantedName && it.value().id != from.id) {
            byName.append({&it.value(), folderPathOf(it.value().folderId, folders)});
        }
    }
    if (byName.isEmpty()) {
        return fail(QStringLiteral("not_found"));
    }

    auto pick = [&](const std::function<bool(const Candidate &)> &pred) -> const Command * {
        const Command *found = nullptr;
        for (const Candidate &c : byName) {
            if (pred(c)) {
                if (found) {
                    return nullptr; // ambíguo neste nível: tenta o próximo critério
                }
                found = c.command;
            }
        }
        return found;
    };

    const QString fromFolder = folderPathOf(from.folderId, folders);
    if (!wantedFolder.isEmpty()) {
        const QString relative = fromFolder.isEmpty() ? wantedFolder : fromFolder + QLatin1Char('/') + wantedFolder;
        if (const Command *c = pick([&](const Candidate &k) { return k.folderPath == relative; })) return c;
        if (const Command *c = pick([&](const Candidate &k) { return k.folderPath == wantedFolder; })) return c;
        if (const Command *c = pick([&](const Candidate &k) {
                return k.folderPath.endsWith(QLatin1Char('/') + wantedFolder);
            })) {
            return c;
        }
        return fail(byName.size() > 1 ? QStringLiteral("ambiguous") : QStringLiteral("not_found"));
    }
    if (const Command *c = pick([&](const Candidate &k) { return k.folderPath == fromFolder; })) return c;
    if (byName.size() == 1) return byName.first().command;
    return fail(QStringLiteral("ambiguous"));
}

} // namespace kai::core
