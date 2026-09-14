#include <QTest>
#include <QSignalSpy>
#include <QLineEdit>
#include <QCompleter>
#include <QAbstractButton>
#include <QToolButton>
#include <QAbstractItemModel>

#include "ui/shared/collection-chip-picker.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Campo de chips com busca embutida dos parâmetros ligados a coleção: digitar filtra, ativar cria a chip,
// remover/adicionar multi sem abrir diálogo.
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
    // O "x" da chip fica no centro vertical dela (e a pílula tem altura uniforme): é pintado pelo botão, sem a folha de
    // estilo global de QToolButton que o deixava torto. Com KAI_TEST_SCREENSHOT_DIR grava um PNG para olhar o visual.
    void theCloseButtonIsCenteredInTheChipAndTheFieldCanBeSavedAsAnImage()
    {
        CollectionChipPickerWidget picker;
        picker.setEntries(makeEntries(), QStringLiteral("name"));
        picker.setSelectedIds({QStringLiteral("e1"), QStringLiteral("e2")});
        picker.resize(420, 140);
        picker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&picker));

        const QList<QWidget *> chips = picker.findChildren<QWidget *>(QStringLiteral("collectionChip"));
        QCOMPARE(chips.size(), 2);
        for (QWidget *chip : chips) {
            QAbstractButton *closeBtn = nullptr;
            for (QAbstractButton *btn : chip->findChildren<QAbstractButton *>()) {
                closeBtn = btn;
            }
            QVERIFY(closeBtn);
            QVERIFY2(qAbs(closeBtn->geometry().center().y() - chip->rect().center().y()) <= 1,
                     qPrintable(QStringLiteral("x em %1, chip centro %2").arg(closeBtn->geometry().center().y())
                                    .arg(chip->rect().center().y())));
            QVERIFY(closeBtn->width() == closeBtn->height()); // área quadrada: o círculo de hover não amassa
        }
        QCOMPARE(chips.at(0)->height(), chips.at(1)->height());

        const QByteArray dir = qgetenv("KAI_TEST_SCREENSHOT_DIR");
        if (!dir.isEmpty()) {
            picker.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/collection-chips.png"));
        }
    }

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
        // Campo limpo depois de escolher: o clear() é agendado pro próximo ciclo do loop (QLineEdit::setCompleter()
        // reescreve o texto na MESMA ativação, depois de um clear() síncrono), então o teste deixa o loop rodar.
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

    // Remover uma chip (botão "x") atualiza selectedIds() e avisa via selectionChanged().
    void removingAChipUpdatesSelectionAndEmitsSignal()
    {
        CollectionChipPickerWidget picker;
        picker.setEntries(makeEntries(), QStringLiteral("name"));
        picker.setSelectedIds({QStringLiteral("e1")});
        QCOMPARE(picker.selectedIds(), QStringList{QStringLiteral("e1")});

        QAbstractButton *removeBtn = nullptr;
        for (QAbstractButton *btn : picker.findChildren<QAbstractButton *>()) {
            if (btn != picker.pickButton()) { removeBtn = btn; break; }
        }
        QVERIFY2(removeBtn != nullptr, "botão de remover a chip não encontrado");

        QSignalSpy spy(&picker, &CollectionChipPickerWidget::selectionChanged);
        emit removeBtn->clicked();

        QVERIFY(picker.selectedIds().isEmpty());
        QCOMPARE(spy.count(), 1);
    }

    // setSelectedIds() é pré-seleção PROGRAMÁTICA: NÃO reemite selectionChanged() (evita gravação redundante em
    // ParameterFormDialog).
    void setSelectedIdsDoesNotEmitSelectionChanged()
    {
        CollectionChipPickerWidget picker;
        picker.setEntries(makeEntries(), QStringLiteral("name"));
        QSignalSpy spy(&picker, &CollectionChipPickerWidget::selectionChanged);
        picker.setSelectedIds({QStringLiteral("e1")});
        QCOMPARE(spy.count(), 0);
        QCOMPARE(picker.selectedIds(), QStringList{QStringLiteral("e1")});
    }

    // Backspace no campo VAZIO remove a ÚLTIMA chip (tag-input); com texto, apaga texto e não mexe na seleção.
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
