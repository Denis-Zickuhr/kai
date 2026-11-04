#include <QTest>
#include <QComboBox>
#include <QListWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QTableWidget>
#include <QToolButton>
#include <QDialogButtonBox>

#include "ui/features/collections/collection-selector-dialog.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Cobre a tela dedicada de seleção/consumo de coleção: paginação, favorito
// como estrela toggle, e ordenação por histórico.
class TestCollectionSelector : public QObject {
    Q_OBJECT

private:
    static Collection makeCollection(int n)
    {
        Collection col;
        col.id = QStringLiteral("col1");
        col.name = QStringLiteral("Clientes");
        col.schema = {
            {QStringLiteral("key"), QStringLiteral("ID"), CollectionFieldType::Key},
            {QStringLiteral("value"), QStringLiteral("Nome"), CollectionFieldType::Value},
        };
        for (int i = 0; i < n; ++i) {
            CollectionEntry e;
            e.id = QStringLiteral("e%1").arg(i);
            e.values = {{QStringLiteral("key"), QString::number(i)},
                        {QStringLiteral("value"), QStringLiteral("Nome%1").arg(i)}};
            col.entries << e;
        }
        return col;
    }

private slots:
    void tablePaginatesAt25ByDefault()
    {
        CollectionSelectorDialog dialog(makeCollection(60));
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->columnCount(), 2); // 2 do schema (estrela embutida, sem coluna)
        QCOMPARE(table->rowCount(), 25);   // 1a página de 25
    }

    void favoriteIsAStarToggle()
    {
        // A estrela é um ícone embutido na 1ª célula (favorito em UserRole+1).
        CollectionSelectorDialog dialog(makeCollection(3));
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        const QTableWidgetItem *first = table->item(0, 0);
        QVERIFY(first != nullptr);
        QVERIFY(!first->icon().isNull());          // tem a estrela embutida
        QVERIFY(first->data(Qt::UserRole + 1).isValid()); // estado de favorito
    }

    // O filtro salvo da última vez é reaplicado ANTES da primeira paginação (senão a lista reabre inteira).
    void initialFilterIsPreappliedOnOpen()
    {
        CollectionFilterState saved;
        saved.search = QStringLiteral("Nome1");
        CollectionSelectorDialog dialog(makeCollection(3), {}, true, nullptr, saved);

        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        // Só "Nome1" bate na busca salva.
        QCOMPARE(table->rowCount(), 1);

        auto *search = dialog.findChild<QLineEdit *>();
        QVERIFY(search != nullptr);
        QCOMPARE(search->text(), QStringLiteral("Nome1"));
    }

    // filterState() é o que o chamador persiste: reflete o valor pré-carregado e os ajustes feitos na tela.
    void filterStateReflectsSearchAndFavoritesToggle()
    {
        CollectionSelectorDialog dialog(makeCollection(3));
        auto *search = dialog.findChild<QLineEdit *>();
        auto *favorites = dialog.findChild<QCheckBox *>();
        QVERIFY(search != nullptr);
        QVERIFY(favorites != nullptr);

        QTest::keyClicks(search, QStringLiteral("Nome2"));
        favorites->setChecked(true);

        const CollectionFilterState state = dialog.filterState();
        QCOMPARE(state.search, QStringLiteral("Nome2"));
        QVERIFY(state.favoritesOnly);
    }

    // A QTableWidget é reconstruída a cada troca de página, o que apagava a seleção: entradas escolhidas em páginas
    // diferentes chegam TODAS em selectedEntries() ao aceitar.
    void multiSelectPersistsAcrossPageChanges()
    {
        Collection col = makeCollection(30); // 2 páginas no tamanho padrão (25)
        CollectionSelectorDialog dialog(col);
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 25);

        table->selectRow(0);
        const QString firstPageEntryId = table->item(0, 0)->data(Qt::UserRole).toString();
        QVERIFY(!firstPageEntryId.isEmpty());

        auto *nextButton = dialog.findChild<QToolButton *>(QStringLiteral("collectionSelectorNextButton"));
        QVERIFY(nextButton != nullptr);
        emit nextButton->clicked();
        QCOMPARE(table->rowCount(), 5); // 30 - 25 restantes na página 2

        table->selectRow(0);
        const QString secondPageEntryId = table->item(0, 0)->data(Qt::UserRole).toString();
        QVERIFY(!secondPageEntryId.isEmpty());
        QVERIFY(firstPageEntryId != secondPageEntryId);

        auto *buttonBox = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttonBox != nullptr);
        emit buttonBox->accepted();

        const QVector<CollectionEntry> selected = dialog.selectedEntries();
        QCOMPARE(selected.size(), 2);
        QStringList ids;
        for (const CollectionEntry &e : selected) ids << e.id;
        QVERIFY(ids.contains(firstPageEntryId));
        QVERIFY(ids.contains(secondPageEntryId));
    }

    // Label de contagem + botões "selecionar tudo (filtrados)" e "limpar seleção".
    void selectAllFilteredAndClearSelectionButtonsWork()
    {
        Collection col = makeCollection(30);
        CollectionSelectorDialog dialog(col);
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);

        auto *selectAllButton = dialog.findChild<QToolButton *>(QStringLiteral("collectionSelectorSelectAllButton"));
        auto *clearButton = dialog.findChild<QToolButton *>(QStringLiteral("collectionSelectorClearSelectionButton"));
        QVERIFY(selectAllButton != nullptr);
        QVERIFY(clearButton != nullptr);

        // "Selecionar tudo (filtrados)" pega as 30 entradas, não só as 25
        // visíveis na página atual.
        emit selectAllButton->clicked();
        auto *buttonBox = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttonBox != nullptr);
        emit buttonBox->accepted();
        QCOMPARE(dialog.selectedEntries().size(), 30);

        emit clearButton->clicked();
        emit buttonBox->accepted();
        QCOMPARE(dialog.selectedEntries().size(), 0);
    }

    void historyOrdersEntriesFirst()
    {
        Collection col = makeCollection(5);
        const QStringList history = {QStringLiteral("e3")};
        CollectionSelectorDialog dialog(col, history);
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        const QTableWidgetItem *first = table->item(0, 0);
        QVERIFY(first != nullptr);
        QCOMPARE(first->data(Qt::UserRole).toString(), QStringLiteral("e3"));
    }

    // Regressão: duplo-clique numa linha deve coletar AQUELA entrada em
    // selectedEntries() (o bug era accept() sem coletar a seleção).
    void doubleClickSelectsClickedEntry()
    {
        Collection col = makeCollection(4);
        auto *dialog = new CollectionSelectorDialog(col);
        auto *table = dialog->findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        QObject::connect(dialog, &QDialog::accepted, dialog, [dialog]() { /* no-op */ });
        QTableWidgetItem *cell = table->item(1, 0);
        QVERIFY(cell != nullptr);
        emit table->itemDoubleClicked(cell);
        const QVector<CollectionEntry> sel = dialog->selectedEntries();
        QCOMPARE(sel.size(), 1);
        QCOMPARE(sel.at(0).id, cell->data(Qt::UserRole).toString());
        delete dialog;
    }

    // Os operadores do filtro por campo eram RÓTULO do combo E CHAVE de comparação (ff.op == "sim"/"contém"):
    // traduzir os rótulos quebraria o filtro. A chave (userData) tem de existir no combo e ser estável, não o texto exibido.
    void filterOperatorsUseStableKeysNotLabels()
    {
        Collection col;
        col.id = QStringLiteral("c_ops");
        col.name = QStringLiteral("Ops");
        CollectionField texto;
        texto.name = QStringLiteral("nome");
        texto.label = QStringLiteral("Nome");
        texto.type = CollectionFieldType::Text;
        CollectionField flag;
        flag.name = QStringLiteral("ativo");
        flag.label = QStringLiteral("Ativo");
        flag.type = CollectionFieldType::Bool;
        col.schema = {texto, flag};

        CollectionEntry e1;
        e1.values.insert(QStringLiteral("nome"), QStringLiteral("acme-corp"));
        e1.values.insert(QStringLiteral("ativo"), QStringLiteral("true"));
        CollectionEntry e2;
        e2.values.insert(QStringLiteral("nome"), QStringLiteral("outro"));
        e2.values.insert(QStringLiteral("ativo"), QStringLiteral("false"));
        col.entries = {e1, e2};

        CollectionSelectorDialog dialog(col, QStringList{}, /*multiSelect=*/false, nullptr);

        // Filtros via DROPDOWN: ativa cada campo de "Adicionar filtro" para criar os cartões e lê as chaves dos combos de operador.
        QComboBox *addCombo = nullptr;
        for (QComboBox *cb : dialog.findChildren<QComboBox *>()) {
            // O combo de adicionar tem itens cujo userData é um nome de campo
            // do schema (nome/ativo). Os combos de página (25/50/100) não têm.
            for (int i = 0; i < cb->count(); ++i) {
                const QString d = cb->itemData(i).toString();
                if (d == QStringLiteral("nome") || d == QStringLiteral("ativo")) {
                    addCombo = cb; break;
                }
            }
            if (addCombo) break;
        }
        QVERIFY2(addCombo != nullptr, "dropdown de adicionar filtro não encontrado");
        for (int i = 0; i < addCombo->count(); ++i) {
            if (!addCombo->itemData(i).toString().isEmpty()) {
                emit addCombo->activated(i);
            }
        }

        const QList<QComboBox *> combos = dialog.findChildren<QComboBox *>();
        QVERIFY2(!combos.isEmpty(), "nenhum combo de operador encontrado");

        QStringList keysFound;
        for (QComboBox *combo : combos) {
            if (combo == addCombo) continue; // o dropdown de adicionar não é operador
            for (int i = 0; i < combo->count(); ++i) {
                const QString key = combo->itemData(i).toString();
                if (!key.isEmpty()) {
                    keysFound << key;
                    // A chave deve ser do conjunto ASCII CANÔNICO (não comparo chave != rótulo: em INGLÊS coincidem, ex: "contains").
                    static const QStringList canonical = {
                        QStringLiteral("none"), QStringLiteral("eq"), QStringLiteral("gt"),
                        QStringLiteral("lt"), QStringLiteral("yes"), QStringLiteral("no"),
                        QStringLiteral("contains"), QStringLiteral("equals"),
                    };
                    QVERIFY2(canonical.contains(key),
                             qPrintable(QStringLiteral("chave fora do conjunto canônico: %1").arg(key)));
                }
            }
        }
        // As chaves de texto e de booleano precisam existir.
        QVERIFY2(keysFound.contains(QStringLiteral("contains")),
                 qPrintable(QStringLiteral("chaves: %1").arg(keysFound.join(","))));
        QVERIFY(keysFound.contains(QStringLiteral("equals")));
        QVERIFY(keysFound.contains(QStringLiteral("yes")));
        QVERIFY(keysFound.contains(QStringLiteral("no")));
    }
};

QTEST_MAIN(TestCollectionSelector)
#include "test_collection_selector.moc"
