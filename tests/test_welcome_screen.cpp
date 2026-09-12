#include <QTest>
#include <QTemporaryDir>
#include <QPushButton>
#include <QSignalSpy>

#include "core/config-manager.h"
#include "ui/main-window.h"
#include "ui/welcome-screen.h"
#include "ui/command-tree-widget.h"

using namespace kai::ui;
using kai::core::ConfigManager;
using kai::core::CommandsData;
using kai::core::Folder;
using kai::core::Command;

// Tela de boas-vindas estilo VSCode (pedido do usuário: "Ao bootar o kai
// sem nenhum comando, ele dar instruções básicas..."). Verifica a condição
// de exibição (0 comandos E 0 pastas — instalação limpa/config apagada,
// não "a pasta selecionada está vazia") e a troca automática ao criar o
// primeiro item, via MainWindow::updateWelcomeScreenVisibility().
class TestWelcomeScreen : public QObject {
    Q_OBJECT

private slots:
    void freshInstallShowsWelcomeScreenAndHidesTree()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *welcome = window.findChild<WelcomeScreen *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        QVERIFY(welcome != nullptr);
        QVERIFY(tree != nullptr);

        QVERIFY(welcome->isVisible());
        QVERIFY(!tree->isVisible());
    }

    void addingAFolderSwitchesBackToTheTree()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        // Persiste uma pasta ANTES de construir a janela (equivalente a
        // handleNewFolderRequested já ter rodado numa sessão anterior) —
        // reloadCommandTree() no boot (chamado por loadConfig()) já deve
        // refletir o novo estado, sem precisar simular o diálogo modal.
        ConfigManager config;
        CommandsData data;
        Folder folder;
        folder.id = QStringLiteral("f_1");
        folder.name = QStringLiteral("Minha Pasta");
        data.folders << folder;
        QVERIFY(config.saveCommands(data));

        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *welcome = window.findChild<WelcomeScreen *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        QVERIFY(welcome != nullptr);
        QVERIFY(tree != nullptr);

        QVERIFY(!welcome->isVisible());
        QVERIFY(tree->isVisible());
    }

    void nonEmptyFolderWithNoCommandsStillShowsTree()
    {
        // Regressão da condição escolhida: uma PASTA vazia (sem comandos
        // dentro) num app já usado não deve reexibir a tela de boas-vindas —
        // só a ausência TOTAL de pastas E comandos conta como "recém-bootado".
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        ConfigManager config;
        CommandsData data;
        Folder folder;
        folder.id = QStringLiteral("f_empty");
        folder.name = QStringLiteral("Pasta Vazia");
        data.folders << folder;
        QVERIFY(config.saveCommands(data));

        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *welcome = window.findChild<WelcomeScreen *>();
        QVERIFY(welcome != nullptr);
        QVERIFY(!welcome->isVisible());
    }

    // O botão "+ Nova Pasta" do passo 1 dispara o MESMO sinal que o botão
    // da toolbar (verificado aqui na origem — WelcomeScreen — sem precisar
    // de uma MainWindow completa).
    void newFolderButtonEmitsSignal()
    {
        WelcomeScreen welcome;
        QSignalSpy spy(&welcome, &WelcomeScreen::newFolderRequested);
        auto *button = welcome.findChild<QPushButton *>();
        QVERIFY(button != nullptr);
        // O primeiro QPushButton construído é o do passo 1 ("+ Nova Pasta").
        button->click();
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestWelcomeScreen)
#include "test_welcome_screen.moc"
