#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "cli/cli-local-executor.h"
#include "core/config-manager.h"
#include "core/models.h"

using namespace kai::cli;
using namespace kai::core;

// Testa o modo LOCAL de CLI Paths de PONTA A PONTA (achar o kai.yml no
// diretório atual, resolver o caminho, ligar parâmetros e EXECUTAR de
// verdade) — as peças de baixo já são testadas isoladamente
// (CliPathResolver/CliParamBinder); isto cobre a integração
// real entre elas + engine::ExecutionPipeline, verificada via um SMOKE
// TEST manual antes de escrever isto (kai demo hello --quem=Kai rodando
// de verdade num diretório temporário, saída "Hello Kai", exit 0).
//
// Usa um arquivo de SAÍDA em vez de capturar stdout (mais simples e
// robusto que redirecionar o fd do processo de teste) para verificar que
// o comando de fato rodou.
class TestCliLocalExecutor : public QObject {
    Q_OBJECT

private:
    QString m_originalCwd;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_originalCwd = QDir::currentPath();
        // Modo global delega ao app via IPC: socket exclusivo deste teste (nunca
        // o de um Kai de verdade rodando na máquina) e sem subir app nenhum —
        // sem app, o global cai no fallback de rodar no próprio processo.
        qputenv("KAI_IPC_SOCKET_NAME_OVERRIDE",
                QByteArray("kai-test-cli-local-") + QByteArray::number(QCoreApplication::applicationPid()));
        qputenv("KAI_CLI_NO_APP_LAUNCH", "1");
    }

    void cleanup()
    {
        QDir::setCurrent(m_originalCwd);
    }

    // Sem kai.json/kai.yml no diretório: handled=false, sem efeito nenhum
    // (main() deve seguir o fluxo normal).
    void noLocalFileMeansNotHandled()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("hello")});
        QVERIFY(!outcome.handled);
    }

    // 1º token é um verbo reservado (ex: "run"): nunca tratado como CLI
    // Path local, mesmo que exista um kai.yml no diretório — main() deve
    // seguir pro despacho de verbo de sempre.
    void reservedVerbIsNeverHandledLocally()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write("project_name: \"X\"\n");
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("run")});
        QVERIFY(!outcome.handled);
    }

    // Execução REAL de ponta a ponta: resolve o caminho, liga o parâmetro,
    // confia (pré-populado pra não precisar ler stdin no teste) e RODA o
    // comando shell de verdade — verificado pelo arquivo que ele escreve.
    void resolvesAndExecutesRealCommand()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());

        const QString outputFile = dir.filePath(QStringLiteral("out.txt"));
        const QString yaml = QStringLiteral(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Hello\"\n"
            "    type: shell\n"
            "    cli_path: hello\n"
            "    command: \"echo Hello {{quem}} > %1\"\n"
            "    params:\n"
            "      - name: quem\n"
            "        type: text\n"
            "        default: \"Mundo\"\n"
            "        optional: true\n").arg(outputFile);
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(yaml.toUtf8());
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath(
            {QStringLiteral("kai"), QStringLiteral("hello"), QStringLiteral("--quem=Kai")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 0);

        QFile out(outputFile);
        QVERIFY2(out.open(QIODevice::ReadOnly), "comando não rodou de verdade (arquivo de saída não existe)");
        QCOMPARE(QString::fromUtf8(out.readAll()).trimmed(), QStringLiteral("Hello Kai"));
    }

    // KIP (spec 11 §15): o modo LOCAL (kai <caminho> numa pasta com kai.yml) recusa
    // comandos KIP — eles precisam da view do app — e aponta o `kai -g`.
    void kipCommandIsRefusedInLocalMode()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        const QString marker = dir.filePath(QStringLiteral("ran.txt"));
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(QStringLiteral(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Wizard\"\n"
            "    type: shell\n"
            "    cli_path: wizard\n"
            "    kip: true\n"
            "    command: \"touch %1\"\n").arg(marker).toUtf8());
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("wizard")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 1);
        QVERIFY2(!QFile::exists(marker), "o comando KIP não pode ter rodado no modo local");
    }

    void nonKipCommandStillRunsInLocalModeNextToAKipOne()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        const QString marker = dir.filePath(QStringLiteral("ran.txt"));
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(QStringLiteral(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Wizard\"\n"
            "    type: shell\n"
            "    cli_path: wizard\n"
            "    kip: true\n"
            "    command: \"true\"\n"
            "  - name: \"Plain\"\n"
            "    type: shell\n"
            "    cli_path: plain\n"
            "    command: \"touch %1\"\n").arg(marker).toUtf8());
        kaiYml.close();
        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("plain")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 0);
        QVERIFY(QFile::exists(marker));
    }

    // Parâmetro opcional NÃO informado usa o default — mesma execução real,
    // sem --quem=.
    void unfilledOptionalParamUsesDefault()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());

        const QString outputFile = dir.filePath(QStringLiteral("out.txt"));
        const QString yaml = QStringLiteral(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Hello\"\n"
            "    type: shell\n"
            "    cli_path: hello\n"
            "    command: \"echo Hello {{quem}} > %1\"\n"
            "    params:\n"
            "      - name: quem\n"
            "        type: text\n"
            "        default: \"Mundo\"\n"
            "        optional: true\n").arg(outputFile);
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(yaml.toUtf8());
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("hello")});
        QCOMPARE(outcome.exitCode, 0);

        QFile out(outputFile);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(out.readAll()).trimmed(), QStringLiteral("Hello Mundo"));
    }

    // Pedido do usuário ("resolve o do global"): a variável do pacote de
    // Ambiente ATIVO (Configurações > Ambientes, a MESMA fonte que a GUI
    // usa) precisa ser injetada na execução local também — antes disso, o
    // executor local só olhava env_vars de pasta + parâmetros, ignorando o
    // "Global" por completo (limitação documentada, agora corrigida).
    // QStandardPaths::setTestModeEnabled(true) (ligado em initTestCase)
    // já isola o settings.json deste teste do ~/.config/kai real.
    void activeGlobalEnvironmentIsInjectedIntoLocalExecution()
    {
        ConfigManager configManager;
        SettingsData settings = configManager.loadSettings();
        Environment env;
        env.id = QStringLiteral("env_test");
        env.name = QStringLiteral("Test");
        env.vars[QStringLiteral("SAUDACAO")] = QStringLiteral("Salve");
        settings.environments = {env};
        settings.activeEnvironmentId = env.id;
        QVERIFY(configManager.saveSettings(settings));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());

        const QString outputFile = dir.filePath(QStringLiteral("out.txt"));
        const QString yaml = QStringLiteral(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Hello\"\n"
            "    type: shell\n"
            "    cli_path: hello\n"
            "    command: \"echo {{SAUDACAO}} Kai > %1\"\n").arg(outputFile);
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(yaml.toUtf8());
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("hello")});
        QCOMPARE(outcome.exitCode, 0);

        QFile out(outputFile);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(out.readAll()).trimmed(), QStringLiteral("Salve Kai"));
    }

    // O código de saída do COMANDO chega ao terminal (antes: sempre 0 ou 1) —
    // importa pra scripts e CI (`kai test all || ...`, 127, 130 no Ctrl+C).
    void commandExitCodeIsPropagated()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Falha\"\n"
            "    type: shell\n"
            "    cli_path: falha\n"
            "    command: \"exit 7\"\n");
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("falha")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 7);
    }

    // Caminho que não bate com nada: exit 2, handled=true.
    void unresolvedPathExitsWithUsageError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write("project_name: \"X\"\n");
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("bogus")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 2);
    }

    // Falta o parâmetro obrigatório: exit 2 (erro de uso), sem chegar a
    // pedir confiança/executar nada.
    void missingRequiredParamExitsWithUsageError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Env\"\n"
            "    type: shell\n"
            "    cli_path: setenv\n"
            "    command: \"echo {{ambiente}}\"\n"
            "    params:\n"
            "      - name: ambiente\n"
            "        type: text\n");
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath({QStringLiteral("kai"), QStringLiteral("setenv")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 2);
    }

    // "--help" nunca pede confiança nem executa nada — só imprime a ajuda,
    // exit 0, mesmo sem NENHUM parâmetro obrigatório informado.
    void helpFlagNeverPromptsOrExecutes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path());
        QFile kaiYml(dir.filePath(QStringLiteral("kai.yml")));
        QVERIFY(kaiYml.open(QIODevice::WriteOnly));
        kaiYml.write(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Env\"\n"
            "    type: shell\n"
            "    cli_path: setenv\n"
            "    command: \"echo {{ambiente}}\"\n"
            "    params:\n"
            "      - name: ambiente\n"
            "        type: text\n");
        kaiYml.close();

        const LocalExecutionOutcome outcome = runLocalCliPath(
            {QStringLiteral("kai"), QStringLiteral("setenv"), QStringLiteral("--help")});
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 0);
    }
    // Bug real: `kai -g <path>` (modo GLOBAL) ignorava os alvos de terminal
    // do app — o comando caía no shell nativo (cmd.exe no Windows) em vez
    // do alvo padrão (ex: WSL), quebrando com "'docker' não é reconhecido".
    // O modo global tem que rodar como a GUI rodaria.
    void globalModeRunsThroughTheAppDefaultTerminalTarget()
    {
        ConfigManager configManager;
        SettingsData settings = configManager.loadSettings();
        const SettingsData originalSettings = settings;
        TerminalProfile marker;
        marker.name = QStringLiteral("Marcador");
        marker.commandTemplate = QStringLiteral("KAI_VIA_TARGET=sim bash -c {{command}}");
        marker.shell = ShellFlavor::Posix;
        marker.usePty = false;
        marker.isDefault = true;
        settings.terminalProfiles = {marker};
        QVERIFY(configManager.saveSettings(settings));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir::setCurrent(dir.path()); // sem kai.yml: nada de modo local aqui
        const QString outputFile = dir.filePath(QStringLiteral("out.txt"));

        CommandsData data;
        Folder folder;
        folder.id = QStringLiteral("f_global");
        folder.name = QStringLiteral("Global");
        folder.cliPath = QStringLiteral("grupo");
        data.folders = {folder};
        Command command;
        command.id = QStringLiteral("c_marca");
        command.name = QStringLiteral("Marca");
        command.folderId = folder.id;
        command.cliPath = QStringLiteral("marca");
        command.type = CommandType::Command;
        command.command = QStringLiteral("echo via=$KAI_VIA_TARGET > %1").arg(outputFile);
        data.commands = {command};
        QVERIFY(configManager.saveCommands(data));

        const LocalExecutionOutcome outcome = runGlobalCliDiscover(
            {QStringLiteral("kai"), QStringLiteral("grupo"), QStringLiteral("marca")});
        QVERIFY(configManager.saveSettings(originalSettings));
        QVERIFY(configManager.saveCommands(CommandsData{}));
        QVERIFY(outcome.handled);
        QCOMPARE(outcome.exitCode, 0);

        QFile out(outputFile);
        QVERIFY2(out.open(QIODevice::ReadOnly), "comando global não rodou");
        QCOMPARE(QString::fromUtf8(out.readAll()).trimmed(), QStringLiteral("via=sim"));
    }

    // kai.exe (Windows) chamado de uma pasta do WSL: o modo local roda o
    // comando DENTRO da distro de origem (como um kai nativo do Linux
    // rodaria), não no cmd.exe — "docker"/"./script.sh" não existem lá.
    void wslUncCwdMapsToABridgeIntoTheSameDistro()
    {
        const std::optional<TerminalProfile> bridge =
            localWslBridgeProfile(QStringLiteral("//wsl.localhost/Ubuntu-22.04/home/corin/projects/kai"));
        QVERIFY(bridge.has_value());
        QVERIFY(bridge->isDefault);
        QCOMPARE(bridge->shell, ShellFlavor::Posix);
        // Sem aspas: `wsl.exe -d "Ubuntu"` falha com WSL_E_DISTRO_NOT_FOUND.
        QVERIFY2(bridge->commandTemplate.startsWith(QStringLiteral("wsl.exe -d Ubuntu-22.04 -- ")),
                 qPrintable(bridge->commandTemplate));
        QVERIFY(bridge->commandTemplate.contains(QStringLiteral("{{command_b64}}")));

        QVERIFY(localWslBridgeProfile(QStringLiteral("\\\\wsl$\\Debian\\home")).has_value());
        QVERIFY(!localWslBridgeProfile(QStringLiteral("C:/Users/corin/projeto")).has_value());
        QVERIFY(!localWslBridgeProfile(QStringLiteral("/home/corin/projects/kai")).has_value());
        QVERIFY(!localWslBridgeProfile(QStringLiteral("//fileserver/share/projeto")).has_value());
        QVERIFY(!localWslBridgeProfile(QStringLiteral("//wsl.localhost/Minha Distro&x/home")).has_value());
    }
};

QTEST_MAIN(TestCliLocalExecutor)
#include "test_cli_local_executor.moc"
