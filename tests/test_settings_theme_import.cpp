#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QComboBox>

#include "ui/settings-dialog.h"
#include "core/config-manager.h"

using namespace kai::ui;

// Cobre o item "adicione a opção de escolher tema por arquivo... o sistema
// vai fazer uma cópia do tema e jogar na pasta onde ficam os outros"
// (feedback do usuário). Chama SettingsDialog::importThemeFromPath()
// diretamente (em vez de dirigir um QFileDialog real, que não dá pra
// automatizar num teste offscreen) — mesma lógica usada pelo handler do
// botão "Importar tema...".
class TestSettingsThemeImport : public QObject {
    Q_OBJECT

private slots:
    void importCopiesFileIntoThemesDirAndUpdatesCombo()
    {
        QTemporaryDir themesDir;
        QVERIFY(themesDir.isValid());
        QTemporaryDir sourceDir;
        QVERIFY(sourceDir.isValid());

        // Tema "base" já instalado + o arquivo que vamos importar (fora da
        // pasta de temas, simulando um download do usuário).
        const QString existingThemePath = QDir(themesDir.path()).filePath(QStringLiteral("dracula.json"));
        QFile existingTheme(existingThemePath);
        QVERIFY(existingTheme.open(QIODevice::WriteOnly));
        existingTheme.write("{\"schema_version\":1}");
        existingTheme.close();

        const QString sourcePath = QDir(sourceDir.path()).filePath(QStringLiteral("my-custom-theme.json"));
        QFile sourceFile(sourcePath);
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        sourceFile.write("{\"schema_version\":1,\"name\":\"My Custom Theme\"}");
        sourceFile.close();

        kai::core::SettingsData settings;
        settings.activeTheme = QStringLiteral("dracula");
        kai::core::CommandsData commandsData;
        QVector<kai::core::Collection> collections;
        SettingsDialog dialog(settings, {QStringLiteral("dracula")}, themesDir.path(),
            commandsData, collections, [] {}, [] {});

        const QString imported = dialog.importThemeFromPath(sourcePath, /*overwriteExisting=*/false);
        QCOMPARE(imported, QStringLiteral("my-custom-theme"));

        // O arquivo foi realmente copiado pra dentro de themesDirPath().
        QVERIFY(QFile::exists(QDir(themesDir.path()).filePath(QStringLiteral("my-custom-theme.json"))));

        // O combo de tema (aba Aparência) já reflete o novo tema, selecionado.
        auto *combo = dialog.findChild<QComboBox *>();
        // O primeiro QComboBox pode ser o de idioma dependendo da ordem de
        // criação dos widgets; procura especificamente pelo item importado
        // em QUALQUER combo da janela — o que importa é que ele apareça e
        // esteja selecionado em algum.
        bool foundSelected = false;
        for (QComboBox *box : dialog.findChildren<QComboBox *>()) {
            if (box->currentText() == QStringLiteral("my-custom-theme")) {
                foundSelected = true;
                break;
            }
        }
        Q_UNUSED(combo);
        QVERIFY(foundSelected);
    }

    void importFailsSilentlyWhenSourceDoesNotExist()
    {
        QTemporaryDir themesDir;
        QVERIFY(themesDir.isValid());

        kai::core::SettingsData settings;
        kai::core::CommandsData commandsData;
        QVector<kai::core::Collection> collections;
        SettingsDialog dialog(settings, {}, themesDir.path(), commandsData, collections, [] {}, [] {});

        const QString imported = dialog.importThemeFromPath(QStringLiteral("/tmp/does-not-exist-kai-test.json"), true);
        QVERIFY(imported.isEmpty());
    }

    void importWithoutOverwriteFlagRefusesCollision()
    {
        QTemporaryDir themesDir;
        QVERIFY(themesDir.isValid());
        QTemporaryDir sourceDir;
        QVERIFY(sourceDir.isValid());

        const QString destPath = QDir(themesDir.path()).filePath(QStringLiteral("clash.json"));
        QFile existing(destPath);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("{\"schema_version\":1}");
        existing.close();

        const QString sourcePath = QDir(sourceDir.path()).filePath(QStringLiteral("clash.json"));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("{\"schema_version\":2}");
        source.close();

        kai::core::SettingsData settings;
        kai::core::CommandsData commandsData;
        QVector<kai::core::Collection> collections;
        SettingsDialog dialog(settings, {}, themesDir.path(), commandsData, collections, [] {}, [] {});

        // Sem overwriteExisting=true, uma colisão de nome não deve mexer no
        // arquivo já instalado.
        const QString imported = dialog.importThemeFromPath(sourcePath, false);
        QVERIFY(imported.isEmpty());

        QFile destFile(destPath);
        QVERIFY(destFile.open(QIODevice::ReadOnly));
        QCOMPARE(destFile.readAll(), QByteArray("{\"schema_version\":1}"));
    }
};

QTEST_MAIN(TestSettingsThemeImport)
#include "test_settings_theme_import.moc"
