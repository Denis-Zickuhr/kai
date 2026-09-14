#include "cli/completion-installer.h"

#include "cli/cli-completion.h"
#include "utils/console-context.h"
#include "utils/translation-manager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace kai::cli {

namespace {

const QString kBlockBegin = QStringLiteral("# >>> kai completion >>>");
const QString kBlockEnd = QStringLiteral("# <<< kai completion <<<");

// O que o Kai consegue fazer com um caminho de completion. QFileInfo::exists() devolve false tanto para "não existe"
// quanto para "não tenho permissão nem para checar" (a pasta pai sem permissão de busca); só diferenciar os dois
// permite dizer ao usuário o que houve em vez de criar/ignorar às cegas.
enum class PathAccess { Missing, Accessible, Denied };

PathAccess probePath(const QString &path)
{
    const QFileInfo info(path);
    if (info.exists()) {
        return info.isReadable() && info.isWritable() ? PathAccess::Accessible : PathAccess::Denied;
    }
    // Sobe até a primeira pasta que existe: se nela o Kai não pode nem "entrar", o arquivo pode existir e não dá
    // para saber.
    QFileInfo ancestor(info.absolutePath());
    while (!ancestor.exists() && !ancestor.isRoot() && ancestor.absoluteFilePath() != ancestor.absolutePath()) {
        ancestor = QFileInfo(ancestor.absolutePath());
    }
    if (ancestor.exists() && ancestor.isDir() && !ancestor.isExecutable()) {
        return PathAccess::Denied;
    }
    return PathAccess::Missing;
}

bool pathExists(const QString &path)
{
    return probePath(path) == PathAccess::Accessible;
}

QString homeRelative(const QString &path, const CompletionEnvironment &env)
{
    const QString prefix = env.fsRoot + env.home;
    if (!env.home.isEmpty() && path.startsWith(prefix + QLatin1Char('/'))) {
        return QStringLiteral("~") + path.mid(prefix.size());
    }
    return env.fsRoot.isEmpty() ? QDir::toNativeSeparators(path) : path.mid(env.fsRoot.size());
}

// Lê um arquivo como texto. `bom` guarda o BOM UTF-8 pra regravar igual;
// UTF-16 não é editado (devolve false) — corromperia o arquivo do usuário.
bool readTextFile(const QString &path, QString &text, QByteArray &bom)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) {
        return false;
    }
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bom = bytes.left(3);
        bytes.remove(0, 3);
    }
    text = QString::fromUtf8(bytes);
    return true;
}

bool writeTextFile(const QString &path, const QString &text, const QByteArray &bom)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(bom);
    file.write(text.toUtf8());
    return file.commit();
}

#ifndef Q_OS_WIN
std::optional<CompletionEnvironment> posixEnvironment(CompletionShell shell)
{
    CompletionEnvironment env;
    env.shell = shell;
    env.home = QDir::homePath();
    if (env.home.isEmpty()) {
        return std::nullopt;
    }
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
    env.dataHome = QDir::isAbsolutePath(xdg) ? xdg : env.home + QStringLiteral("/.local/share");
    const QString zdotdir = qEnvironmentVariable("ZDOTDIR");
    env.rcDir = QDir::isAbsolutePath(zdotdir) ? zdotdir : env.home;
    env.key = completionShellName(shell);
    env.label = env.key;
    if (shell == CompletionShell::Pwsh) {
        env.profilePath = env.home + QStringLiteral("/.config/powershell/Microsoft.PowerShell_profile.ps1");
    }
    return env;
}

#endif

#ifdef Q_OS_WIN
CompletionEnvironment powershellEnvironment(CompletionShell shell)
{
    CompletionEnvironment env;
    env.shell = shell;
    env.key = completionShellName(shell);
    env.label = shell == CompletionShell::Pwsh ? QStringLiteral("PowerShell 7") : QStringLiteral("Windows PowerShell");
    env.windowsLineEndings = true;
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString dir = shell == CompletionShell::Pwsh ? QStringLiteral("PowerShell") : QStringLiteral("WindowsPowerShell");
    env.profilePath = docs + QLatin1Char('/') + dir + QStringLiteral("/Microsoft.PowerShell_profile.ps1");
    return env;
}

// Uid padrão e distro padrão, do registro do WSL (sem acordar nenhuma distro).
struct LxssInfo {
    QString defaultDistro;
    QHash<QString, int> defaultUid;
};

LxssInfo readLxss()
{
    LxssInfo info;
    QSettings lxss(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"),
                   QSettings::NativeFormat);
    const QString defaultGuid = lxss.value(QStringLiteral("DefaultDistribution")).toString();
    for (const QString &guid : lxss.childGroups()) {
        lxss.beginGroup(guid);
        const QString name = lxss.value(QStringLiteral("DistributionName")).toString();
        info.defaultUid.insert(name, lxss.value(QStringLiteral("DefaultUid"), 1000).toInt());
        if (guid.compare(defaultGuid, Qt::CaseInsensitive) == 0) {
            info.defaultDistro = name;
        }
        lxss.endGroup();
    }
    return info;
}

QStringList runningWslDistros()
{
    QProcess process;
    process.start(QStringLiteral("wsl.exe"), {QStringLiteral("-l"), QStringLiteral("--running"), QStringLiteral("-q")});
    if (!process.waitForFinished(5000)) {
        process.kill();
        return {};
    }
    return decodeWslDistroList(process.readAllStandardOutput());
}

std::optional<CompletionEnvironment> wslEnvironment(bool scanWsl, std::optional<CompletionShell> forced)
{
    const LxssInfo lxss = readLxss();
    QString distro = wslDistroFromUncPath(QDir::currentPath());
    if (distro.isEmpty() && scanWsl) {
        const QStringList running = runningWslDistros();
        distro = running.contains(lxss.defaultDistro) ? lxss.defaultDistro : running.value(0);
    }
    if (distro.isEmpty()) {
        return std::nullopt;
    }
    QString root = QStringLiteral("//wsl.localhost/") + distro;
    if (!QDir(root).exists()) {
        root = QStringLiteral("//wsl$/") + distro;
        if (!QDir(root).exists()) {
            return std::nullopt;
        }
    }
    QFile passwd(root + QStringLiteral("/etc/passwd"));
    if (!passwd.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const auto entry = passwdEntryForUid(QString::fromUtf8(passwd.readAll()), lxss.defaultUid.value(distro, 1000));
    if (!entry || entry->home.isEmpty()) {
        return std::nullopt;
    }
    std::optional<CompletionShell> shell = forced;
    if (!shell) {
        shell = completionShellFromName(QFileInfo(entry->shell).fileName());
        if (shell && *shell != CompletionShell::Bash && *shell != CompletionShell::Zsh) {
            shell.reset();
        }
    }
    if (!shell) {
        return std::nullopt;
    }
    CompletionEnvironment env;
    env.shell = *shell;
    env.fsRoot = root;
    env.home = entry->home;
    env.dataHome = entry->home + QStringLiteral("/.local/share");
    env.rcDir = entry->home;
    env.key = QStringLiteral("wsl:%1:%2").arg(distro, completionShellName(*shell));
    env.label = QStringLiteral("%1 (WSL: %2)").arg(completionShellName(*shell), distro);
    return env;
}
#endif

QStringList displayPaths(const QVector<CompletionTarget> &targets, const CompletionEnvironment &env)
{
    QStringList lines;
    for (const CompletionTarget &t : targets) {
        if (t.createIfMissing || pathExists(t.path)) {
            lines << QStringLiteral("  ") + homeRelative(t.path, env);
        }
    }
    return lines;
}

} // namespace

QString completionShellName(CompletionShell shell)
{
    switch (shell) {
    case CompletionShell::Bash: return QStringLiteral("bash");
    case CompletionShell::Zsh: return QStringLiteral("zsh");
    case CompletionShell::PowerShell: return QStringLiteral("powershell");
    case CompletionShell::Pwsh: return QStringLiteral("pwsh");
    }
    return {};
}

std::optional<CompletionShell> completionShellFromName(const QString &name)
{
    const QString n = name.toLower();
    if (n == QLatin1String("bash")) return CompletionShell::Bash;
    if (n == QLatin1String("zsh")) return CompletionShell::Zsh;
    if (n == QLatin1String("powershell")) return CompletionShell::PowerShell;
    if (n == QLatin1String("pwsh")) return CompletionShell::Pwsh;
    return std::nullopt;
}

bool hasMarkerBlock(const QString &existing)
{
    const int begin = existing.indexOf(kBlockBegin);
    return begin >= 0 && existing.indexOf(kBlockEnd, begin) > begin;
}

QString upsertMarkerBlock(const QString &existing, const QString &body, const QString &eol)
{
    QStringList bodyLines = body.split(QLatin1Char('\n'));
    while (!bodyLines.isEmpty() && bodyLines.last().trimmed().isEmpty()) {
        bodyLines.removeLast();
    }
    for (QString &line : bodyLines) {
        line.remove(QLatin1Char('\r'));
    }
    const QString block = kBlockBegin + eol + bodyLines.join(eol) + eol + kBlockEnd + eol;

    if (hasMarkerBlock(existing)) {
        const int begin = existing.indexOf(kBlockBegin);
        const int endMarker = existing.indexOf(kBlockEnd, begin);
        int after = existing.indexOf(QLatin1Char('\n'), endMarker);
        after = after < 0 ? existing.size() : after + 1;
        return existing.left(begin) + block + existing.mid(after);
    }
    QString result = existing;
    if (!result.isEmpty()) {
        if (!result.endsWith(QLatin1Char('\n'))) {
            result += eol;
        }
        result += eol;
    }
    return result + block;
}

QString removeMarkerBlock(const QString &existing)
{
    if (!hasMarkerBlock(existing)) {
        return existing;
    }
    int begin = existing.indexOf(kBlockBegin);
    const int endMarker = existing.indexOf(kBlockEnd, begin);
    int after = existing.indexOf(QLatin1Char('\n'), endMarker);
    after = after < 0 ? existing.size() : after + 1;
    // Leva junto a linha em branco que o upsert deixou antes do bloco.
    const QString before = existing.left(begin);
    if (before.endsWith(QStringLiteral("\n\n")) || before.endsWith(QStringLiteral("\r\n\r\n"))) {
        begin -= before.endsWith(QStringLiteral("\r\n\r\n")) ? 2 : 1;
    }
    return existing.left(begin) + existing.mid(after);
}

QString wslDistroFromUncPath(const QString &path)
{
    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    static const QRegularExpression re(QStringLiteral("^//(?:wsl\\.localhost|wsl\\$)/([^/]+)(?:/|$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(normalized);
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList decodeWslDistroList(const QByteArray &raw)
{
    QString text;
    // wsl.exe escreve UTF-16LE (o 2º byte do 1º caractere ASCII é NUL).
    if (raw.size() >= 2 && (raw.at(1) == '\0' || raw.startsWith("\xFF\xFE"))) {
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData()), raw.size() / 2);
    } else {
        text = QString::fromUtf8(raw);
    }
    QStringList names;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line.remove(QLatin1Char('\r')).remove(QChar(0xFEFF)).remove(QChar(0));
        line = line.trimmed();
        if (!line.isEmpty()) {
            names << line;
        }
    }
    return names;
}

std::optional<PasswdEntry> passwdEntryForUid(const QString &passwdText, int uid)
{
    for (const QString &line : passwdText.split(QLatin1Char('\n'))) {
        const QStringList fields = line.split(QLatin1Char(':'));
        if (fields.size() >= 7 && fields.at(2).trimmed() == QString::number(uid)) {
            return PasswdEntry{fields.at(5).trimmed(), fields.at(6).trimmed()};
        }
    }
    return std::nullopt;
}

QVector<CompletionTarget> completionTargets(const CompletionEnvironment &env)
{
    QVector<CompletionTarget> targets;
    switch (env.shell) {
    case CompletionShell::Bash: {
        const QString loaderDir = env.dataHome + QStringLiteral("/bash-completion/completions");
        targets.append({env.fsRoot + loaderDir + QStringLiteral("/kai"), completionScript(QStringLiteral("bash")), false, true});
        targets.append({env.fsRoot + loaderDir + QStringLiteral("/kai.exe"),
                        QStringLiteral("# kai.exe - loads the kai completion, managed by Kai\n. \"%1/kai\"\n").arg(loaderDir),
                        false, true});
        // O bash-completion carrega esses arquivos sozinho; sem ele, só um
        // bloco no .bashrc faz o bash enxergar o script.
        static const QStringList loaders = {
            QStringLiteral("/usr/share/bash-completion/bash_completion"), QStringLiteral("/etc/bash_completion"),
            QStringLiteral("/opt/homebrew/etc/profile.d/bash_completion.sh"),
            QStringLiteral("/usr/local/etc/profile.d/bash_completion.sh")};
        bool hasLoader = false;
        for (const QString &l : loaders) {
            hasLoader = hasLoader || pathExists(env.fsRoot + l);
        }
        if (!hasLoader) {
            CompletionTarget rc;
            rc.path = env.fsRoot + env.home + QStringLiteral("/.bashrc");
            rc.content = QStringLiteral("[ -r \"%1/kai\" ] && . \"%1/kai\"").arg(loaderDir);
            rc.markerBlock = true;
            rc.createIfMissing = false;
            targets.append(rc);
        }
        break;
    }
    case CompletionShell::Zsh: {
        const QString script = env.dataHome + QStringLiteral("/kai/completion.zsh");
        targets.append({env.fsRoot + script, completionScript(QStringLiteral("zsh")), false, true});
        CompletionTarget rc;
        rc.path = env.fsRoot + env.rcDir + QStringLiteral("/.zshrc");
        rc.content = QStringLiteral("(( $+functions[compdef] )) || { autoload -Uz compinit && compinit; }\n"
                                    "[ -r \"%1\" ] && source \"%1\"").arg(script);
        rc.markerBlock = true;
        targets.append(rc);
        break;
    }
    case CompletionShell::PowerShell:
    case CompletionShell::Pwsh: {
        CompletionTarget profile;
        profile.path = env.profilePath;
        profile.content = completionScript(QStringLiteral("powershell"));
        profile.markerBlock = true;
        profile.crlfWhenNew = env.windowsLineEndings;
        targets.append(profile);
        break;
    }
    }
    return targets;
}

bool completionPathDenied(const QString &path)
{
    return probePath(path) == PathAccess::Denied;
}

QStringList applyCompletionTargets(const QVector<CompletionTarget> &targets, bool onlyExisting, QString *error)
{
    QStringList changed;
    for (const CompletionTarget &t : targets) {
        if (probePath(t.path) == PathAccess::Denied) {
            // Sem permissão para checar/ler/gravar: nada a fazer com este arquivo, e o chamador é avisado.
            if (error && error->isEmpty()) {
                *error = t.path;
            }
            continue;
        }
        const bool exists = pathExists(t.path);
        if (!exists && (onlyExisting || !t.createIfMissing)) {
            continue;
        }
        QString existing;
        QByteArray bom;
        if (exists && !readTextFile(t.path, existing, bom)) {
            if (error && error->isEmpty()) {
                *error = t.path;
            }
            continue;
        }
        QString updated;
        if (t.markerBlock) {
            if (onlyExisting && !hasMarkerBlock(existing)) {
                continue;
            }
            const QString eol = existing.contains(QStringLiteral("\r\n")) || (!exists && t.crlfWhenNew)
                ? QStringLiteral("\r\n") : QStringLiteral("\n");
            updated = upsertMarkerBlock(existing, t.content, eol);
        } else {
            updated = t.content;
        }
        if (exists && updated == existing) {
            continue;
        }
        if (!writeTextFile(t.path, updated, bom)) {
            if (error && error->isEmpty()) {
                *error = t.path;
            }
            continue;
        }
        changed << t.path;
    }
    return changed;
}

QStringList removeCompletionTargets(const QVector<CompletionTarget> &targets, QStringList *failed)
{
    QStringList changed;
    auto fail = [failed](const QString &path) {
        if (failed) {
            failed->append(path);
        }
    };
    for (const CompletionTarget &t : targets) {
        const PathAccess access = probePath(t.path);
        if (access == PathAccess::Missing) {
            continue;
        }
        if (access == PathAccess::Denied) {
            fail(t.path); // não dá para nem saber se há algo do Kai ali
            continue;
        }
        if (!t.markerBlock) {
            if (QFile::remove(t.path)) {
                changed << t.path;
            } else {
                fail(t.path);
            }
            continue;
        }
        QString existing;
        QByteArray bom;
        if (!readTextFile(t.path, existing, bom)) {
            fail(t.path);
            continue;
        }
        if (!hasMarkerBlock(existing)) {
            continue;
        }
        if (writeTextFile(t.path, removeMarkerBlock(existing), bom)) {
            changed << t.path;
        } else {
            fail(t.path);
        }
    }
    return changed;
}

QString completionConsentFilePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("completion-setup.json"));
}

std::optional<CompletionDecision> loadCompletionDecision(const QString &consentFile, const QString &key)
{
    QFile file(consentFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonValue entry = root.value(QStringLiteral("environments")).toObject().value(key);
    if (!entry.isObject()) {
        return std::nullopt;
    }
    CompletionDecision decision;
    decision.accepted = entry.toObject().value(QStringLiteral("accepted")).toBool();
    decision.version = entry.toObject().value(QStringLiteral("version")).toInt();
    return decision;
}

bool hasCompletionDecisionWithPrefix(const QString &consentFile, const QString &prefix)
{
    QFile file(consentFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject environments = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("environments")).toObject();
    for (auto it = environments.begin(); it != environments.end(); ++it) {
        if (it.key().startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

bool saveCompletionDecision(const QString &consentFile, const QString &key, const CompletionDecision &decision)
{
    QJsonObject root;
    {
        QFile file(consentFile);
        if (file.open(QIODevice::ReadOnly)) {
            root = QJsonDocument::fromJson(file.readAll()).object();
        } else if (file.exists()) {
            return false; // existe mas não dá para ler: regravar do zero apagaria as decisões dos outros ambientes
        }
    }
    QJsonObject environments = root.value(QStringLiteral("environments")).toObject();
    environments.insert(key, QJsonObject{{QStringLiteral("accepted"), decision.accepted},
                                         {QStringLiteral("version"), decision.version}});
    root.insert(QStringLiteral("environments"), environments);

    QDir().mkpath(QFileInfo(consentFile).absolutePath());
    QSaveFile out(consentFile);
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return out.commit();
}

std::optional<CompletionEnvironment> detectCompletionEnvironment(bool scanWsl)
{
    const QString parent = utils::parentProcessName();
#ifdef Q_OS_WIN
    if (parent == QLatin1String("powershell.exe")) {
        return powershellEnvironment(CompletionShell::PowerShell);
    }
    if (parent == QLatin1String("pwsh.exe")) {
        return powershellEnvironment(CompletionShell::Pwsh);
    }
    if (utils::launchedFromWslInterop()) {
        // Fora de uma pasta da distro (o Windows Terminal costuma abrir em /mnt/c/...) o diretório atual não diz
        // qual é. Aí procura a distro em execução — mas só até haver alguma decisão de WSL guardada, para não
        // pagar o wsl.exe a cada execução depois.
        const bool scan = scanWsl || !hasCompletionDecisionWithPrefix(completionConsentFilePath(), QStringLiteral("wsl:"));
        return wslEnvironment(scan, std::nullopt);
    }
    return std::nullopt;
#else
    Q_UNUSED(scanWsl);
    // Pai que é um shell conhecido; senão, o shell de login do usuário.
    std::optional<CompletionShell> shell = completionShellFromName(parent);
    if (!shell) {
        shell = completionShellFromName(QFileInfo(qEnvironmentVariable("SHELL")).fileName());
    }
    if (!shell || *shell == CompletionShell::PowerShell) {
        return std::nullopt;
    }
    return posixEnvironment(*shell);
#endif
}

std::optional<CompletionEnvironment> completionEnvironmentFor(CompletionShell shell, QString *error)
{
#ifdef Q_OS_WIN
    if (shell == CompletionShell::PowerShell || shell == CompletionShell::Pwsh) {
        return powershellEnvironment(shell);
    }
    if (utils::launchedFromWslInterop()) {
        if (const auto env = wslEnvironment(true, shell)) {
            return env;
        }
        if (error) {
            *error = utils::tr(QStringLiteral("cli.completion.error.no_wsl_distro"));
        }
        return std::nullopt;
    }
    if (error) {
        *error = utils::tr(QStringLiteral("cli.completion.error.unsupported_here")).arg(completionShellName(shell));
    }
    return std::nullopt;
#else
    if (shell == CompletionShell::PowerShell) {
        shell = CompletionShell::Pwsh;
    }
    const auto env = posixEnvironment(shell);
    if (!env && error) {
        *error = utils::tr(QStringLiteral("cli.completion.error.no_home"));
    }
    return env;
#endif
}

namespace {

QString installedMessage(const CompletionEnvironment &env, const QStringList &changed)
{
    QStringList shown;
    for (const QString &p : changed) {
        shown << QStringLiteral("  ") + homeRelative(p, env);
    }
    return utils::tr(QStringLiteral("cli.completion.installed")).arg(env.label, shown.join(QLatin1Char('\n')));
}

bool userAccepts(const QString &answer, bool &known)
{
    const QString a = answer.trimmed().toLower();
    known = true;
    if (a.isEmpty() || a == QLatin1String("y") || a == QLatin1String("yes") || a == QLatin1String("s")
        || a == QLatin1String("sim")) {
        return true;
    }
    if (a == QLatin1String("n") || a == QLatin1String("no") || a == QLatin1String("nao")
        || a == QStringLiteral("não")) {
        return false;
    }
    known = false;
    return false;
}

} // namespace

void offerShellCompletion()
{
    if (qEnvironmentVariableIsSet("KAI_FORCE_GUI")) {
        return;
    }
    const auto env = detectCompletionEnvironment(false);
    if (!env) {
        return;
    }
    const QString consentFile = completionConsentFilePath();
    const auto decision = loadCompletionDecision(consentFile, env->key);
    if (decision) {
        // Aceitou antes: só atualiza o que ainda existe (não recria o que o
        // usuário apagou; pra isso há `kai completion install`).
        if (decision->accepted && decision->version < kCompletionVersion) {
            QString failed;
            applyCompletionTargets(completionTargets(*env), true, &failed);
            if (!failed.isEmpty() && completionPathDenied(failed)) {
                QTextStream warn(stderr);
                warn << utils::tr(QStringLiteral("cli.completion.error.denied")).arg(failed) << "\n";
            }
            saveCompletionDecision(consentFile, env->key, {true, kCompletionVersion});
        }
        return;
    }
    if (!utils::stdoutIsInteractiveTerminal() || !utils::stdinIsInteractiveTerminal()) {
        return;
    }

    const QVector<CompletionTarget> targets = completionTargets(*env);
    QTextStream err(stderr);
    err << utils::tr(QStringLiteral("cli.completion.prompt")).arg(env->label, displayPaths(targets, *env).join(QLatin1Char('\n')));
    err.flush();

    char buffer[256];
    if (!std::fgets(buffer, sizeof(buffer), stdin)) {
        return;
    }
    bool known = false;
    const bool accepted = userAccepts(QString::fromLocal8Bit(buffer), known);
    if (!known) {
        return; // resposta ilegível: pergunta de novo na próxima
    }
    if (!accepted) {
        saveCompletionDecision(consentFile, env->key, {false, kCompletionVersion});
        err << utils::tr(QStringLiteral("cli.completion.declined")) << "\n";
        return;
    }
    QString failed;
    const QStringList changed = applyCompletionTargets(targets, false, &failed);
    if (!failed.isEmpty()) {
        if (completionPathDenied(failed)) {
            // Sem permissão num arquivo: avisa qual e para de perguntar a cada execução; `kai completion install`
            // tenta de novo depois de ajustar as permissões.
            err << utils::tr(QStringLiteral("cli.completion.error.denied")).arg(failed) << "\n";
            saveCompletionDecision(consentFile, env->key, {false, kCompletionVersion});
        } else {
            err << utils::tr(QStringLiteral("cli.completion.error.write")).arg(failed) << "\n";
        }
        return;
    }
    if (!saveCompletionDecision(consentFile, env->key, {true, kCompletionVersion})) {
        err << utils::tr(QStringLiteral("cli.completion.error.consent_unwritable")).arg(consentFile) << "\n";
    }
    err << installedMessage(*env, changed) << "\n";
}

int runCompletionInstall(const QStringList &args, bool install)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    std::optional<CompletionEnvironment> env;
    QString error;
    if (args.size() > 3) {
        const auto shell = completionShellFromName(args.at(3));
        if (!shell) {
            err << utils::tr(QStringLiteral("cli.error.usage.completion")) << "\n";
            return 2;
        }
        env = completionEnvironmentFor(*shell, &error);
    } else {
        env = detectCompletionEnvironment(true);
        if (!env) {
            error = utils::tr(QStringLiteral("cli.completion.error.no_shell"));
        }
    }
    if (!env) {
        err << error << "\n";
        return 2;
    }

    const QVector<CompletionTarget> targets = completionTargets(*env);
    const QString consentFile = completionConsentFilePath();
    if (install) {
        QString failed;
        const QStringList changed = applyCompletionTargets(targets, false, &failed);
        if (!failed.isEmpty()) {
            err << (completionPathDenied(failed) ? utils::tr(QStringLiteral("cli.completion.error.denied"))
                                                 : utils::tr(QStringLiteral("cli.completion.error.write"))).arg(failed)
                << "\n";
            return 2;
        }
        saveCompletionDecision(consentFile, env->key, {true, kCompletionVersion});
        out << (changed.isEmpty() ? utils::tr(QStringLiteral("cli.completion.already")).arg(env->label)
                                  : installedMessage(*env, changed)) << "\n";
        return 0;
    }
    QStringList notRemoved;
    const QStringList removed = removeCompletionTargets(targets, &notRemoved);
    saveCompletionDecision(consentFile, env->key, {false, kCompletionVersion});
    if (!notRemoved.isEmpty()) {
        // Não diz "removido" quando ficou algo para trás.
        for (const QString &p : notRemoved) {
            err << (completionPathDenied(p) ? utils::tr(QStringLiteral("cli.completion.error.denied"))
                                            : utils::tr(QStringLiteral("cli.completion.error.remove_failed"))).arg(p)
                << "\n";
        }
        return 2;
    }
    out << utils::tr(QStringLiteral("cli.completion.removed")).arg(env->label) << "\n";
    for (const QString &p : removed) {
        out << "  " << homeRelative(p, *env) << "\n";
    }
    return 0;
}

} // namespace kai::cli
