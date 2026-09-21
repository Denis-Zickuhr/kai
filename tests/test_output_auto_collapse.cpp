// Recolher a Saída sozinha ao selecionar pasta/coleção e reabrir ao voltar a um
// comando (SettingsData::autoCollapseOutputOnFolders). A regra decide pelo
// estado FINAL da seleção (a árvore reconstruída emite uma rajada de seleções
// transitórias), não vira preferência (terminal_collapsed) e não desfaz uma
// escolha feita à mão.

#include <QTest>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTabBar>
#include <QSignalSpy>

#include "core/config-manager.h"
#include "ui/main-window.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/features/command-editor/command-tree-widget.h"

using namespace kai::ui;
using namespace kai::core;

// A regra roda na volta seguinte do event loop e recolher espera ~120 ms.
#define EXPECT_EXPANDED(drawer, value, message) \
    QTRY_VERIFY2_WITH_TIMEOUT((drawer)->isExpanded() == (value), message, 2000)
// Para afirmar que algo NÃO muda: deixa passar mais que o atraso de recolher.
#define SETTLE() QTest::qWait(400)

class TestOutputAutoCollapse : public QObject {
    Q_OBJECT

private:
    static void seed(const QTemporaryDir &tempDir, bool autoCollapse)
    {
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
        ConfigManager config;
        CommandsData data;
        Folder root; root.id = "f_root"; root.name = "Raiz";
        Folder sub; sub.id = "f_sub"; sub.name = "Sub"; sub.parentId = "f_root";
        Command cmd; cmd.id = "c_a"; cmd.name = "Alpha"; cmd.folderId = "f_root"; cmd.command = "echo oi";
        // Segunda pasta raiz (outra aba) com um comando e uma subpasta.
        Folder root2; root2.id = "f_root2"; root2.name = "Raiz2"; root2.order = 1;
        Folder sub2; sub2.id = "f_sub2"; sub2.name = "Sub2"; sub2.parentId = "f_root2";
        Command cmd2; cmd2.id = "c_b"; cmd2.name = "Bravo"; cmd2.folderId = "f_root2"; cmd2.command = "echo oi";
        data.folders << root << sub << root2 << sub2;
        data.commands << cmd << cmd2;
        QVERIFY(config.saveCommands(data));
        SettingsData settings = config.loadSettings();
        settings.autoCollapseOutputOnFolders = autoCollapse;
        settings.terminalCollapsed = false;
        QVERIFY(config.saveSettings(settings));
    }

    struct Ui {
        MainWindow window;
        TerminalDrawer *drawer = nullptr;
        CommandTreeWidget *tree = nullptr;
        QTabWidget *tabs = nullptr;
    };

    // Janela pronta, com a Saída aberta e o boot já assentado.
    static void open(Ui &ui)
    {
        ui.window.resize(1000, 700);
        ui.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&ui.window));
        ui.drawer = ui.window.findChild<TerminalDrawer *>();
        ui.tree = ui.window.findChild<CommandTreeWidget *>();
        ui.tabs = ui.tree ? ui.tree->findChild<QTabWidget *>() : nullptr;
        QVERIFY(ui.drawer != nullptr && ui.tree != nullptr && ui.tabs != nullptr);
        SETTLE();
        ui.drawer->setExpanded(true);
        SETTLE();
    }

    static int tabIndex(QTabWidget *tabs, const QString &rootId)
    {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabBar()->tabData(i).toString() == rootId) {
                return i;
            }
        }
        return -1;
    }

private slots:
    void folderCollapsesTheOutputAndACommandReopensIt()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        seed(tempDir, true);
        Ui ui;
        open(ui);

        QVERIFY(ui.tree->selectCommand(QStringLiteral("c_a")));
        EXPECT_EXPANDED(ui.drawer, true, "comando deveria manter a Saída aberta");

        QVERIFY(ui.tree->selectCommand(QStringLiteral("f_sub")));
        EXPECT_EXPANDED(ui.drawer, false, "pasta selecionada deveria recolher a Saída");

        QVERIFY(ui.tree->selectCommand(QStringLiteral("c_a")));
        EXPECT_EXPANDED(ui.drawer, true, "comando selecionado deveria reabrir a Saída");

        // O recolhimento automático NÃO foi gravado como preferência.
        ConfigManager config;
        QVERIFY(!config.loadSettings().terminalCollapsed);
    }

    // Trocar de pasta raiz (aba): vale o item corrente da aba nova.
    void switchingRootTabFollowsTheNewTabsSelection()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        seed(tempDir, true);
        Ui ui;
        open(ui);
        const int tabWithCommandB = tabIndex(ui.tabs, QStringLiteral("f_root2"));
        const int tabWithFolder = tabIndex(ui.tabs, QStringLiteral("f_root"));
        QVERIFY(tabWithCommandB >= 0 && tabWithFolder >= 0 && tabWithCommandB != tabWithFolder);

        ui.tabs->setCurrentIndex(tabWithCommandB);
        QVERIFY(ui.tree->selectCommand(QStringLiteral("c_b")));
        EXPECT_EXPANDED(ui.drawer, true, "comando da aba B");
        ui.tabs->setCurrentIndex(tabWithFolder);
        QVERIFY(ui.tree->selectCommand(QStringLiteral("f_sub")));
        EXPECT_EXPANDED(ui.drawer, false, "pasta da aba A deveria recolher");

        ui.tabs->setCurrentIndex(tabWithCommandB);
        EXPECT_EXPANDED(ui.drawer, true, "aba cujo item corrente é um comando deveria reabrir a Saída");
        ui.tabs->setCurrentIndex(tabWithFolder);
        EXPECT_EXPANDED(ui.drawer, false, "aba cujo item corrente é uma pasta deveria recolher");
        ui.tabs->setCurrentIndex(tabWithCommandB);
        EXPECT_EXPANDED(ui.drawer, true, "voltar à aba do comando deveria reabrir");
    }

    // Mesmo caminho, mas com cliques REAIS (aba e item): é o que o usuário faz.
    void mouseClicksOnTabsAndItemsFollowTheRule()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        seed(tempDir, true);
        Ui ui;
        open(ui);
        auto clickTab = [&ui](int index) {
            QTest::mouseClick(ui.tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                              ui.tabs->tabBar()->tabRect(index).center());
        };
        auto clickItem = [&ui](const QString &text) {
            auto *current = qobject_cast<QTreeWidget *>(ui.tabs->currentWidget());
            const auto found = current->findItems(text, Qt::MatchExactly | Qt::MatchRecursive);
            QVERIFY2(!found.isEmpty(), qPrintable(text));
            current->scrollToItem(found.first());
            QTest::mouseClick(current->viewport(), Qt::LeftButton, Qt::NoModifier,
                              current->visualItemRect(found.first()).center());
        };
        const int tabA = tabIndex(ui.tabs, QStringLiteral("f_root"));
        const int tabB = tabIndex(ui.tabs, QStringLiteral("f_root2"));

        clickTab(tabB);
        clickItem(QStringLiteral("Bravo"));
        EXPECT_EXPANDED(ui.drawer, true, "clicar no comando");
        clickTab(tabA);
        clickItem(QStringLiteral("Sub"));
        EXPECT_EXPANDED(ui.drawer, false, "clicar na pasta deveria recolher");

        clickTab(tabB);
        EXPECT_EXPANDED(ui.drawer, true, "trocar para a aba cujo item corrente é um comando deveria reabrir");
        clickTab(tabA);
        EXPECT_EXPANDED(ui.drawer, false, "trocar para a aba cujo item corrente é uma pasta deveria recolher");
        clickItem(QStringLiteral("Alpha"));
        EXPECT_EXPANDED(ui.drawer, true, "clicar no comando deveria reabrir");
    }

    // A árvore é reconstruída em segundo plano (tema recarregado, configuração
    // salva...): a rajada de seleções transitórias não pode piscar a Saída nem
    // deixá-la no estado errado — vale o item selecionado no fim.
    void rebuildingTheTreeNeitherFlickersNorLeavesTheWrongState()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        seed(tempDir, true);
        Ui ui;
        open(ui);
        const CommandsData data = ConfigManager().loadCommands();

        QVERIFY(ui.tree->selectCommand(QStringLiteral("c_a")));
        EXPECT_EXPANDED(ui.drawer, true, "comando selecionado");
        // Conta quantas vezes a Saída mudou de estado durante a reconstrução.
        QSignalSpy changes(ui.drawer, &TerminalDrawer::expandedChanged);
        ui.tree->setData(data.folders, data.commands);
        SETTLE();
        QCOMPARE(ui.tree->currentSelectionId(), QStringLiteral("c_a"));
        QVERIFY2(ui.drawer->isExpanded(), "reconstruir com um comando selecionado fechou a Saída");
        QCOMPARE(changes.count(), 0);

        QVERIFY(ui.tree->selectCommand(QStringLiteral("f_sub")));
        EXPECT_EXPANDED(ui.drawer, false, "pasta selecionada");
        ui.tree->setData(data.folders, data.commands);
        SETTLE();
        QCOMPARE(ui.tree->currentSelectionId(), QStringLiteral("f_sub"));
        QVERIFY2(!ui.drawer->isExpanded(), "reconstruir com uma pasta selecionada reabriu a Saída");
    }

    // No boot o estado inicial já respeita a seleção: a janela abre com uma
    // pasta selecionada, então a Saída começa recolhida (e não aberta até a
    // primeira navegação).
    void bootStateFollowsTheInitialSelection()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        seed(tempDir, true);
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        QVERIFY(drawer != nullptr && tree != nullptr);
        SETTLE();
        const bool folderSelected = tree->currentSelectionIsFolder() || tree->currentSelectionIsCollection();
        QCOMPARE(drawer->isExpanded(), !folderSelected);
    }

    // Quem recolheu à mão continua recolhido: a regra não reabre o que ela
    // mesma não recolheu.
    void manualCollapseIsRespected()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        seed(tempDir, true);
        Ui ui;
        open(ui);

        QVERIFY(ui.tree->selectCommand(QStringLiteral("c_a")));
        SETTLE();
        ui.drawer->setExpanded(false);           // escolha manual
        QVERIFY(ui.tree->selectCommand(QStringLiteral("f_sub")));
        SETTLE();
        QVERIFY(!ui.drawer->isExpanded());
        QVERIFY(ui.tree->selectCommand(QStringLiteral("c_a")));
        SETTLE();
        QVERIFY2(!ui.drawer->isExpanded(), "a regra não pode reabrir uma Saída recolhida à mão");
    }

    // Navegar depressa entre pasta e comando não balança o layout: uma pasta
    // seguida de um comando dentro do atraso de recolher não recolhe nada.
    void quickPassOverAFolderDoesNotCollapse()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        seed(tempDir, true);
        Ui ui;
        open(ui);
        QVERIFY(ui.tree->selectCommand(QStringLiteral("c_a")));
        SETTLE();
        QSignalSpy changes(ui.drawer, &TerminalDrawer::expandedChanged);
        QVERIFY(ui.tree->selectCommand(QStringLiteral("f_sub")));
        QVERIFY(ui.tree->selectCommand(QStringLiteral("c_a")));
        SETTLE();
        QCOMPARE(changes.count(), 0);
        QVERIFY(ui.drawer->isExpanded());
    }

    // Com a opção desligada nada muda ao navegar.
    void disabledSettingLeavesTheOutputAlone()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        seed(tempDir, false);
        Ui ui;
        open(ui);

        QVERIFY(ui.tree->selectCommand(QStringLiteral("f_sub")));
        SETTLE();
        QVERIFY(ui.drawer->isExpanded());
    }
};

QTEST_MAIN(TestOutputAutoCollapse)
#include "test_output_auto_collapse.moc"
