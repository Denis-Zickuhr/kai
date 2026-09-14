#include "engine/execution-pipeline.h"
#include "engine/output-responder-matcher.h"

#include <QProcess>

#include <QTimer>
#include <QProcessEnvironment>
#include <QSet>
#include <QRegularExpression>
#include <QUuid>

#include "utils/logger.h"
#include "utils/translation-manager.h"
#include "utils/path-format.h"

namespace kai::engine {

namespace {
constexpr const char *kLogTag = "ExecutionPipeline";
// Sentinela único que separa a saída real do hook do dump de ambiente
// anexado para captura (T9). Escolhido para ser improvável em qualquer
// saída legítima.
constexpr const char *kEnvSentinel = "__KAI_ENV_CAPTURE_7f3a91__";

// True se `name` é um identificador shell válido ([A-Za-z_][A-Za-z0-9_]*).
// Nomes com ponto ou símbolos (ex: "c.value", vindos de campos de coleção)
// NÃO são exportáveis via `export NOME=...` (o bash aborta com "not a valid
// identifier"). Usado para pular esses nomes no prefixo de export.
bool isValidShellIdentifier(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    const QChar first = name.at(0);
    if (!(first.isLetter() || first == QLatin1Char('_'))) {
        return false;
    }
    for (const QChar &c : name) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_'))) {
            return false;
        }
    }
    return true;
}

// Conversão de path Windows -> WSL/POSIX (bug real reportado: ao importar
// um projeto pelo Windows, o PROJECT_PATH/working_dir vira um caminho
// Windows que o bash do WSL não navega — o `cd` falha e o comando roda no
// CWD errado). Extraída pra utils::toPosixPath (compartilhada com o
// file-picker de parâmetros — ver Parameter::filePathFormat).

// Resolve o sabor EFETIVO do alvo: se Auto, detecta pelo template.
core::ShellFlavor effectiveShellFlavor(const core::TerminalProfile &t)
{
    if (t.shell != core::ShellFlavor::Auto) {
        return t.shell;
    }
    const QString lo = t.commandTemplate.toLower();
    if (lo.contains(QLatin1String("powershell")) || lo.contains(QLatin1String("pwsh"))) {
        return core::ShellFlavor::PowerShell;
    }
    if ((lo.contains(QLatin1String("cmd.exe")) || lo.startsWith(QLatin1String("cmd ")))
        && !lo.contains(QLatin1String("powershell")) && !lo.contains(QLatin1String("pwsh"))) {
        return core::ShellFlavor::Cmd;
    }
    return core::ShellFlavor::Posix;
}

// Monta o prefixo de env + o wrapper de working-dir na SINTAXE do sabor.
// Corrige o bug reportado: alvo PowerShell recebia `export K='V'` (bash) e
// abortava com "O termo 'export' não é reconhecido".
//   - Posix:      export K='V'          / cd '<dir>' && { <cmd>\n}
//   - PowerShell: $env:K='V'            / Set-Location -LiteralPath '<dir>'
//   - Cmd:        set "K=V"             / cd /d "<dir>"
// `remotePrefix` (set -m / echo $$) só é passado para Posix (kill remoto WSL).
QString buildTargetedCommand(core::ShellFlavor flavor,
                             const QMap<QString, QString> &resolvedEnv,
                             const QString &interpolatedCommand,
                             const QString &interpolatedWorkingDir,
                             const QString &remotePrefixPosix)
{
    QString prefix;
    QString body = interpolatedCommand;

    if (flavor == core::ShellFlavor::PowerShell) {
        for (auto it = resolvedEnv.constBegin(); it != resolvedEnv.constEnd(); ++it) {
            if (!isValidShellIdentifier(it.key())) {
                continue;
            }
            QString v = it.value();
            v.replace(QStringLiteral("'"), QStringLiteral("''")); // PS: aspa simples dobrada
            prefix += QStringLiteral("$env:%1='%2'\n").arg(it.key(), v);
        }
        if (!interpolatedWorkingDir.isEmpty()) {
            QString dir = interpolatedWorkingDir;
            dir.replace(QStringLiteral("'"), QStringLiteral("''"));
            body = QStringLiteral("Set-Location -LiteralPath '%1'\n%2").arg(dir, interpolatedCommand);
        }
        return prefix + body;
    }

    if (flavor == core::ShellFlavor::Cmd) {
        for (auto it = resolvedEnv.constBegin(); it != resolvedEnv.constEnd(); ++it) {
            if (!isValidShellIdentifier(it.key())) {
                continue;
            }
            QString v = it.value();
            v.replace(QStringLiteral("\""), QStringLiteral("\"\"")); // cmd: aspa dupla dobrada
            prefix += QStringLiteral("set \"%1=%2\"\n").arg(it.key(), v);
        }
        if (!interpolatedWorkingDir.isEmpty()) {
            body = QStringLiteral("cd /d \"%1\"\n%2").arg(interpolatedWorkingDir, interpolatedCommand);
        }
        return prefix + body;
    }

    // Posix (bash/sh/WSL/docker exec bash).
    prefix = remotePrefixPosix;
    for (auto it = resolvedEnv.constBegin(); it != resolvedEnv.constEnd(); ++it) {
        if (!isValidShellIdentifier(it.key())) {
            continue;
        }
        QString v = it.value();
        v.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        prefix += QStringLiteral("export %1='%2'\n").arg(it.key(), v);
    }
    if (!interpolatedWorkingDir.isEmpty()) {
        QString dir = utils::toPosixPath(interpolatedWorkingDir);
        dir.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        body = QStringLiteral("cd '%1' && { %2\n}").arg(dir, interpolatedCommand);
    }
    return prefix + body;
}
}

ExecutionPipeline::ExecutionPipeline(QObject *parent)
    : QObject(parent)
{
}

void ExecutionPipeline::setHookTimeoutMs(int ms)
{
    m_hookTimeoutMs = ms;
}

void ExecutionPipeline::setTerminalProfiles(const QVector<core::TerminalProfile> &targets)
{
    m_terminalProfiles = targets;
}

void ExecutionPipeline::setFolders(const QVector<core::Folder> &folders)
{
    m_folders = folders;
}

QString ExecutionPipeline::wrapForEnvCapture(const QString &interpolatedCommand, core::ShellFlavor flavor) const
{
    // Roda o comando e, em seguida (mesmo shell), emite o sentinela em nova
    // linha e o ambiente completo (uma variável por linha), na SINTAXE do
    // sabor de shell que vai de fato executar isto — mesmo bug já corrigido
    // uma vez em buildTargetedCommand (export/set -m em bash quebrava sob
    // PowerShell), nunca aplicado aqui: esta função sempre usou sintaxe
    // POSIX (`echo '...'` + `env`), então "Export variables" simplesmente
    // não funcionava fora de um alvo Posix/WSL - nem sob "shell normal"
    // (cmd.exe, o padrão sem alvo de terminal no Windows) nem sob PowerShell
    // (`env` não existe em nenhum dos dois; aspas simples são literais em
    // ambos, não delimitador de string).
    const QString sentinel = QString::fromLatin1(kEnvSentinel);
    if (flavor == core::ShellFlavor::PowerShell) {
        // Get-ChildItem Env: dá um objeto por variável; formata cada um
        // como NAME=VALUE (mesmo formato que ingestCapturedEnv já espera).
        return QStringLiteral(
            "%1\nWrite-Output '%2'\nGet-ChildItem Env: | ForEach-Object { \"$($_.Name)=$($_.Value)\" }")
            .arg(interpolatedCommand, sentinel);
    }
    if (flavor == core::ShellFlavor::Cmd) {
        // `set` (sem argumento) do cmd.exe já dumpa TODAS as variáveis como
        // NAME=VALUE, uma por linha - equivalente exato do `env` POSIX.
        // Sentinela sem aspas: aspas simples são caractere literal no cmd.
        return QStringLiteral("%1\necho %2\nset").arg(interpolatedCommand, sentinel);
    }
    // Posix (bash/sh/WSL/docker exec bash) - comportamento original.
    return QStringLiteral("%1\necho '%2'\nenv").arg(interpolatedCommand, sentinel);
}

void ExecutionPipeline::ingestCapturedEnv(const QString &rawOutput, const QString &scopeKey,
                                           const QVector<core::DeclaredEnvVar> &declaredVars)
{
    if (!m_envManager) {
        return;
    }
    // LISTA BRANCA obrigatória (bug real reportado, com risco de segurança:
    // "exportar esta exportando automaticamente envs do OS, essas envs
    // quebram o funcionamento se exportadas... preciso apenas exportar as
    // envs ADVERSAS e incomuns"). A versão antiga capturava TUDO que fosse
    // novo/diferente do ambiente herdado, com uma lista de ruído hardcoded
    // que nunca ia prever toda variável de sistema/distro/WSL possível —
    // inevitavelmente vazava alguma coisa perigosa mais cedo ou mais tarde.
    // Agora só os nomes DECLARADOS no comando (Command::declaredEnvVars,
    // "algo parecido" com os extratores HTTP) são considerados; nada além
    // deles, declarado ou não. Sem NENHUM nome declarado, não há o que
    // procurar — nem vale a pena abrir o dump.
    if (declaredVars.isEmpty()) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Captura de env: 'Exportar variáveis' ligado mas nenhuma variável declarada; nada capturado."));
        return;
    }

    const int sentinelPos = rawOutput.indexOf(QString::fromLatin1(kEnvSentinel));
    if (sentinelPos < 0) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Captura de env: sentinela não encontrado na saída do hook; nada capturado."));
        return;
    }

    const QString envDump = rawOutput.mid(sentinelPos + static_cast<int>(qstrlen(kEnvSentinel)));
    // Uma variável por linha (KEY=VALUE). Remove \r de terminais que
    // convertem quebras de linha.
    const QStringList lines = envDump.split(QChar(u'\n'), Qt::SkipEmptyParts);

    QMap<QString, QString> foundValues; // nome -> valor, só os que realmente apareceram no dump
    for (const QString &rawLine : lines) {
        QString line = rawLine;
        if (line.endsWith(QChar(u'\r'))) {
            line.chop(1);
        }
        const int eq = line.indexOf(QChar(u'='));
        if (eq <= 0) {
            continue;
        }
        foundValues.insert(line.left(eq), line.mid(eq + 1));
    }

    int captured = 0;
    for (const core::DeclaredEnvVar &declared : declaredVars) {
        const QString name = declared.name.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        // Nome declarado mas o processo NÃO setou: vira variável dinâmica
        // VAZIA em vez de ficar de fora (pedido explícito: "se não ficam
        // vazias, até pra ajudar em debug" — o inspetor de variáveis deixa
        // óbvio que aquele nome era esperado mas nunca veio).
        const QString value = foundValues.value(name);
        // ESCOPO por variável declarada (mesma semântica de
        // EnvExtractor::scope — ver comentário lá): "global" força Global
        // independente do projeto atual; "project" usa o escopo capturado
        // no INÍCIO da execução (ver comentário no header/chamador — bug
        // real: trocar de projeto enquanto o comando ainda rodava jogava a
        // variável no projeto ERRADO).
        const QString targetScope = declared.scope == QStringLiteral("global") ? QString() : scopeKey;
        m_envManager->setDynamicVarInScope(targetScope, name, value);
        if (declared.persist) {
            emit dynamicVarPersistRequested(targetScope, name, value);
        }
        ++captured;
    }

    utils::Logger::info(kLogTag,
        QStringLiteral("Captura de env do hook: %1 variável(is) declarada(s) processada(s).").arg(captured));
}

QString ExecutionPipeline::effectiveTerminalProfileName(const core::Command &command) const
{
    // RESOLUÇÃO HIERÁRQUICA do perfil (feedback do usuário: pastas têm
    // perfil; comando/subpasta podem "Herdar do pai"). Parte do nível mais
    // ESPECÍFICO e sobe até achar um valor concreto:
    //   comando -> pasta do comando -> pasta pai -> ... -> raiz.
    // "@parent" (kInheritTerminalTarget) = continua subindo. Um nome ou
    // vazio ("local") ENCERRA a subida (vazio = decisão explícita de rodar
    // local naquele nível, ainda sujeita ao default global abaixo). Guard
    // de profundidade contra ciclos em dados corrompidos.
    const QString inherit = QString::fromLatin1(core::kInheritTerminalTarget);
    QString resolvedTarget = command.terminalTarget;
    if (resolvedTarget == inherit) {
        resolvedTarget.clear(); // se a cadeia toda herdar, cai no default global
        auto folderById = [this](const QString &id) -> const core::Folder * {
            for (const core::Folder &f : m_folders) {
                if (f.id == id) return &f;
            }
            return nullptr;
        };
        const core::Folder *cursor = folderById(command.folderId);
        for (int guard = 0; guard < 64 && cursor; ++guard) {
            if (cursor->terminalTarget != inherit) {
                resolvedTarget = cursor->terminalTarget; // valor concreto (nome ou vazio)
                break;
            }
            if (!cursor->parentId.has_value()) {
                break; // chegou à raiz herdando: cai no default global
            }
            cursor = folderById(cursor->parentId.value());
        }
    }

    // Só retorna um alvo que REALMENTE existe na lista (com template). Se o
    // comando referencia um alvo inexistente (ex: removido), NÃO retornamos
    // esse nome — cairíamos num estado onde o start() omite o working_dir
    // (achando que há alvo) mas o applyTerminalProfile não injeta o cd
    // (alvo não encontrado), perdendo o working_dir silenciosamente
    // (achado da auditoria). Nesse caso, cai para o alvo PADRÃO ou local.
    auto exists = [this](const QString &name) {
        for (const core::TerminalProfile &t : m_terminalProfiles) {
            if (t.name == name && !t.commandTemplate.isEmpty()) return true;
        }
        return false;
    };
    if (!resolvedTarget.isEmpty() && exists(resolvedTarget)) {
        return resolvedTarget;
    }
    // Sem alvo (ou alvo inexistente): usa o marcado como PADRÃO (isDefault).
    for (const core::TerminalProfile &t : m_terminalProfiles) {
        if (t.isDefault && !t.commandTemplate.isEmpty()) {
            return t.name;
        }
    }
    // NENHUM marcado como padrão, mas existe EXATAMENTE UM alvo válido: ele é
    // obviamente o pretendido. Causa raiz de processos fantasmas reportada
    // ("maga env prod casaferrari" seguia vivo no WSL após o Stop): com o
    // único alvo 'WSL' sem is_default e o comando sem terminal_target, este
    // método devolvia vazio, o pipeline não gerava o pid-file remoto e o kill
    // remoto NUNCA era armado — no Windows sobrava só o taskkill, que não
    // alcança processos Linux (eles não são filhos Windows do wsl.exe).
    int validCount = 0;
    QString onlyName;
    for (const core::TerminalProfile &t : m_terminalProfiles) {
        if (!t.commandTemplate.isEmpty()) {
            ++validCount;
            onlyName = t.name;
        }
    }
    if (validCount == 1) {
        return onlyName;
    }
    return QString();
}

QString ExecutionPipeline::wrapScriptInTarget(const core::TerminalProfile &target, const QString &script) const
{
    QString wrapped = target.commandTemplate;
    if (wrapped.contains(QStringLiteral("{{command_b64}}"))) {
        wrapped.replace(QStringLiteral("{{command_b64}}"),
                        QString::fromLatin1(script.toUtf8().toBase64()));
        return wrapped;
    }
    const int ph = wrapped.indexOf(QStringLiteral("{{command}}"));
    if (ph >= 0) {
        QString safe = script;
        safe.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        const QChar before = ph > 0 ? wrapped.at(ph - 1) : QChar();
        const int afterIdx = ph + 11;
        const QChar after = afterIdx < wrapped.size() ? wrapped.at(afterIdx) : QChar();
        const bool alreadyQuoted =
            (before == QLatin1Char('\'') && after == QLatin1Char('\'')) ||
            (before == QLatin1Char('"')  && after == QLatin1Char('"'));
        if (!alreadyQuoted) {
            safe = QStringLiteral("'") + safe + QStringLiteral("'");
        }
        wrapped.replace(QStringLiteral("{{command}}"), safe);
        return wrapped;
    }
    return QString(); // template sem placeholder: sem como injetar
}

QString ExecutionPipeline::buildRemoteKillCommandLine(const QString &targetName,
                                                      const QString &remoteRunId) const
{
    if (targetName.isEmpty() || remoteRunId.isEmpty()) {
        return QString();
    }
    for (const core::TerminalProfile &t : m_terminalProfiles) {
        if (t.name != targetName || t.commandTemplate.isEmpty()) {
            continue;
        }
        // Mata o GRUPO de processos do lado remoto: le o PID gravado pelo
        // shell remoto, descobre o PGID e manda TERM e depois KILL no grupo
        // (pega node/nodemon/filhos). Remove o arquivo no fim.
        const QString pidFile = QStringLiteral("/tmp/kai-%1.pid").arg(remoteRunId);
        // Mata o processo remoto de forma EXAUSTIVA. Três camadas, porque cada
        // uma sozinha deixa fantasmas:
        //  (1) Árvore de descendentes por PPID: pega netos/bisnetos mesmo que
        //      tenham chamado setsid() e escapado do process group (era o furo
        //      do kill -PGID puro: `maga` sobe filhos que se desgarram).
        //  (2) Process group (PGID): pega irmãos criados pelo shell.
        //  (3) O próprio PID, como último recurso.
        // Em cada camada: TERM primeiro (permite shutdown limpo), KILL depois.
        const QString script = QStringLiteral(
            "P=$(cat '%1' 2>/dev/null); "
            "[ -z \"$P\" ] && exit 0; "
            // coleta recursiva de descendentes (largura, sem depender de pstree)
            "collect() { local p=$1; local kids; "
            "kids=$(ps -o pid= --ppid \"$p\" 2>/dev/null); "
            "for k in $kids; do echo \"$k\"; collect \"$k\"; done; }; "
            "TREE=$(collect \"$P\"); "
            "PG=$(ps -o pgid= -p \"$P\" 2>/dev/null | tr -d ' '); "
            // SEGURANÇA: só mata o GRUPO se o processo for o LÍDER dele
            // (PGID == PID). Verificado empiricamente que `set -m` NÃO garante
            // liderança quando o shell roda sem terminal de controle: o PGID
            // vira o do processo PAI, e matar esse grupo atingiria processos
            // alheios (inclusive o relay do WSL). Sem liderança, confiamos na
            // árvore por PPID, que é precisa.
            "[ \"$PG\" != \"$P\" ] && PG=\"\"; "
            // TERM: descendentes (de baixo para cima), grupo, e o próprio
            "for t in $TREE; do kill -TERM \"$t\" 2>/dev/null; done; "
            "[ -n \"$PG\" ] && kill -TERM -\"$PG\" 2>/dev/null; "
            "kill -TERM \"$P\" 2>/dev/null; "
            "sleep 1; "
            // KILL no que sobrou
            "for t in $TREE; do kill -KILL \"$t\" 2>/dev/null; done; "
            "[ -n \"$PG\" ] && kill -KILL -\"$PG\" 2>/dev/null; "
            "kill -KILL \"$P\" 2>/dev/null; "
            "rm -f '%1' 2>/dev/null; exit 0").arg(pidFile);
        return wrapScriptInTarget(t, script);
    }
    return QString();
}

QString ExecutionPipeline::applyTerminalProfile(const core::Command &command,
                                               const QString &interpolatedCommand,
                                               const QString &interpolatedWorkingDir,
                                               const QString &remoteRunId) const
{
    const QString targetName = effectiveTerminalProfileName(command);
    if (targetName.isEmpty()) {
        return interpolatedCommand;
    }
    for (const core::TerminalProfile &target : m_terminalProfiles) {
        if (target.name == targetName && !target.commandTemplate.isEmpty()) {
            QString wrapped = target.commandTemplate;
            const core::ShellFlavor flavor = effectiveShellFlavor(target);

            // Prefixo de env + working-dir na SINTAXE do sabor do alvo. O bug
            // reportado era gerar `export`/`set -m` (bash) para um alvo
            // PowerShell. Agora cada sabor tem a sua sintaxe.
            // O prefixo de kill remoto (set -m / echo $$) só faz sentido em
            // Posix (WSL): é onde o processo Linux não é filho Windows do
            // wsl.exe e precisa de um kill do lado de dentro. Para PowerShell/
            // Cmd o remoteRunId nem é gerado (ver run()), mas gateamos aqui
            // por segurança.
            QString remotePrefixPosix;
            if (!remoteRunId.isEmpty() && flavor == core::ShellFlavor::Posix) {
                // `set -m` (job control) faz o shell virar LÍDER do próprio
                // process group, para o kill remoto acertar a árvore certa.
                remotePrefixPosix += QStringLiteral("set -m 2>/dev/null || true\n");
                remotePrefixPosix += QStringLiteral("echo $$ > '/tmp/kai-%1.pid' 2>/dev/null\n").arg(remoteRunId);
            }

            const QMap<QString, QString> resolvedEnv = m_envManager->resolvedEnv();
            const QString effectiveCommand = buildTargetedCommand(
                flavor, resolvedEnv, interpolatedCommand, interpolatedWorkingDir, remotePrefixPosix);

            // Injeção À PROVA DE CRASH via base64 (comando pode ter aspas/
            // quebras de linha). O template pode usar {{command_b64}}.
            if (wrapped.contains(QStringLiteral("{{command_b64}}"))) {
                const QString b64 = QString::fromLatin1(effectiveCommand.toUtf8().toBase64());
                wrapped.replace(QStringLiteral("{{command_b64}}"), b64);
            }
            // {{command}}: monta o comando de forma À PROVA DE ASPAS/ESPAÇOS.
            // Escapa aspas simples (idiom '\'') E — bug real reportado
            // ("maga env prod X" chegava só como "maga"; read -p perdia os
            // args) — ENVOLVE o comando em aspas simples QUANDO o template
            // não já o cerca com aspas. Muitos templates (ex: o padrão do
            // usuário: "... bash -lc {{command}}") deixam o placeholder
            // "solto", fazendo o bash tratar só a 1ª palavra como script e o
            // resto como $0,$1,... — agora sempre vira um único argumento.
            const bool hadB64 = target.commandTemplate.contains(QStringLiteral("{{command_b64}}"));
            const int ph = wrapped.indexOf(QStringLiteral("{{command}}"));
            if (!hadB64 && ph < 0) {
                // Template SEM nenhum placeholder: não há como injetar o
                // comando — antes retornava o template cru, executando-o em
                // vez do comando (COMANDO PERDIDO em silêncio). Agora avisa
                // e executa LOCAL, preservando o comando do usuário.
                utils::Logger::error(kLogTag,
                    QStringLiteral("Alvo de terminal '%1' não tem {{command}} nem {{command_b64}} no template; "
                                   "executando o comando LOCALMENTE para não perdê-lo.").arg(target.name));
                return interpolatedCommand;
            }
            if (ph >= 0) {
                if (flavor == core::ShellFlavor::Posix) {
                    QString safe = effectiveCommand;
                    safe.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
                    // Já está entre aspas (simples ou duplas) no template?
                    const QChar before = ph > 0 ? wrapped.at(ph - 1) : QChar();
                    const int afterIdx = ph + 11; // len("{{command}}") == 11
                    const QChar after = afterIdx < wrapped.size() ? wrapped.at(afterIdx) : QChar();
                    const bool alreadyQuoted =
                        (before == QLatin1Char('\'') && after == QLatin1Char('\'')) ||
                        (before == QLatin1Char('"')  && after == QLatin1Char('"'));
                    if (!alreadyQuoted) {
                        safe = QStringLiteral("'") + safe + QStringLiteral("'");
                    }
                    wrapped.replace(QStringLiteral("{{command}}"), safe);
                } else {
                    // PowerShell/Cmd: o escaping POSIX ('\'' ) não se aplica.
                    // Esses sabores devem usar {{command_b64}} (à prova de
                    // aspas). Se usarem {{command}} cru, substituímos sem
                    // reescapar — o autor do template é responsável pelo
                    // quoting da sua sintaxe.
                    wrapped.replace(QStringLiteral("{{command}}"), effectiveCommand);
                }
            }

            utils::Logger::info(kLogTag,
                QStringLiteral("Comando '%1' via alvo de terminal '%2'%3.")
                    .arg(command.id, target.name,
                         interpolatedWorkingDir.isEmpty() ? QString()
                             : QStringLiteral(" (cd %1)").arg(interpolatedWorkingDir)));
            return wrapped;
        }
    }
    // Alvo referenciado mas não encontrado: executa local, sem quebrar.
    utils::Logger::warning(kLogTag,
        QStringLiteral("Alvo de terminal '%1' do comando '%2' não encontrado; executando local.")
            .arg(command.terminalTarget, command.id));
    return interpolatedCommand;
}

ProcessRunner *ExecutionPipeline::activeProcessRunner() const
{
    return runnerFor(m_activeRunnerCommandId);
}

ProcessRunner *ExecutionPipeline::runnerFor(const QString &commandId) const
{
    if (commandId.isEmpty()) {
        return nullptr;
    }
    const auto it = m_runners.find(commandId);
    if (it == m_runners.end() || !it->second) {
        return nullptr;
    }
    return it->second->isRunning() ? it->second.get() : nullptr;
}

QStringList ExecutionPipeline::runningCommandIds() const
{
    QStringList ids;
    for (const auto &entry : m_runners) {
        if (entry.second && entry.second->isRunning()) {
            ids << entry.first;
        }
    }
    return ids;
}

std::unique_ptr<ProcessRunner> ExecutionPipeline::releaseRunnerFor(const QString &commandId)
{
    const auto it = m_runners.find(commandId);
    if (it == m_runners.end()) {
        return nullptr;
    }
    std::unique_ptr<ProcessRunner> released = std::move(it->second);
    m_runners.erase(it);
    return released;
}

std::unique_ptr<ProcessRunner> ExecutionPipeline::releaseActiveProcessRunner()
{
    return releaseRunnerFor(m_activeRunnerCommandId);
}

void ExecutionPipeline::run(const core::Command &command,
                             const QMap<QString, core::Command> &allCommands,
                             core::EnvironmentManager &envManager)
{
    // Nova execução DESTE comando: libera o cleanup para disparar outra vez.
    m_cleanupFiredFor.remove(command.id);

    m_allCommands = &allCommands;
    m_envManager = &envManager;
    m_mainCommand = command;
    // Snapshot dos ids desta cadeia (usado por abort() para não encerrar
    // runners de OUTRAS execuções em paralelo — ver comentário em abort()).
    m_currentChainCommandIds.clear();
    m_currentChainCommandIds.insert(command.id);
    for (const QString &hookId : command.hooks.pre) {
        m_currentChainCommandIds.insert(hookId);
    }
    for (const QString &hookId : command.hooks.post) {
        m_currentChainCommandIds.insert(hookId);
    }

    utils::Logger::info(kLogTag, QStringLiteral("Iniciando pipeline para comando '%1'.").arg(command.id));
    utils::Logger::info(kLogTag,
        QStringLiteral("Hooks do comando '%1': pre=[%2] post=[%3]")
            .arg(command.id, command.hooks.pre.join(QStringLiteral(", ")),
                 command.hooks.post.join(QStringLiteral(", "))));
    emit stageStarted(command.id, PipelineStage::PreHooks);

    runQueue(command.hooks.pre, PipelineStage::PreHooks, [this]() {
        emit stageStarted(m_mainCommand.id, PipelineStage::MainCommand);

        runSingleCommand(m_mainCommand, [this](bool success, const QString &errorMessage) {
            if (!success) {
                abort(PipelineStage::MainCommand, m_mainCommand.id, errorMessage);
                return;
            }

            emit stageStarted(m_mainCommand.id, PipelineStage::PostHooks);
            runQueue(m_mainCommand.hooks.post, PipelineStage::PostHooks, [this]() {
                PipelineResult result;
                result.success = true;
                utils::Logger::info(kLogTag,
                    QStringLiteral("Pipeline concluído com sucesso para '%1'.").arg(m_mainCommand.id));
                // CLEANUP sempre roda ao terminar, inclusive no sucesso.
                if (m_allCommands) {
                    runCleanupHooks(m_mainCommand, *m_allCommands);
                }
                emit pipelineFinished(result);
            });
        });
    });
}

void ExecutionPipeline::runQueue(QStringList queue, PipelineStage stage, std::function<void()> onAllSucceeded)
{
    if (queue.isEmpty()) {
        onAllSucceeded();
        return;
    }

    const QString commandId = queue.takeFirst();
    const auto it = m_allCommands->constFind(commandId);

    if (it == m_allCommands->constEnd()) {
        abort(stage, commandId, QStringLiteral("Hook referencia um comando inexistente: '%1'.").arg(commandId));
        return;
    }

    const core::Command hookCommand = it.value();

    runSingleCommand(hookCommand, [this, queue, stage, onAllSucceeded, commandId](bool success, const QString &errorMessage) mutable {
        if (!success) {
            // Falha de Pre-Hook aborta o pipeline inteiro e nunca dispara o
            // comando principal.
            abort(stage, commandId, errorMessage);
            return;
        }
        runQueue(queue, stage, onAllSucceeded);
    });
}

namespace {
// Rótulo de uma condição para log/debug: o NOME dado pelo usuário, ou (sem
// nome) um resumo automático "left op right" — nunca fica em branco na
// mensagem de pulo/falha.
QString conditionLabel(const core::ExecutionCondition &c)
{
    if (!c.name.trimmed().isEmpty()) {
        return c.name;
    }
    return QStringLiteral("%1 %2 %3").arg(c.left, c.op, c.right).trimmed();
}
} // namespace

ExecutionPipeline::ConditionEvalResult ExecutionPipeline::evaluateConditions(const core::Command &command) const
{
    ConditionEvalResult result;
    if (command.executionConditions.isEmpty() || !m_envManager) {
        return result; // passed = true (sem guarda = sempre roda)
    }
    const bool isAnd = command.conditionCombinator != QStringLiteral("or");
    if (isAnd) {
        // E: a PRIMEIRA condição HABILITADA que falhar já é a responsável
        // pelo pulo — as demais nem precisam ser avaliadas. Uma condição
        // desabilitada (ExecutionCondition::enabled == false — feedback do
        // usuário: "a flag de habilitar/desabilitar era por condição, não
        // pelo total") é IGNORADA, como se não estivesse na lista.
        for (const core::ExecutionCondition &c : command.executionConditions) {
            if (!c.enabled) {
                continue;
            }
            if (!m_envManager->evaluateCondition(c.left, c.op, c.right)) {
                result.passed = false;
                result.decidingConditionLabel = conditionLabel(c);
                return result;
            }
        }
        return result; // todas as habilitadas passaram (ou nenhuma estava habilitada)
    }
    // OU: só falha se NENHUMA condição HABILITADA passar — a mensagem então
    // lista todas as habilitadas (não dá pra apontar "a" responsável, já
    // que o pulo é resultado do conjunto).
    QStringList labels;
    bool anyEnabled = false;
    for (const core::ExecutionCondition &c : command.executionConditions) {
        if (!c.enabled) {
            continue;
        }
        anyEnabled = true;
        if (m_envManager->evaluateCondition(c.left, c.op, c.right)) {
            return result; // passed = true
        }
        labels << conditionLabel(c);
    }
    if (!anyEnabled) {
        return result; // nenhuma condição habilitada = sem guarda, sempre roda
    }
    result.passed = false;
    result.decidingConditionLabel = labels.join(QStringLiteral(", "));
    return result;
}

void ExecutionPipeline::runSingleCommand(const core::Command &command, std::function<void(bool, const QString &)> onDone)
{
    // CONDIÇÃO DE EXECUÇÃO (core::Command::executionConditions — feedback do
    // usuário): checada aqui pois runSingleCommand é o ÚNICO pedágio por
    // onde passam o comando principal, pre-hooks E post-hooks (ver
    // Hooks::pre/post) — uma checagem cobre os três, sem duplicar. Cleanup
    // hooks (execução destacada, sobrevive ao pipeline) são checados
    // separadamente em runCleanupHooks.
    const ConditionEvalResult conditionResult = evaluateConditions(command);
    if (!conditionResult.passed) {
        const QString commandLabel = command.name.isEmpty() ? command.id : command.name;
        emit logMessage(command.id,
            utils::tr(QStringLiteral("execution_pipeline.condition_skipped"))
                .arg(commandLabel, conditionResult.decidingConditionLabel) + QStringLiteral("\n"),
            false);
        if (command.conditionSkipBehavior == QStringLiteral("failure")) {
            onDone(false, utils::tr(QStringLiteral("execution_pipeline.condition_failed"))
                .arg(commandLabel, conditionResult.decidingConditionLabel));
        } else {
            // "success" (padrão): sem isto a UI não tinha como distinguir
            // isto de uma execução real — via httpResultReady (ver
            // comentário do sinal) só pra HTTP, que é quem tem abas
            // Resposta/Headers/Request que ficavam com dado velho em cache.
            if (command.type == core::CommandType::Http) {
                emit commandSkippedByCondition(command.id, conditionResult.decidingConditionLabel);
            }
            onDone(true, QString());
        }
        return;
    }

    if (command.type == core::CommandType::Http) {
        if (!command.httpConfig.has_value()) {
            onDone(false, QStringLiteral("Comando HTTP '%1' sem http_config.").arg(command.id));
            return;
        }

        auto runner = std::make_unique<HttpRunner>();
        HttpRunner *rawRunner = runner.get();
        m_activeHttpRunner = std::move(runner);

        connect(rawRunner, &HttpRunner::logMessage, this, [this, id = command.id](const QString &text, bool isError) {
            emit logMessage(id, text, isError);
        });
        connect(rawRunner, &HttpRunner::dynamicVarPersistRequested, this,
            [this](const QString &scopeKey, const QString &name, const QString &value) {
            emit dynamicVarPersistRequested(scopeKey, name, value);
        });

        connect(rawRunner, &HttpRunner::finished, this,
            [this, onDone, id = command.id](const HttpResult &result) {
        // SAÍDA V2: entrega o resultado estruturado para as abas da interface.
        emit httpResultReady(id, result);
            // HTTP >= 400 é tratado como erro de pipeline.
            onDone(result.success, result.success ? QString() : result.errorMessage);
        });

        rawRunner->execute(command.httpConfig.value(), *m_envManager);
        return;
    }

    // Shell.
    // REGISTRY por commandId: se este MESMO comando já tem um runner vivo,
    // encerra-o antes (re-execução do mesmo comando). Runners de OUTROS
    // comandos são preservados — antes um slot único fazia o 2º comando
    // DESTRUIR o runner do 1º, matando o Stop/entrada e deixando fantasma.
    if (auto existing = m_runners.find(command.id); existing != m_runners.end()) {
        if (existing->second && existing->second->isRunning()) {
            utils::Logger::info(kLogTag,
                QStringLiteral("Re-execução de '%1': encerrando o runner anterior.").arg(command.id));
            existing->second->stop();
        }
        // Não destrói aqui (o dtor é bloqueante): deixa o finished antigo
        // limpar. Substituímos a entrada abaixo via insert.
        m_runners.erase(existing);
    }
    auto runner = std::make_unique<ProcessRunner>();
    ProcessRunner *rawRunner = runner.get();
    m_runners.insert_or_assign(command.id, std::move(runner));
    m_activeRunnerCommandId = command.id; // dono do runner atual (compat)

    // Captura de ambiente (T9): só faz sentido em hook shell NÃO-background
    // (o processo precisa terminar para lermos o ambiente resultante).
    const bool captureEnv = command.captureEnv && !command.isBackground;
    m_captureEnvActive = captureEnv;
    m_captureBuffer.clear();
    // Escopo de variáveis dinâmicas CAPTURADO agora (síncrono, início da
    // execução) — ver comentário em ingestCapturedEnv/header: nunca lido de
    // novo lá dentro, que roda no callback `finished`, possivelmente bem
    // depois de o escopo AMBIENTE (EnvironmentManager::m_currentDynamicScope)
    // já ter mudado por causa de outra seleção do usuário.
    const QString captureScopeKey = m_envManager ? m_envManager->currentDynamicVarScope() : QString();

    // Captura de env não precisa de PTY (não é interativo) e o PTY
    // atrapalharia o parse (ecoa o comando e converte quebras de linha),
    // então usamos o QProcess normal nesse caso, cujo stdout preserva as
    // linhas de `env` de forma limpa.
    if (captureEnv) {
        rawRunner->setUsePty(false);
    }

    // TTY controlável POR ALVO DE TERMINAL (feedback do usuário: uns
    // comandos EXIGEM tty — ex: 'maga zord script', docker -it, programas
    // que checam isatty — e falham com "input device is not a TTY" sem ele;
    // outros QUEBRAM com tty — ex: read/&&/aspas sob ConPTY frágil). A
    // escolha agora é do ALVO: cada TerminalProfile tem uma flag usePty.
    //   - usePty=true  => ConPTY (Windows) / forkpty (Unix): terminal real.
    //   - usePty=false => QProcess robusto (lifecycle/kill confiáveis, sem
    //                     garbling), ideal p/ &&, aspas, pipes.
    // Sem alvo de terminal, mantém o padrão (ConPTY/forkpty), que dá o
    // comportamento de terminal para comandos locais.
    const QString effTarget = effectiveTerminalProfileName(command);
    QString remoteRunId; // id do arquivo de PID remoto (kill do lado WSL)
    // Sabor efetivo desta execução, usado por wrapForEnvCapture (abaixo) pra
    // gerar a sintaxe certa de dump de ambiente. SEM alvo de terminal, o
    // comando roda no shell padrão da PLATAFORMA (cmd.exe no Windows, bash
    // no Unix — ver ProcessRunner::start) — não Posix incondicionalmente,
    // que era o bug: "Export variables" nunca funcionava em "shell normal"
    // (sem alvo) no Windows.
#if defined(Q_OS_WIN)
    core::ShellFlavor captureShellFlavor = core::ShellFlavor::Cmd;
#else
    core::ShellFlavor captureShellFlavor = core::ShellFlavor::Posix;
#endif
    if (!effTarget.isEmpty()) {
        bool targetUsePty = true;
        core::ShellFlavor targetFlavor = core::ShellFlavor::Posix;
        for (const core::TerminalProfile &t : m_terminalProfiles) {
            if (t.name == effTarget) {
                targetUsePty = t.usePty;
                targetFlavor = effectiveShellFlavor(t);
                break;
            }
        }
        captureShellFlavor = targetFlavor;
        // NÃO religa o PTY aqui quando é captura de env: mais acima já
        // desligamos de propósito (comentário logo ali - o PTY ecoa o
        // comando e converte quebras de linha, quebrando o parse do
        // sentinela/dump). Bug real encontrado: esta atribuição
        // INCONDICIONAL rodava DEPOIS e reativava o PTY sempre que o alvo
        // resolvido (quase sempre há um - até "@parent" cai num default)
        // tinha usePty=true, o caso comum - desfazendo aquilo em silêncio.
        // "Export variables" então nunca encontrava o sentinela (ou
        // encontrava dados corrompidos pelo próprio eco/wrap do PTY) toda
        // vez que havia QUALQUER alvo de terminal resolvido - exatamente o
        // reportado: "testei... export TESTE=1 e não jogou a ENV pra saída".
        if (!captureEnv) {
            rawRunner->setUsePty(targetUsePty);
        }
        // KILL REMOTO: no WSL2 os processos Linux NÃO são filhos Windows do
        // wsl.exe, então taskkill deixa fantasmas. Geramos um runId, o shell
        // remoto grava seu PID em /tmp/kai-<runId>.pid e o stop() dispara um
        // kill do GRUPO lá dentro pelo mesmo alvo de terminal. SÓ faz sentido
        // em alvo POSIX (WSL/bash) — em PowerShell/Cmd o processo é filho
        // Windows normal e o `echo $$ > /tmp/...` (sintaxe bash) quebraria.
        if (targetFlavor == core::ShellFlavor::Posix) {
            remoteRunId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
            rawRunner->setRemoteKillCommandLine(buildRemoteKillCommandLine(effTarget, remoteRunId));
        }
        utils::Logger::info(kLogTag,
            QStringLiteral("Comando '%1' com alvo '%2'%3: usePty=%4.")
                .arg(command.id, effTarget,
                     command.terminalTarget.isEmpty() ? QStringLiteral(" (padrão)") : QString(),
                     targetUsePty ? QStringLiteral("true (ConPTY/tty)")
                                  : QStringLiteral("false (QProcess robusto)")));
    }

    // Auto-responsores: matcher por execução com buffer rolante. Escuta a
    // saída e responde prompts automaticamente via stdin (feedback do
    // usuário). Só cria se o comando tem responders (custo zero caso contrário).
    std::shared_ptr<OutputResponderMatcher> matcher;
    if (!command.responders.isEmpty()) {
        // m_envManager habilita {{VAR}} na resposta (feedback do usuário:
        // "interpolação de responsores automáticos... atualmente não
        // suportam interpolação de envs").
        matcher = std::make_shared<OutputResponderMatcher>(command.responders, m_envManager);
    }

    connect(rawRunner, &ProcessRunner::outputReady, this,
            [this, id = command.id, captureEnv, matcher, rawRunner](const QString &text, bool isError) {
        if (captureEnv) {
            // Acumula tudo para extrair o ambiente ao final, e mostra no
            // terminal apenas a parte ANTES do sentinela (o dump de `env`
            // fica oculto para não poluir a saída).
            m_captureBuffer += text;
            const QString sentinel = QString::fromLatin1(kEnvSentinel);
            const int pos = m_captureBuffer.indexOf(sentinel);
            if (pos < 0) {
                emit logMessage(id, text, isError);
            } else {
                // Emite só o que faltava antes do sentinela (uma única vez).
                const int alreadyShown = m_captureBuffer.length() - text.length();
                if (alreadyShown < pos) {
                    emit logMessage(id, m_captureBuffer.mid(alreadyShown, pos - alreadyShown), isError);
                }
            }
            return;
        }
        emit logMessage(id, text, isError);

        // Auto-responsores: casa a saída e responde no stdin. Ecoa a
        // resposta no log para o usuário ver quem respondeu.
        if (matcher && rawRunner->isRunning()) {
            const auto replies = matcher->feed(text);
            for (const auto &reply : replies) {
                emit logMessage(id,
                    utils::tr(QStringLiteral("execution_pipeline.auto_reply")).arg(reply.text) + QStringLiteral("\n"), false);
                rawRunner->writeToStdin(reply.text);
            }
        }
    });

    if (command.isBackground) {
        // processos background mantêm-se vivos e o pipeline
        // não espera o término deles — considera sucesso imediato ao
        // iniciar, permitindo que hooks subsequentes/o restante do
        // pipeline prossigam sem bloquear no processo de longa duração
        // (ex: um dev server que roda indefinidamente).
        connect(rawRunner, &ProcessRunner::started, this, [this, id = command.id, onDone]() {
            emit backgroundProcessStarted(id, runnerFor(id));
            onDone(true, QString());
        });

        connect(rawRunner, &ProcessRunner::finished, this, [this, id = command.id, ignoreExitCode = command.ignoreExitCode](const ProcessResult &result) {
            // IGNORAR CÓDIGO DE SAÍDA (core::Command::ignoreExitCode —
            // feedback do usuário: um `explorer.exe` chamado do WSL pra
            // abrir uma pasta no Windows retorna exit code != 0 mesmo
            // funcionando; não é um erro de verdade). Crash de processo
            // (sinal/segfault) continua reportado normalmente.
            const bool success = ignoreExitCode ? !result.crashed : (result.exitCode == 0 && !result.crashed);
            emit logMessage(id,
                // Só reporta o término quando FALHOU: o encerramento normal é
                // silencioso (o badge já mostra o estado). Antes toda saída
                // terminava com uma linha de boilerplate.
                success ? QString()
                        : utils::tr(QStringLiteral("execution_pipeline.process_ended")).arg(result.exitCode) + QStringLiteral("\n"),
                !success);
        });

        rawRunner->start(applyTerminalProfile(command, m_envManager->interpolate(command.command), m_envManager->interpolate(command.workingDir), remoteRunId),
                         effTarget.isEmpty() ? m_envManager->interpolate(command.workingDir) : QString(),
                         m_envManager->resolvedEnv());
        return;
    }

    connect(rawRunner, &ProcessRunner::finished, this,
        [this, onDone, captureEnv, captureScopeKey, declaredEnvVars = command.declaredEnvVars,
         id = command.id, ignoreExitCode = command.ignoreExitCode](const ProcessResult &result) {
        if (captureEnv) {
            ingestCapturedEnv(m_captureBuffer, captureScopeKey, declaredEnvVars);
            m_captureBuffer.clear();
            m_captureEnvActive = false;
        }
        // Remove o runner do registry de forma DIFERIDA: destruir aqui seria
        // destruir o objeto dentro do seu próprio sinal (e o dtor é
        // bloqueante). QueuedConnection roda no próximo ciclo do event loop.
        QMetaObject::invokeMethod(this, [this, id]() {
            const auto it = m_runners.find(id);
            if (it != m_runners.end() && it->second && !it->second->isRunning()) {
                m_runners.erase(it);
            }
        }, Qt::QueuedConnection);
        // IGNORAR CÓDIGO DE SAÍDA: ver comentário do branch isBackground
        // acima — mesma regra aqui pro caminho de execução única.
        const bool success = ignoreExitCode ? !result.crashed : (result.exitCode == 0 && !result.crashed);
        onDone(success, success ? QString() : result.errorMessage);
    });

    const QString interpolatedCommand = m_envManager->interpolate(command.command);
    const QString interpolatedWorkingDir = m_envManager->interpolate(command.workingDir);

    // ORDEM IMPORTA (bug real encontrado testando o próprio fix de sintaxe
    // por sabor): com um alvo de terminal, applyTerminalProfile embrulha o
    // comando numa invocação de shell ANINHADA (ex: template
    // "bash -lc {{command}}" vira `bash -lc '<comando>'` — um PROCESSO
    // FILHO separado). Se wrapForEnvCapture rodasse DEPOIS (como antes),
    // "echo SENTINELA" + "env" ficavam FORA dessa invocação aninhada,
    // rodando no shell EXTERNO — que nunca viu o `export` (ele aconteceu
    // dentro do bash -lc filho, que já tinha terminado e sumido). Capturava
    // sempre zero variáveis com qualquer alvo de terminal. Agora
    // wrapForEnvCapture roda ANTES: o sentinela+dump entram como parte do
    // MESMO texto que applyTerminalProfile embrulha na invocação aninhada,
    // então tudo roda na mesma sessão de shell que o `export`.
    const QString finalCommand = captureEnv
        ? applyTerminalProfile(command, wrapForEnvCapture(interpolatedCommand, captureShellFlavor), interpolatedWorkingDir, remoteRunId)
        : applyTerminalProfile(command, interpolatedCommand, interpolatedWorkingDir, remoteRunId);

    // Com alvo de terminal, o working_dir já é aplicado via `cd` DENTRO do
    // shell (applyTerminalProfile) — NÃO passamos ao QProcess::setWorkingDir,
    // pois o caminho pode ser Windows/WSL inválido no lado do lançador
    // (cmd.exe/wsl.exe), causando FailedToStart (bug reportado:
    // "comando/shell não encontrado" ao subir a API via WSL).
    rawRunner->start(finalCommand,
                     effTarget.isEmpty() ? interpolatedWorkingDir : QString(),
                     m_envManager->resolvedEnv());
}


void ExecutionPipeline::runCleanupHooks(const core::Command &command,
                                        const QMap<QString, core::Command> &allCommands)
{
    if (command.hooks.cleanup.isEmpty()) {
        return;
    }
    // IDEMPOTENTE por comando: ver comentário de m_cleanupFiredFor no header.
    if (m_cleanupFiredFor.contains(command.id)) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Cleanup de '%1' já foi disparado nesta execução; ignorando.")
                .arg(command.id));
        return;
    }
    m_cleanupFiredFor.insert(command.id);
    utils::Logger::info(kLogTag,
        QStringLiteral("Executando %1 hook(s) de cleanup de '%2'.")
            .arg(command.hooks.cleanup.size()).arg(command.id));

    for (const QString &hookId : command.hooks.cleanup) {
        const auto it = allCommands.constFind(hookId);
        if (it == allCommands.constEnd()) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Hook de cleanup '%1' não encontrado; ignorando.").arg(hookId));
            continue;
        }
        const core::Command &hook = it.value();
        if (hook.type != core::CommandType::Shell) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Hook de cleanup '%1' não é shell; ignorando.").arg(hookId));
            continue;
        }
        // CONDIÇÃO DE EXECUÇÃO: cleanup roda destacado (fora de
        // runSingleCommand), então precisa da própria checagem — mesma regra
        // do comando principal/pre/post-hooks (ver evaluateConditions).
        const ConditionEvalResult hookConditionResult = evaluateConditions(hook);
        if (!hookConditionResult.passed) {
            emit logMessage(command.id,
                utils::tr(QStringLiteral("execution_pipeline.condition_skipped"))
                    .arg(hook.name.isEmpty() ? hookId : hook.name, hookConditionResult.decidingConditionLabel) + QStringLiteral("\n"),
                false);
            continue;
        }

        // DESTACADO de propósito: o cleanup precisa sobreviver ao encerramento
        // do pipeline (e até ao fechamento do app), e não pode bloquear a GUI
        // nem virar mais um runner no registry que alguém tente matar.
        const QString effTarget = effectiveTerminalProfileName(hook);
        const QString script = applyTerminalProfile(
            hook, m_envManager->interpolate(hook.command),
            m_envManager->interpolate(hook.workingDir), QString());

        emit logMessage(command.id,
            utils::tr(QStringLiteral("execution_pipeline.cleanup.running")).arg(hook.name.isEmpty() ? hookId : hook.name) + QStringLiteral("\n"), false);

        bool started = false;
#if defined(Q_OS_WIN)
        // VERBATIM via setNativeArguments. Passar o script numa LISTA de
        // argumentos faz o QProcess re-aplicar quoting do Windows sobre uma
        // linha que já contém as aspas simples do bash (wsl.exe ... bash -lc
        // '...'), desmontando o comando — o mesmo bug que o start() já havia
        // corrigido (lá o comentário registra que "maga env prod {{c}}" chegava
        // só como "maga"). O cleanup não tinha seguido essa lição.
        {
            QProcess detached;
            detached.setProgram(QStringLiteral("cmd.exe"));
            detached.setNativeArguments(QStringLiteral("/c ") + script);
            started = detached.startDetached();
        }
#else
        started = QProcess::startDetached(QStringLiteral("/bin/bash"),
                                          {QStringLiteral("-lc"), script});
#endif
        if (!started) {
            utils::Logger::error(kLogTag,
                QStringLiteral("Falha ao iniciar o hook de cleanup '%1'.").arg(hookId));
            emit logMessage(command.id,
                utils::tr(QStringLiteral("execution_pipeline.cleanup.start_failed")).arg(hookId) + QStringLiteral("\n"), true);
        }
        Q_UNUSED(effTarget);
    }
}

void ExecutionPipeline::abort(PipelineStage stage, const QString &commandId, const QString &errorMessage)
{
    // Encerra runners AINDA VIVOS antes de abortar (achado de auditoria:
    // abort() só emitia pipelineFinished, deixando o processo/runner órfão —
    // ex: hook referenciando comando inexistente enquanto o anterior drena).
    // ESCOPO: só runners cujo id pertence a ESTA CADEIA (m_currentChainCommandIds
    // — o comando principal + seus próprios pre/post hooks), NUNCA todo o
    // registry. Bug real (relatado como "trava a entrada do Claude"/"race
    // condition" ao rodar outros comandos com um terminal interativo já
    // conectado): este loop antes iterava m_runners INTEIRO, então abortar
    // um comando comum que falhasse (DNS de uma request HTTP, hook com id
    // inexistente, exit code != 0...) encerrava também qualquer OUTRO
    // processo em execução naquele instante — inclusive um terminal
    // interativo totalmente não relacionado (ex.: Claude Code rodando em
    // paralelo), travando sua entrada sem nenhum motivo real.
    // NOTA sobre a mensagem que isto gera: ProcessRunner::stop() marca
    // m_stopRequested, e o texto associado (process_runner.error.
    // stopped_by_user) foi deliberadamente NEUTRALIZADO ("parada
    // solicitada", sem dizer "pelo usuário") — este stop() é disparado
    // pelo PIPELINE internamente (abortando por causa de OUTRO comando
    // que falhou, ex: DNS de uma requisição HTTP), não porque o usuário
    // clicou em Parar. Dizer "a pedido do usuário" aqui era enganoso (bug
    // relatado, com screenshot: um erro de DNS aparecia seguido de
    // "Processo encerrado a pedido do usuário", como se o usuário tivesse
    // pedido algo que ele não pediu).
    for (auto &entry : m_runners) {
        if (!m_currentChainCommandIds.contains(entry.first)) {
            continue;
        }
        if (entry.second && entry.second->isRunning()) {
            utils::Logger::warning(kLogTag,
                QStringLiteral("Abort do pipeline: encerrando runner de '%1'.").arg(entry.first));
            entry.second->stop();
        }
    }
    PipelineResult result;
    result.success = false;
    result.failedStage = stage;
    result.failedCommandId = commandId;
    result.errorMessage = errorMessage;

    const QString stageName = stage == PipelineStage::PreHooks ? QStringLiteral("PreHooks") :
                               stage == PipelineStage::MainCommand ? QStringLiteral("MainCommand") :
                               QStringLiteral("PostHooks");

    utils::Logger::error(kLogTag,
        QStringLiteral("Pipeline abortado no estágio '%1' pelo comando '%2': %3")
            .arg(stageName, commandId, errorMessage));

    emit logMessage(commandId, utils::tr(QStringLiteral("execution_pipeline.aborted")).arg(errorMessage) + QStringLiteral("\n"), true);
    // CLEANUP também no caminho de FALHA/CRASH/abort — é justamente quando mais
    // importa desmontar o que ficou de pé.
    if (m_allCommands) {
        runCleanupHooks(m_mainCommand, *m_allCommands);
    }
    emit pipelineFinished(result);
}

} // namespace kai::engine
