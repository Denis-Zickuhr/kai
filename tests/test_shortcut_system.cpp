#include <QTest>
#include <QKeyEvent>
#include <QTemporaryDir>

#include "ui/shared/shortcut-capture-field.h"
#include "core/config-manager.h"

using namespace kai::ui;
using namespace kai::core;

// Cobre a "revolução dos atalhos": o campo inteligente de
// captura de teclas e a persistência dos novos atalhos em settings.json.
class TestShortcutSystem : public QObject {
    Q_OBJECT

private slots:
    void captureFieldStartsWithInitialSequence()
    {
        ShortcutCaptureField field(QStringLiteral("Ctrl+Q"));
        QCOMPARE(field.keySequenceString(), QStringLiteral("Ctrl+Q"));
        QVERIFY(field.isReadOnly()); // não aceita digitação livre
    }

    void captureFieldDetectsPressedCombination()
    {
        ShortcutCaptureField field;
        QVERIFY(field.keySequenceString().isEmpty());

        // Simula o usuário pressionando Ctrl+Shift+N.
        QKeyEvent event(QEvent::KeyPress, Qt::Key_N,
                        Qt::ControlModifier | Qt::ShiftModifier);
        QCoreApplication::sendEvent(&field, &event);

        QCOMPARE(field.keySequenceString(), QStringLiteral("Ctrl+Shift+N"));
    }

    void captureFieldIgnoresBareModifiers()
    {
        ShortcutCaptureField field(QStringLiteral("F2"));

        // Pressionar apenas Ctrl (sem tecla real) não deve alterar o valor.
        QKeyEvent ctrlOnly(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
        QCoreApplication::sendEvent(&field, &ctrlOnly);
        QCOMPARE(field.keySequenceString(), QStringLiteral("F2"));
    }

    void captureFieldBackspaceClearsSequence()
    {
        ShortcutCaptureField field(QStringLiteral("Ctrl+N"));
        QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
        QCoreApplication::sendEvent(&field, &backspace);
        QVERIFY(field.keySequenceString().isEmpty());
    }

    void captureFieldEscapeDoesNotChangeValue()
    {
        ShortcutCaptureField field(QStringLiteral("Ctrl+Q"));
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(&field, &escape);
        QCOMPARE(field.keySequenceString(), QStringLiteral("Ctrl+Q"));
    }

    void newShortcutsPersistAndReloadFromSettingsJson()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        // ConfigManager usa AppConfigLocation; forçamos um diretório
        // isolado via HOME/XDG para não tocar no config real do usuário.
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        ConfigManager config;
        SettingsData data;
        data.editItemShortcut = QStringLiteral("F2");
        data.deleteItemShortcut = QStringLiteral("Del");
        data.newFolderShortcut = QStringLiteral("Ctrl+Shift+N");
        data.newCommandShortcut = QStringLiteral("Ctrl+N");
        data.quitAppShortcut = QStringLiteral("Ctrl+Q");
        data.toggleSearchShortcut = QStringLiteral("Ctrl+F");
        QVERIFY(config.saveSettings(data));

        const SettingsData reloaded = config.loadSettings();
        QCOMPARE(reloaded.editItemShortcut, QStringLiteral("F2"));
        QCOMPARE(reloaded.deleteItemShortcut, QStringLiteral("Del"));
        QCOMPARE(reloaded.newFolderShortcut, QStringLiteral("Ctrl+Shift+N"));
        QCOMPARE(reloaded.newCommandShortcut, QStringLiteral("Ctrl+N"));
        QCOMPARE(reloaded.quitAppShortcut, QStringLiteral("Ctrl+Q"));
        QCOMPARE(reloaded.toggleSearchShortcut, QStringLiteral("Ctrl+F"));
    }

    void defaultsAreSensibleWhenSettingsAbsent()
    {
        SettingsData data;
        QCOMPARE(data.editItemShortcut, QStringLiteral("F2"));
        QCOMPARE(data.deleteItemShortcut, QStringLiteral("Delete"));
        QCOMPARE(data.newFolderShortcut, QStringLiteral("Ctrl+Shift+N"));
        QCOMPARE(data.newCommandShortcut, QStringLiteral("Ctrl+N"));
        QCOMPARE(data.quitAppShortcut, QStringLiteral("Ctrl+Q"));
        QCOMPARE(data.toggleSearchShortcut, QStringLiteral("Ctrl+F"));
    }
};

QTEST_MAIN(TestShortcutSystem)
#include "test_shortcut_system.moc"
