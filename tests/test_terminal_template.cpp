#include <QTest>

#include "core/terminal-template.h"

using namespace kai::core;

// Modo simples dos Perfis de Execução: o formulário gera o template e um template existente só volta ao formulário
// quando ele o reproduz exatamente.
class TestTerminalTemplate : public QObject {
    Q_OBJECT

private slots:
    // Os presets antigos (que o usuário já tem salvos) precisam sair idênticos dos valores padrão do formulário.
    void defaultsReproduceTheOldPresets()
    {
        QCOMPARE(buildTemplate(defaultSpec(TemplateKind::Wsl), true),
                 QStringLiteral("wsl.exe -- bash -lic 'eval \"$(echo \"$1\" | base64 -d)\"' kai {{command_b64}}"));
        QCOMPARE(buildTemplate(defaultSpec(TemplateKind::PowerShell), true),
                 QStringLiteral("cmd.exe /d /s /c powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
                                "^\"Invoke-Expression([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("
                                "'{{command_b64}}')))^\""));
        QCOMPARE(buildTemplate(defaultSpec(TemplateKind::LocalShell), false), QStringLiteral("{{command}}"));
    }

    void dockerOnWslHasNoWorkingDirByDefault()
    {
        TemplateSpec spec = defaultSpec(TemplateKind::Docker);
        spec.dockerHost = DockerHost::Wsl;
        spec.container = QStringLiteral("crazy_booth");
        QCOMPARE(buildTemplate(spec, true),
                 QStringLiteral("wsl.exe -- bash -lic 'docker exec -i crazy_booth bash -lc \"$(echo $1 | base64 -d)\"' "
                                "kai {{command_b64}}"));
        spec.workingDir = QStringLiteral("/var/www/html");
        QCOMPARE(buildTemplate(spec, true),
                 QStringLiteral("wsl.exe -- bash -lic 'docker exec -i -w /var/www/html crazy_booth bash -lc "
                                "\"$(echo $1 | base64 -d)\"' kai {{command_b64}}"));
    }

    void dockerHostsFollowTheHostSystem()
    {
        TemplateSpec spec = defaultSpec(TemplateKind::Docker);
        spec.container = QStringLiteral("web");
        QCOMPARE(buildTemplate(spec, false),
                 QStringLiteral("docker exec -i web bash -lc \"$(echo {{command_b64}} | base64 -d)\""));
        QCOMPARE(buildTemplate(spec, true),
                 QStringLiteral("docker.exe exec -i web bash -lc \"eval \\\"$(echo {{command_b64}} ^| base64 -d)\\\"\""));

        spec.dockerHost = DockerHost::Ssh;
        spec.sshTarget = QStringLiteral("dev@box");
        spec.dockerTty = true;
        QCOMPARE(buildTemplate(spec, false),
                 QStringLiteral("ssh -t dev@box \"docker exec -i -t web bash -lc \\\"\\$(echo {{command_b64}} | base64 -d)\\\"\""));
        QCOMPARE(buildTemplate(spec, true),
                 QStringLiteral("ssh.exe -t dev@box \"docker exec -i -t web bash -lc \\\"$(echo {{command_b64}} ^| base64 -d)\\\"\""));
    }

    void everySpecRoundTripsThroughParse()
    {
        QVector<TemplateSpec> specs;
        TemplateSpec wsl = defaultSpec(TemplateKind::Wsl);
        specs << wsl;
        wsl.distro = QStringLiteral("Ubuntu-22.04");
        wsl.workingDir = QStringLiteral("/srv/my app");
        wsl.shell = QStringLiteral("zsh");
        wsl.interactive = false;
        specs << wsl;
        for (const DockerHost host : {DockerHost::Local, DockerHost::Wsl, DockerHost::Ssh}) {
            TemplateSpec docker = defaultSpec(TemplateKind::Docker);
            docker.dockerHost = host;
            docker.container = QStringLiteral("api");
            docker.sshTarget = QStringLiteral("me@host");
            specs << docker;
            docker.workingDir = QStringLiteral("/app");
            docker.dockerTty = true;
            docker.interactive = true;
            docker.shell = QStringLiteral("sh");
            specs << docker;
        }
        TemplateSpec ps = defaultSpec(TemplateKind::PowerShell);
        specs << ps;
        ps.powerShellExe = QStringLiteral("pwsh.exe");
        ps.noProfile = false;
        ps.bypassPolicy = false;
        specs << ps;
        TemplateSpec local = defaultSpec(TemplateKind::LocalShell);
        specs << local;
        local.shell = QStringLiteral("bash");
        local.login = true;
        specs << local;

        for (const bool windows : {false, true}) {
            for (const TemplateSpec &spec : std::as_const(specs)) {
                const QString text = buildTemplate(spec, windows);
                const auto parsed = parseTemplate(text, windows);
                QVERIFY2(parsed.has_value(), qPrintable(text));
                QCOMPARE(buildTemplate(*parsed, windows), text);
                QCOMPARE(int(parsed->kind), int(spec.kind));
            }
        }
    }

    void parseReadsTheFieldsOfAnExistingProfile()
    {
        const auto spec = parseTemplate(
            QStringLiteral("wsl.exe -- bash -lic 'docker exec -i -w /var/www/html crazy_booth bash -lc "
                           "\"$(echo $1 | base64 -d)\"' kai {{command_b64}}"), true);
        QVERIFY(spec.has_value());
        QCOMPARE(int(spec->kind), int(TemplateKind::Docker));
        QCOMPARE(int(spec->dockerHost), int(DockerHost::Wsl));
        QCOMPARE(spec->container, QStringLiteral("crazy_booth"));
        QCOMPARE(spec->workingDir, QStringLiteral("/var/www/html"));
        QCOMPARE(spec->shell, QStringLiteral("bash"));
        QVERIFY(spec->login);
        QVERIFY(!spec->interactive);
    }

    // O que o formulário não reproduz fica no modo avançado, intacto.
    void parseRejectsHandWrittenTemplates()
    {
        QVERIFY(!parseTemplate(QStringLiteral("wsl.exe -- bash -lc '{{command}}'"), true).has_value());
        QVERIFY(!parseTemplate(QStringLiteral("wsl.exe -d Ubuntu -u root -- bash -lic 'x' kai {{command_b64}}"), true).has_value());
        QVERIFY(!parseTemplate(QStringLiteral("docker exec -it web sh -c {{command}}"), false).has_value());
        QVERIFY(!parseTemplate(QString(), false).has_value());
        QVERIFY(!parseTemplate(QStringLiteral("ssh dev@box uptime"), false).has_value());
    }

    void containerListRunsWhereDockerIs()
    {
        TemplateSpec spec = defaultSpec(TemplateKind::Docker);
        ContainerListCommand local = containerListCommand(spec, false);
        QCOMPARE(local.program, QStringLiteral("docker"));
        QCOMPARE(local.arguments.first(), QStringLiteral("ps"));

        spec.dockerHost = DockerHost::Wsl;
        spec.distro = QStringLiteral("Debian");
        const ContainerListCommand wsl = containerListCommand(spec, true);
        QCOMPARE(wsl.program, QStringLiteral("wsl.exe"));
        QCOMPARE(wsl.arguments.mid(0, 2), (QStringList{QStringLiteral("-d"), QStringLiteral("Debian")}));
        QVERIFY(wsl.arguments.last().startsWith(QStringLiteral("docker ps")));

        spec.dockerHost = DockerHost::Ssh;
        spec.sshTarget = QStringLiteral("dev@box");
        const ContainerListCommand ssh = containerListCommand(spec, false);
        QCOMPARE(ssh.program, QStringLiteral("ssh"));
        QVERIFY(ssh.arguments.contains(QStringLiteral("BatchMode=yes"))); // nunca fica esperando senha
        QVERIFY(ssh.arguments.contains(QStringLiteral("dev@box")));
    }
};

QTEST_APPLESS_MAIN(TestTerminalTemplate)
#include "test_terminal_template.moc"
