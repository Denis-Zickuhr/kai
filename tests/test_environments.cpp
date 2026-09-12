// Testes da feature de Environments como pacotes selecionáveis:
//  - migração do global_env_vars legado para um pacote "Global";
//  - round-trip (save/load) da lista de environments e do pacote ativo;
//  - seleção/persistência do environment ativo.
// Usa QStandardPaths em modo de teste para isolar em um diretório temporário
// (não toca no ~/.config/kai real do usuário).

#include <QTest>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/config-manager.h"

using namespace kai::core;

class TestEnvironments : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // Garante um diretório limpo por execução.
        ConfigManager cm;
        const QString dir = cm.configDirPath();
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
    }

    // Um settings.json legado (só global_env_vars, sem environments) deve
    // migrar para um pacote "Global" com essas variáveis, e ficar ativo.
    void legacyGlobalEnvVarsMigrateToPackage()
    {
        ConfigManager cm;
        // Escreve um settings.json legado à mão.
        QJsonObject root;
        QJsonObject env;
        env["PORT"] = "8080";
        env["TOKEN"] = "abc";
        root["global_env_vars"] = env;
        QFile f(cm.settingsFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
        f.close();

        const SettingsData loaded = cm.loadSettings();
        QCOMPARE(loaded.environments.size(), 1);
        QCOMPARE(loaded.environments.first().name, QStringLiteral("Global"));
        QCOMPARE(loaded.environments.first().vars.value(QStringLiteral("PORT")), QStringLiteral("8080"));
        QCOMPARE(loaded.environments.first().vars.value(QStringLiteral("TOKEN")), QStringLiteral("abc"));
        // O pacote migrado deve estar ativo.
        QCOMPARE(loaded.activeEnvironmentId, loaded.environments.first().id);
    }

    // Round-trip: salvar múltiplos environments + ativo e recarregar mantém
    // tudo idêntico.
    void environmentsRoundTrip()
    {
        ConfigManager cm;
        // Limpa o arquivo pra não herdar do teste anterior.
        QFile::remove(cm.settingsFilePath());

        SettingsData s;
        Environment dev;
        dev.id = "env_dev";
        dev.name = "Dev";
        dev.vars.insert("BASE_URL", "http://localhost:3000");
        Environment prod;
        prod.id = "env_prod";
        prod.name = "Prod";
        prod.vars.insert("BASE_URL", "https://api.prod.com");
        s.environments = {dev, prod};
        s.activeEnvironmentId = "env_prod";

        QVERIFY(cm.saveSettings(s));

        const SettingsData loaded = cm.loadSettings();
        QCOMPARE(loaded.environments.size(), 2);
        QCOMPARE(loaded.environments.at(0).id, QStringLiteral("env_dev"));
        QCOMPARE(loaded.environments.at(1).id, QStringLiteral("env_prod"));
        QCOMPARE(loaded.environments.at(1).vars.value("BASE_URL"), QStringLiteral("https://api.prod.com"));
        QCOMPARE(loaded.activeEnvironmentId, QStringLiteral("env_prod"));
    }

    // Variáveis secretas: as chaves marcadas como secretas sobrevivem ao
    // round-trip (save/load) junto com os valores.
    void secretKeysRoundTrip()
    {
        ConfigManager cm;
        QFile::remove(cm.settingsFilePath());
        SettingsData s;
        Environment e;
        e.id = "env_sec";
        e.name = "Sec";
        e.vars.insert("API_KEY", "super-secret");
        e.vars.insert("PUBLIC", "ok");
        e.secretKeys.insert("API_KEY");
        s.environments = {e};
        s.activeEnvironmentId = "env_sec";
        QVERIFY(cm.saveSettings(s));

        const SettingsData loaded = cm.loadSettings();
        QCOMPARE(loaded.environments.size(), 1);
        QVERIFY(loaded.environments.first().secretKeys.contains(QStringLiteral("API_KEY")));
        QVERIFY(!loaded.environments.first().secretKeys.contains(QStringLiteral("PUBLIC")));
        QCOMPARE(loaded.environments.first().vars.value("API_KEY"), QStringLiteral("super-secret"));
    }

    // Alvos de terminal: a flag usePty (TTY por alvo) sobrevive ao
    // round-trip (save/load). Um alvo é "tty", outro "robusto".
    void terminalTargetUsePtyRoundTrip()
    {
        ConfigManager cm;
        QFile::remove(cm.settingsFilePath());
        SettingsData s;
        TerminalProfile tty;
        tty.name = "WSL (tty)";
        tty.commandTemplate = "wsl -d Ubuntu -- bash -lc '{{command}}'";
        tty.usePty = true;
        TerminalProfile robust;
        robust.name = "WSL robusto";
        robust.commandTemplate = "wsl -d Ubuntu -- bash -lc \"echo {{command_b64}} | base64 -d | bash\"";
        robust.usePty = false;
        s.terminalProfiles = {tty, robust};
        QVERIFY(cm.saveSettings(s));

        const SettingsData loaded = cm.loadSettings();
        // +1 pelo pseudo-alvo "Default", sempre presente (auto-seedado
        // pra dar um lugar de configurar Saída/TTY sem precisar de
        // wrapping — ver ConfigManager::loadSettings).
        QCOMPARE(loaded.terminalProfiles.size(), 3);
        bool sawTty = false, sawRobust = false;
        for (const TerminalProfile &t : loaded.terminalProfiles) {
            if (t.name == QStringLiteral("WSL (tty)")) { sawTty = true; QVERIFY(t.usePty); }
            if (t.name == QStringLiteral("WSL robusto")) { sawRobust = true; QVERIFY(!t.usePty); }
        }
        QVERIFY(sawTty && sawRobust);
    }

    // Se o activeEnvironmentId apontar para um pacote inexistente, o load
    // deve cair no primeiro pacote existente (fallback seguro).
    void activeIdFallsBackWhenMissing()
    {
        ConfigManager cm;
        QFile::remove(cm.settingsFilePath());

        SettingsData s;
        Environment a;
        a.id = "env_a";
        a.name = "A";
        s.environments = {a};
        s.activeEnvironmentId = "env_inexistente";
        QVERIFY(cm.saveSettings(s));

        const SettingsData loaded = cm.loadSettings();
        QCOMPARE(loaded.activeEnvironmentId, QStringLiteral("env_a"));
    }
};

QTEST_MAIN(TestEnvironments)
#include "test_environments.moc"
