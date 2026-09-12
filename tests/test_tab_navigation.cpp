#include <QTest>
#include <QTabWidget>
#include <QTabBar>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QFile>
#include <QToolButton>
#include <QPushButton>
#include <QLineEdit>
#include <QSignalSpy>
#include <QMenu>
#include <QTimer>

#include "ui/command-tree-widget.h"
#include "ui/project-selector.h"
#include "ui/item-actions-bar.h"
#include "ui/folder-editor-dialog.h"

using namespace kai::ui;
using namespace kai::core;

class TestTabNavigation : public QObject {
    Q_OBJECT

private:
    static QVector<Folder> makeRootFolders()
    {
        Folder one;
        one.id = QStringLiteral("f_one");
        one.name = QStringLiteral("Uma");
        one.icon = QStringLiteral("home");

        Folder two;
        two.id = QStringLiteral("f_two");
        two.name = QStringLiteral("Duas");
        two.icon = QStringLiteral("database");

        Folder three;
        three.id = QStringLiteral("f_three");
        three.name = QStringLiteral("Tres");
        three.icon = QStringLiteral("terminal");
        three.isProject = true; // ainda deve ser uma aba normal

        return {one, two, three};
    }

private slots:
    void rootFoldersBecomeIconTabs()
    {
        CommandTreeWidget widget;
        widget.setData(makeRootFolders(), {});

        auto *tabWidget = widget.findChild<QTabWidget *>();
        QVERIFY(tabWidget != nullptr);
        QCOMPARE(tabWidget->count(), 3);
        QCOMPARE(tabWidget->tabText(0), QStringLiteral("Uma"));
        QCOMPARE(tabWidget->tabText(1), QStringLiteral("Duas"));
        QCOMPARE(tabWidget->tabText(2), QStringLiteral("Tres"));
        QVERIFY(!tabWidget->tabBar()->tabIcon(0).isNull());
        QVERIFY(!tabWidget->tabBar()->tabIcon(1).isNull());
        QVERIFY(!tabWidget->tabBar()->tabIcon(2).isNull());
    }

    // Pedido do usuário: "quero a possibilidade de editar pastas com o
    // botão direito (pastas raiz)" — antes a tab bar não tinha NENHUM menu
    // de contexto; só dava pra editar a pasta-raiz via o botão de editar
    // da toolbar (que segue a aba ativa). O menu real (showTabContextMenu)
    // abre um QMenu::exec() BLOQUEANTE — tentar dirigir isso via sinais
    // sintéticos provou ser frágil/plataforma-dependente neste ambiente
    // (travou sob QT_QPA_PLATFORM=offscreen). Verifica só a FIAÇÃO —
    // mesmo padrão desta suíte de testes, que não exercita QMenu::exec()
    // em nenhum outro lugar: a tab bar aceita o evento de contexto.
    void rootTabAcceptsCustomContextMenu()
    {
        CommandTreeWidget widget;
        widget.setData(makeRootFolders(), {});

        auto *tabBar = widget.findChild<QTabBar *>();
        QVERIFY(tabBar != nullptr);
        QCOMPARE(tabBar->contextMenuPolicy(), Qt::CustomContextMenu);
    }

    void selectNextTabWrapsAroundCircularly()
    {
        CommandTreeWidget widget;
        widget.setData(makeRootFolders(), {});
        auto *tabWidget = widget.findChild<QTabWidget *>();
        QVERIFY(tabWidget != nullptr);

        tabWidget->setCurrentIndex(0);
        widget.selectNextTab();
        QCOMPARE(tabWidget->currentIndex(), 1);
        widget.selectNextTab();
        QCOMPARE(tabWidget->currentIndex(), 2);
        widget.selectNextTab();
        QCOMPARE(tabWidget->currentIndex(), 0);
    }

    void selectPreviousTabWrapsAroundCircularly()
    {
        CommandTreeWidget widget;
        widget.setData(makeRootFolders(), {});
        auto *tabWidget = widget.findChild<QTabWidget *>();
        QVERIFY(tabWidget != nullptr);

        tabWidget->setCurrentIndex(0);
        widget.selectPreviousTab();
        QCOMPARE(tabWidget->currentIndex(), 2);
        widget.selectPreviousTab();
        QCOMPARE(tabWidget->currentIndex(), 1);
        widget.selectPreviousTab();
        QCOMPARE(tabWidget->currentIndex(), 0);
    }

    void importedProjectBecomesNormalFolder()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QFile kaiJson(directory.filePath(QStringLiteral("kai.json")));
        QVERIFY(kaiJson.open(QIODevice::WriteOnly));
        kaiJson.write(R"json({
            "project_name": "Projeto Importado",
            "icon": "database",
            "commands": [{"name": "Build", "type": "shell", "command": "echo ok"}]
        })json");
        kaiJson.close();

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(directory.path());
        QVERIFY(result.success);
        QVERIFY(!result.folder.isProject);
        QCOMPARE(result.folder.projectPath.value(), directory.path());
        QCOMPARE(result.commands.size(), 1);
    }

    void currentRootFolderIdReflectsActiveTab()
    {
        CommandTreeWidget widget;
        widget.setData(makeRootFolders(), {});
        auto *tabWidget = widget.findChild<QTabWidget *>();
        QVERIFY(tabWidget != nullptr);

        tabWidget->setCurrentIndex(0);
        QCOMPARE(widget.currentRootFolderId(), QStringLiteral("f_one"));

        tabWidget->setCurrentIndex(2);
        QCOMPARE(widget.currentRootFolderId(), QStringLiteral("f_three"));
    }

    void itemActionsBarEditFolderButtonIsAlwaysEnabled()
    {
        // Botão novo (ajuste pós-reformulação de abas): a
        // pasta raiz da aba atual não aparece mais como item navegável,
        // então este botão precisa estar sempre habilitado, mesmo sem
        // nenhuma seleção na árvore. Grupo "Item" (ItemActionsBar).
        ItemActionsBar bar;

        bool signalEmitted = false;
        QObject::connect(&bar, &ItemActionsBar::editCurrentFolderRequested,
                          [&signalEmitted]() { signalEmitted = true; });

        QPushButton *editFolderButton = nullptr;
        for (auto *button : bar.findChildren<QPushButton *>()) {
            if (button->toolTip() == QStringLiteral("Edit Current Folder (tab)")) {
                editFolderButton = button;
                break;
            }
        }
        QVERIFY(editFolderButton != nullptr);
        QVERIFY(editFolderButton->isEnabled());

        editFolderButton->click();
        QVERIFY(signalEmitted);
    }

    void itemActionsBarNewFolderAndNewCommandButtonsAreAlwaysEnabled()
    {
        // Botões fixos migrados do menu superior (feedback do usuário):
        // sempre habilitados, independente de seleção. Grupo "Item"
        // (ItemActionsBar).
        ItemActionsBar bar;

        bool newFolderEmitted = false;
        bool newCommandEmitted = false;
        QObject::connect(&bar, &ItemActionsBar::newFolderRequested,
                          [&newFolderEmitted]() { newFolderEmitted = true; });
        QObject::connect(&bar, &ItemActionsBar::newCommandRequested,
                          [&newCommandEmitted]() { newCommandEmitted = true; });

        QPushButton *newFolderButton = nullptr;
        QPushButton *newCommandButton = nullptr;
        for (auto *button : bar.findChildren<QPushButton *>()) {
            if (button->toolTip() == QStringLiteral("New Folder")) {
                newFolderButton = button;
            } else if (button->toolTip() == QStringLiteral("New Command")) {
                newCommandButton = button;
            }
        }
        QVERIFY(newFolderButton != nullptr);
        QVERIFY(newCommandButton != nullptr);
        QVERIFY(newFolderButton->isEnabled());
        QVERIFY(newCommandButton->isEnabled());

        newFolderButton->click();
        newCommandButton->click();
        QVERIFY(newFolderEmitted);
        QVERIFY(newCommandEmitted);
    }

    void firstItemIsSelectedNotJustCurrentWhenEnteringTab()
    {
        // Bug reportado: ao entrar numa aba, o primeiro item vinha
        // "corrente" (↑/↓ navegavam) mas SEM highlight, porque não estava
        // de fato SELECIONADO — o QSS pinta ::item:selected. Aqui
        // garantimos que o primeiro comando da aba ativa nasce
        // selecionado (isSelected), habilitando o destaque visual.
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Raiz");

        Command c1;
        c1.id = QStringLiteral("c_1");
        c1.folderId = root.id;
        c1.name = QStringLiteral("Primeiro");
        c1.type = CommandType::Shell;
        c1.command = QStringLiteral("echo 1");

        Command c2;
        c2.id = QStringLiteral("c_2");
        c2.folderId = root.id;
        c2.name = QStringLiteral("Segundo");
        c2.type = CommandType::Shell;
        c2.command = QStringLiteral("echo 2");

        CommandTreeWidget widget;
        widget.setData({root}, {c1, c2});

        auto *tabWidget = widget.findChild<QTabWidget *>();
        QVERIFY(tabWidget != nullptr);
        auto *tree = qobject_cast<QTreeWidget *>(tabWidget->currentWidget());
        QVERIFY(tree != nullptr);

        // Deve haver um item corrente E ele deve estar selecionado.
        QVERIFY(tree->currentItem() != nullptr);
        QVERIFY(tree->currentItem()->isSelected());
        QCOMPARE(tree->selectedItems().size(), 1);
    }

    void folderEditorDialogPreselectsSuggestedParent()
    {
        Folder folderA;
        folderA.id = QStringLiteral("f_a");
        folderA.name = QStringLiteral("Pasta A");

        Folder folderB;
        folderB.id = QStringLiteral("f_b");
        folderB.name = QStringLiteral("Pasta B");

        FolderEditorDialog dialog({folderA, folderB}, nullptr, nullptr, QStringLiteral("f_b"));

        auto *nameField = dialog.findChild<QLineEdit *>();
        QVERIFY(nameField != nullptr);
        nameField->setText(QStringLiteral("Subpasta Nova"));

        const Folder built = dialog.buildFolder();
        QVERIFY(built.parentId.has_value());
        QCOMPARE(built.parentId.value(), QStringLiteral("f_b"));
    }
};

QTEST_MAIN(TestTabNavigation)
#include "test_tab_navigation.moc"
