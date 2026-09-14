#include "cli/cli-app-verbs.h"
#include "cli/app-delegate.h"

#include "core/run-history.h"
#include "utils/duration-format.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace kai::cli {

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}
QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

const QStringList &raiseLevels()
{
    static const QStringList levels = {QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("error")};
    return levels;
}

// Resposta padrão do IpcServer ({"ok":..., "message":...}) -> terminal.
int printAppReply(const std::optional<QJsonObject> &reply)
{
    if (!reply) {
        err() << utils::tr(QStringLiteral("cli.error.not_running")) << "\n";
        err().flush();
        return 2;
    }
    const bool ok = reply->value(QStringLiteral("ok")).toBool();
    const QString message = reply->value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
        (ok ? out() : err()) << message << "\n";
    }
    out().flush();
    err().flush();
    return ok ? 0 : 1;
}

} // namespace

QString resolveImportTarget(const QString &argument, const QString &currentDir)
{
    const QFileInfo info(QDir(currentDir).filePath(argument.isEmpty() ? QStringLiteral(".") : argument));
    if (info.isFile()) {
        return info.absoluteFilePath();
    }
    if (info.isDir()) {
        const QFileInfo candidate(QDir(info.absoluteFilePath()).filePath(QStringLiteral("kai.yml")));
        if (candidate.isFile()) {
            return candidate.absoluteFilePath();
        }
    }
    return QString();
}

RaiseRequest parseRaiseArgs(const QStringList &argsAfterVerb)
{
    RaiseRequest req;
    QStringList words;
    for (int i = 0; i < argsAfterVerb.size(); ++i) {
        const QString &a = argsAfterVerb.at(i);
        // "--level error" e "--level=error" (idem --title).
        auto takeValue = [&](const QString &flag, QString &target) {
            if (a == flag && i + 1 < argsAfterVerb.size()) {
                target = argsAfterVerb.at(++i);
                return true;
            }
            if (a.startsWith(flag + QLatin1Char('='))) {
                target = a.mid(flag.size() + 1);
                return true;
            }
            return false;
        };
        if (takeValue(QStringLiteral("--level"), req.level) || takeValue(QStringLiteral("--title"), req.title)) {
            continue;
        }
        words << a;
    }
    req.level = req.level.trimmed().toLower();
    req.message = words.join(QLatin1Char(' ')).trimmed();
    if (!raiseLevels().contains(req.level) || req.message.isEmpty()) {
        req.error = utils::tr(QStringLiteral("cli.error.usage.raise"));
    }
    return req;
}

ImportRequest parseImportArgs(const QStringList &argsAfterVerb)
{
    ImportRequest req;
    for (int i = 0; i < argsAfterVerb.size(); ++i) {
        const QString &a = argsAfterVerb.at(i);
        QString list;
        if (a == QStringLiteral("--only")) {
            if (i + 1 >= argsAfterVerb.size()) {
                req.error = utils::tr(QStringLiteral("cli.error.usage.import"));
                return req;
            }
            list = argsAfterVerb.at(++i);
        } else if (a.startsWith(QStringLiteral("--only="))) {
            list = a.mid(7);
        } else if (a.startsWith(QStringLiteral("--"))) {
            req.error = utils::tr(QStringLiteral("cli.error.usage.import"));
            return req;
        } else if (req.argument.isEmpty()) {
            req.argument = a;
            continue;
        } else {
            req.error = utils::tr(QStringLiteral("cli.error.usage.import"));
            return req;
        }
        for (const QString &name : list.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            req.only << name.trimmed();
        }
        if (req.only.isEmpty()) {
            req.error = utils::tr(QStringLiteral("cli.error.usage.import"));
            return req;
        }
    }
    return req;
}

int runImportVerb(const QStringList &args)
{
    utils::Logger::setConsoleOutputEnabled(false);
    const ImportRequest parsed = parseImportArgs(args.mid(2));
    if (!parsed.error.isEmpty()) {
        err() << parsed.error << "\n";
        err().flush();
        return 2;
    }
    const QString argument = parsed.argument;
    const QString target = resolveImportTarget(argument, QDir::currentPath());
    if (target.isEmpty()) {
        err() << utils::tr(QStringLiteral("cli.import.not_found"))
                     .arg(QDir::toNativeSeparators(QDir(QDir::currentPath()).filePath(argument)))
              << "\n";
        err().flush();
        return 2;
    }
    QJsonObject request;
    request[QStringLiteral("cmd")] = QStringLiteral("import");
    request[QStringLiteral("path")] = target;
    if (!parsed.only.isEmpty()) {
        request[QStringLiteral("only")] = QJsonArray::fromStringList(parsed.only);
    }
    return printAppReply(requestApp(request));
}

int runRaiseVerb(const QStringList &args)
{
    utils::Logger::setConsoleOutputEnabled(false);
    const RaiseRequest req = parseRaiseArgs(args.mid(2));
    if (!req.error.isEmpty()) {
        err() << req.error << "\n";
        err().flush();
        return 2;
    }
    QJsonObject request;
    request[QStringLiteral("cmd")] = QStringLiteral("raise");
    request[QStringLiteral("level")] = req.level;
    request[QStringLiteral("title")] = req.title;
    request[QStringLiteral("message")] = req.message;
    return printAppReply(requestApp(request, 15000));
}

namespace {

QString statusLabel(bool success)
{
    return success ? utils::tr(QStringLiteral("cli.history.ok")) : utils::tr(QStringLiteral("cli.history.failed"));
}

QJsonObject recordToJson(const core::RunRecord &r, bool withOutput)
{
    QJsonObject o;
    o[QStringLiteral("command")] = r.commandName;
    o[QStringLiteral("command_id")] = r.commandId;
    o[QStringLiteral("type")] = r.commandType;
    o[QStringLiteral("started_at")] = r.startedAt.toString(Qt::ISODate);
    o[QStringLiteral("duration_ms")] = static_cast<double>(r.durationMs);
    o[QStringLiteral("success")] = r.success;
    if (withOutput) {
        o[QStringLiteral("output")] = r.output;
    }
    return o;
}

} // namespace

int runAttachVerb(const QStringList &args)
{
    const QString target = args.value(2).trimmed();
    if (target.isEmpty()) {
        err() << utils::tr(QStringLiteral("cli.error.usage.attach")) << "\n";
        err().flush();
        return 2;
    }
    const std::optional<int> exitCode = attachViaApp(target);
    if (!exitCode) {
        err() << utils::tr(QStringLiteral("cli.error.not_running")) << "\n";
        err().flush();
        return 2;
    }
    return *exitCode;
}

int runHistoryVerb(const QStringList &args)
{
    int limit = 15;
    bool json = false;
    for (const QString &a : args.mid(2)) {
        bool isNumber = false;
        const int n = a.toInt(&isNumber);
        if (isNumber && n > 0) {
            limit = n;
        } else if (a == QStringLiteral("--json")) {
            json = true;
        }
    }
    const QVector<core::RunRecord> records = core::RunHistory().load().mid(0, limit);
    if (json) {
        QJsonArray arr;
        for (const core::RunRecord &r : records) {
            arr.append(recordToJson(r, false));
        }
        out() << QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        out().flush();
        return 0;
    }
    if (records.isEmpty()) {
        out() << utils::tr(QStringLiteral("cli.history.empty")) << "\n";
        out().flush();
        return 0;
    }
    int statusWidth = 0;
    for (const bool ok : {true, false}) {
        statusWidth = qMax(statusWidth, static_cast<int>(statusLabel(ok).size()));
    }
    for (const core::RunRecord &r : records) {
        const QString status = statusLabel(r.success);
        out() << "  " << r.startedAt.toLocalTime().toString(QStringLiteral("dd/MM HH:mm")) << "  "
              << status << QString(statusWidth - status.size(), QLatin1Char(' ')) << "  "
              << QStringLiteral("%1").arg(utils::formatShortDuration(r.durationMs), 7) << "  "
              << r.commandName << "\n";
    }
    out().flush();
    return 0;
}

int runLastVerb(const QStringList &args)
{
    const bool withLog = args.contains(QStringLiteral("--log"));
    const bool json = args.contains(QStringLiteral("--json"));
    const QVector<core::RunRecord> records = core::RunHistory().load();
    if (records.isEmpty()) {
        out() << utils::tr(QStringLiteral("cli.history.empty")) << "\n";
        out().flush();
        return 0;
    }
    const core::RunRecord &r = records.first();
    if (json) {
        out() << QString::fromUtf8(QJsonDocument(recordToJson(r, withLog)).toJson(QJsonDocument::Indented));
        out().flush();
        return 0;
    }
    out() << r.commandName << "  —  " << statusLabel(r.success) << "  —  "
          << utils::formatShortDuration(r.durationMs) << "  —  "
          << r.startedAt.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")) << "\n";
    if (withLog) {
        out() << "\n" << r.output;
        if (!r.output.endsWith(QLatin1Char('\n'))) {
            out() << "\n";
        }
    } else if (!r.output.isEmpty()) {
        out() << utils::tr(QStringLiteral("cli.history.log_hint")) << "\n";
    }
    out().flush();
    return 0;
}

} // namespace kai::cli
