#include <QTest>
#include <QTreeWidget>
#include <QTabWidget>
#include <QTabBar>

#include "ui/command-tree-widget.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

namespace {
constexpr int kCommandIdRole = Qt::UserRole + 1;
constexpr int kIsCommandRole = Qt::UserRole + 2;

QTreeWidgetItem *findChildByText(QTreeWidgetItem *parent, const QString &text)
{
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->text(0) == text) {
            return parent->child(i);
        }
    }
    return nullptr;
}

// Cada pasta raiz agora é uma aba dinâmica; o teste localiza a árvore
// pela identificação armazenada no QTabBar, sem depender de posição fixa.
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

// Cobre a hierarquia real de pastas dentro de uma aba dinâmica: a pasta
// raiz representa a própria aba (nome + ícone no QTabBar) e NÃO é
// renderizada como item dentro da sua árvore — só filhas diretas
// (subpastas e comandos) aparecem como itens de topo
// (ajuste pós-reformulação de abas, feedback do usuário).
class TestFolderHierarchy : public QObject {
    Q_OBJECT

private slots:
    void rootFolderIsNotRenderedAsItemInsideItsOwnTab()
    {
        Folder parentFolder;
        parentFolder.id = QStringLiteral("f_parent");
        parentFolder.name = QStringLiteral("Pasta Pai");

        Folder childFolder;
        childFolder.id = QStringLiteral("f_child");
        childFolder.name = QStringLiteral("Pasta Filha");
        childFolder.parentId = QStringLiteral("f_parent");

        CommandTreeWidget widget;
        widget.setData({parentFolder, childFolder}, {});

        auto *customTree = treeForRoot(widget, QStringLiteral("f_parent"));
        QVERIFY(customTree != nullptr);

        // A pasta raiz ("Pasta Pai") não deve aparecer em nenhum item da
        // própria árvore — ela já está representada pela aba em si.
        bool rootFoundAsItem = false;
        for (int i = 0; i < customTree->topLevelItemCount(); ++i) {
            if (customTree->topLevelItem(i)->text(0) == QStringLiteral("Pasta Pai")) {
                rootFoundAsItem = true;
            }
        }
        QVERIFY(!rootFoundAsItem);

        // A pasta filha, por ser filha direta da raiz, é item de topo.
        QCOMPARE(customTree->topLevelItemCount(), 1);
        QCOMPARE(customTree->topLevelItem(0)->text(0), QStringLiteral("Pasta Filha"));
    }

    void threeLevelHierarchyIsNestedCorrectly()
    {
        Folder grandparent;
        grandparent.id = QStringLiteral("f_1");
        grandparent.name = QStringLiteral("Nivel 1");

        Folder parent;
        parent.id = QStringLiteral("f_2");
        parent.name = QStringLiteral("Nivel 2");
        parent.parentId = QStringLiteral("f_1");

        Folder child;
        child.id = QStringLiteral("f_3");
        child.name = QStringLiteral("Nivel 3");
        child.parentId = QStringLiteral("f_2");

        CommandTreeWidget widget;
        widget.setData({grandparent, parent, child}, {});

        auto *customTree = treeForRoot(widget, QStringLiteral("f_1"));

        // "Nivel 1" é a raiz/aba e não aparece como item; "Nivel 2" (sua
        // filha direta) é o único item de topo da árvore.
        QCOMPARE(customTree->topLevelItemCount(), 1);

        QTreeWidgetItem *level2 = customTree->topLevelItem(0);
        QCOMPARE(level2->text(0), QStringLiteral("Nivel 2"));
        QCOMPARE(level2->childCount(), 1);

        QTreeWidgetItem *level3 = level2->child(0);
        QCOMPARE(level3->text(0), QStringLiteral("Nivel 3"));
    }

    void commandsAreNestedInsideTheirActualFolderRegardlessOfHierarchyDepth()
    {
        Folder parentFolder;
        parentFolder.id = QStringLiteral("f_parent");
        parentFolder.name = QStringLiteral("Pasta Pai");

        Folder childFolder;
        childFolder.id = QStringLiteral("f_child");
        childFolder.name = QStringLiteral("Pasta Filha");
        childFolder.parentId = QStringLiteral("f_parent");

        Command command;
        command.id = QStringLiteral("c_1");
        command.name = QStringLiteral("Comando na Filha");
        command.folderId = QStringLiteral("f_child");

        CommandTreeWidget widget;
        widget.setData({parentFolder, childFolder}, {command});

        auto *customTree = treeForRoot(widget, QStringLiteral("f_parent"));
        QTreeWidgetItem *childItem = customTree->topLevelItem(0);
        QCOMPARE(childItem->text(0), QStringLiteral("Pasta Filha"));

        QTreeWidgetItem *commandItem = findChildByText(childItem, QStringLiteral("Comando na Filha"));
        QVERIFY(commandItem != nullptr);
        QVERIFY(commandItem->data(0, kIsCommandRole).toBool());
        QCOMPARE(commandItem->data(0, kCommandIdRole).toString(), QStringLiteral("c_1"));
    }

    void commandsDirectlyInsideRootFolderBecomeTopLevelItems()
    {
        // Comando que pertence diretamente à pasta raiz (não a uma
        // subpasta) não deve ser descartado silenciosamente
        // agora que a raiz não tem mais item de pasta correspondente —
        // deve aparecer como item de topo da árvore da aba.
        Folder rootFolder;
        rootFolder.id = QStringLiteral("f_root");
        rootFolder.name = QStringLiteral("Raiz");

        Command command;
        command.id = QStringLiteral("c_1");
        command.name = QStringLiteral("Comando Direto");
        command.folderId = QStringLiteral("f_root");

        CommandTreeWidget widget;
        widget.setData({rootFolder}, {command});

        auto *tree = treeForRoot(widget, QStringLiteral("f_root"));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QTreeWidgetItem *commandItem = tree->topLevelItem(0);
        QCOMPARE(commandItem->text(0), QStringLiteral("Comando Direto"));
        QVERIFY(commandItem->data(0, kIsCommandRole).toBool());
    }

    void orphanFolderBecomesFallbackRootTabWithoutRenderingItself()
    {
        // Pasta referenciando um parentId que não existe (ex: pasta pai
        // excluída) não deve ser descartada silenciosamente —
        // torna-se sua própria aba raiz de fallback. Como toda raiz, não
        // aparece como item dentro da própria árvore (agora vazia, pois
        // não tem filhas).
        Folder orphan;
        orphan.id = QStringLiteral("f_orphan");
        orphan.name = QStringLiteral("Pasta Orfa");
        orphan.parentId = QStringLiteral("f_inexistente");

        CommandTreeWidget widget;
        widget.setData({orphan}, {});

        auto *tabWidget = widget.findChild<QTabWidget *>();
        bool foundTab = false;
        for (int i = 0; i < tabWidget->count(); ++i) {
            if (tabWidget->tabBar()->tabData(i).toString() == QStringLiteral("f_orphan")) {
                foundTab = true;
                QCOMPARE(tabWidget->tabText(i), QStringLiteral("Pasta Orfa"));
            }
        }
        QVERIFY(foundTab);

        auto *tree = treeForRoot(widget, QStringLiteral("f_orphan"));
        QCOMPARE(tree->topLevelItemCount(), 0);
    }
};

QTEST_MAIN(TestFolderHierarchy)
#include "test_folder_hierarchy.moc"
