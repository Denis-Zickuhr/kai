#include <QHash>
#include <QSet>
#include <QTabWidget>
#include <QTest>

#include <QJsonObject>
#include <QTreeWidget>

#include "core/tree-view.h"
#include "ui/features/command-editor/command-tree-widget.h"

using namespace kai::core;
using kai::ui::CommandTreeWidget;

// Árvores extras (Exibir → Commands ▸): o filtro puro, o arrastar numa árvore filtrada e o repasse de dados da principal.
class TestTreeView : public QObject {
    Q_OBJECT

    static Folder folder(const QString &id, const QString &parent = QString(), int order = -1)
    {
        Folder f;
        f.id = id;
        f.name = id;
        if (!parent.isEmpty()) {
            f.parentId = parent;
        }
        f.order = order;
        return f;
    }
    static Command command(const QString &id, const QString &folderId, CommandType type = CommandType::Command, int order = -1)
    {
        Command c;
        c.id = id;
        c.name = id;
        c.folderId = folderId;
        c.type = type;
        c.order = order;
        return c;
    }
    static Note note(const QString &id, const QString &folderId)
    {
        Note n;
        n.id = id;
        n.name = id;
        n.folderId = folderId;
        return n;
    }
    // R { A { c1, h1(http) }, B { c2 }, n1 }
    static TreeData sample()
    {
        TreeData d;
        d.folders = {folder("R"), folder("A", "R"), folder("B", "R")};
        d.commands = {command("c1", "A"), command("h1", "A", CommandType::Http), command("c2", "B")};
        d.notes = {note("n1", "R")};
        return d;
    }
    static QStringList ids(const QVector<Command> &v)
    {
        QStringList out;
        for (const Command &c : v) out << c.id;
        return out;
    }
    static QStringList ids(const QVector<Folder> &v)
    {
        QStringList out;
        for (const Folder &f : v) out << f.id;
        return out;
    }

private slots:
    // Ações por árvore: o modo, os grupos, os botões na linha e quais ("só ícones de executar") sobrevivem ao JSON.
    void actionSettingsRoundTripAndFallBackToTheDefaults()
    {
        TreeViewDef view;
        view.id = "t";
        view.actionsMode = "custom";
        view.actionGroups = {"exec", "item"};
        view.inlineActions = "on";
        view.inlineKinds = {"play"};
        const TreeViewDef back = TreeViewDef::fromJson(view.toJson());
        QCOMPARE(back.actionsMode, QStringLiteral("custom"));
        QCOMPARE(back.actionGroups, (QStringList{"exec", "item"}));
        QCOMPARE(back.inlineActions, QStringLiteral("on"));
        QCOMPARE(back.inlineKinds, (QStringList{"play"}));
        QVERIFY(back == view);

        // Arquivo antigo (sem as chaves) e valores inválidos: o padrão.
        const TreeViewDef old = TreeViewDef::fromJson(QJsonObject{{"id", "x"}, {"actions_mode", "bogus"}, {"inline_actions", "maybe"}});
        QCOMPARE(old.actionsMode, QStringLiteral("auto"));
        QCOMPARE(old.inlineActions, QStringLiteral("inherit"));
        QVERIFY(old.actionGroups.isEmpty());
    }

    void aNotesOnlyTreeDoesNotShowRunnables()
    {
        TreeViewDef notes;
        notes.types = {"note"};
        QVERIFY(!notes.showsRunnables());
        QVERIFY(notes.showsType("note"));
        QVERIFY(!notes.showsType("pipeline"));
        QVERIFY(TreeViewDef{}.showsRunnables()); // sem filtro de tipo mostra tudo
    }

    void emptyViewShowsEverything()
    {
        const TreeData out = filterTreeData(TreeViewDef{}, sample());
        QCOMPARE(ids(out.folders), (QStringList{"R", "A", "B"}));
        QCOMPARE(ids(out.commands), (QStringList{"c1", "h1", "c2"}));
        QCOMPARE(out.notes.size(), 1);
    }

    void typeFilterKeepsOnlyThoseItemsAndTheFoldersAroundThem()
    {
        TreeViewDef view;
        view.types = {"http"};
        const TreeData out = filterTreeData(view, sample());
        QCOMPARE(ids(out.commands), QStringList{"h1"});
        QCOMPARE(ids(out.folders), (QStringList{"R", "A"})); // B não tem nenhum http
        QVERIFY(out.notes.isEmpty());
    }

    void chosenFolderBecomesTheRootOfTheTree()
    {
        TreeViewDef view;
        view.folderIds = {"A"};
        const TreeData out = filterTreeData(view, sample());
        QCOMPARE(ids(out.folders), QStringList{"A"}); // sem R: A é a raiz
        QCOMPARE(ids(out.commands), (QStringList{"c1", "h1"}));
        QVERIFY(out.notes.isEmpty());
    }

    void chosenFolderShowsEvenWhenNothingInsideMatches()
    {
        TreeViewDef view;
        view.folderIds = {"B"};
        view.types = {"note"};
        const TreeData out = filterTreeData(view, sample());
        QCOMPARE(ids(out.folders), QStringList{"B"});
        QVERIFY(out.commands.isEmpty());
    }

    void parentCommandsStayAsContextForAKeptChild()
    {
        TreeData d = sample();
        d.commands << command("child", "c1", CommandType::Http);
        TreeViewDef view;
        view.types = {"http"};
        const TreeData out = filterTreeData(view, d);
        QVERIFY(ids(out.commands).contains(QStringLiteral("child")));
        QVERIFY(ids(out.commands).contains(QStringLiteral("c1"))); // o pai fica, mesmo sendo de outro tipo
    }

    void jsonRoundTripAndBadInput()
    {
        TreeViewDef view;
        view.id = "t1";
        view.name = "HTTP";
        view.icon = "globe";
        view.folderIds = {"A", "B"};
        view.types = {"http", "bogus"};
        view.runningOnly = true;
        view.query = "api";
        const TreeViewDef back = TreeViewDef::fromJson(view.toJson());
        QCOMPARE(back.folderIds, view.folderIds);
        QCOMPARE(back.types, QStringList{"http"}); // tipo desconhecido é descartado
        QVERIFY(back.runningOnly);
        QCOMPARE(back.query, QStringLiteral("api"));
        QVERIFY(TreeViewDef::fromJson(QJsonObject{{"folder_ids", 5}}).folderIds.isEmpty());
    }

    // Arrastar numa árvore filtrada: os visíveis trocam entre si as posições; o oculto não sai do lugar.
    void reorderInAFilteredTreeKeepsHiddenItemsInPlace()
    {
        TreeData d;
        d.folders = {folder("R")};
        d.commands = {command("a", "R", CommandType::Http, 0), command("x", "R", CommandType::Command, 1),
                      command("b", "R", CommandType::Http, 2), command("c", "R", CommandType::Http, 3)};
        // A árvore http mostra a, b, c; o usuário arrasta c para o topo: c, a, b.
        const QVector<TreePlacement> visual = {
            {"c", TreePlacement::Kind::Command, "R", 0},
            {"a", TreePlacement::Kind::Command, "R", 0},
            {"b", TreePlacement::Kind::Command, "R", 0}};
        const QVector<TreePlacement> out = remapFilteredReorder(d, visual, "R");
        QCOMPARE(out.size(), 4);
        QHash<QString, int> order;
        for (const TreePlacement &p : out) {
            order.insert(p.id, p.order);
            QCOMPARE(p.parentId, QStringLiteral("R"));
        }
        QCOMPARE(order.value("x"), 1);              // o oculto continua na posição 1
        QVERIFY(order.value("c") < order.value("a"));
        QVERIFY(order.value("a") < order.value("b"));
        QCOMPARE(QSet<int>(order.cbegin(), order.cend()).size(), 4); // posições todas distintas
    }

    void reorderThatDoesNotMatchTheRootWritesNothing()
    {
        TreeData d = sample();
        const QVector<TreePlacement> visual = {{"ghost", TreePlacement::Kind::Command, "A", 0}};
        QVERIFY(remapFilteredReorder(d, visual, "A").isEmpty());
    }

    void followerReceivesFilteredDataFromTheMainTree()
    {
        CommandTreeWidget main;
        CommandTreeWidget follower;
        TreeViewDef view;
        view.folderIds = {"A"};
        follower.setView(view);
        main.addFollower(&follower);

        TreeData d = sample();
        main.setData(d.folders, d.commands, {}, d.notes);
        auto *tabs = follower.findChild<QTabWidget *>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1); // só A, como raiz
        auto *tree = qobject_cast<QTreeWidget *>(tabs->widget(0));
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 2);

        d.commands << command("c3", "A");
        main.setData(d.folders, d.commands, {}, d.notes);
        tree = qobject_cast<QTreeWidget *>(follower.findChild<QTabWidget *>()->widget(0));
        QCOMPARE(tree->topLevelItemCount(), 3); // a novidade da principal chegou
        QVERIFY(follower.isFiltered());
        QVERIFY(!main.isFiltered());
    }

    // Selecionar numa árvore extra espelha a seleção na principal sem trocar a aba que o usuário deixou à vista nela.
    void mirroredSelectionDoesNotSwitchTheVisibleTab()
    {
        CommandTreeWidget main;
        TreeData d;
        d.folders = {folder("R1", QString(), 0), folder("R2", QString(), 1)};
        d.commands = {command("c1", "R1"), command("c2", "R2")};
        main.setData(d.folders, d.commands, {}, {});
        auto *tabs = main.findChild<QTabWidget *>();
        QVERIFY(tabs && tabs->count() == 2);
        tabs->setCurrentIndex(0);

        QVERIFY(main.selectCommand("c2", false));
        QCOMPARE(tabs->currentIndex(), 0); // a aba da principal não mudou
        QCOMPARE(main.currentSelectionId(), QStringLiteral("c2")); // mas a seleção da principal é a espelhada

        QVERIFY(main.selectCommand("c2")); // seleção explícita (paleta, "ir para") mostra o item
        QCOMPARE(tabs->currentIndex(), 1);
    }
};

QTEST_MAIN(TestTreeView)
#include "test_tree_view.moc"
