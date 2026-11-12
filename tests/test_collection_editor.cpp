#include <QTest>
#include <QTableWidget>
#include <QComboBox>
#include <QListWidget>
#include <QPlainTextEdit>

#include "ui/features/collections/collection-editor-dialog.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Grid de edição de coleções: simular edição de células é frágil em teste headless, então cobre o contrato de
// dados: o diálogo abre com a coleção dada e buildCollection() a devolve preservada após o round-trip interno.
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
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        // Colunas: seleção (checkbox) + campos do schema; asserção RELATIVA ao schema para não quebrar com ajustes de layout.
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

    // O diálogo recebe a lista de pastas e mostra um combo que reflete a folderId da coleção.
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
        auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("collectionEditorFolderCombo"));
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->currentData().toString(), QStringLiteral("f1"));
        const Collection built = dialog.buildCollection();
        QCOMPARE(built.folderId, QStringLiteral("f1"));
    }

    static QVector<CollectionField> keyValueSchema()
    {
        return {{QStringLiteral("key"), QStringLiteral("Chave"), CollectionFieldType::Key},
                {QStringLiteral("value"), QStringLiteral("Nome"), CollectionFieldType::Value}};
    }

    // Colar da área de transferência: planilha (tab) com cabeçalho pelo RÓTULO do campo.
    void clipboardTabSeparatedRowsWithHeaderByLabel()
    {
        const auto entries = CollectionEditorDialog::parseImportText(
            QStringLiteral("Nome\tChave\nAlice\tu1\nBob\tu2\n"), keyValueSchema());
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).values.value(QStringLiteral("value")), QStringLiteral("Alice"));
        QCOMPARE(entries.at(0).values.value(QStringLiteral("key")), QStringLiteral("u1"));
        QCOMPARE(entries.at(1).values.value(QStringLiteral("key")), QStringLiteral("u2"));
        QVERIFY(entries.at(0).id != entries.at(1).id);
    }

    void clipboardRowsWithoutHeaderMapByPosition()
    {
        const auto tabs = CollectionEditorDialog::parseImportText(
            QStringLiteral("u1\tAlice\r\nu2\tBob"), keyValueSchema());
        QCOMPARE(tabs.size(), 2);
        QCOMPARE(tabs.at(1).values.value(QStringLiteral("key")), QStringLiteral("u2"));
        QCOMPARE(tabs.at(1).values.value(QStringLiteral("value")), QStringLiteral("Bob"));

        const auto commas = CollectionEditorDialog::parseImportText(QStringLiteral("u1,Alice"), keyValueSchema());
        QCOMPARE(commas.size(), 1);
        QCOMPARE(commas.at(0).values.value(QStringLiteral("value")), QStringLiteral("Alice"));
    }

    void clipboardJsonArrayAndSingleObject()
    {
        const auto array = CollectionEditorDialog::parseImportText(
            QStringLiteral(R"([{"key":"a","value":"A","favorite":true},{"key":"b","value":"B"},{}])"), keyValueSchema());
        QCOMPARE(array.size(), 2); // o objeto vazio é omitido
        QVERIFY(array.at(0).favorite);
        QVERIFY(!array.at(1).favorite);

        const auto single = CollectionEditorDialog::parseImportText(
            QStringLiteral(R"({"key":"z","value":"Z"})"), keyValueSchema());
        QCOMPARE(single.size(), 1);
        QCOMPARE(single.at(0).values.value(QStringLiteral("value")), QStringLiteral("Z"));
    }

    void clipboardWithoutRecordsGivesNothing()
    {
        QVERIFY(CollectionEditorDialog::parseImportText(QString(), keyValueSchema()).isEmpty());
        QVERIFY(CollectionEditorDialog::parseImportText(QStringLiteral("  \n\t\n "), keyValueSchema()).isEmpty());
        QVERIFY(CollectionEditorDialog::parseImportText(QStringLiteral("not json"), keyValueSchema(), true).isEmpty());
    }

    void dialogHasGeneralDataAndAdvancedTabs()
    {
        Collection col;
        col.id = QStringLiteral("c");
        col.name = QStringLiteral("Servidores");
        CollectionEntry e;
        e.id = QStringLiteral("e1");
        e.values = {{QStringLiteral("key"), QStringLiteral("k")}, {QStringLiteral("value"), QStringLiteral("v")}};
        col.entries = {e};
        CollectionEditorDialog dialog(col);
        auto *nav = dialog.findChild<QListWidget *>(QStringLiteral("settingsNav"));
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->count(), 3);
        QVERIFY(nav->item(1)->text().contains(QStringLiteral("(1)"))); // Dados mostra quantas entradas há
    }

    // A aba Avançado edita o JSON da coleção: sair dela devolve o texto ao modelo.
    void advancedTabJsonRoundTripsIntoTheCollection()
    {
        Collection col;
        col.id = QStringLiteral("c");
        col.name = QStringLiteral("Antes");
        CollectionEditorDialog dialog(col);
        auto *nav = dialog.findChild<QListWidget *>(QStringLiteral("settingsNav"));
        auto *editor = dialog.findChild<QPlainTextEdit *>();
        QVERIFY(nav && editor);
        nav->setCurrentRow(2); // Avançado
        QVERIFY(editor->toPlainText().contains(QStringLiteral("Antes")));
        QString text = editor->toPlainText();
        text.replace(QStringLiteral("Antes"), QStringLiteral("Depois"));
        editor->setPlainText(text);
        nav->setCurrentRow(1); // sair aplica o JSON
        QCOMPARE(dialog.buildCollection().name, QStringLiteral("Depois"));
        QCOMPARE(nav->currentRow(), 1);
    }

    // Campo "secret": a grade mostra mascarado, mas buildCollection() preserva o valor VERDADEIRO (não grava o
    // texto mascarado por cima, o que apagaria o segredo ao reabrir e fechar).
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
