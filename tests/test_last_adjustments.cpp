#include <QTest>
#include <QTreeWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QComboBox>
#include <QLineEdit>
#include <functional>

#include "ui/command-tree-widget.h"
#include "ui/command-editor-dialog.h"
#include "ui/folder-editor-dialog.h"
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

// Cobre os 3 últimos ajustes solicitados pelo usuário antes de pausar a
// sessão: (1) pastas iniciam colapsadas por padrão; (2) Enter alterna
// expandir/colapsar numa pasta selecionada; (3) o combo de pasta destino
// do CommandEditorDialog permite escolher/trocar livremente a pasta do
// comando, em vez de ficar fixo na pasta onde o diálogo foi aberto.
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
        QVERIFY(!folderItem->isExpanded()); // colapsada por padrão

        // Simula Enter/duplo-clique (QTreeWidget::itemActivated).
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

        auto *folderCombo = dialog.findChildren<QComboBox *>().first();
        QVERIFY(folderCombo != nullptr);
        // 3 itens agora: "(Raiz)" (folderId vazio) + as 2 pastas. A opção
        // Raiz foi adicionada (feedback do usuário: comando também vai à
        // raiz, como coleção).
        QCOMPARE(folderCombo->count(), 3);

        // Seleção inicial deve refletir a pasta passada no construtor.
        QCOMPARE(folderCombo->currentData().toString(), QStringLiteral("f_a"));

        // Usuário troca para a Pasta B.
        const int indexOfB = folderCombo->findData(QStringLiteral("f_b"));
        QVERIFY(indexOfB >= 0);
        folderCombo->setCurrentIndex(indexOfB);

        auto *nameField = dialog.findChild<QLineEdit *>();
        nameField->setText(QStringLiteral("Comando Teste"));

        const Command built = dialog.buildCommand();
        QCOMPARE(built.folderId, QStringLiteral("f_b"));

        // Escolher "(Raiz)" (data vazia) DEVE resultar em folderId vazio —
        // regressão do bug: antes caía de volta na pasta original.
        const int rootIdx = folderCombo->findData(QString());
        QVERIFY(rootIdx >= 0);
        folderCombo->setCurrentIndex(rootIdx);
        const Command builtRoot = dialog.buildCommand();
        QVERIFY(builtRoot.folderId.isEmpty());
    }

    // Regressão (bug reportado): ao EDITAR um comando, ele "saía da pasta"
    // e "pulava para o fim da lista". Causa raiz: buildCommand() criava um
    // Command novo (order == -1 default) descartando o order do existente,
    // fazendo-o cair no grupo "sem ordem manual" (fim) na renderização.
    // A correção preserva order (e folderId via combo) na edição.
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
        existing.order = 3; // posição manual definida por drag anterior

        CommandEditorDialog dialog(QStringLiteral("f_a"), {}, {folderA}, nullptr, &existing);

        const Command built = dialog.buildCommand();
        QCOMPARE(built.order, 3);                          // order preservado
        QCOMPARE(built.folderId, QStringLiteral("f_a"));   // não saiu da pasta
        QCOMPARE(built.id, QStringLiteral("c_existing"));  // mesmo id (edição)
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

    // Regressão (bug reportado: "não consigo gravar parâmetros dinâmicos para
    // comandos HTTP"). Antes, buildCommand() só atribuía command.params no ramo
    // SHELL; comandos HTTP descartavam os parâmetros ao salvar. E o carregamento
    // idem: um HTTP existente abria sem seus params. Este teste trava os dois
    // lados: um HTTP com params, editado no diálogo, deve SAIR com os params.
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

    // Regressão (bug reportado): ao finalizar a edição de um comando, as
    // pastas autocolapsavam porque persistir chama setData->rebuildTabs,
    // que recria a árvore do zero. A correção captura os ids expandidos
    // antes e os reexpande depois. Este teste simula: expande uma subpasta,
    // dispara um novo setData (equivalente ao persist pós-edição) e
    // verifica que a subpasta continua expandida.
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

        // Localiza o item da subpasta e o expande.
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

        // Dispara o rebuild (equivalente ao persist pós-edição).
        widget.setData({root, sub}, {cmd});

        // A subpasta deve continuar expandida após o rebuild.
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
