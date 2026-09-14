#include <QTest>
#include <QTreeWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QPushButton>

#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/shared/action-sidebar.h"
#include "ui/shared/item-actions-bar.h"
#include "ui/shared/fuzzy-search.h"
#include "core/models.h"
#include "utils/translation-manager.h"

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

QToolButton *buttonByTooltip(const ActionSidebar &sidebar, const QString &tooltip)
{
    for (auto *button : sidebar.findChildren<QToolButton *>()) {
        if (button->toolTip() == tooltip) {
            return button;
        }
    }
    return nullptr;
}

QPushButton *buttonByTooltip(const ItemActionsBar &bar, const QString &tooltip)
{
    for (auto *button : bar.findChildren<QPushButton *>()) {
        if (button->toolTip() == tooltip) {
            return button;
        }
    }
    return nullptr;
}
}

// Cobre a arquitetura de ROW ACTIONS na ActionSidebar (feedback do
// usuário: os antigos botões inline por linha na árvore viraram row
// actions sempre carregadas na sidebar, habilitadas conforme o estado da
// linha selecionada). A árvore de comandos agora é de coluna única (sem
// a coluna de controles inline). Os textos dos tooltips vêm do language
// pack i18n; em ambiente de teste sem troca de idioma, o padrão é inglês.
class TestUsabilityAdjustments : public QObject {
    Q_OBJECT

private slots:
    void treeIsSingleColumnWithoutInlineControls()
    {
        Folder root;
        root.id = QStringLiteral("f_1");
        root.name = QStringLiteral("Raiz");

        Command command;
        command.id = QStringLiteral("c_1");
        command.name = QStringLiteral("Comando");
        command.folderId = QStringLiteral("f_1");

        CommandTreeWidget widget;
        widget.setData({root}, {command});

        auto *tree = treeForRoot(widget, QStringLiteral("f_1"));
        QVERIFY(tree != nullptr);
        // Duas colunas agora: [0] ícone+nome (com estrutura de árvore) e
        // [1] indicador de status "rodando" grande e separado (feedback do
        // usuário). Os controles inline permanecem removidos (viraram row
        // actions na ActionSidebar).
        QCOMPARE(tree->columnCount(), 2);
        QVERIFY(tree->itemWidget(tree->topLevelItem(0), 0) == nullptr);
        QVERIFY(tree->itemWidget(tree->topLevelItem(0), 1) == nullptr);
    }

    void sidebarRowActionsAlwaysLoaded()
    {
        // As row actions (grupo Execução) ficam SEMPRE carregadas (widgets
        // existem), apenas habilitadas/desabilitadas conforme o contexto.
        // Edit/Delete moram no grupo Item (ItemActionsBar); "editar body
        // (rápido)" mora aqui, no grupo Execução (pedido do usuário).
        ActionSidebar sidebar;
        QVERIFY(buttonByTooltip(sidebar, QStringLiteral("Run command")) != nullptr);
        QVERIFY(buttonByTooltip(sidebar, QStringLiteral("Stop (terminate process)")) != nullptr);
        QVERIFY(buttonByTooltip(sidebar, QStringLiteral("Force stop (kill process)")) != nullptr);
        QVERIFY(buttonByTooltip(sidebar, QStringLiteral("Reset and restart command")) != nullptr);
        QVERIFY(buttonByTooltip(sidebar, QStringLiteral("Edit body (quick)")) != nullptr);

        ItemActionsBar itemBar;
        QVERIFY(buttonByTooltip(itemBar, QStringLiteral("Edit Command")) != nullptr);
        QVERIFY(buttonByTooltip(itemBar, QStringLiteral("Delete Command")) != nullptr);
    }

    void rowActionsEnabledAccordingToContext()
    {
        ActionSidebar sidebar;
        auto *play = buttonByTooltip(sidebar, QStringLiteral("Run command"));
        auto *stop = buttonByTooltip(sidebar, QStringLiteral("Stop (terminate process)"));
        auto *reset = buttonByTooltip(sidebar, QStringLiteral("Reset and restart command"));
        auto *editBody = buttonByTooltip(sidebar, QStringLiteral("Edit body (quick)"));

        // Sem seleção: tudo desabilitado.
        sidebar.setRowContext(false, false, false, false);
        QVERIFY(!play->isEnabled());
        QVERIFY(!stop->isEnabled());
        QVERIFY(!editBody->isEnabled());

        // Comando parado selecionado: play habilitado, stop não. Editar
        // body só habilita para comando HTTP (último parâmetro).
        sidebar.setRowContext(true, true, false, false, false);
        QVERIFY(play->isEnabled());
        QVERIFY(!stop->isEnabled());
        QVERIFY(!editBody->isEnabled());
        sidebar.setRowContext(true, true, false, false, true);
        QVERIFY(editBody->isEnabled());

        // Comando rodando: stop habilitado e reset habilitado; play não.
        sidebar.setRowContext(true, true, true, false);
        QVERIFY(!play->isEnabled());
        QVERIFY(stop->isEnabled());
        QVERIFY(reset->isEnabled());

        // Comando parado que falhou: reset continua habilitado, play volta.
        sidebar.setRowContext(true, true, false, true);
        QVERIFY(play->isEnabled());
        QVERIFY(reset->isEnabled());

        // Pasta selecionada: play/stop/reset/editBody desabilitados.
        sidebar.setRowContext(true, false, false, false);
        QVERIFY(!play->isEnabled());
        QVERIFY(!stop->isEnabled());
        QVERIFY(!reset->isEnabled());
        QVERIFY(!editBody->isEnabled());

        // ItemActionsBar: editar/excluir habilitam com qualquer seleção
        // (pasta ou comando).
        ItemActionsBar itemBar;
        auto *edit = buttonByTooltip(itemBar, QStringLiteral("Edit Command"));
        itemBar.setRowContext(false);
        QVERIFY(!edit->isEnabled());
        itemBar.setRowContext(true);
        QVERIFY(edit->isEnabled());
    }

    void playActionEmitsSignal()
    {
        ActionSidebar sidebar;
        sidebar.setRowContext(true, true, false, false);

        bool played = false;
        QObject::connect(&sidebar, &ActionSidebar::playSelectedRequested,
                          [&played]() { played = true; });

        buttonByTooltip(sidebar, QStringLiteral("Run command"))->click();
        QVERIFY(played);
    }

    void searchBarHasNoIconAndSimplifiedPlaceholder()
    {
        FuzzySearchBar searchBar;
        // Placeholder agora vem do i18n — compara contra a mesma chave, não
        // um literal PT fixo (o padrão do TranslationManager é EN).
        QCOMPARE(searchBar.placeholderText(), kai::utils::tr(QStringLiteral("fuzzy_search.placeholder")));
        QVERIFY(searchBar.actions().isEmpty());
    }

    void selectionAndTreeSurviveRunningStatusChange()
    {
        Folder root;
        root.id = QStringLiteral("f_1");
        root.name = QStringLiteral("Raiz");

        Command command;
        command.id = QStringLiteral("c_1");
        command.name = QStringLiteral("Comando");
        command.folderId = QStringLiteral("f_1");

        CommandTreeWidget widget;
        widget.setData({root}, {command});

        auto *tabWidget = widget.findChild<QTabWidget *>();
        auto *treeBefore = qobject_cast<QTreeWidget *>(tabWidget->currentWidget());
        treeBefore->setCurrentItem(treeBefore->topLevelItem(0));

        widget.setRunningCommandIds({QStringLiteral("c_1")});

        auto *treeAfter = qobject_cast<QTreeWidget *>(tabWidget->currentWidget());
        QCOMPARE(treeAfter, treeBefore); // mesmo QTreeWidget, não recriado.
        QVERIFY(treeAfter->currentItem() != nullptr);
        QCOMPARE(treeAfter->currentItem()->text(0), QStringLiteral("Comando"));
    }

    void commandLabelShowsOnlyNameWithoutParams()
    {
        Folder root;
        root.id = QStringLiteral("f_1");
        root.name = QStringLiteral("Raiz");

        Parameter param;
        param.name = QStringLiteral("env");
        param.type = ParameterType::Select;

        Command command;
        command.id = QStringLiteral("c_1");
        command.name = QStringLiteral("Deploy");
        command.folderId = QStringLiteral("f_1");
        command.params = {param};

        CommandTreeWidget widget;
        widget.setData({root}, {command});

        auto *tabWidget = widget.findChild<QTabWidget *>();
        auto *tree = qobject_cast<QTreeWidget *>(tabWidget->currentWidget());
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Deploy"));
    }

    void runningIndicatorAppearsOnIcon()
    {
        Folder root;
        root.id = QStringLiteral("f_1");
        root.name = QStringLiteral("Raiz");

        Command command;
        command.id = QStringLiteral("c_1");
        command.name = QStringLiteral("Comando");
        command.folderId = QStringLiteral("f_1");

        CommandTreeWidget widget;
        widget.setData({root}, {command});

        auto *tree = treeForRoot(widget, QStringLiteral("f_1"));
        QTreeWidgetItem *commandItem = tree->topLevelItem(0);
        // O indicador de "rodando" agora fica GRANDE e SEPARADO na coluna
        // de status (1), não sobreposto ao ícone do comando (coluna 0)
        // — feedback do usuário. Coluna 0 (ícone do comando) permanece
        // inalterada entre idle/running; coluna 1 ganha/perde o ícone.
        const QIcon col0Idle = commandItem->icon(0);
        QVERIFY(commandItem->icon(1).isNull()); // sem indicador quando idle

        widget.setRunningCommandIds({QStringLiteral("c_1")});
        QVERIFY(!tree->topLevelItem(0)->icon(1).isNull());        // indicador aparece na col 1
        QCOMPARE(tree->topLevelItem(0)->icon(0).pixmap(16, 16).toImage(),
                 col0Idle.pixmap(16, 16).toImage());              // ícone do comando não muda

        widget.setRunningCommandIds({});
        QVERIFY(tree->topLevelItem(0)->icon(1).isNull());         // indicador some ao parar
    }

    void commandNestedInsideCommandStillRenders()
    {
        // Bug corrigido (feedback do usuário): agrupar comando
        // dentro de comando é permitido; o comando aninhado deve
        // continuar aparecendo na árvore (folderId do filho aponta para o
        // id do comando-pai) e o pai continua executável.
        Folder root;
        root.id = QStringLiteral("f_1");
        root.name = QStringLiteral("Raiz");

        Command parent;
        parent.id = QStringLiteral("c_parent");
        parent.name = QStringLiteral("Pai");
        parent.folderId = QStringLiteral("f_1");

        Command child;
        child.id = QStringLiteral("c_child");
        child.name = QStringLiteral("Filho");
        child.folderId = QStringLiteral("c_parent"); // aninhado sob o comando pai

        CommandTreeWidget widget;
        widget.setData({root}, {parent, child});

        auto *tree = treeForRoot(widget, QStringLiteral("f_1"));
        QVERIFY(tree != nullptr);
        // O pai é item de topo e o filho aparece como seu filho na árvore.
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Pai"));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("Filho"));
    }
};

QTEST_MAIN(TestUsabilityAdjustments)
#include "test_usability_adjustments.moc"
