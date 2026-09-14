#include <QTest>
#include <QTemporaryDir>
#include <QPushButton>
#include <QSignalSpy>

#include "core/config-manager.h"
#include "ui/main-window.h"
#include "ui/shared/welcome-screen.h"
#include "ui/shared/action-group-container.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "utils/design-tokens.h"

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
    // Troca de tema: a tela de boas-vindas montava TODO o estilo na construção
    // (accent de fallback, o roxo do Dracula, antes do tema ser publicado) e
    // nunca mais acompanhava. applyTheme() refaz com o accent atual — e os
    // botões continuam ligados aos sinais.
    void welcomeScreenFollowsTheAccentAfterAThemeChange()
    {
        namespace tk = kai::utils::tokens;
        tk::publishTheme({{QStringLiteral("bg"), QStringLiteral("#101010")},
                          {QStringLiteral("fg"), QStringLiteral("#eeeeee")},
                          {QStringLiteral("accent_color"), QStringLiteral("#ff2244")}});
        WelcomeScreen welcome;
        auto anyStyleHas = [&welcome](const QString &hex) {
            for (QWidget *child : welcome.findChildren<QWidget *>()) {
                if (child->styleSheet().contains(hex, Qt::CaseInsensitive)) {
                    return true;
                }
            }
            return false;
        };
        QVERIFY(anyStyleHas(QStringLiteral("#ff2244")));

        tk::publishTheme({{QStringLiteral("bg"), QStringLiteral("#101010")},
                          {QStringLiteral("fg"), QStringLiteral("#eeeeee")},
                          {QStringLiteral("accent_color"), QStringLiteral("#22cc88")}});
        QVERIFY2(anyStyleHas(QStringLiteral("#ff2244")), "sem applyTheme o estilo continua no accent antigo");
        welcome.applyTheme();
        QVERIFY(anyStyleHas(QStringLiteral("#22cc88")));
        QVERIFY(!anyStyleHas(QStringLiteral("#ff2244")));

        QSignalSpy folderSpy(&welcome, &WelcomeScreen::newFolderRequested);
        QSignalSpy commandSpy(&welcome, &WelcomeScreen::newCommandRequested);
        const auto buttons = welcome.findChildren<QPushButton *>();
        QVERIFY(buttons.size() >= 2);
        buttons.at(0)->click();
        buttons.at(1)->click();
        QCOMPARE(folderSpy.count(), 1);
        QCOMPARE(commandSpy.count(), 1);
        tk::publishTheme({});
    }

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

    // Pedido do usuário: na tela de boas-vindas NADA além dela aparece — nem a
    // Saída, nem a lista, nem as barras de ação; fechar devolve tudo.
    void welcomeScreenHidesOutputListAndActionBars()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *welcome = window.findChild<WelcomeScreen *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *drawer = window.findChild<TerminalDrawer *>();
        QVERIFY(welcome != nullptr && tree != nullptr && drawer != nullptr);

        QVERIFY(welcome->isVisible());
        QVERIFY(!tree->isVisible());
        QVERIFY2(!drawer->isVisible(), "a Saída deve ficar oculta na tela de boas-vindas");
        for (auto *bar : window.findChildren<ActionGroupContainer *>()) {
            QVERIFY2(!bar->isVisible(), "barras de ação devem ficar ocultas na tela de boas-vindas");
        }

        // Fechar a tela devolve a lista e a Saída.
        emit welcome->closeRequested();
        QTest::qWait(30);
        QVERIFY(!welcome->isVisible());
        QVERIFY(tree->isVisible());
        QVERIFY(drawer->isVisible());
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
