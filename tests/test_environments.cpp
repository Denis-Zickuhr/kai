// Environments como pacotes selecionáveis: migração do global_env_vars legado para um pacote "Global",
// round-trip da lista e do pacote ativo, persistência do ativo. QStandardPaths em modo de teste isola o
// diretório (não toca no ~/.config/kai real).

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
        ConfigManager cm;
        const QString dir = cm.configDirPath();
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
    }

    // Um settings.json legado (só global_env_vars) migra para um pacote "Global" ativo.
    void legacyGlobalEnvVarsMigrateToPackage()
    {
        ConfigManager cm;
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
        QCOMPARE(loaded.activeEnvironmentId, loaded.environments.first().id);
    }

    // Round-trip: múltiplos environments + ativo recarregam idênticos.
    void environmentsRoundTrip()
    {
        ConfigManager cm;
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

    // Chaves marcadas como secretas sobrevivem ao round-trip junto com os valores.
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

    // A flag usePty (TTY por alvo) sobrevive ao round-trip.
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
        QCOMPARE(loaded.terminalProfiles.size(), 2);
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
