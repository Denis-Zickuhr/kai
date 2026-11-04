#include <QTest>
#include <QTreeWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QComboBox>
#include <QLineEdit>
#include <functional>

#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

namespace {
QTreeWidget *treeForRoot(CommandTreeWidget &widget, const QString &rootId)
{
    auto *tabWidget = widget.findChild<QTabWidget *>();
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->tabBar()->tabData(i).toString() == rootId) {
            return qobject_cast<QTreeWidget *>(tabWidget->widget(i));
        }
    }
    return nullptr;
}
}

// Pastas iniciam colapsadas; Enter alterna expandir/colapsar numa pasta selecionada; o combo de pasta
// destino do CommandEditorDialog permite trocar livremente a pasta do comando.
class TestLastAdjustments : public QObject {
    Q_OBJECT

private slots:
    void foldersStartCollapsedByDefault()
    {
        Folder rootFolder;
        rootFolder.id = QStringLiteral("f_root");
        rootFolder.name = QStringLiteral("Raiz");

        Folder subFolder;
        subFolder.id = QStringLiteral("f_sub");
        subFolder.name = QStringLiteral("Pasta");
        subFolder.parentId = QStringLiteral("f_root");

        Command command;
        command.id = QStringLiteral("c_1");
        command.name = QStringLiteral("Comando");
        command.folderId = QStringLiteral("f_sub");

        CommandTreeWidget widget;
        widget.setData({rootFolder, subFolder}, {command});

        auto *customTree = treeForRoot(widget, QStringLiteral("f_root"));
        QCOMPARE(customTree->topLevelItemCount(), 1);
        QVERIFY(!customTree->topLevelItem(0)->isExpanded());
    }

    void activatingFolderItemTogglesExpansion()
    {
        Folder rootFolder;
        rootFolder.id = QStringLiteral("f_root");
        rootFolder.name = QStringLiteral("Raiz");

        Folder subFolder;
        subFolder.id = QStringLiteral("f_sub");
        subFolder.name = QStringLiteral("Pasta");
        subFolder.parentId = QStringLiteral("f_root");

        CommandTreeWidget widget;
        widget.setData({rootFolder, subFolder}, {});

        auto *customTree = treeForRoot(widget, QStringLiteral("f_root"));
        QTreeWidgetItem *folderItem = customTree->topLevelItem(0);
        QVERIFY(!folderItem->isExpanded());

        emit customTree->itemActivated(folderItem, 0);
        QVERIFY(folderItem->isExpanded());

        emit customTree->itemActivated(folderItem, 0);
        QVERIFY(!folderItem->isExpanded());
    }

    void commandEditorDialogAllowsChoosingDestinationFolder()
    {
        Folder folderA;
        folderA.id = QStringLiteral("f_a");
        folderA.name = QStringLiteral("Pasta A");

        Folder folderB;
        folderB.id = QStringLiteral("f_b");
        folderB.name = QStringLiteral("Pasta B");

        CommandEditorDialog dialog(QStringLiteral("f_a"), {}, {folderA, folderB}, nullptr);

        // Não usa o primeiro QComboBox da árvore: outro combo (ex.: PERFIL) pode vir antes do da pasta.
        auto *folderCombo = dialog.findChild<QComboBox *>(QStringLiteral("commandEditorFolderCombo"));
        QVERIFY(folderCombo != nullptr);
        // 3 itens: "(Raiz)" (folderId vazio) + as 2 pastas.
        QCOMPARE(folderCombo->count(), 3);

        QCOMPARE(folderCombo->currentData().toString(), QStringLiteral("f_a"));

        const int indexOfB = folderCombo->findData(QStringLiteral("f_b"));
        QVERIFY(indexOfB >= 0);
        folderCombo->setCurrentIndex(indexOfB);

        auto *nameField = dialog.findChild<QLineEdit *>();
        nameField->setText(QStringLiteral("Comando Teste"));

        const Command built = dialog.buildCommand();
        QCOMPARE(built.folderId, QStringLiteral("f_b"));

        // Escolher "(Raiz)" (data vazia) resulta em folderId vazio, não na pasta original.
        const int rootIdx = folderCombo->findData(QString());
        QVERIFY(rootIdx >= 0);
        folderCombo->setCurrentIndex(rootIdx);
        const Command builtRoot = dialog.buildCommand();
        QVERIFY(builtRoot.folderId.isEmpty());
    }

    // Editar um comando preserva order (e folderId via combo): buildCommand() criava um Command novo
    // (order == -1) e o comando "pulava para o fim da lista".
    void editingCommandPreservesManualOrder()
    {
        Folder folderA;
        folderA.id = QStringLiteral("f_a");
        folderA.name = QStringLiteral("Pasta A");

        Command existing;
        existing.id = QStringLiteral("c_existing");
        existing.folderId = QStringLiteral("f_a");
        existing.name = QStringLiteral("Comando Existente");
        existing.command = QStringLiteral("echo hi");
        existing.order = 3;

        CommandEditorDialog dialog(QStringLiteral("f_a"), {}, {folderA}, nullptr, &existing);

        const Command built = dialog.buildCommand();
        QCOMPARE(built.order, 3);
        QCOMPARE(built.folderId, QStringLiteral("f_a"));
        QCOMPARE(built.id, QStringLiteral("c_existing"));
    }

    // Regressão: mesma preservação de order ao editar uma PASTA.
    void editingFolderPreservesManualOrder()
    {
        Folder existing;
        existing.id = QStringLiteral("f_existing");
        existing.name = QStringLiteral("Pasta Existente");
        existing.order = 5;

        FolderEditorDialog dialog({existing}, nullptr, &existing);
        const Folder built = dialog.buildFolder();
        QCOMPARE(built.order, 5);
        QCOMPARE(built.id, QStringLiteral("f_existing"));
    }

    // buildCommand() só atribuía command.params no ramo SHELL, e o carregamento idem: um HTTP com params,
    // editado no diálogo, deve sair com os params.
    void httpCommandPreservesDynamicParams()
    {
        Folder folderA;
        folderA.id = QStringLiteral("f_a");
        folderA.name = QStringLiteral("A");

        Command existing;
        existing.id = QStringLiteral("c_http");
        existing.folderId = QStringLiteral("f_a");
        existing.name = QStringLiteral("Busca");
        existing.type = CommandType::Http;
        HttpConfig cfg;
        cfg.method = HttpMethod::Get;
        cfg.url = QStringLiteral("https://api.x/{{id}}");
        existing.httpConfig = cfg;
        Parameter p;
        p.name = QStringLiteral("id");
        p.label = QStringLiteral("ID");
        p.type = ParameterType::Text;
        existing.params = {p};

        CommandEditorDialog dialog(QStringLiteral("f_a"), {}, {folderA}, nullptr, &existing);
        const Command built = dialog.buildCommand();

        QCOMPARE(built.type, CommandType::Http);
        QCOMPARE(built.params.size(), 1);
        QCOMPARE(built.params.at(0).name, QStringLiteral("id"));
    }

    // Persistir chama setData->rebuildTabs, que recria a árvore do zero: os ids expandidos são capturados antes
    // e reexpandidos depois. Expande uma subpasta, dispara novo setData e confere que continua expandida.
    void rebuildPreservesFolderExpansionState()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Raiz");

        Folder sub;
        sub.id = QStringLiteral("f_sub");
        sub.name = QStringLiteral("Sub");
        sub.parentId = QStringLiteral("f_root");

        Command cmd;
        cmd.id = QStringLiteral("c_in_sub");
        cmd.folderId = QStringLiteral("f_sub");
        cmd.name = QStringLiteral("Cmd na Sub");
        cmd.command = QStringLiteral("echo x");

        CommandTreeWidget widget;
        widget.setData({root, sub}, {cmd});

        QTreeWidget *tree = treeForRoot(widget, QStringLiteral("f_root"));
        QVERIFY(tree != nullptr);

        QTreeWidgetItem *subItem = nullptr;
        std::function<void(QTreeWidgetItem *)> find = [&](QTreeWidgetItem *item) {
            if (item->data(0, Qt::UserRole + 1).toString() == QStringLiteral("f_sub")) {
                subItem = item;
            }
            for (int i = 0; i < item->childCount(); ++i) {
                find(item->child(i));
            }
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            find(tree->topLevelItem(i));
        }
        QVERIFY(subItem != nullptr);
        subItem->setExpanded(true);
        QVERIFY(subItem->isExpanded());

        widget.setData({root, sub}, {cmd});

        QTreeWidget *treeAfter = treeForRoot(widget, QStringLiteral("f_root"));
        QVERIFY(treeAfter != nullptr);
        QTreeWidgetItem *subAfter = nullptr;
        std::function<void(QTreeWidgetItem *)> find2 = [&](QTreeWidgetItem *item) {
            if (item->data(0, Qt::UserRole + 1).toString() == QStringLiteral("f_sub")) {
                subAfter = item;
            }
            for (int i = 0; i < item->childCount(); ++i) {
                find2(item->child(i));
            }
        };
        for (int i = 0; i < treeAfter->topLevelItemCount(); ++i) {
            find2(treeAfter->topLevelItem(i));
        }
        QVERIFY(subAfter != nullptr);
        QVERIFY(subAfter->isExpanded());
    }
};

QTEST_MAIN(TestLastAdjustments)
#include "test_last_adjustments.moc"
