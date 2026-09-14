#include <QTest>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QVBoxLayout>

#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/shared/draggable-tree-widget.h"
#include "ui/shared/draggable-table-widget.h"
#include "ui/features/command-editor/parameter-editor-widget.h"
#include "core/models.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using namespace kai::core;

// Cobre o bug reportado: "ordenação de comandos não funciona via drag".
// Causa raiz: o handler era conectado a QAbstractItemModel::rowsMoved, que
// o QTreeWidget InternalMove quase nunca dispara (usa remove+insert). A
// correção usa DraggableTreeWidget::itemsDropped (emitido no dropEvent).
// Aqui validamos que, uma vez reordenados os itens na árvore e disparado o
// sinal de drop, o CommandTreeWidget emite structureChanged com a nova
// ordem sequencial (o item movido para o topo recebe order 0).
class TestDragReorder : public QObject {
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

private slots:
    // Reescrito depois da reescrita de DraggableTreeWidget (pedido do
    // usuário: "bota isso nos cmds agora" — mesma correção da tabela de
    // Parâmetros, sem QDrag/mimeData nenhum): a versão antiga deste teste
    // dizia explicitamente "sem simular um drop real de mouse (inviável
    // em teste headless)" — verdade só enquanto o mecanismo dependia do
    // D&D NATIVO do Qt (que precisa de um loop de eventos do SO de
    // verdade). Rastreamento de mouse manual não tem essa limitação:
    // QTest::mousePress/mouseMove/mouseRelease são eventos comuns, o
    // gesto de arrastar de verdade agora É testável.
    void realMouseDragReordersSiblingsAboveAndBelow()
    {
        DraggableTreeWidget tree;
        auto *a = new QTreeWidgetItem(&tree, {QStringLiteral("A")});
        auto *b = new QTreeWidgetItem(&tree, {QStringLiteral("B")});
        auto *c = new QTreeWidgetItem(&tree, {QStringLiteral("C")});
        Q_UNUSED(b);
        tree.resize(300, 300);
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));

        QSignalSpy spy(&tree, &DraggableTreeWidget::itemsDropped);

        // Arrasta "C" pro TOPO do retângulo de "A" (zona "Above" — terço
        // superior) — deve virar irmão, ANTES de "A": [C, A, B].
        const QRect aRect = tree.visualItemRect(a);
        const QRect cRect = tree.visualItemRect(c);
        const QPoint fromPos = cRect.center();
        const QPoint toPos(aRect.center().x(), aRect.top() + 2); // bem no topo = zona "Above"

        QTest::mousePress(tree.viewport(), Qt::LeftButton, Qt::NoModifier, fromPos);
        QTest::mouseMove(tree.viewport(), QPoint((fromPos.x() + toPos.x()) / 2, (fromPos.y() + toPos.y()) / 2));
        QTest::mouseMove(tree.viewport(), toPos);
        QTest::mouseRelease(tree.viewport(), Qt::LeftButton, Qt::NoModifier, toPos);

        QVERIFY2(spy.count() >= 1, "itemsDropped não disparou");
        QCOMPARE(tree.topLevelItemCount(), 3);
        QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("C"));
        QCOMPARE(tree.topLevelItem(1)->text(0), QStringLiteral("A"));
        QCOMPARE(tree.topLevelItem(2)->text(0), QStringLiteral("B"));
    }

    // Mesmo gesto, mas soltando no MEIO do item alvo (zona "On") — deve
    // REPARENTAR (virar filho), não reordenar como irmão. Sem nenhum
    // setCanAcceptChildrenPredicate() (comportamento PADRÃO do widget,
    // sem restrição) — quem quiser restringir (ver CommandTreeWidget)
    // chama o setter; sem chamar, qualquer item aceita, como sempre foi.
    void realMouseDragOntoItemReparentsAsChild()
    {
        DraggableTreeWidget tree;
        auto *folder = new QTreeWidgetItem(&tree, {QStringLiteral("Pasta")});
        auto *leaf = new QTreeWidgetItem(&tree, {QStringLiteral("Comando")});
        tree.resize(300, 300);
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));

        QSignalSpy spy(&tree, &DraggableTreeWidget::itemsDropped);

        const QRect leafRect = tree.visualItemRect(leaf);
        const QRect folderRect = tree.visualItemRect(folder);
        const QPoint fromPos = leafRect.center();
        const QPoint toPos = folderRect.center(); // bem no meio = zona "On"

        QTest::mousePress(tree.viewport(), Qt::LeftButton, Qt::NoModifier, fromPos);
        QTest::mouseMove(tree.viewport(), QPoint((fromPos.x() + toPos.x()) / 2, (fromPos.y() + toPos.y()) / 2));
        QTest::mouseMove(tree.viewport(), toPos);
        QTest::mouseRelease(tree.viewport(), Qt::LeftButton, Qt::NoModifier, toPos);

        QVERIFY2(spy.count() >= 1, "itemsDropped não disparou");
        QCOMPARE(tree.topLevelItemCount(), 1);
        QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("Pasta"));
        QCOMPARE(tree.topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree.topLevelItem(0)->child(0)->text(0), QStringLiteral("Comando"));
    }

    // Bug real reportado: "eu to conseguindo aninhar cmds dentro de
    // outros cmds, não era pra dar". CommandTreeWidget restringe a zona
    // "On" a itens que não são comando/coleção (só pastas aceitam
    // filhos) — soltar um comando bem no MEIO de outro comando deve cair
    // pra reordenar como irmão (a metade de cima/baixo decide acima ou
    // abaixo), nunca reparentar.
    void draggingCommandOntoAnotherCommandNeverNestsIt()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Raiz");

        Command a;
        a.id = QStringLiteral("c_a");
        a.folderId = QStringLiteral("f_root");
        a.name = QStringLiteral("Alpha");
        a.command = QStringLiteral("echo a");
        a.order = 0;

        Command b;
        b.id = QStringLiteral("c_b");
        b.folderId = QStringLiteral("f_root");
        b.name = QStringLiteral("Bravo");
        b.command = QStringLiteral("echo b");
        b.order = 1;

        CommandTreeWidget widget;
        widget.setData({root}, {a, b});
        auto *tree = qobject_cast<DraggableTreeWidget *>(treeForRoot(widget, QStringLiteral("f_root")));
        QVERIFY(tree);
        widget.resize(300, 300);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));

        QTreeWidgetItem *alpha = tree->topLevelItem(0);
        QTreeWidgetItem *bravo = tree->topLevelItem(1);
        QVERIFY(alpha);
        QVERIFY(bravo);

        QSignalSpy spy(&widget, &CommandTreeWidget::structureChanged);

        // Solta "Bravo" bem no CENTRO de "Alpha" (zona "On", se aceita) —
        // como comandos não aceitam filhos, deve virar "Below" (Bravo
        // continua depois de Alpha — ordem inalterada) em vez de filho.
        const QPoint fromPos = tree->visualItemRect(bravo).center();
        const QPoint toPos = tree->visualItemRect(alpha).center();
        QTest::mousePress(tree->viewport(), Qt::LeftButton, Qt::NoModifier, fromPos);
        QTest::mouseMove(tree->viewport(), QPoint((fromPos.x() + toPos.x()) / 2, (fromPos.y() + toPos.y()) / 2));
        QTest::mouseMove(tree->viewport(), toPos);
        QTest::mouseRelease(tree->viewport(), Qt::LeftButton, Qt::NoModifier, toPos);

        QCOMPARE(alpha->childCount(), 0);
        QCOMPARE(bravo->childCount(), 0);
        QCOMPARE(tree->topLevelItemCount(), 2);
        if (spy.count() > 0) {
            // Se o drop foi tratado como reorder (Below, sem mudança de
            // pai), structureChanged pode disparar — o que importa é que
            // NENHUM comando virou pai do outro.
            const auto placements = qvariant_cast<QVector<TreeNodePlacement>>(spy.last().at(0));
            for (const TreeNodePlacement &p : placements) {
                QCOMPARE(p.parentId, QStringLiteral("f_root"));
            }
        }
    }

    // Move o 2º comando para o topo da árvore e dispara o recálculo de
    // ordem (handleItemsReordered, slot invocado pelo itemsDropped). O
    // structureChanged resultante deve refletir a nova ordem: o item
    // movido para o topo com order 0.
    void movingCommandToTopReindexesOrderViaStructureChanged()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Raiz");

        Command a;
        a.id = QStringLiteral("c_a");
        a.folderId = QStringLiteral("f_root");
        a.name = QStringLiteral("Alpha");
        a.command = QStringLiteral("echo a");
        a.order = 0;

        Command b;
        b.id = QStringLiteral("c_b");
        b.folderId = QStringLiteral("f_root");
        b.name = QStringLiteral("Bravo");
        b.command = QStringLiteral("echo b");
        b.order = 1;

        CommandTreeWidget widget;
        widget.setData({root}, {a, b});

        QTreeWidget *tree = treeForRoot(widget, QStringLiteral("f_root"));
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->topLevelItemCount(), 2);

        // Ordem inicial na árvore: Alpha, Bravo.
        QCOMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole + 1).toString(), QStringLiteral("c_a"));

        QSignalSpy spy(&widget, &CommandTreeWidget::structureChanged);
        QVERIFY(spy.isValid());

        // Simula o resultado de um drag "Bravo para o topo": remove Bravo e
        // reinsere na posição 0 (o que o QTreeWidget faz internamente no
        // drop). Depois dispara o recálculo de ordem via o slot conectado
        // ao itemsDropped.
        QTreeWidgetItem *bravo = tree->takeTopLevelItem(1);
        tree->insertTopLevelItem(0, bravo);
        QMetaObject::invokeMethod(&widget, "handleItemsReordered");

        QVERIFY(spy.count() >= 1);
        const auto placements = qvariant_cast<QVector<TreeNodePlacement>>(spy.last().at(0));
        // Encontra a colocação de cada comando.
        int orderA = -99, orderB = -99;
        for (const TreeNodePlacement &p : placements) {
            if (p.id == QStringLiteral("c_a")) orderA = p.order;
            if (p.id == QStringLiteral("c_b")) orderB = p.order;
        }
        // Bravo foi para o topo => order 0; Alpha => order 1.
        QCOMPARE(orderB, 0);
        QCOMPARE(orderA, 1);
    }

    // Reproduz o bug reportado: reordenar PASTAS via drag não funciona.
    // Cria 3 subpastas sob uma raiz, move a 3a para o topo e verifica que
    // o structureChanged reflete a nova ordem das pastas.
    void movingFolderToTopReindexesOrderViaStructureChanged()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Raiz");

        auto sub = [](const QString &id, const QString &name, int order) {
            Folder f;
            f.id = id;
            f.name = name;
            f.parentId = QStringLiteral("f_root");
            f.order = order;
            return f;
        };
        Folder s1 = sub(QStringLiteral("f_s1"), QStringLiteral("Um"), 0);
        Folder s2 = sub(QStringLiteral("f_s2"), QStringLiteral("Dois"), 1);
        Folder s3 = sub(QStringLiteral("f_s3"), QStringLiteral("Tres"), 2);

        CommandTreeWidget widget;
        widget.setData({root, s1, s2, s3}, {});

        QTreeWidget *tree = treeForRoot(widget, QStringLiteral("f_root"));
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->topLevelItemCount(), 3);

        QSignalSpy spy(&widget, &CommandTreeWidget::structureChanged);
        QVERIFY(spy.isValid());

        // Move "Tres" (índice 2) para o topo.
        QTreeWidgetItem *tres = tree->takeTopLevelItem(2);
        tree->insertTopLevelItem(0, tres);
        QMetaObject::invokeMethod(&widget, "handleItemsReordered");

        QVERIFY(spy.count() >= 1);
        const auto placements = qvariant_cast<QVector<TreeNodePlacement>>(spy.last().at(0));
        int o1 = -99, o2 = -99, o3 = -99;
        for (const TreeNodePlacement &p : placements) {
            if (p.id == QStringLiteral("f_s1")) o1 = p.order;
            if (p.id == QStringLiteral("f_s2")) o2 = p.order;
            if (p.id == QStringLiteral("f_s3")) o3 = p.order;
        }
        // Tres foi para o topo => order 0; Um => 1; Dois => 2.
        QCOMPARE(o3, 0);
        QCOMPARE(o1, 1);
        QCOMPARE(o2, 2);
    }

    // Ciclo completo: após reordenar e aplicar os novos orders, um novo
    // setData (reload, simulando reabrir o app) deve manter a árvore na
    // ordem manual — Tres, Um, Dois no topo.
    void reorderedFolderOrderSurvivesReload()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Raiz");
        auto sub = [](const QString &id, const QString &name, int order) {
            Folder f; f.id = id; f.name = name;
            f.parentId = QStringLiteral("f_root"); f.order = order; return f;
        };
        // Estado JÁ reordenado (Tres=0, Um=1, Dois=2), como ficaria após o
        // drag + persistência.
        Folder s1 = sub(QStringLiteral("f_s1"), QStringLiteral("Um"), 1);
        Folder s2 = sub(QStringLiteral("f_s2"), QStringLiteral("Dois"), 2);
        Folder s3 = sub(QStringLiteral("f_s3"), QStringLiteral("Tres"), 0);

        CommandTreeWidget widget;
        widget.setData({root, s1, s2, s3}, {});
        QTreeWidget *tree = treeForRoot(widget, QStringLiteral("f_root"));
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->topLevelItemCount(), 3);
        // A ordem visual deve refletir a ordem manual: Tres, Um, Dois.
        QCOMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole + 1).toString(), QStringLiteral("f_s3"));
        QCOMPARE(tree->topLevelItem(1)->data(0, Qt::UserRole + 1).toString(), QStringLiteral("f_s1"));
        QCOMPARE(tree->topLevelItem(2)->data(0, Qt::UserRole + 1).toString(), QStringLiteral("f_s2"));
    }
    // Reproduz o bug reportado: uma COLEÇÃO que era a última é movida para
    // cima e "não sai do lugar". Causa raiz dupla: (1) o visit() tratava
    // coleção como pasta (isCollection não existia), então o MainWindow não
    // persistia; (2) o rebuild concatenava pastas->comandos->coleções,
    // jogando coleções sempre ao fim. Aqui validamos que o placement da
    // coleção vem marcado isCollection=true com a nova ordem, e que após um
    // reload com os orders aplicados a coleção aparece ACIMA da pasta.
    void movingCollectionUpReindexesAndSurvivesReload()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        root.name = QStringLiteral("Raiz");

        Folder sub;
        sub.id = QStringLiteral("f_sub");
        sub.name = QStringLiteral("Pasta");
        sub.parentId = QStringLiteral("f_root");
        sub.order = 0;

        Collection col;
        col.id = QStringLiteral("col_x");
        col.name = QStringLiteral("Clientes");
        col.folderId = QStringLiteral("f_root");
        col.order = 1; // começa DEPOIS da pasta (última)

        CommandTreeWidget widget;
        widget.setData({root, sub}, {}, {col});

        QTreeWidget *tree = treeForRoot(widget, QStringLiteral("f_root"));
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->topLevelItemCount(), 2);
        // Ordem inicial: Pasta (0), Clientes (1).
        QCOMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole + 1).toString(), QStringLiteral("f_sub"));
        QCOMPARE(tree->topLevelItem(1)->data(0, Qt::UserRole + 1).toString(), QStringLiteral("col_x"));

        QSignalSpy spy(&widget, &CommandTreeWidget::structureChanged);
        QVERIFY(spy.isValid());

        // Move a coleção (índice 1) para o topo.
        QTreeWidgetItem *clientes = tree->takeTopLevelItem(1);
        tree->insertTopLevelItem(0, clientes);
        QMetaObject::invokeMethod(&widget, "handleItemsReordered");

        QVERIFY(spy.count() >= 1);
        const auto placements = qvariant_cast<QVector<TreeNodePlacement>>(spy.last().at(0));
        int orderCol = -99, orderFolder = -99;
        bool colFlaggedAsCollection = false;
        for (const TreeNodePlacement &p : placements) {
            if (p.id == QStringLiteral("col_x")) {
                orderCol = p.order;
                colFlaggedAsCollection = p.isCollection;
            }
            if (p.id == QStringLiteral("f_sub")) orderFolder = p.order;
        }
        // A coleção deve estar marcada como coleção (senão o MainWindow não
        // persiste) e com order menor que a pasta (foi movida pra cima).
        QVERIFY2(colFlaggedAsCollection, "Placement da coleção deve ter isCollection=true.");
        QVERIFY2(orderCol < orderFolder, "Coleção movida ao topo deve ter order menor que a pasta.");

        // Aplica os novos orders e recarrega (simula reabrir o app): a
        // coleção deve aparecer ACIMA da pasta.
        Collection colMoved = col; colMoved.order = orderCol;
        Folder subMoved = sub;     subMoved.order = orderFolder;
        CommandTreeWidget reloaded;
        reloaded.setData({root, subMoved}, {}, {colMoved});
        QTreeWidget *tree2 = treeForRoot(reloaded, QStringLiteral("f_root"));
        QVERIFY(tree2 != nullptr);
        QCOMPARE(tree2->topLevelItemCount(), 2);
        QCOMPARE(tree2->topLevelItem(0)->data(0, Qt::UserRole + 1).toString(), QStringLiteral("col_x"));
        QCOMPARE(tree2->topLevelItem(1)->data(0, Qt::UserRole + 1).toString(), QStringLiteral("f_sub"));
    }
    // Órfãos (feedback do usuário: permitir criar comandos sem pasta): um
    // comando cujo folderId não resolve para nenhuma pasta deve aparecer
    // numa pasta PADRÃO sintética ("Geral"), nunca sumir. Também vale para
    // folderId vazio.
    void orphanCommandAppearsUnderDefaultFolder()
    {
        Command semPasta;
        semPasta.id = QStringLiteral("c_orfao");
        semPasta.folderId = QString(); // sem pasta
        semPasta.name = QStringLiteral("Solto");
        semPasta.command = QStringLiteral("echo solto");

        Command pastaInexistente;
        pastaInexistente.id = QStringLiteral("c_orfao2");
        pastaInexistente.folderId = QStringLiteral("f_nao_existe");
        pastaInexistente.name = QStringLiteral("Perdido");
        pastaInexistente.command = QStringLiteral("echo perdido");

        CommandTreeWidget widget;
        // Sem NENHUMA pasta cadastrada.
        widget.setData({}, {semPasta, pastaInexistente});

        auto *tabWidget = widget.findChild<QTabWidget *>();
        QVERIFY(tabWidget != nullptr);
        // Deve existir uma aba padrão ("Geral"/"General", conforme o idioma
        // ativo — via i18n, não mais hardcoded) com os dois órfãos.
        QCOMPARE(tabWidget->count(), 1);
        QCOMPARE(tabWidget->tabText(0), kai::utils::tr(QStringLiteral("folder.default_name")));
        auto *tree = qobject_cast<QTreeWidget *>(tabWidget->widget(0));
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->topLevelItemCount(), 2);
    }

    // Pedido do usuário: "parâmetros dinâmicos são fortamente
    // posicionais, preciso de uma estratégia de reorder, podemos usar a
    // mesma estratégia da saída de terminal [árvore de comandos]". A
    // MECÂNICA é diferente de propósito (ver comentário em
    // DraggableTableWidget — cell widgets de ação não sobrevivem a um
    // InternalMove nativo), mas o resultado observável é o mesmo: soltar
    // uma linha reordena o modelo de verdade. Sem simular um drag real de
    // mouse (inviável em teste headless, mesma ressalva do teste da
    // árvore acima) — dispara o sinal rowMoved diretamente, exercitando o
    // handler de reorder de ParameterEditorWidget de ponta a ponta.
    void draggingParameterRowReordersTheRealModel()
    {
        Parameter a;
        a.name = QStringLiteral("alpha");
        a.type = ParameterType::Text;

        Parameter b;
        b.name = QStringLiteral("bravo");
        b.type = ParameterType::Text;

        Parameter c;
        c.name = QStringLiteral("charlie");
        c.type = ParameterType::Text;

        ParameterEditorWidget editor;
        editor.setParameters({a, b, c});

        auto *table = editor.findChild<DraggableTableWidget *>();
        QVERIFY(table != nullptr);

        QSignalSpy spy(&editor, &ParameterEditorWidget::changed);

        // Arrasta "charlie" (linha 2) pro topo (posição 0).
        emit table->rowMoved(2, 0);

        QCOMPARE(spy.count(), 1);
        const QVector<Parameter> reordered = editor.parameters();
        QCOMPARE(reordered.size(), 3);
        QCOMPARE(reordered.at(0).name, QStringLiteral("charlie"));
        QCOMPARE(reordered.at(1).name, QStringLiteral("alpha"));
        QCOMPARE(reordered.at(2).name, QStringLiteral("bravo"));
    }

    // Bug real reportado, DUAS vezes ("as vezes some o param" e depois
    // "ordenação ainda fica errada... é como se o drag colocasse o param
    // dentro do outro componente"): o D&D NATIVO do Qt (QDrag/
    // InternalMove, versão anterior de DraggableTableWidget) tinha um
    // payload de mimeData que outro widget sob o cursor podia engolir no
    // release — não dava pra reproduzir isso com um teste de sinal direto
    // (emit rowMoved(...) acima), porque o bug estava exatamente no
    // MECANISMO de captura do mouse, não no handler de reorder. A versão
    // atual não usa QDrag nenhum (mouse tracking manual), o que finalmente
    // permite testar o gesto de VERDADE com QMouseEvent sintético — sem
    // loop de D&D nativo do SO envolvido, isto roda igual em headless.
    void realMouseDragReordersTheTableWithoutEscapingToAnotherWidget()
    {
        Parameter a; a.name = QStringLiteral("alpha"); a.type = ParameterType::Text;
        Parameter b; b.name = QStringLiteral("bravo"); b.type = ParameterType::Text;
        Parameter c; c.name = QStringLiteral("charlie"); c.type = ParameterType::Text;

        // Um QLineEdit VIZINHO, no mesmo formulário — exatamente o
        // cenário do bug relatado: um campo de texto perto da tabela que
        // podia "engolir" o texto do parâmetro arrastado.
        QWidget host;
        auto *layout = new QVBoxLayout(&host);
        auto *neighborField = new QLineEdit(&host);
        layout->addWidget(neighborField);
        auto *editor = new ParameterEditorWidget(&host);
        layout->addWidget(editor);
        editor->setParameters({a, b, c});

        host.resize(400, 400);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));

        auto *table = editor->findChild<DraggableTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 3);

        const QRect fromRect = table->visualRect(table->model()->index(2, 1)); // "charlie"
        const QRect toRect = table->visualRect(table->model()->index(0, 1));   // linha do "alpha"
        QVERIFY(fromRect.isValid());
        QVERIFY(toRect.isValid());
        const QPoint fromPos = fromRect.center();
        const QPoint toPos = toRect.center();

        QSignalSpy spy(editor, &ParameterEditorWidget::changed);

        // Gesto real: pressiona em "charlie", move em alguns passos
        // (supera o limiar de drag do Qt) até a linha do "alpha", solta.
        QTest::mousePress(table->viewport(), Qt::LeftButton, Qt::NoModifier, fromPos);
        const QPoint mid((fromPos.x() + toPos.x()) / 2, (fromPos.y() + toPos.y()) / 2);
        QTest::mouseMove(table->viewport(), mid);
        QTest::mouseMove(table->viewport(), toPos);
        QTest::mouseRelease(table->viewport(), Qt::LeftButton, Qt::NoModifier, toPos);

        QVERIFY2(spy.count() >= 1, "O drag deveria ter reordenado o modelo real (changed() não disparou)");
        const QVector<Parameter> reordered = editor->parameters();
        QCOMPARE(reordered.size(), 3);
        QCOMPARE(reordered.at(0).name, QStringLiteral("charlie"));
        QCOMPARE(reordered.at(1).name, QStringLiteral("alpha"));
        QCOMPARE(reordered.at(2).name, QStringLiteral("bravo"));

        // O ponto central do bug: o campo vizinho nunca deveria ter
        // recebido nada — sem QDrag/mimeData, não existe payload pra
        // "escapar" pra ele.
        QVERIFY2(neighborField->text().isEmpty(),
            "O campo vizinho não deveria ter recebido nenhum texto do drag");
    }

    // Bug real reportado, PERSISTINDO mesmo depois de tirar o D&D nativo:
    // "arrasto, alguns some, outros vao errados" — no plural, sugerindo
    // que o problema aparece ao longo de VÁRIOS drags na mesma sessão,
    // não só no primeiro. Causa real: mouseReleaseEvent() pulava
    // QTableWidget::mouseReleaseEvent() no caminho de drag concluído —
    // isso deixava o estado INTERNO de QAbstractItemView (índice
    // pressionado/seleção) preso apontando pra uma linha que
    // rebuildTable() já tinha destruído, corrompendo a PRÓXIMA
    // interação. Este teste arrasta MAIS DE UMA VEZ em sequência (o
    // cenário que reproduz) e confere a ordem final E a integridade da
    // tabela (nenhuma linha "fantasma"/sem item) depois de CADA drag.
    void multipleSequentialDragsNeverCorruptStateOrLoseRows()
    {
        QVector<Parameter> params;
        for (const QString &name : {QStringLiteral("alpha"), QStringLiteral("bravo"),
                                     QStringLiteral("charlie"), QStringLiteral("delta"),
                                     QStringLiteral("echo")}) {
            Parameter p; p.name = name; p.type = ParameterType::Text;
            params << p;
        }

        ParameterEditorWidget editor;
        editor.setParameters(params);
        editor.resize(400, 400);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));

        auto *table = editor.findChild<DraggableTableWidget *>();
        QVERIFY(table);

        // toRow pode ser rowCount() (arrastar pro FIM, depois da última
        // linha) — não existe índice de modelo pra isso, então mira um
        // ponto alguns pixels ABAIXO da última linha (mesmo cenário que o
        // clamp de updateIndicatorForPos existe pra cobrir).
        auto dragRow = [&](int fromRow, int toRow) {
            const QRect fromRect = table->visualRect(table->model()->index(fromRow, 1));
            QVERIFY(fromRect.isValid());
            QPoint toPos;
            if (toRow < table->rowCount()) {
                const QRect toRect = table->visualRect(table->model()->index(toRow, 1));
                QVERIFY(toRect.isValid());
                toPos = toRect.center();
            } else {
                const QRect lastRect = table->visualRect(table->model()->index(table->rowCount() - 1, 1));
                QVERIFY(lastRect.isValid());
                toPos = QPoint(lastRect.center().x(), lastRect.bottom() + 5);
            }
            const QPoint fromPos = fromRect.center();
            QTest::mousePress(table->viewport(), Qt::LeftButton, Qt::NoModifier, fromPos);
            QTest::mouseMove(table->viewport(),
                QPoint((fromPos.x() + toPos.x()) / 2, (fromPos.y() + toPos.y()) / 2));
            QTest::mouseMove(table->viewport(), toPos);
            QTest::mouseRelease(table->viewport(), Qt::LeftButton, Qt::NoModifier, toPos);
        };
        auto namesInOrder = [&]() {
            QStringList names;
            for (const Parameter &p : editor.parameters()) {
                names << p.name;
            }
            return names;
        };
        auto verifyNoPhantomRows = [&]() {
            QCOMPARE(table->rowCount(), editor.parameters().size());
            for (int row = 0; row < table->rowCount(); ++row) {
                QVERIFY2(table->item(row, 1) != nullptr,
                    qPrintable(QStringLiteral("linha %1 ficou sem item após o drag").arg(row)));
            }
        };

        // alpha,bravo,charlie,delta,echo -> arrasta "echo" (4) pro topo.
        dragRow(4, 0);
        QCOMPARE(namesInOrder(), (QStringList{"echo", "alpha", "bravo", "charlie", "delta"}));
        verifyNoPhantomRows();

        // Segundo drag, na tabela JÁ reconstruída pelo primeiro: arrasta
        // "alpha" (agora linha 1) pro fim.
        dragRow(1, 5);
        QCOMPARE(namesInOrder(), (QStringList{"echo", "bravo", "charlie", "delta", "alpha"}));
        verifyNoPhantomRows();

        // Terceiro drag: arrasta "charlie" (linha 2) pra logo antes de "echo" (linha 0).
        dragRow(2, 0);
        QCOMPARE(namesInOrder(), (QStringList{"charlie", "echo", "bravo", "delta", "alpha"}));
        verifyNoPhantomRows();
    }
};

QTEST_MAIN(TestDragReorder)
#include "test_drag_reorder.moc"
