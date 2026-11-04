#include "engine/execution-pipeline-helpers.h"

#include "utils/path-format.h"

namespace kai::engine::detail {

// True se `name` é um identificador shell válido ([A-Za-z_][A-Za-z0-9_]*);
// nomes como "c.value" (campos de coleção) quebram `export NOME=...` no bash.
static bool isValidShellIdentifier(const QString &name)
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

// As linhas `export K='V'` do ambiente (só nomes que o shell aceita): o que um alvo POSIX recebe NO TEXTO do comando,
// já que as variáveis do processo local não atravessam o wsl.exe/docker/ssh.
QString posixEnvExports(const QMap<QString, QString> &env)
{
    QString exports;
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        if (!isValidShellIdentifier(it.key())) {
            continue;
        }
        QString v = it.value();
        v.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        exports += QStringLiteral("export %1='%2'\n").arg(it.key(), v);
    }
    return exports;
}

// Prefixo de env + wrapper de working-dir na SINTAXE do sabor (um `export`
// bash quebra no PowerShell).
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
            // Diretório que não existe (ex.: dentro de um container): segue onde está, sem aviso, e não derruba o comando.
            body = QStringLiteral("if (Test-Path -LiteralPath '%1' -PathType Container) { Set-Location -LiteralPath '%1' }\n%2")
                       .arg(dir, interpolatedCommand);
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
            // `cd` que falha não impede o comando: segue no diretório atual, sem aviso.
            body = QStringLiteral("cd /d \"%1\" 2>nul\n%2").arg(interpolatedWorkingDir, interpolatedCommand);
        }
        return prefix + body;
    }

    // Posix (bash/sh/WSL/docker exec bash).
    prefix = remotePrefixPosix + posixEnvExports(resolvedEnv);
    if (!interpolatedWorkingDir.isEmpty()) {
        QString dir = utils::toPosixPath(interpolatedWorkingDir);
        dir.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        // Diretório que não existe (ex.: dentro de um container): roda onde está, sem aviso, em vez de o `cd` falho (com
        // &&) derrubar o comando inteiro. O bloco { } mantém o código de saída do comando.
        body = QStringLiteral("cd '%1' 2>/dev/null || true\n{ %2\n}").arg(dir, interpolatedCommand);
    }
    return prefix + body;
}

} // namespace kai::engine::detail
