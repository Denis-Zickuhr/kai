#include <QTest>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QTemporaryDir>

#include "ui/main-window.h"
#include "ui/terminal-drawer.h"
#include "ui/pty-terminal-widget.h"
#include "core/config-manager.h"

using namespace kai::ui;
using namespace kai::core;

// Semântica de janela (feedback do usuário): o "X" da barra e a
// ação "Ocultar" apenas escondem a janela (hide) mantendo o processo
// rodando; nenhum dos dois encerra o app (isso é só do Ctrl+Q / "Sair").
// Reexibir é feito pelo atalho global (funcional sob xcb/X11) ou bandeja.
class TestWindowVisibility : public QObject {
    Q_OBJECT

private slots:
    void toggleHidesAndShowsKeepingProcessAlive()
    {
        MainWindow window;
        window.show();
        QTest::qWait(50);
        QVERIFY(window.isVisible());

        // "Ocultar" (mesmo slot do atalho global e do menu): esconde.
        QMetaObject::invokeMethod(&window, "toggleVisibility");
        QTest::qWait(50);
        QVERIFY(!window.isVisible());

        // Alternar de novo reexibe a janela.
        QMetaObject::invokeMethod(&window, "toggleVisibility");
        QTest::qWait(50);
        QVERIFY(window.isVisible());
    }

    void closingWindowHidesButKeepsAppRunning()
    {
        MainWindow window;
        window.show();
        QTest::qWait(50);
        QVERIFY(window.isVisible());

        // Simula clicar no "X" da barra de título (QCloseEvent).
        QCloseEvent closeEvent;
        QCoreApplication::sendEvent(&window, &closeEvent);
        QTest::qWait(50);

        // O "X" fecha a JANELA (oculta) mas NÃO encerra o app: o evento é
        // ignorado (rejeitado) e a janela apenas fica escondida.
        QVERIFY(!closeEvent.isAccepted());
        QVERIFY(!window.isVisible());
    }

    // "Iniciar visível" (feedback do usuário: "abre sozinho" quando o
    // atalho global falha ao registrar por motivo alheio — WSL/WSLg,
    // colisão com outro app). settings.startVisible=true (padrão) deve
    // decidir "visível" independente do atalho — inclusive neste ambiente
    // de teste (offscreen), onde o atalho NUNCA registra de verdade.
    void shouldStartVisibleIsTrueByDefaultRegardlessOfHotkey()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        MainWindow window;
        QVERIFY(!window.globalHotkeyRegistered()); // offscreen: nunca registra
        QVERIFY(window.shouldStartVisible()); // startVisible=true por padrão
    }

    // Com "Iniciar visível" desligado, a decisão é SEMPRE oculto — NUNCA cai
    // de volta pro status do atalho global (bug relatado: a versão anterior
    // fazia esse fallback, então desligar a opção continuava abrindo a
    // janela quando o atalho falhava, o mesmo problema que a opção deveria
    // resolver).
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

    // Bug relatado: com windowMode "maximized"/"fullscreen" salvo,
    // MainWindow ficava visível LOGO NO CONSTRUTOR (applyWindowGeometryPreference
    // chamava showMaximized()/showFullScreen() incondicionalmente ali), bem
    // antes de main.cpp sequer chegar a checar shouldStartVisible() —
    // "Iniciar visível" desligado não fazia diferença nenhuma nesse caso.
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
        // Nunca chama window.show() — só a CONSTRUÇÃO já não deve deixar a
        // janela visível quando o modo é "maximized" e a opção está desligada.
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

    // Pedido do usuário: "quero um novo atalho, funcionara na janela
    // normal apenas [não global]... por padrão vai ser esc" — QShortcut
    // comum (não QHotkey), dispara só com a janela ativa. Esc some com a
    // janela igual ao botão X (hide simples, não fecha o app).
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

    // Esc NÃO pode esconder a janela enquanto o foco está dentro de um
    // terminal INTERATIVO — é tecla de uso comum lá dentro (ex: sair do
    // modo de inserção do vim); sequestrá-la quebraria o programa rodando
    // dentro do terminal.
    void escShortcutDoesNotHideWindowWhileInteractiveTerminalFocused()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

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
