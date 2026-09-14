#include "core/terminal-template.h"

#include <QRegularExpression>

namespace kai::core {

namespace {

QString shellFlags(const TemplateSpec &spec)
{
    return QLatin1Char('-') + (spec.login ? QStringLiteral("l") : QString()) + (spec.interactive ? QStringLiteral("i") : QString())
        + QLatin1Char('c');
}

// Argumento de uma linha cmd.exe/bash: com espaço vai entre aspas duplas.
QString quoteArg(const QString &value)
{
    QString v = value;
    v.remove(QLatin1Char('"'));
    return v.contains(QLatin1Char(' ')) ? QLatin1Char('"') + v + QLatin1Char('"') : v;
}

QString unquoteArg(const QString &token)
{
    if (token.size() >= 2 && token.startsWith(QLatin1Char('"')) && token.endsWith(QLatin1Char('"'))) {
        return token.mid(1, token.size() - 2);
    }
    return token;
}

// `docker exec` com o container e o shell; `payload` é o texto que entra como comando (já com o quoting do destino).
QString dockerExec(const TemplateSpec &spec, const QString &docker, const QString &payload)
{
    QString line = docker + QStringLiteral(" exec -i");
    if (spec.dockerTty) {
        line += QStringLiteral(" -t");
    }
    if (!spec.workingDir.trimmed().isEmpty()) {
        line += QStringLiteral(" -w ") + quoteArg(spec.workingDir.trimmed());
    }
    return line + QLatin1Char(' ') + spec.container.trimmed() + QLatin1Char(' ') + spec.shell.trimmed() + QLatin1Char(' ')
        + shellFlags(spec) + QLatin1Char(' ') + payload;
}

QString wslPrefix(const TemplateSpec &spec)
{
    QString line = QStringLiteral("wsl.exe");
    if (!spec.distro.trimmed().isEmpty()) {
        line += QStringLiteral(" -d ") + quoteArg(spec.distro.trimmed());
    }
    return line;
}

QString buildDocker(const TemplateSpec &spec, bool windowsHost)
{
    // O comando chega em base64 e é decodificado onde o shell do container o lê: aspas e quebras de linha passam intactas.
    switch (spec.dockerHost) {
    case DockerHost::Wsl: {
        const QString inner = dockerExec(spec, QStringLiteral("docker"), QStringLiteral("\"$(echo $1 | base64 -d)\""));
        return wslPrefix(spec) + QStringLiteral(" -- bash -lic '") + inner + QStringLiteral("' kai {{command_b64}}");
    }
    case DockerHost::Ssh: {
        const QString target = quoteArg(spec.sshTarget.trimmed());
        const QString tty = spec.dockerTty ? QStringLiteral(" -t") : QString();
        const QString payload = QStringLiteral("\"$(echo {{command_b64}} | base64 -d)\"");
        QString remote = dockerExec(spec, QStringLiteral("docker"), payload);
        if (windowsHost) {
            remote.replace(QLatin1Char('"'), QStringLiteral("\\\""));
            remote.replace(QLatin1Char('|'), QStringLiteral("^|"));
            return QStringLiteral("ssh.exe") + tty + QLatin1Char(' ') + target + QStringLiteral(" \"") + remote + QLatin1Char('"');
        }
        remote.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        remote.replace(QLatin1Char('$'), QStringLiteral("\\$"));
        return QStringLiteral("ssh") + tty + QLatin1Char(' ') + target + QStringLiteral(" \"") + remote + QLatin1Char('"');
    }
    case DockerHost::Local:
        break;
    }
    if (windowsHost) {
        // Sem shell do lado do Windows para expandir `$(...)`: o `eval` roda dentro do container; `^|` é o pipe escapado do cmd.
        return dockerExec(spec, QStringLiteral("docker.exe"),
                          QStringLiteral("\"eval \\\"$(echo {{command_b64}} ^| base64 -d)\\\"\""));
    }
    return dockerExec(spec, QStringLiteral("docker"), QStringLiteral("\"$(echo {{command_b64}} | base64 -d)\""));
}

QString buildWsl(const TemplateSpec &spec)
{
    QString line = wslPrefix(spec);
    if (!spec.workingDir.trimmed().isEmpty()) {
        line += QStringLiteral(" --cd ") + quoteArg(spec.workingDir.trimmed());
    }
    return line + QStringLiteral(" -- ") + spec.shell.trimmed() + QLatin1Char(' ') + shellFlags(spec)
        + QStringLiteral(" 'eval \"$(echo \"$1\" | base64 -d)\"' kai {{command_b64}}");
}

QString buildPowerShell(const TemplateSpec &spec)
{
    QString line = QStringLiteral("cmd.exe /d /s /c ") + spec.powerShellExe;
    if (spec.noProfile) {
        line += QStringLiteral(" -NoProfile");
    }
    if (spec.bypassPolicy) {
        line += QStringLiteral(" -ExecutionPolicy Bypass");
    }
    return line + QStringLiteral(" -Command ^\"Invoke-Expression([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("
                                 "'{{command_b64}}')))^\"");
}

QString buildLocalShell(const TemplateSpec &spec)
{
    if (spec.shell.trimmed().isEmpty()) {
        return QStringLiteral("{{command}}");
    }
    return spec.shell.trimmed() + QLatin1Char(' ') + shellFlags(spec) + QStringLiteral(" {{command}}");
}

// Um argumento: entre aspas duplas ou sem espaço.
const char *kToken = R"("[^"]*"|\S+)";

void readShellFlags(TemplateSpec &spec, const QString &flags)
{
    spec.login = flags.contains(QLatin1Char('l'));
    spec.interactive = flags.contains(QLatin1Char('i'));
}

} // namespace

TemplateSpec defaultSpec(TemplateKind kind)
{
    TemplateSpec spec;
    spec.kind = kind;
    switch (kind) {
    case TemplateKind::Wsl:
        spec.login = true;
        spec.interactive = true;
        break;
    case TemplateKind::Docker:
        spec.login = true;
        spec.interactive = false;
        break;
    case TemplateKind::PowerShell:
        break;
    case TemplateKind::LocalShell:
        spec.shell.clear(); // o shell do próprio Kai
        spec.login = false;
        spec.interactive = false;
        break;
    }
    return spec;
}

ShellFlavor defaultShellFor(TemplateKind kind)
{
    return kind == TemplateKind::PowerShell ? ShellFlavor::PowerShell : ShellFlavor::Posix;
}

bool defaultPtyFor(TemplateKind kind)
{
    return kind != TemplateKind::PowerShell; // sob ConPTY o PowerShell embrulhado em cmd.exe quebra aspas e pipes
}

QString buildTemplate(const TemplateSpec &spec, bool windowsHost)
{
    switch (spec.kind) {
    case TemplateKind::Wsl:
        return buildWsl(spec);
    case TemplateKind::Docker:
        return buildDocker(spec, windowsHost);
    case TemplateKind::PowerShell:
        return buildPowerShell(spec);
    case TemplateKind::LocalShell:
        return buildLocalShell(spec);
    }
    return QString();
}

std::optional<TemplateSpec> parseTemplate(const QString &commandTemplate, bool windowsHost)
{
    const QString text = commandTemplate.trimmed();
    TemplateSpec spec;
    const QString token = QString::fromLatin1(kToken);

    if (text.startsWith(QStringLiteral("cmd.exe /d /s /c "))) {
        spec.kind = TemplateKind::PowerShell;
        static const QRegularExpression exe(QStringLiteral(R"(^cmd\.exe /d /s /c (powershell\.exe|pwsh\.exe) )"));
        const QRegularExpressionMatch m = exe.match(text);
        if (!m.hasMatch()) {
            return std::nullopt;
        }
        spec.powerShellExe = m.captured(1);
        spec.noProfile = text.contains(QStringLiteral(" -NoProfile"));
        spec.bypassPolicy = text.contains(QStringLiteral(" -ExecutionPolicy Bypass"));
    } else if (text.startsWith(QStringLiteral("wsl.exe"))) {
        static const QRegularExpression distro(QStringLiteral(R"(^wsl\.exe(?: -d (%1))?)").arg(QString::fromLatin1(kToken)));
        const QRegularExpressionMatch dm = distro.match(text);
        spec.distro = unquoteArg(dm.captured(1));
        if (text.contains(QStringLiteral("docker exec "))) {
            spec.kind = TemplateKind::Docker;
            spec.dockerHost = DockerHost::Wsl;
            static const QRegularExpression docker(QStringLiteral(
                R"(docker exec -i( -t)?(?: -w (%1))? (\S+) (\w+) -([li]*)c ")").arg(QString::fromLatin1(kToken)));
            const QRegularExpressionMatch m = docker.match(text);
            if (!m.hasMatch()) {
                return std::nullopt;
            }
            spec.dockerTty = !m.captured(1).isEmpty();
            spec.workingDir = unquoteArg(m.captured(2));
            spec.container = m.captured(3);
            spec.shell = m.captured(4);
            readShellFlags(spec, m.captured(5));
        } else {
            spec.kind = TemplateKind::Wsl;
            static const QRegularExpression wsl(QStringLiteral(R"(^wsl\.exe(?: -d (?:%1))?(?: --cd (%1))? -- (\w+) -([li]*)c )")
                                                    .arg(QString::fromLatin1(kToken)));
            const QRegularExpressionMatch m = wsl.match(text);
            if (!m.hasMatch()) {
                return std::nullopt;
            }
            spec.workingDir = unquoteArg(m.captured(1));
            spec.shell = m.captured(2);
            readShellFlags(spec, m.captured(3));
        }
    } else if (text.startsWith(QStringLiteral("ssh"))) {
        spec.kind = TemplateKind::Docker;
        spec.dockerHost = DockerHost::Ssh;
        static const QRegularExpression ssh(QStringLiteral(
            R"re(^ssh(?:\.exe)?(?: -t)? (%1) .*?docker exec -i(?: -t)?(?: -w (?:%1))? (\S+) (\w+) -([li]*)c )re")
                                                .arg(QString::fromLatin1(kToken)));
        const QRegularExpressionMatch m = ssh.match(text);
        if (!m.hasMatch()) {
            return std::nullopt;
        }
        spec.sshTarget = unquoteArg(m.captured(1));
        spec.dockerTty = text.contains(QStringLiteral(" exec -i -t"));
        static const QRegularExpression wd(QStringLiteral(R"(docker exec -i(?: -t)? -w (%1) )").arg(QString::fromLatin1(kToken)));
        const QRegularExpressionMatch wm = wd.match(text);
        spec.workingDir = wm.hasMatch() ? unquoteArg(wm.captured(1)) : QString();
        spec.container = m.captured(2);
        spec.shell = m.captured(3);
        readShellFlags(spec, m.captured(4));
    } else if (text.startsWith(QStringLiteral("docker"))) {
        spec.kind = TemplateKind::Docker;
        spec.dockerHost = DockerHost::Local;
        static const QRegularExpression docker(QStringLiteral(
            R"(^docker(?:\.exe)? exec -i( -t)?(?: -w (%1))? (\S+) (\w+) -([li]*)c )").arg(QString::fromLatin1(kToken)));
        const QRegularExpressionMatch m = docker.match(text);
        if (!m.hasMatch()) {
            return std::nullopt;
        }
        spec.dockerTty = !m.captured(1).isEmpty();
        spec.workingDir = unquoteArg(m.captured(2));
        spec.container = m.captured(3);
        spec.shell = m.captured(4);
        readShellFlags(spec, m.captured(5));
    } else if (text == QStringLiteral("{{command}}")) {
        spec = defaultSpec(TemplateKind::LocalShell);
    } else {
        static const QRegularExpression local(QStringLiteral(R"(^(\w+) -([li]*)c \{\{command\}\}$)"));
        const QRegularExpressionMatch m = local.match(text);
        if (!m.hasMatch()) {
            return std::nullopt;
        }
        spec.kind = TemplateKind::LocalShell;
        spec.shell = m.captured(1);
        readShellFlags(spec, m.captured(2));
    }
    // Só vale o que o formulário reproduz exatamente.
    return buildTemplate(spec, windowsHost) == text ? std::optional<TemplateSpec>(spec) : std::nullopt;
}

ContainerListCommand containerListCommand(const TemplateSpec &spec, bool windowsHost)
{
    // Nome e imagem, separados por tab.
    const QString format = QStringLiteral("{{.Names}}\t{{.Image}}");
    switch (spec.dockerHost) {
    case DockerHost::Wsl: {
        QStringList args;
        if (!spec.distro.trimmed().isEmpty()) {
            args << QStringLiteral("-d") << spec.distro.trimmed();
        }
        args << QStringLiteral("--") << QStringLiteral("bash") << QStringLiteral("-lic")
             << QStringLiteral("docker ps --format '%1'").arg(format);
        return {QStringLiteral("wsl.exe"), args};
    }
    case DockerHost::Ssh:
        return {windowsHost ? QStringLiteral("ssh.exe") : QStringLiteral("ssh"),
                {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"), QStringLiteral("-o"), QStringLiteral("ConnectTimeout=8"),
                 spec.sshTarget.trimmed(), QStringLiteral("docker ps --format '%1'").arg(format)}};
    case DockerHost::Local:
        break;
    }
    return {windowsHost ? QStringLiteral("docker.exe") : QStringLiteral("docker"),
            {QStringLiteral("ps"), QStringLiteral("--format"), format}};
}

} // namespace kai::core
