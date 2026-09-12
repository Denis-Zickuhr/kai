#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

#include <algorithm>

#include "core/config-manager.h"

using namespace kai::core;

// Persistência e Recuperação de Configuração Corrompida.
class TestConfigManager : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        // Isola cada teste em um diretório de config temporário, evitando
        // tocar no ~/.config/kai real da máquina do usuário.
        m_tempDir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tempDir->isValid());
        qputenv("XDG_CONFIG_HOME", m_tempDir->path().toUtf8());
    }

    void cleanup()
    {
        m_tempDir.reset();
    }

    // escreve JSON inválido em commands.json, espera que o Kai crie
    // backup .bak.[TIMESTAMP], restaure estado vazio e não crashe.
    void corruptedCommandsFileIsRecoveredWithBackup()
    {
        ConfigManager manager;
        const QString path = manager.commandsFilePath();
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArrayLiteral(R"({"folders": [invalido...})"));
        file.close();

        QSignalSpy recoveredSpy(&manager, &ConfigManager::configRecovered);

        const CommandsData data = manager.loadCommands();

        QCOMPARE(data.folders.size(), 0);
        QCOMPARE(data.commands.size(), 0);
        QCOMPARE(recoveredSpy.count(), 1);

        const QString backupPath = recoveredSpy.at(0).at(1).toString();
        QVERIFY(!backupPath.isEmpty());
        QVERIFY(QFile::exists(backupPath));
        QVERIFY(backupPath.contains(QStringLiteral("commands.json.bak.")));
    }

    void corruptedSettingsFileIsRecoveredWithBackup()
    {
        ConfigManager manager;
        const QString path = manager.settingsFilePath();
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArrayLiteral("{ this is not json"));
        file.close();

        QSignalSpy recoveredSpy(&manager, &ConfigManager::configRecovered);

        const SettingsData data = manager.loadSettings();

        // Recuperação devolve os DEFAULTS do modelo. O tema padrão passou a
        // ser "kai-dark" na repaginação visual (era "dracula").
        QCOMPARE(data.activeTheme, QStringLiteral("kai-dark"));
        QCOMPARE(recoveredSpy.count(), 1);
    }

    // Round-trip: salvar e recarregar deve preservar os dados (valida a
    // escrita atômica tmp+rename).
    void saveAndLoadRoundTripPreservesData()
    {
        ConfigManager manager;

        Folder folder;
        folder.id = "f_test";
        folder.name = "Test Folder";
        folder.envVars["PORT"] = "8080";

        Command cmd;
        cmd.id = "c_test";
        cmd.folderId = "f_test";
        cmd.name = "Echo";
        cmd.command = "echo hello";

        CommandsData data;
        data.folders << folder;
        data.commands << cmd;

        QVERIFY(manager.saveCommands(data));
        QVERIFY(QFile::exists(manager.commandsFilePath()));

        const CommandsData reloaded = manager.loadCommands();
        QCOMPARE(reloaded.folders.size(), 1);
        QCOMPARE(reloaded.commands.size(), 1);
        QCOMPARE(reloaded.folders.at(0).name, QStringLiteral("Test Folder"));
        QCOMPARE(reloaded.commands.at(0).command, QStringLiteral("echo hello"));
    }

    void loadingMissingFileReturnsEmptyStateWithoutCrash()
    {
        ConfigManager manager;
        const CommandsData data = manager.loadCommands();
        QCOMPARE(data.folders.size(), 0);
        QCOMPARE(data.commands.size(), 0);
    }

    // Dois comandos com o MESMO id no commands.json (ex: importação/versão
    // antiga): o load deve PRESERVAR ambos, renomeando o segundo para um id
    // único — em vez de um sobrescrever o outro no mapa id->comando
    // (bug real: params/hooks "sumiam" ao executar porque o comando errado
    // vencia). O PRIMEIRO mantém o id original.
    void loadDeduplicatesRepeatedCommandIds()
    {
        ConfigManager manager;
        const QString path = manager.commandsFilePath();
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));

        // Primeiro comando (com 1 parâmetro) e um segundo com o MESMO id
        // (sem parâmetros) — a colisão que causava o bug.
        const QString json = QStringLiteral(R"({
            "folders": [],
            "commands": [
                {"id": "c_dup", "name": "A", "type": "shell", "command": "x",
                 "params": [{"name": "t", "type": "text", "label": "T", "default": ""}]},
                {"id": "c_dup", "name": "B", "type": "shell", "command": "y", "params": []}
            ]
        })");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(json.toUtf8());
        file.close();

        const CommandsData data = manager.loadCommands();
        QCOMPARE(data.commands.size(), 2);
        // Ambos sobrevivem com ids DISTINTOS.
        QVERIFY(data.commands.at(0).id != data.commands.at(1).id);
        // O primeiro mantém o id original e os seus parâmetros.
        QCOMPARE(data.commands.at(0).id, QStringLiteral("c_dup"));
        QCOMPARE(data.commands.at(0).params.size(), 1);
    }

    // (3) As opções de exibição da Saída persistem em settings.json entre
    // sessões (feedback do usuário). Round-trip dos 6 campos.
    void outputViewOptionsRoundTrip()
    {
        ConfigManager manager;
        SettingsData s = manager.loadSettings();
        s.outputLineNumbers = true;
        s.outputWrap = true;
        s.outputTimestamps = true;
        s.outputAutoScroll = false;
        s.outputCompact = true;
        s.outputFontSize = 14;
        QVERIFY(manager.saveSettings(s));

        const SettingsData loaded = manager.loadSettings();
        QCOMPARE(loaded.outputLineNumbers, true);
        QCOMPARE(loaded.outputWrap, true);
        QCOMPARE(loaded.outputTimestamps, true);
        QCOMPARE(loaded.outputAutoScroll, false);
        QCOMPARE(loaded.outputCompact, true);
        QCOMPARE(loaded.outputFontSize, 14);
    }

    // O sabor de shell do alvo de terminal persiste (bug do export).
    void terminalTargetShellFlavorRoundTrip()
    {
        ConfigManager manager;
        SettingsData s = manager.loadSettings();
        TerminalProfile t;
        t.name = QStringLiteral("PS");
        t.commandTemplate = QStringLiteral("powershell.exe -Command {{command}}");
        t.shell = ShellFlavor::PowerShell;
        t.usePty = false;
        s.terminalProfiles = {t};
        QVERIFY(manager.saveSettings(s));

        const SettingsData loaded = manager.loadSettings();
        // +1 pelo pseudo-alvo "Default", sempre presente (auto-seedado
        // pra dar um lugar de configurar Saída/TTY sem precisar de
        // wrapping — ver ConfigManager::loadSettings).
        QCOMPARE(loaded.terminalProfiles.size(), 2);
        const auto it = std::find_if(loaded.terminalProfiles.constBegin(), loaded.terminalProfiles.constEnd(),
            [](const TerminalProfile &t) { return t.name == QStringLiteral("PS"); });
        QVERIFY(it != loaded.terminalProfiles.constEnd());
        QCOMPARE(it->shell, ShellFlavor::PowerShell);
        QCOMPARE(it->usePty, false);
    }

    // Migração: um alvo PowerShell antigo com a gambiarra $l[2..] é reescrito
    // na carga (o template não contém mais o corte de linhas). Escrevemos o
    // alvo legado via API (saveSettings) e depois forçamos o padrão legado no
    // template para simular a config antiga.
    void legacyPowerShellTemplateIsMigratedOnLoad()
    {
        ConfigManager manager;
        SettingsData s = manager.loadSettings();
        TerminalProfile legacy;
        legacy.name = QStringLiteral("PS");
        legacy.shell = ShellFlavor::PowerShell;
        legacy.commandTemplate =
            QStringLiteral("powershell.exe -Command \"$l=$c.Split([char]10);"
                           "$c=[string]::Join([char]10,$l[2..($l.Length-1)]);Invoke-Expression($c)\"");
        s.terminalProfiles = {legacy};
        QVERIFY(manager.saveSettings(s));

        const SettingsData loaded = manager.loadSettings();
        // +1 pelo pseudo-alvo "Default", sempre presente — ver comentário
        // equivalente em terminalTargetShellFlavorRoundTrip().
        QCOMPARE(loaded.terminalProfiles.size(), 2);
        const auto it = std::find_if(loaded.terminalProfiles.constBegin(), loaded.terminalProfiles.constEnd(),
            [](const TerminalProfile &t) { return t.name == QStringLiteral("PS"); });
        QVERIFY(it != loaded.terminalProfiles.constEnd());
        QVERIFY2(!it->commandTemplate.contains(QStringLiteral("[2..")),
                 "o template PowerShell legado não foi migrado (ainda tem $l[2..])");
    }

    // Notificações (feature nova): os 6 toggles devem sobreviver ao ciclo
    // salvar->carregar, e o default de uma instalação nova (sem
    // settings.json prévio) precisa ser o opt-in seguro (master switch
    // desligado, só sucesso de background desligado por padrão também).
    void notificationSettingsRoundTrip()
    {
        ConfigManager manager;

        const SettingsData defaults = manager.loadSettings();
        QVERIFY(!defaults.notificationsEnabled);
        QVERIFY(defaults.notifyOnCommandFailure);
        QVERIFY(defaults.notifyOnBackgroundProcessCrash);
        QVERIFY(!defaults.notifyOnBackgroundProcessSuccess);
        QVERIFY(defaults.notifyOnConfigRecovered);
        QVERIFY(!defaults.notifyEvenWhenFocused);

        SettingsData s = defaults;
        s.notificationsEnabled = true;
        s.notifyOnCommandFailure = false;
        s.notifyOnBackgroundProcessCrash = false;
        s.notifyOnBackgroundProcessSuccess = true;
        s.notifyOnConfigRecovered = false;
        s.notifyEvenWhenFocused = true;
        QVERIFY(manager.saveSettings(s));

        const SettingsData loaded = manager.loadSettings();
        QVERIFY(loaded.notificationsEnabled);
        QVERIFY(!loaded.notifyOnCommandFailure);
        QVERIFY(!loaded.notifyOnBackgroundProcessCrash);
        QVERIFY(loaded.notifyOnBackgroundProcessSuccess);
        QVERIFY(!loaded.notifyOnConfigRecovered);
        QVERIFY(loaded.notifyEvenWhenFocused);
    }

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;

    // Repaginação visual: densidade, estilo de canto e flags de efeito devem
    // sobreviver ao ciclo salvar->carregar (senão a preferência do usuário se
    // perde no próximo boot).
    void appearanceSettingsRoundTrip()
    {
        ConfigManager manager;

        SettingsData s;
        s.activeTheme = QStringLiteral("kai-liquid");
        s.uiDensity = QStringLiteral("compact");
        s.uiCornerStyle = 2;
        s.fxShadows = true;
        s.fxTranslucency = true;
        s.fxBlur = true;
        s.fxAnimations = true;
        QVERIFY(manager.saveSettings(s));

        const SettingsData loaded = manager.loadSettings();
        QCOMPARE(loaded.activeTheme, QStringLiteral("kai-liquid"));
        QCOMPARE(loaded.uiDensity, QStringLiteral("compact"));
        QCOMPARE(loaded.uiCornerStyle, 2);
        QVERIFY(loaded.fxShadows);
        QVERIFY(loaded.fxTranslucency);
        QVERIFY(loaded.fxBlur);
        QVERIFY(loaded.fxAnimations);
    }
};

QTEST_MAIN(TestConfigManager)
#include "test_config_manager.moc"
