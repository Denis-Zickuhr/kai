#include <QTest>
#include <QTableWidget>
#include <QComboBox>

#include "ui/features/collections/collection-editor-dialog.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Cobre o grid de edição de coleções. Como simular edição de
// células/cliques é frágil em teste headless, focamos no contrato de
// dados: o diálogo abre com a coleção dada, e buildCollection() a devolve
// preservando schema/entries/tags/favoritos após o round-trip interno
// (setupUi -> rebuildTable -> collectTableIntoEntries).
class TestCollectionEditor : public QObject {
    Q_OBJECT

private slots:
    void buildCollectionPreservesSchemaAndEntries()
    {
        Collection col;
        col.id = QStringLiteral("col1");
        col.name = QStringLiteral("Usuários");
        col.schema = {
            {QStringLiteral("key"), QStringLiteral("Chave"), CollectionFieldType::Key},
            {QStringLiteral("value"), QStringLiteral("Nome"), CollectionFieldType::Value},
            {QStringLiteral("email"), QStringLiteral("E-mail"), CollectionFieldType::Email},
        };
        CollectionEntry e;
        e.id = QStringLiteral("e1");
        e.values = {{QStringLiteral("key"), QStringLiteral("u1")},
                    {QStringLiteral("value"), QStringLiteral("Alice")},
                    {QStringLiteral("email"), QStringLiteral("a@x.com")}};
        e.favorite = true;
        col.entries = {e};

        CollectionEditorDialog dialog(col);
        // Localiza a tabela e força a coleta de volta (aceitar faria isso;
        // aqui validamos o buildCollection direto após o setup).
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        // Layout de colunas: [0]=seleção (checkbox), [1]=favorito (estrela),
        // [2..]=campos do schema, [última]=tags. Asserção RELATIVA ao schema para
        // não quebrar a cada ajuste de layout: 2 colunas fixas na frente + N
        // seleção + os 3 campos do schema (favorito virou ícone embutido na
        // 1a coluna, sem coluna própria; tags removidas).
        const int fixedLeadingColumns = 1;  // só seleção
        QCOMPARE(table->columnCount(), fixedLeadingColumns + 3);
        QCOMPARE(table->rowCount(), 1);

        const Collection built = dialog.buildCollection();
        QCOMPARE(built.schema.size(), 3);
        QCOMPARE(built.entries.size(), 1);
        QCOMPARE(built.entries.at(0).values.value(QStringLiteral("value")), QStringLiteral("Alice"));
        QCOMPARE(built.entries.at(0).values.value(QStringLiteral("email")), QStringLiteral("a@x.com"));
        QVERIFY(built.entries.at(0).favorite);
    }

    void emptySchemaFallsBackToDefaultInDialog()
    {
        Collection col;
        col.id = QStringLiteral("col2");
        col.name = QStringLiteral("Vazia");
        // schema vazio -> o diálogo deve aplicar o default key/value.
        CollectionEditorDialog dialog(col);
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        // seleção + key + value (favorito é ícone embutido, sem coluna).
        QCOMPARE(table->columnCount(), 1 + 2);
        const Collection built = dialog.buildCollection();
        QCOMPARE(built.schema.size(), 2);
    }

    // Regressão: o diálogo recebe a lista de pastas e mostra um combo de
    // pasta que reflete a folderId da coleção (organização em pastas).
    void dialogExposesFolderCombo()
    {
        Collection col;
        col.id = QStringLiteral("col3");
        col.name = QStringLiteral("Clientes");
        col.folderId = QStringLiteral("f1");

        QVector<Folder> folders;
        Folder f;
        f.id = QStringLiteral("f1");
        f.name = QStringLiteral("Projeto A");
        folders << f;

        CollectionEditorDialog dialog(col, folders);
        auto *combo = dialog.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        // (Raiz) + Projeto A = 2 itens; a pasta atual deve estar selecionada.
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->currentData().toString(), QStringLiteral("f1"));
        // buildCollection preserva a folderId.
        const Collection built = dialog.buildCollection();
        QCOMPARE(built.folderId, QStringLiteral("f1"));
    }

    // Campo "secret": (1) a grade mostra mascarado, não o valor real; (2)
    // buildCollection (que passa por collectTableIntoEntries) preserva o
    // valor VERDADEIRO — não deve gravar de volta o texto mascarado por
    // cima (regressão que apagaria o segredo real ao simplesmente reabrir
    // e fechar o diálogo).
    void secretFieldIsMaskedInGridButValuePreservedOnBuild()
    {
        Collection col;
        col.id = QStringLiteral("col4");
        col.name = QStringLiteral("Credenciais");
        col.schema = {
            {QStringLiteral("label"), QStringLiteral("Label"), CollectionFieldType::Text},
            {QStringLiteral("token"), QStringLiteral("Token"), CollectionFieldType::Text,
             /*visible=*/true, /*secret=*/true},
        };
        CollectionEntry e;
        e.id = QStringLiteral("e1");
        e.values = {{QStringLiteral("label"), QStringLiteral("prod")},
                    {QStringLiteral("token"), QStringLiteral("segredo-de-verdade")}};
        col.entries = {e};

        CollectionEditorDialog dialog(col);
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        // Colunas: [0]=seleção, [1]=label, [2]=token (secreto).
        QTableWidgetItem *secretItem = table->item(0, 2);
        QVERIFY(secretItem != nullptr);
        QVERIFY(!secretItem->text().contains(QStringLiteral("segredo-de-verdade")));
        QVERIFY(!(secretItem->flags() & Qt::ItemIsEditable));

        const Collection built = dialog.buildCollection();
        QCOMPARE(built.entries.at(0).values.value(QStringLiteral("token")),
            QStringLiteral("segredo-de-verdade"));
        QCOMPARE(built.entries.at(0).values.value(QStringLiteral("label")), QStringLiteral("prod"));
    }
};

QTEST_MAIN(TestCollectionEditor)
#include "test_collection_editor.moc"
