#include <QTest>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QTemporaryDir>

#include "non-fresh-config.h"
#include "ui/main-window.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/features/output/pty-terminal-widget.h"
#include "core/config-manager.h"

using namespace kai::ui;
using namespace kai::core;

// O "X" da barra e a ação "Ocultar" só escondem a janela (hide), mantendo o processo; só Ctrl+Q / "Sair" encerra.
// Reexibir é pelo atalho global (xcb/X11) ou pela bandeja.
class TestWindowVisibility : public QObject {
    Q_OBJECT

private slots:
    void toggleHidesAndShowsKeepingProcessAlive()
    {
        MainWindow window;
        window.show();
        QTest::qWait(50);
        QVERIFY(window.isVisible());

        QMetaObject::invokeMethod(&window, "toggleVisibility");
        QTest::qWait(50);
        QVERIFY(!window.isVisible());

        QMetaObject::invokeMethod(&window, "toggleVisibility");
        QTest::qWait(50);
        QVERIFY(window.isVisible());
    }

    // Regressão: o clique no ícone da barra de tarefas não minimizava. Com FramelessWindowHint o Qt não aplica os hints
    // padrão; sem WindowMinimizeButtonHint/WindowSystemMenuHint o Windows cria a janela sem WS_MINIMIZEBOX/WS_SYSMENU e
    // ignora o SC_MINIMIZE.
    void framelessWindowKeepsNativeMinimizeHints()
    {
        MainWindow window;
        const Qt::WindowFlags flags = window.windowFlags();
        QVERIFY(flags & Qt::FramelessWindowHint);
        QVERIFY(flags & Qt::WindowMinimizeButtonHint);
        QVERIFY(flags & Qt::WindowSystemMenuHint);
    }

    void closingWindowHidesButKeepsAppRunning()
    {
        MainWindow window;
        window.show();
        QTest::qWait(50);
        QVERIFY(window.isVisible());

        QCloseEvent closeEvent;
        QCoreApplication::sendEvent(&window, &closeEvent);
        QTest::qWait(50);

        // O "X" oculta a janela mas não encerra o app: o evento é ignorado.
        QVERIFY(!closeEvent.isAccepted());
        QVERIFY(!window.isVisible());
    }

    // startVisible=true (padrão) decide "visível" independente do atalho global, que pode falhar ao registrar
    // (WSL/WSLg, colisão com outro app) e nunca registra em offscreen.
    void shouldStartVisibleIsTrueByDefaultRegardlessOfHotkey()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        MainWindow window;
        QVERIFY(!window.globalHotkeyRegistered());
        QVERIFY(window.shouldStartVisible());
    }

    // Com "Iniciar visível" desligado a decisão é sempre oculto, sem cair no status do atalho global (antes a janela
    // abria quando o atalho falhava).
    void shouldStartVisibleIsAlwaysFalseWhenDisabledRegardlessOfHotkey()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        ConfigManager manager;
        SettingsData settings = manager.loadSettings();
        settings.startVisible = false;
        QVERIFY(manager.saveSettings(settings));

        MainWindow window;
        QVERIFY(!window.shouldStartVisible());
    }

    // Regressão: com windowMode "maximized"/"fullscreen" salvo, o construtor já mostrava a janela
    // (applyWindowGeometryPreference chamava showMaximized()/showFullScreen()), antes de main.cpp checar
    // shouldStartVisible().
    void windowStaysHiddenAtConstructionWhenMaximizedModeAndStartVisibleDisabled()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        ConfigManager manager;
        SettingsData settings = manager.loadSettings();
        settings.startVisible = false;
        settings.windowMode = QStringLiteral("maximized");
        QVERIFY(manager.saveSettings(settings));

        MainWindow window;
        // Sem window.show(): só a construção já não pode deixar a janela visível.
        QVERIFY(!window.isVisible());
    }

    // Mesmo teste para "fullscreen".
    void windowStaysHiddenAtConstructionWhenFullscreenModeAndStartVisibleDisabled()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        ConfigManager manager;
        SettingsData settings = manager.loadSettings();
        settings.startVisible = false;
        settings.windowMode = QStringLiteral("fullscreen");
        QVERIFY(manager.saveSettings(settings));

        MainWindow window;
        QVERIFY(!window.isVisible());
    }

    // Esc: QShortcut comum (não QHotkey), dispara só com a janela ativa e oculta como o botão X.
    void escShortcutHidesWindow()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(window.isVisible());

        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::qWait(50);
        QVERIFY(!window.isVisible());
    }

    // Esc não esconde a janela com o foco num terminal interativo: é tecla de uso comum lá (ex: sair do modo de
    // inserção do vim).
    void escShortcutDoesNotHideWindowWhileInteractiveTerminalFocused()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        useNonFreshConfig(tempDir); // config vazia = boas-vindas = Saída oculta

        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto *drawer = window.findChild<TerminalDrawer *>();
        QVERIFY(drawer != nullptr);
        drawer->setInteractiveMode(true);
        auto *pty = drawer->findChild<PtyTerminalWidget *>();
        QVERIFY(pty != nullptr);
        pty->setFocus();
        QTest::qWait(50);

        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::qWait(50);
        QVERIFY(window.isVisible());
    }
};

QTEST_MAIN(TestWindowVisibility)
#include "test_window_visibility.moc"
