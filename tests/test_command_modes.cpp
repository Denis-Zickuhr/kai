#include <QTest>
#include <QPlainTextEdit>
#include <QTemporaryDir>

#include "ui/command-json-editor-dialog.h"
#include "core/config-manager.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Cobre a feature de modos de criação/edição de comandos:
// persistência dos modos em settings.json e o editor de JSON cru
// (CommandJsonEditorDialog) usado no modo avançado.
class TestCommandModes : public QObject {
    Q_OBJECT

private slots:
    void defaultModesAreStandard()
    {
        SettingsData data;
        QCOMPARE(data.commandCreationMode, QStringLiteral("standard"));
        QCOMPARE(data.commandEditMode, QStringLiteral("standard"));
    }

    void modesPersistAndReload()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        ConfigManager config;
        SettingsData data;
        data.commandCreationMode = QStringLiteral("advanced");
        data.commandEditMode = QStringLiteral("standard");
        QVERIFY(config.saveSettings(data));

        const SettingsData reloaded = config.loadSettings();
        QCOMPARE(reloaded.commandCreationMode, QStringLiteral("advanced"));
        QCOMPARE(reloaded.commandEditMode, QStringLiteral("standard"));
    }

    void jsonEditorCreationStartsWithPrefilledTemplate()
    {
        // Criação (existingCommand == nullptr): o campo já começa com um
        // template JSON contendo o folder_id de destino resolvido.
        CommandJsonEditorDialog dialog(nullptr, QStringLiteral("f_target"), nullptr);
        auto *field = dialog.findChild<QPlainTextEdit *>();
        QVERIFY(field != nullptr);
        const QString text = field->toPlainText();
        QVERIFY(text.contains(QStringLiteral("\"folder_id\": \"f_target\"")));
        QVERIFY(text.contains(QStringLiteral("\"type\": \"shell\"")));
        QVERIFY(text.contains(QStringLiteral("\"hooks\"")));
    }

    void jsonEditorEditShowsSerializedCommand()
    {
        Command cmd;
        cmd.id = QStringLiteral("c_1");
        cmd.folderId = QStringLiteral("f_1");
        cmd.name = QStringLiteral("Build");
        cmd.type = CommandType::Shell;
        cmd.command = QStringLiteral("make release");

        CommandJsonEditorDialog dialog(&cmd, QStringLiteral("f_1"), nullptr);
        auto *field = dialog.findChild<QPlainTextEdit *>();
        QVERIFY(field != nullptr);
        const QString text = field->toPlainText();
        QVERIFY(text.contains(QStringLiteral("\"name\": \"Build\"")));
        QVERIFY(text.contains(QStringLiteral("make release")));
    }

    void jsonEditorParsesEditedJsonBackIntoCommand()
    {
        CommandJsonEditorDialog dialog(nullptr, QStringLiteral("f_target"), nullptr);
        auto *field = dialog.findChild<QPlainTextEdit *>();
        QVERIFY(field != nullptr);

        field->setPlainText(QStringLiteral(
            "{\n"
            "  \"name\": \"Deploy\",\n"
            "  \"folder_id\": \"f_target\",\n"
            "  \"type\": \"shell\",\n"
            "  \"command\": \"./deploy.sh\",\n"
            "  \"is_background\": true\n"
            "}"));

        // Aceita via o slot de validação (mesmo caminho do botão OK).
        QMetaObject::invokeMethod(&dialog, "handleAcceptRequested");

        const Command built = dialog.buildCommand();
        QCOMPARE(built.name, QStringLiteral("Deploy"));
        QCOMPARE(built.folderId, QStringLiteral("f_target"));
        QCOMPARE(built.command, QStringLiteral("./deploy.sh"));
        QVERIFY(built.isBackground);
        // id gerado automaticamente quando o JSON não traz um.
        QVERIFY(!built.id.isEmpty());
    }
};

QTEST_MAIN(TestCommandModes)
#include "test_command_modes.moc"
