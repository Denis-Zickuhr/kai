#include <QTest>
#include <QComboBox>
#include <QListWidget>
#include <QTableWidget>
#include <QToolButton>

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
        // A estrela agora é um ÍCONE EMBUTIDO na 1a célula (não mais coluna
        // própria): o item tem um ícone e o estado de favorito em UserRole+1.
        CollectionSelectorDialog dialog(makeCollection(3));
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        const QTableWidgetItem *first = table->item(0, 0);
        QVERIFY(first != nullptr);
        QVERIFY(!first->icon().isNull());          // tem a estrela embutida
        QVERIFY(first->data(Qt::UserRole + 1).isValid()); // estado de favorito
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
        // Fecha o diálogo assim que aceitar (o duplo-clique chama accept()).
        QObject::connect(dialog, &QDialog::accepted, dialog, [dialog]() { /* no-op */ });
        // Dispara o duplo-clique na 2a linha (índice 1), coluna do 1o campo (0).
        QTableWidgetItem *cell = table->item(1, 0);
        QVERIFY(cell != nullptr);
        emit table->itemDoubleClicked(cell);
        const QVector<CollectionEntry> sel = dialog->selectedEntries();
        QCOMPARE(sel.size(), 1);
        QCOMPARE(sel.at(0).id, cell->data(Qt::UserRole).toString());
        delete dialog;
    }

    // REGRESSÃO PREVENIDA na tradução dos diálogos de coleção: os operadores do
    // filtro por campo do schema eram usados como RÓTULO do combo E como CHAVE
    // de comparação (ff.op == "sim"/"contém"). Traduzir os rótulos quebraria o
    // filtro silenciosamente em qualquer idioma. Agora o rótulo é traduzido e a
    // chave (userData) é estável — este teste trava isso: as chaves precisam
    // existir no combo e NÃO podem ser o texto exibido.
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

        // Filtros dinâmicos via DROPDOWN: encontra o combo "Adicionar filtro"
        // (itens com UserRole = nome do campo) e ativa cada campo para criar os
        // cartões, cujos combos de operador expõem as chaves estáveis.
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

        // Os combos de operador dos cartões devem expor CHAVES estáveis.
        const QList<QComboBox *> combos = dialog.findChildren<QComboBox *>();
        QVERIFY2(!combos.isEmpty(), "nenhum combo de operador encontrado");

        QStringList keysFound;
        for (QComboBox *combo : combos) {
            if (combo == addCombo) continue; // o dropdown de adicionar não é operador
            for (int i = 0; i < combo->count(); ++i) {
                const QString key = combo->itemData(i).toString();
                if (!key.isEmpty()) {
                    keysFound << key;
                    // A chave deve ser do conjunto ASCII CANÔNICO. É esse o
                    // invariante que protege a lógica: se alguém voltar a usar o
                    // texto exibido como chave, num idioma traduzido a chave
                    // deixaria de pertencer a este conjunto e o teste quebra.
                    // (Não comparo chave != rótulo porque em INGLÊS eles
                    // coincidem legitimamente, ex: "contains".)
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
