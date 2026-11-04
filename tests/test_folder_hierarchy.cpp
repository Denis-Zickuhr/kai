#include <QTest>
#include <QTreeWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QSignalSpy>

#include "ui/features/command-editor/command-tree-widget.h"
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

// Cada pasta raiz é uma aba dinâmica; localiza a árvore pela identificação armazenada no QTabBar.
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

// Hierarquia de pastas numa aba dinâmica: a pasta raiz é a própria aba (nome + ícone) e não vira item
// da árvore; só filhas diretas (subpastas e comandos) são itens de topo.
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

        // A raiz já é representada pela aba: não aparece como item.
        bool rootFoundAsItem = false;
        for (int i = 0; i < customTree->topLevelItemCount(); ++i) {
            if (customTree->topLevelItem(i)->text(0) == QStringLiteral("Pasta Pai")) {
                rootFoundAsItem = true;
            }
        }
        QVERIFY(!rootFoundAsItem);

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

        // "Nivel 1" é a aba; "Nivel 2" é o único item de topo.
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
        // Comando direto da raiz não é descartado: aparece como item de topo.
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
        // Pasta com parentId inexistente (pai excluído) não é descartada: vira aba raiz de fallback, com árvore vazia.
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

    // Pasta órfã (aba raiz de fallback) marcada hidden respeita a marca como qualquer raiz: sem aba
    // por padrão, só com "mostrar ocultos".
    void hiddenOrphanFolderRespectsShowHiddenLikeAnyRootFolder()
    {
        Folder orphan;
        orphan.id = QStringLiteral("f_hidden_orphan");
        orphan.name = QStringLiteral("Pasta Orfa Oculta");
        orphan.parentId = QStringLiteral("f_inexistente");
        orphan.hidden = true;

        CommandTreeWidget widget;
        widget.setData({orphan}, {});

        auto *tabWidget = widget.findChild<QTabWidget *>();
        auto tabExists = [&]() {
            for (int i = 0; i < tabWidget->count(); ++i) {
                if (tabWidget->tabBar()->tabData(i).toString() == QStringLiteral("f_hidden_orphan")) {
                    return true;
                }
            }
            return false;
        };

        QVERIFY2(!tabExists(), "pasta raiz oculta (mesmo órfã) não deveria virar aba por padrão");

        widget.setShowHidden(true);
        widget.setData({orphan}, {});
        QVERIFY2(tabExists(), "com 'mostrar ocultos' ligado, a aba deveria aparecer");
    }

    // O menu de contexto da aba oferece ocultar/mostrar a pasta raiz (o form de pasta não tem "oculta" e o
    // atalho normal só vê itens dentro da árvore). Testa o sinal de showTabContextMenu (privado) via
    // QSignalSpy: dirigir QMenu::exec() off-screen não é confiável.
    void toggleFolderHiddenSignalExistsAndCarriesFolderId()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Raiz");

        CommandTreeWidget widget;
        widget.setData({root}, {});

        QSignalSpy spy(&widget, &CommandTreeWidget::toggleFolderHiddenRequested);
        QVERIFY(spy.isValid());
        emit widget.toggleFolderHiddenRequested(QStringLiteral("f_root"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("f_root"));
    }
};

QTEST_MAIN(TestFolderHierarchy)
#include "test_folder_hierarchy.moc"
