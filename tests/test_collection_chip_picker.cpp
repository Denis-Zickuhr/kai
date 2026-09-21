#include <QTest>
#include <QSignalSpy>
#include <QLineEdit>
#include <QCompleter>
#include <QToolButton>
#include <QAbstractItemModel>

#include "ui/shared/collection-chip-picker.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Cobre o campo de chips com busca embutida dos parâmetros ligados a
// coleção (feedback do usuário: "ao digitar no campo ele funciona tipo uma
// lista... vai criando a chip, e posso ir removendo ou adicionar multi sem
// nem abrir nada").
class TestCollectionChipPicker : public QObject {
    Q_OBJECT

private:
    static QVector<CollectionEntry> makeEntries()
    {
        CollectionEntry a;
        a.id = QStringLiteral("e1");
        a.values[QStringLiteral("name")] = QStringLiteral("Alice");
        CollectionEntry b;
        b.id = QStringLiteral("e2");
        b.values[QStringLiteral("name")] = QStringLiteral("Bob");
        return {a, b};
    }

private slots:
    // Digitar filtra as sugestões (fuzzy, mesmo critério do
    // CollectionSelectorDialog) e ativar uma delas cria a chip na hora —
    // sem abrir diálogo nenhum.
    void typingFiltersAndActivatingSuggestionAddsChip()
    {
        CollectionChipPickerWidget picker;
        picker.setEntries(makeEntries(), QStringLiteral("name"));
        QSignalSpy spy(&picker, &CollectionChipPickerWidget::selectionChanged);

        auto *search = picker.searchField();
        QVERIFY(search != nullptr);
        QTest::keyClicks(search, QStringLiteral("Ali"));

        auto *completer = picker.findChild<QCompleter *>();
        QVERIFY(completer != nullptr);
        QVERIFY(completer->model() != nullptr);
        QCOMPARE(completer->model()->rowCount(), 1); // só "Alice" bate em "Ali"
        const QModelIndex idx = completer->model()->index(0, 0);
        QCOMPARE(idx.data(Qt::UserRole).toString(), QStringLiteral("e1"));

        emit completer->activated(idx);

        QCOMPARE(picker.selectedIds(), QStringList{QStringLiteral("e1")});
        QCOMPARE(spy.count(), 1);
        // Campo limpo depois de escolher, pronto pra próxima busca — o
        // clear() é agendado pro próximo ciclo do loop de eventos (ver
        // comentário no construtor: QLineEdit::setCompleter() reescreve o
        // texto por baixo dos panos na MESMA ativação, depois de um
        // clear() síncrono), então o teste precisa deixar o loop rodar.
        QTest::qWait(10);
        QVERIFY(search->text().isEmpty());
    }

    // Uma entrada já escolhida não aparece de novo nas sugestões (evita
    // duplicar a mesma chip).
    void alreadySelectedEntryIsExcludedFromSuggestions()
    {
        CollectionChipPickerWidget picker;
        picker.setEntries(makeEntries(), QStringLiteral("name"));
        picker.setSelectedIds({QStringLiteral("e1")});

        auto *search = picker.searchField();
        QTest::keyClicks(search, QStringLiteral("Ali"));

        auto *completer = picker.findChild<QCompleter *>();
        QVERIFY(completer != nullptr);
        QCOMPARE(completer->model()->rowCount(), 0);
    }

    // Bug de UX que este widget resolve: remover uma chip (botão "x")
    // atualiza selectedIds() e avisa o formulário via selectionChanged().
    void removingAChipUpdatesSelectionAndEmitsSignal()
    {
        CollectionChipPickerWidget picker;
        picker.setEntries(makeEntries(), QStringLiteral("name"));
        picker.setSelectedIds({QStringLiteral("e1")});
        QCOMPARE(picker.selectedIds(), QStringList{QStringLiteral("e1")});

        QToolButton *removeBtn = nullptr;
        for (QToolButton *btn : picker.findChildren<QToolButton *>()) {
            if (btn != picker.pickButton()) { removeBtn = btn; break; }
        }
        QVERIFY2(removeBtn != nullptr, "botão de remover a chip não encontrado");

        QSignalSpy spy(&picker, &CollectionChipPickerWidget::selectionChanged);
        emit removeBtn->clicked();

        QVERIFY(picker.selectedIds().isEmpty());
        QCOMPARE(spy.count(), 1);
    }

    // setSelectedIds() é a pré-seleção PROGRAMÁTICA (último valor usado, ou
    // volta da tela de seleção dedicada) — o chamador já sabe que mudou,
    // então NÃO deve reemitir selectionChanged() (evitaria um loop/gravação
    // redundante em ParameterFormDialog).
    void setSelectedIdsDoesNotEmitSelectionChanged()
    {
        CollectionChipPickerWidget picker;
        picker.setEntries(makeEntries(), QStringLiteral("name"));
        QSignalSpy spy(&picker, &CollectionChipPickerWidget::selectionChanged);
        picker.setSelectedIds({QStringLiteral("e1")});
        QCOMPARE(spy.count(), 0);
        QCOMPARE(picker.selectedIds(), QStringList{QStringLiteral("e1")});
    }

    // Feature pedida pelo usuário: "o backspace precisa de ação pra apagar
    // as linhas" — Backspace no campo de busca VAZIO remove a ÚLTIMA chip
    // (padrão de tag-input tipo Gmail/Slack). Com o campo NÃO vazio,
    // Backspace deve continuar apagando texto normalmente (não mexer na
    // seleção).
    void backspaceOnEmptyFieldRemovesLastChip()
    {
        CollectionChipPickerWidget picker;
        picker.setEntries(makeEntries(), QStringLiteral("name"));
        picker.setSelectedIds({QStringLiteral("e1"), QStringLiteral("e2")});

        auto *search = picker.searchField();
        QVERIFY(search != nullptr);
        QVERIFY(search->text().isEmpty());

        QSignalSpy spy(&picker, &CollectionChipPickerWidget::selectionChanged);
        QTest::keyClick(search, Qt::Key_Backspace);

        QCOMPARE(picker.selectedIds(), QStringList{QStringLiteral("e1")}); // só a ÚLTIMA saiu
        QCOMPARE(spy.count(), 1);
    }

    void backspaceWithTextInFieldDoesNotTouchSelection()
    {
        CollectionChipPickerWidget picker;
        picker.setEntries(makeEntries(), QStringLiteral("name"));
        picker.setSelectedIds({QStringLiteral("e1")});

        auto *search = picker.searchField();
        QTest::keyClicks(search, QStringLiteral("x"));
        QSignalSpy spy(&picker, &CollectionChipPickerWidget::selectionChanged);
        QTest::keyClick(search, Qt::Key_Backspace);

        QCOMPARE(picker.selectedIds(), QStringList{QStringLiteral("e1")}); // seleção intacta
        QCOMPARE(spy.count(), 0);
        QVERIFY(search->text().isEmpty()); // só apagou o "x" digitado
    }
};

QTEST_MAIN(TestCollectionChipPicker)
#include "test_collection_chip_picker.moc"
