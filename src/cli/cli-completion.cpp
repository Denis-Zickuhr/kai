#include "cli/cli-completion.h"
#include "cli/cli-local-executor.h"

#include "core/cli-param-binder.h"
#include "core/cli-path-resolver.h"
#include "core/config-manager.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

#include <QDir>
#include <QSet>
#include <QTextStream>

namespace kai::cli {

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
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
    // `tr -d '\r'`: kai.exe (Windows) chamado do WSL escreve CRLF.
    if (shell == QStringLiteral("bash")) {
        return QStringLiteral(R"KAI(# kai — autocomplete (bash). Instale com:  eval "$(kai completion bash)"
_kai_complete() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local IFS=$'\n'
    COMPREPLY=($(kai __complete "${COMP_WORDS[@]:1:COMP_CWORD-1}" "$cur" 2>/dev/null | tr -d '\r'))
    # "--opcao=" completa sem espaço, pro valor vir colado.
    if [[ ${#COMPREPLY[@]} -eq 1 && ${COMPREPLY[0]} == *= ]]; then
        compopt -o nospace 2>/dev/null
    fi
}
complete -o default -F _kai_complete kai
)KAI");
    }
    if (shell == QStringLiteral("zsh")) {
        return QStringLiteral(R"KAI(# kai — autocomplete (zsh). Instale com:  eval "$(kai completion zsh)"
_kai() {
    local -a all withValue plain
    all=("${(@f)$(kai __complete "${(@)words[2,CURRENT-1]}" "${words[CURRENT]}" 2>/dev/null | tr -d '\r')}")
    withValue=(${(M)all:#*=})
    plain=(${all:#*=})
    (( ${#plain} )) && compadd -Q -- $plain
    (( ${#withValue} )) && compadd -Q -S '' -- $withValue
}
compdef _kai kai
)KAI");
    }
    if (shell == QStringLiteral("powershell") || shell == QStringLiteral("pwsh")) {
        return QStringLiteral(R"KAI(# kai — autocomplete (PowerShell). Instale com:  kai completion powershell | Out-String | Invoke-Expression
Register-ArgumentCompleter -Native -CommandName kai, kai.exe -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)
    $words = @($commandAst.CommandElements | Select-Object -Skip 1 | ForEach-Object { $_.ToString() })
    if ($wordToComplete -ne '' -and $words.Count -gt 0) { $words = @($words | Select-Object -SkipLast 1) }
    & kai.exe __complete @words $wordToComplete 2>$null | ForEach-Object {
        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)
    }
}
)KAI");
    }
    return QString();
}

int runCompletionRequest(const QStringList &args)
{
    // Nada de log no terminal: a saída é lida pelo shell, linha a linha.
    utils::Logger::setConsoleOutputEnabled(false);

    QStringList words = args.mid(2);
    const QString partial = words.isEmpty() ? QString() : words.takeLast();

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
                out() << f << '\n';
            }
        }
        out().flush();
        return 0;
    }
    // `kai kip <verbo> ...`: completa verbos/opções do helper (puro: não lê config).
    if (!anyFlag && !words.isEmpty() && words.first() == QStringLiteral("kip")) {
        for (const QString &c : kipCandidates(words.mid(1))) {
            if (c.startsWith(partial)) {
                out() << c << '\n';
            }
        }
        out().flush();
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
    for (const QString &c : candidates) {
        out() << c << '\n';
    }
    out().flush();
    return 0;
}

int printCompletionScript(const QStringList &args)
{
    const QString shell = args.value(2);
    const QString script = completionScript(shell);
    if (script.isEmpty()) {
        QTextStream err(stderr);
        err << utils::tr(QStringLiteral("cli.error.usage.completion")) << "\n";
        return 2;
    }
    out() << script;
    out().flush();
    return 0;
}

} // namespace kai::cli
