#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

#include "utils/autostart-manager.h"
#include "core/config-manager.h"

using namespace kai;

// Cobre a configuração global de autostart (autoboot) e o AutostartManager.
// No Linux, o registro é um .desktop em $XDG_CONFIG_HOME/autostart/kai.desktop,
// então isolamos XDG_CONFIG_HOME num diretório temporário para não tocar
// no ambiente real do usuário. No Windows o backend é o registro (HKCU Run);
// lá os testes de arquivo são pulados, mas o round-trip da config vale igual.
class TestAutostartManager : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        m_tempDir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tempDir->isValid());
        qputenv("XDG_CONFIG_HOME", m_tempDir->path().toUtf8());
    }

    void cleanup()
    {
        // Garante que não fica registro pendente entre testes.
        utils::AutostartManager::setEnabled(false, QStringLiteral("/tmp/kai"));
        m_tempDir.reset();
    }

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    void enableCreatesDesktopEntryWithExecPath()
    {
        const QString exec = QStringLiteral("/usr/local/bin/kai");
        QVERIFY(utils::AutostartManager::setEnabled(true, exec));
        QVERIFY(utils::AutostartManager::isEnabled());

        const QString desktopPath = QDir(m_tempDir->path()).filePath(QStringLiteral("autostart/kai.desktop"));
        QVERIFY(QFile::exists(desktopPath));

        QFile file(desktopPath);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();

        QVERIFY(content.contains(QStringLiteral("[Desktop Entry]")));
        QVERIFY(content.contains(QStringLiteral("Type=Application")));
        QVERIFY(content.contains(QStringLiteral("Exec=/usr/local/bin/kai")));
        QVERIFY(content.contains(QStringLiteral("X-GNOME-Autostart-enabled=true")));
    }

    void execWithSpacesIsQuoted()
    {
        const QString exec = QStringLiteral("/home/user/My Apps/kai");
        QVERIFY(utils::AutostartManager::setEnabled(true, exec));

        const QString desktopPath = QDir(m_tempDir->path()).filePath(QStringLiteral("autostart/kai.desktop"));
        QFile file(desktopPath);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();

        QVERIFY(content.contains(QStringLiteral("Exec=\"/home/user/My Apps/kai\"")));
    }

    void disableRemovesDesktopEntry()
    {
        const QString exec = QStringLiteral("/usr/local/bin/kai");
        QVERIFY(utils::AutostartManager::setEnabled(true, exec));
        QVERIFY(utils::AutostartManager::isEnabled());

        QVERIFY(utils::AutostartManager::setEnabled(false, exec));
        QVERIFY(!utils::AutostartManager::isEnabled());

        const QString desktopPath = QDir(m_tempDir->path()).filePath(QStringLiteral("autostart/kai.desktop"));
        QVERIFY(!QFile::exists(desktopPath));
    }

    void disableWhenNotEnabledIsNoOpAndSucceeds()
    {
        QVERIFY(!utils::AutostartManager::isEnabled());
        QVERIFY(utils::AutostartManager::setEnabled(false, QStringLiteral("/usr/local/bin/kai")));
        QVERIFY(!utils::AutostartManager::isEnabled());
    }

    void syncOnlyChangesStateWhenDiverging()
    {
        const QString exec = QStringLiteral("/usr/local/bin/kai");
        QVERIFY(!utils::AutostartManager::isEnabled());

        // sync(false) num estado já desabilitado: mantém desabilitado.
        QVERIFY(utils::AutostartManager::sync(false, exec));
        QVERIFY(!utils::AutostartManager::isEnabled());

        // sync(true): habilita.
        QVERIFY(utils::AutostartManager::sync(true, exec));
        QVERIFY(utils::AutostartManager::isEnabled());

        // sync(true) de novo: idempotente, continua habilitado.
        QVERIFY(utils::AutostartManager::sync(true, exec));
        QVERIFY(utils::AutostartManager::isEnabled());
    }
#endif

    // Round-trip da config: autostart deve persistir em settings.json.
    void autostartSettingRoundTrips()
    {
        core::ConfigManager manager;

        core::SettingsData data = manager.loadSettings();
        QCOMPARE(data.autostart, false); // default

        data.autostart = true;
        QVERIFY(manager.saveSettings(data));

        const core::SettingsData reloaded = manager.loadSettings();
        QCOMPARE(reloaded.autostart, true);
    }

    // REGRESSÃO (bug reportado: "o Kai não está inicializando no Windows por
    // padrão, mesmo com a flag marcada"). Duas causas somadas: (1) o registro
    // só era escrito quando a opção MUDAVA no diálogo; (2) o sync() só checava
    // se a entrada EXISTIA, não se apontava para o executável atual — então
    // reinstalar em outra pasta deixava um caminho morto.
    void syncFixesStaleExecutablePath()
    {
        const QString oldPath = QDir::temp().filePath(QStringLiteral("kai-old/kai"));
        const QString newPath = QDir::temp().filePath(QStringLiteral("kai-new/kai"));

        // Registra apontando para o caminho ANTIGO.
        QVERIFY(utils::AutostartManager::setEnabled(true, oldPath));
        QVERIFY(utils::AutostartManager::isEnabled());
        QCOMPARE(QDir::toNativeSeparators(utils::AutostartManager::registeredTarget()),
                 QDir::toNativeSeparators(oldPath));

        // sync com o caminho NOVO deve CORRIGIR o destino (antes não corrigia,
        // porque isEnabled() já era true e ele considerava tudo em ordem).
        QVERIFY(utils::AutostartManager::sync(true, newPath));
        QCOMPARE(QDir::toNativeSeparators(utils::AutostartManager::registeredTarget()),
                 QDir::toNativeSeparators(newPath));

        // Desligar remove de fato.
        QVERIFY(utils::AutostartManager::sync(false, newPath));
        QVERIFY(!utils::AutostartManager::isEnabled());
    }

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;

};

QTEST_MAIN(TestAutostartManager)
#include "test_autostart_manager.moc"
