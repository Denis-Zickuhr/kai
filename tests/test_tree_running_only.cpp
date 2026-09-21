// Filtro "exibir apenas comandos em execução" da árvore de comandos
// (CommandTreeWidget::setShowRunningOnly): só os que rodam agora (e as pastas
// que os contêm) ficam visíveis, acompanha a lista de execuções em tempo real,
// combina com a busca e mostra uma dica quando não há nenhum.

#include <QTest>
#include <QTabWidget>
#include <QTabBar>
#include <QTreeWidget>
#include <QLabel>

#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/shared/expand-collapse-bar.h"
#include "utils/design-tokens.h"
#include "core/models.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using namespace kai::core;

class TestTreeRunningOnly : public QObject {
    Q_OBJECT

private:
    static QTreeWidget *treeForRoot(CommandTreeWidget &widget, const QString &rootId)
    {
        auto *tabWidget = widget.findChild<QTabWidget *>();
        for (int i = 0; i < tabWidget->count(); ++i) {
            if (tabWidget->tabBar()->tabData(i).toString() == rootId) {
                return qobject_cast<QTreeWidget *>(tabWidget->widget(i));
            }
        }
        return nullptr;
    }

    // Nomes dos itens (em qualquer nível) que estão visíveis (não escondidos).
    static QStringList visibleNames(QTreeWidget *tree)
    {
        QStringList names;
        std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
            if (item->isHidden()) {
                return;
            }
            names << item->text(0);
            for (int i = 0; i < item->childCount(); ++i) {
                walk(item->child(i));
            }
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            walk(tree->topLevelItem(i));
        }
        names.sort();
        return names;
    }

    // Raiz com: Alpha, Charlie (soltos) e a subpasta Sub com Bravo.
    static void fill(CommandTreeWidget &widget)
    {
        Folder root; root.id = "f_root"; root.name = "Raiz";
        Folder sub; sub.id = "f_sub"; sub.name = "Sub"; sub.parentId = "f_root";
        auto cmd = [](const QString &id, const QString &name, const QString &folder, int order) {
            Command c; c.id = id; c.name = name; c.folderId = folder; c.command = "echo"; c.order = order;
            return c;
        };
        widget.setData({root, sub}, {cmd("c_a", "Alpha", "f_root", 0), cmd("c_b", "Bravo", "f_sub", 0),
                                      cmd("c_c", "Charlie", "f_root", 1)});
        widget.resize(320, 360);
    }

private slots:
    void filterKeepsOnlyRunningCommandsAndTheirFolders()
    {
        CommandTreeWidget widget;
        fill(widget);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QTreeWidget *tree = treeForRoot(widget, QStringLiteral("f_root"));
        QVERIFY(tree != nullptr);

        // Sem o filtro: tudo visível.
        QCOMPARE(visibleNames(tree), (QStringList{"Alpha", "Bravo", "Charlie", "Sub"}));

        widget.setRunningCommandIds({QStringLiteral("c_b")});
        widget.setShowRunningOnly(true);
        QVERIFY(widget.showRunningOnly());
        // Só Bravo e a pasta que o contém.
        QCOMPARE(visibleNames(tree), (QStringList{"Bravo", "Sub"}));

        // Acompanha em tempo real: Alpha começa a rodar, Bravo termina.
        widget.setRunningCommandIds({QStringLiteral("c_a")});
        QCOMPARE(visibleNames(tree), (QStringList{"Alpha"}));

        // Desligar traz tudo de volta.
        widget.setShowRunningOnly(false);
        QCOMPARE(visibleNames(tree), (QStringList{"Alpha", "Bravo", "Charlie", "Sub"}));
    }

    void searchCombinesWithTheFilter()
    {
        CommandTreeWidget widget;
        fill(widget);
        QTreeWidget *tree = treeForRoot(widget, QStringLiteral("f_root"));
        QVERIFY(tree != nullptr);
        widget.setRunningCommandIds({QStringLiteral("c_a"), QStringLiteral("c_b")});
        widget.setShowRunningOnly(true);
        widget.setFilterQuery(QStringLiteral("bra"));
        QCOMPARE(visibleNames(tree), (QStringList{"Bravo", "Sub"}));
        widget.setFilterQuery(QString());
        QCOMPARE(visibleNames(tree), (QStringList{"Alpha", "Bravo", "Sub"}));
    }

    // Troca de tema: o sublinhado da aba selecionada (accent) tem de acompanhar.
    // O estilo das abas raiz só era montado na construção e ficava no accent
    // do tema anterior.
    void rootTabBarStyleFollowsTheAccentAfterAThemeChange()
    {
        namespace tk = kai::utils::tokens;
        tk::publishTheme({{QStringLiteral("bg"), QStringLiteral("#101010")},
                          {QStringLiteral("fg"), QStringLiteral("#eeeeee")},
                          {QStringLiteral("accent_color"), QStringLiteral("#ff2244")}});
        CommandTreeWidget widget;
        fill(widget);
        QTabBar *bar = widget.findChild<QTabWidget *>()->tabBar();
        widget.setTabBarHeight(36);
        QVERIFY(bar->styleSheet().contains(QStringLiteral("#ff2244"), Qt::CaseInsensitive));

        // Outro tema: depois de reaplicar (o que o MainWindow faz ao recarregar
        // o tema), o estilo usa o accent novo e não o antigo.
        tk::publishTheme({{QStringLiteral("bg"), QStringLiteral("#101010")},
                          {QStringLiteral("fg"), QStringLiteral("#eeeeee")},
                          {QStringLiteral("accent_color"), QStringLiteral("#22cc88")}});
        widget.setTabBarHeight(36);
        QVERIFY(bar->styleSheet().contains(QStringLiteral("#22cc88"), Qt::CaseInsensitive));
        QVERIFY(!bar->styleSheet().contains(QStringLiteral("#ff2244"), Qt::CaseInsensitive));
        tk::publishTheme({});
    }

    // O contorno do toggle ligado na barra de exibição segue o accent do tema.
    void expandCollapseBarToggleOutlineFollowsTheAccent()
    {
        namespace tk = kai::utils::tokens;
        tk::publishTheme({{QStringLiteral("accent_color"), QStringLiteral("#ff2244")}});
        ExpandCollapseBar bar;
        bar.applyAccentColor(QColor(Qt::gray));
        QVERIFY(bar.styleSheet().contains(QStringLiteral("#ff2244"), Qt::CaseInsensitive));
        tk::publishTheme({{QStringLiteral("accent_color"), QStringLiteral("#22cc88")}});
        bar.applyAccentColor(QColor(Qt::gray));
        QVERIFY(bar.styleSheet().contains(QStringLiteral("#22cc88"), Qt::CaseInsensitive));
        QVERIFY(!bar.styleSheet().contains(QStringLiteral("#ff2244"), Qt::CaseInsensitive));
        tk::publishTheme({});
    }

    // Nada rodando + filtro ligado: aparece a dica (e some ao ligar um comando).
    void showsHintWhenNothingIsRunning()
    {
        CommandTreeWidget widget;
        fill(widget);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.setShowRunningOnly(true);

        QLabel *hint = nullptr;
        for (QLabel *label : widget.findChildren<QLabel *>()) {
            if (label->text() == kai::utils::tr(QStringLiteral("tree.running_only_empty"))) {
                hint = label;
            }
        }
        QVERIFY(hint != nullptr);
        QVERIFY(hint->isVisible());

        widget.setRunningCommandIds({QStringLiteral("c_a")});
        QVERIFY(!hint->isVisible());
        widget.setRunningCommandIds({});
        QVERIFY(hint->isVisible());
        widget.setShowRunningOnly(false);
        QVERIFY(!hint->isVisible());
    }
};

QTEST_MAIN(TestTreeRunningOnly)
#include "test_tree_running_only.moc"
