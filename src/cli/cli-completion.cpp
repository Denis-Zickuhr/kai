#include "cli/cli-completion.h"
#include "cli/cli-local-executor.h"
#include "cli/completion-installer.h"

#include "core/cli-param-binder.h"
#include "core/cli-path-resolver.h"
#include "core/config-manager.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QDir>
#include <QSet>
#include <QTextStream>

#include <cstdio>

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#endif

namespace kai::cli {

namespace {

// stdout em binário: no Windows o stdout da CRT traduz '\n' em '\r\n', e um
// script (ou candidato) com '\r' quebra o `eval` do bash no WSL.
void writeStdout(const QString &text)
{
#if defined(Q_OS_WIN)
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const QByteArray bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    std::fflush(stdout);
}

// Verbos oferecidos na 1ª posição (os de uso humano; __complete fica de fora).
const QStringList &topLevelVerbs()
{
    static const QStringList verbs = {
        QStringLiteral("run"), QStringLiteral("list"), QStringLiteral("env"),
        QStringLiteral("show"), QStringLiteral("help"), QStringLiteral("import"),
        QStringLiteral("validate"), QStringLiteral("ps"), QStringLiteral("attach"),
        QStringLiteral("kill"), QStringLiteral("raise"), QStringLiteral("completion"),
        QStringLiteral("history"), QStringLiteral("last"), QStringLiteral("init"),
        QStringLiteral("kip"),
    };
    return verbs;
}

// `kai kip <TAB>`: os verbos do helper; depois do verbo, as opções dele.
QStringList kipCandidates(const QStringList &wordsAfterKip)
{
    static const QStringList verbs = {
        QStringLiteral("hello"), QStringLiteral("prompt"), QStringLiteral("confirm"), QStringLiteral("patch"),
        QStringLiteral("invalid"), QStringLiteral("message"), QStringLiteral("markdown"), QStringLiteral("progress"),
        QStringLiteral("steps"), QStringLiteral("step"), QStringLiteral("table"), QStringLiteral("notify"),
        QStringLiteral("set-env"), QStringLiteral("done"), QStringLiteral("chip-result"), QStringLiteral("raw"),
        QStringLiteral("get"), QStringLiteral("--help"),
    };
    if (wordsAfterKip.isEmpty()) {
        return verbs;
    }
    const QString verb = wordsAfterKip.first();
    const QStringList fieldOptions = {
        QStringLiteral("--field"), QStringLiteral("--options"), QStringLiteral("--option"), QStringLiteral("--flag"),
        QStringLiteral("--required"), QStringLiteral("--default"), QStringLiteral("--placeholder"),
        QStringLiteral("--description"), QStringLiteral("--group"), QStringLiteral("--watch"),
        QStringLiteral("--multiple"), QStringLiteral("--search"), QStringLiteral("--no-search"), QStringLiteral("--page-size"),
        QStringLiteral("--no-remember"), QStringLiteral("--min"), QStringLiteral("--max"),
        QStringLiteral("--step"), QStringLiteral("--decimals"), QStringLiteral("--mode"), QStringLiteral("--range"),
        QStringLiteral("--filter"), QStringLiteral("--initial-dir"), QStringLiteral("--path-format"),
        QStringLiteral("--column"), QStringLiteral("--row-key"), QStringLiteral("--rows-json"),
        QStringLiteral("--chip"), QStringLiteral("--chip-description"), QStringLiteral("--chip-icon"),
        QStringLiteral("--chip-danger"), QStringLiteral("--chip-requires"), QStringLiteral("--chip-confirm"),
        QStringLiteral("--chip-confirm-title"), QStringLiteral("--chip-confirm-text"),
        QStringLiteral("--chip-confirm-label"), QStringLiteral("--chip-cancel-label"), QStringLiteral("--no-chips"),
    };
    // Depois de `--field`: os tipos de campo.
    if (wordsAfterKip.size() >= 2 && wordsAfterKip.at(wordsAfterKip.size() - 1) == QStringLiteral("--field")) {
        return {QStringLiteral("text"), QStringLiteral("secret"), QStringLiteral("textarea"), QStringLiteral("number"),
                QStringLiteral("date"), QStringLiteral("select"), QStringLiteral("list"), QStringLiteral("table"),
                QStringLiteral("filepick"), QStringLiteral("folderpick"), QStringLiteral("flags")};
    }
    if (verb == QStringLiteral("prompt")) {
        return QStringList{QStringLiteral("--id"), QStringLiteral("--title"), QStringLiteral("--submit-label"),
                           QStringLiteral("--back"), QStringLiteral("--no-cancel")} + fieldOptions;
    }
    if (verb == QStringLiteral("patch")) {
        return QStringList{QStringLiteral("--id"), QStringLiteral("--seq"), QStringLiteral("--remove")} + fieldOptions;
    }
    if (verb == QStringLiteral("confirm")) {
        return {QStringLiteral("--id"), QStringLiteral("--title"), QStringLiteral("--text"), QStringLiteral("--danger"),
                QStringLiteral("--confirm-label"), QStringLiteral("--cancel-label"), QStringLiteral("--back"),
                QStringLiteral("--no-cancel")};
    }
    if (verb == QStringLiteral("invalid")) {
        return {QStringLiteral("--id"), QStringLiteral("--error"), QStringLiteral("--message")};
    }
    if (verb == QStringLiteral("hello")) {
        return {QStringLiteral("--title"), QStringLiteral("--version")};
    }
    if (verb == QStringLiteral("steps")) {
        return {QStringLiteral("--id"), QStringLiteral("--title"), QStringLiteral("--item"), QStringLiteral("--state"),
                QStringLiteral("--detail")};
    }
    if (verb == QStringLiteral("table")) {
        return {QStringLiteral("--id"), QStringLiteral("--title"), QStringLiteral("--column"), QStringLiteral("--rows-json")};
    }
    if (verb == QStringLiteral("done")) {
        return {QStringLiteral("--title"), QStringLiteral("--text"), QStringLiteral("--level"), QStringLiteral("--action"),
                QStringLiteral("--path-format")};
    }
    if (verb == QStringLiteral("message") || verb == QStringLiteral("notify")) {
        return {QStringLiteral("--level"), QStringLiteral("info"), QStringLiteral("success"), QStringLiteral("warning"),
                QStringLiteral("error")};
    }
    if (verb == QStringLiteral("progress")) {
        return {QStringLiteral("--no-cancel")};
    }
    if (verb == QStringLiteral("chip-result")) {
        // `chip-result <chip> <state> [text]`: depois do chip, os estados.
        if (wordsAfterKip.size() == 3) {
            return {QStringLiteral("running"), QStringLiteral("success"), QStringLiteral("error")};
        }
        return {QStringLiteral("--title"), QStringLiteral("--id")};
    }
    return {};
}


QStringList commandArgumentCandidates(const core::Command &command, const QStringList &remainingArgs)
{
    QStringList candidates;
    QSet<QString> usedFlags;
    int positionalCount = 0;
    for (const QString &arg : remainingArgs) {
        if (arg.startsWith(QStringLiteral("--"))) {
            usedFlags.insert(arg.mid(2).section(QLatin1Char('='), 0, 0));
        } else {
            ++positionalCount;
        }
    }

    // Próximo obrigatório (posicional, na ordem do schema).
    int requiredIndex = 0;
    for (const core::Parameter &p : command.params) {
        if (p.optional) {
            continue;
        }
        if (requiredIndex++ != positionalCount) {
            continue;
        }
        if (p.type == core::ParameterType::Select && p.collectionId.isEmpty()) {
            candidates << core::selectOptionValues(p);
        } else if (p.type == core::ParameterType::Bool) {
            candidates << QStringLiteral("true") << QStringLiteral("false");
        }
        break;
    }

    for (const core::Parameter &p : command.params) {
        if (p.optional && !usedFlags.contains(p.name)) {
            candidates << QStringLiteral("--%1=").arg(p.name);
        }
    }
    candidates << QStringLiteral("--help");
    return candidates;
}

} // namespace

QStringList completionCandidates(const QVector<core::Folder> &folders,
                                 const QVector<core::Command> &commands,
                                 const QStringList &wordsBefore,
                                 const QString &partial,
                                 bool includeVerbs)
{
    QStringList candidates;

    // Valor de um --opcao=<parcial>: completa o valor, mantendo o prefixo.
    if (partial.startsWith(QStringLiteral("--")) && partial.contains(QLatin1Char('='))) {
        const core::CliPathResolution res = core::CliPathResolver(folders, commands).resolve(wordsBefore);
        if (res.kind == core::CliPathResolution::Kind::Command) {
            const QString name = partial.mid(2).section(QLatin1Char('='), 0, 0);
            for (const core::Command &c : commands) {
                if (c.id != res.commandId) {
                    continue;
                }
                for (const core::Parameter &p : c.params) {
                    if (p.name != name) {
                        continue;
                    }
                    QStringList values;
                    if (p.type == core::ParameterType::Select && p.collectionId.isEmpty()) {
                        values = core::selectOptionValues(p);
                    } else if (p.type == core::ParameterType::Bool) {
                        values = {QStringLiteral("true"), QStringLiteral("false")};
                    }
                    for (const QString &v : values) {
                        candidates << QStringLiteral("--%1=%2").arg(name, v);
                    }
                }
            }
        }
    } else {
        const core::CliPathResolver resolver(folders, commands);
        const core::CliPathResolution res = resolver.resolve(wordsBefore);
        // Caminho completo até uma pasta (ou nenhum: a raiz): os filhos dela.
        // Um NotFound no meio do caminho não sugere nada — já errou antes.
        if (res.kind == core::CliPathResolution::Kind::Folder) {
            for (const core::CliPathChildEntry &child : res.children) {
                candidates << child.cliPath;
            }
        } else if (res.kind == core::CliPathResolution::Kind::Command) {
            for (const core::Command &c : commands) {
                if (c.id == res.commandId) {
                    candidates << commandArgumentCandidates(c, res.remainingArgs);
                    break;
                }
            }
        }
        if (includeVerbs && wordsBefore.isEmpty()) {
            candidates << topLevelVerbs();
        }
        // `kai kip ...`: o helper do KIP é o único verbo com completar próprio.
        if (includeVerbs && !wordsBefore.isEmpty() && wordsBefore.first() == QStringLiteral("kip")) {
            candidates << kipCandidates(wordsBefore.mid(1));
        }
    }

    QStringList filtered;
    for (const QString &c : std::as_const(candidates)) {
        if (c.startsWith(partial) && !filtered.contains(c)) {
            filtered << c;
        }
    }
    return filtered;
}

QString completionScript(const QString &shell)
{
    // Só ASCII: o PowerShell 5.1 lê um profile sem BOM como ANSI. O Tab chama
    // `kai __complete --cur=<parcial> <palavras...>`: a parcial vai SEMPRE
    // colada em `--cur=`, porque um argumento vazio é descartado por wrappers
    // `kai() { kai.exe $@; }` e pelo PowerShell 5.1.
    if (shell == QStringLiteral("bash")) {
        return QStringLiteral(R"KAI(# kai - Tab completion (bash), managed by Kai
_kai_complete() {
    local bin cur cword
    local -a words
    bin=$(type -P kai 2>/dev/null) || bin=$(type -P kai.exe 2>/dev/null) || return 0
    if declare -F _get_comp_words_by_ref >/dev/null 2>&1; then
        _get_comp_words_by_ref -n =: cur words cword
    else
        cur=${COMP_WORDS[COMP_CWORD]}
        words=("${COMP_WORDS[@]}")
        cword=$COMP_CWORD
    fi
    local IFS=$'\n'
    local -a found
    found=($("$bin" __complete "--cur=$cur" "${words[@]:1:cword-1}" 2>/dev/null | tr -d '\r'))
    # bash replaces only what follows "=": drop the "--option=" prefix.
    if [[ $cur == *=* ]]; then
        local pre=${cur%"${cur#*=}"} i
        for i in "${!found[@]}"; do found[i]=${found[i]#"$pre"}; done
    fi
    COMPREPLY=("${found[@]}")
    # "--option=" is completed without a trailing space so the value can follow.
    if [[ ${#COMPREPLY[@]} -eq 1 && ${COMPREPLY[0]} == *= ]]; then
        compopt -o nospace 2>/dev/null
    fi
}
complete -o default -F _kai_complete kai kai.exe
)KAI");
    }
    if (shell == QStringLiteral("zsh")) {
        return QStringLiteral(R"KAI(# kai - Tab completion (zsh), managed by Kai
_kai() {
    local bin=${commands[kai]:-${commands[kai.exe]}}
    [[ -n $bin ]] || return 1
    local -a all withValue plain
    all=("${(@f)$("$bin" __complete "--cur=${words[CURRENT]}" "${(@)words[2,CURRENT-1]}" 2>/dev/null | tr -d '\r')}")
    all=(${all:#})
    withValue=(${(M)all:#*=})
    plain=(${all:#*=})
    (( ${#plain} )) && compadd -Q -- $plain
    (( ${#withValue} )) && compadd -Q -S '' -- $withValue
}
compdef _kai kai kai.exe
)KAI");
    }
    if (shell == QStringLiteral("powershell") || shell == QStringLiteral("pwsh")) {
        return QStringLiteral(R"KAI(# kai - Tab completion (PowerShell), managed by Kai
Register-ArgumentCompleter -Native -CommandName kai, kai.exe -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)
    $bin = (Get-Command kai.exe, kai -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if (-not $bin) { return }
    $words = @($commandAst.CommandElements | Select-Object -Skip 1 | ForEach-Object { $_.ToString() })
    if ($wordToComplete -ne '' -and $words.Count -gt 0) { $words = @($words | Select-Object -SkipLast 1) }
    & $bin __complete "--cur=$wordToComplete" @words 2>$null | ForEach-Object {
        if ($_) { [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_) }
    }
}
)KAI");
    }
    return QString();
}

void splitCompletionArgs(const QStringList &args, QStringList &words, QString &partial)
{
    // Protocolo atual: `--cur=<parcial> <palavras...>`. O antigo (parcial por
    // último, posicional) continua aceito pra scripts já instalados.
    static const QString curPrefix = QStringLiteral("--cur=");
    words = args.mid(2);
    partial.clear();
    if (!words.isEmpty() && words.first().startsWith(curPrefix)) {
        partial = words.takeFirst().mid(curPrefix.size());
    } else if (!words.isEmpty()) {
        partial = words.takeLast();
    }
}

int runCompletionRequest(const QStringList &args)
{
    // Nada de log no terminal: a saída é lida pelo shell, linha a linha.
    utils::Logger::setConsoleOutputEnabled(false);

    QStringList words;
    QString partial;
    splitCompletionArgs(args, words, partial);

    // Mesma regra de modo do CLI: flags -g/-d (ver stripLeadingCliFlags)
    // antes do caminho; -g -> app; senão, arquivo local no diretório atual
    // -> local; senão -> app.
    CliFlags flags;
    words = stripLeadingCliFlags(QStringList{QString()} + words, flags).mid(1);
    const bool anyFlag = flags.any();
    const bool forceGlobal = flags.global;

    // Ainda nas flags: oferece as longas que faltam.
    if (words.isEmpty() && partial.startsWith(QLatin1Char('-'))) {
        QStringList flagCandidates;
        QStringList lines;
        if (!flags.global) {
            flagCandidates << QStringLiteral("--global");
        }
        if (!flags.detached) {
            flagCandidates << QStringLiteral("--detached");
        }
        if (!flags.notify) {
            flagCandidates << QStringLiteral("--notify");
        }
        if (!flags.window) {
            flagCandidates << QStringLiteral("--window");
        }
        if (!flags.dryRun) {
            flagCandidates << QStringLiteral("--dry-run");
        }
        if (!flags.json) {
            flagCandidates << QStringLiteral("--json");
        }
        for (const QString &f : flagCandidates) {
            if (f.startsWith(partial)) {
                lines << f;
            }
        }
        writeStdout(lines.join(QLatin1Char('\n')) + (lines.isEmpty() ? QString() : QStringLiteral("\n")));
        return 0;
    }
    // `kai kip <verbo> ...`: completa verbos/opções do helper (puro: não lê config).
    if (!anyFlag && !words.isEmpty() && words.first() == QStringLiteral("kip")) {
        QStringList lines;
        for (const QString &c : kipCandidates(words.mid(1))) {
            if (c.startsWith(partial)) {
                lines << c;
            }
        }
        writeStdout(lines.join(QLatin1Char('\n')) + (lines.isEmpty() ? QString() : QStringLiteral("\n")));
        return 0;
    }
    // Depois de um verbo do kai (run, ps...) não há o que completar aqui.
    if (!anyFlag && !words.isEmpty() && topLevelVerbs().contains(words.first())) {
        return 0;
    }

    QVector<core::Folder> folders;
    QVector<core::Command> commands;
    if (forceGlobal || !loadLocalCliTree(QDir::currentPath(), folders, commands)) {
        core::ConfigManager configManager;
        const core::CommandsData data = configManager.loadCommands();
        folders = data.folders;
        commands = data.commands;
    }

    const QStringList candidates = completionCandidates(folders, commands, words, partial, !anyFlag);
    writeStdout(candidates.join(QLatin1Char('\n')) + (candidates.isEmpty() ? QString() : QStringLiteral("\n")));
    return 0;
}

int printCompletionScript(const QStringList &args)
{
    const QString sub = args.value(2);
    if (sub == QStringLiteral("install") || sub == QStringLiteral("uninstall")) {
        return runCompletionInstall(args, sub == QStringLiteral("install"));
    }
    const QString script = completionScript(sub);
    if (script.isEmpty()) {
        QTextStream err(stderr);
        err << utils::tr(QStringLiteral("cli.error.usage.completion")) << "\n";
        return 2;
    }
    writeStdout(script);
    return 0;
}

} // namespace kai::cli
