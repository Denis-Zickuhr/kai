#include <QTest>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QSignalSpy>
#include <QTableWidget>
#include <QToolButton>

#include "ui/shared/inline-code-field.h"
#include "ui/shared/parameter-field-factory.h"

using namespace kai::core;
using namespace kai::ui;

namespace {

KipField field(KipFieldType type, const QString &name = QStringLiteral("f"))
{
    KipField f;
    f.type = type;
    f.name = name;
    return f;
}

KipOption opt(const QString &value, const QString &label = QString(), const QString &description = QString())
{
    return {value, label.isEmpty() ? value : label, description};
}

QVector<KipOption> manyOptions(int n)
{
    QVector<KipOption> out;
    for (int i = 0; i < n; ++i) out.append(opt(QStringLiteral("v%1").arg(i), QStringLiteral("Option %1").arg(i)));
    return out;
}

QJsonArray manyRows(int n)
{
    QJsonArray rows;
    for (int i = 0; i < n; ++i) {
        rows.append(QJsonObject{{"id", QStringLiteral("r%1").arg(i)}, {"name", QStringLiteral("Row %1").arg(i)},
                                {"size", i * 10}});
    }
    return rows;
}

QVector<KipColumn> columns()
{
    return {{QStringLiteral("name"), QStringLiteral("Name")}, {QStringLiteral("size"), QStringLiteral("Size")}};
}

// Constrói o editor dentro de um pai vivo (o teste segura o pai).
struct Built {
    QWidget host;
    QPointer<KipFieldEditor> editor;

    Built(const KipField &f, const QJsonValue &initial = QJsonValue(QJsonValue::Undefined))
    {
        editor = new KipFieldEditor(f, initial, &host);
        host.show();
    }
};

} // namespace

class TestParameterFieldFactory : public QObject {
    Q_OBJECT

private slots:
    // ---- construtores extraídos do formulário de parâmetros ----
    void parseParamOptionSplitsLabelAndValue()
    {
        QCOMPARE(fields::parseParamOption(QStringLiteral("Prod:production")).label, QStringLiteral("Prod"));
        QCOMPARE(fields::parseParamOption(QStringLiteral("Prod:production")).value, QStringLiteral("production"));
        QCOMPARE(fields::parseParamOption(QStringLiteral("plain")).label, QStringLiteral("plain"));
        QCOMPARE(fields::parseParamOption(QStringLiteral("plain")).value, QStringLiteral("plain"));
        // ":x" não é um separador (rótulo vazio): vira valor literal.
        QCOMPARE(fields::parseParamOption(QStringLiteral(":x")).value, QStringLiteral(":x"));
    }

    void wrapWithLabelMarksRequiredFieldsWithAnAsterisk()
    {
        QWidget host;
        auto *edit = new QLineEdit(&host);
        QWidget *wrapped = fields::wrapWithLabel(&host, QStringLiteral("Name"), edit, true);
        QCOMPARE(edit->parentWidget(), wrapped);
        const auto labels = wrapped->findChildren<QLabel *>();
        QVERIFY(!labels.isEmpty());
        QVERIFY(labels.first()->text().contains(QStringLiteral("*")));

        auto *plain = new QLineEdit(&host);
        QWidget *wrappedPlain = fields::wrapWithLabel(&host, QStringLiteral("Name"), plain, false);
        QVERIFY(!wrappedPlain->findChildren<QLabel *>().first()->text().contains(QStringLiteral("*")));
    }

    void selectComboPutsMostRecentlyUsedFirstAndSelectsTheInitialValue()
    {
        QWidget host;
        const QVector<fields::ChoiceOption> options = {{"A", "a", {}}, {"B", "b", {}}, {"C", "c", {}}};
        QComboBox *combo = fields::makeSelectCombo(&host, options, {QStringLiteral("c"), QStringLiteral("b")},
                                                   QStringLiteral("b"));
        QCOMPARE(combo->itemData(0).toString(), QStringLiteral("c"));
        QCOMPARE(combo->itemData(1).toString(), QStringLiteral("b"));
        QCOMPARE(combo->itemData(2).toString(), QStringLiteral("a"));
        QCOMPARE(combo->currentData().toString(), QStringLiteral("b"));
        QVERIFY(combo->isEditable());
    }

    void checkListSpaceKeyTogglesTheFocusedItem()
    {
        QWidget host;
        const fields::ChoiceListField built = fields::makeCheckList(
            &host, {{"One", "1", {}}, {"Two", "2", {}}}, {QStringLiteral("2")}, true);
        host.show();
        QVERIFY(built.filter);
        QCOMPARE(built.list->item(0)->checkState(), Qt::Unchecked);
        QCOMPARE(built.list->item(1)->checkState(), Qt::Checked);
        built.list->setCurrentRow(0);
        QTest::keyClick(built.list, Qt::Key_Space);
        QCOMPARE(built.list->item(0)->checkState(), Qt::Checked);
        QTest::keyClick(built.list, Qt::Key_Space);
        QCOMPARE(built.list->item(0)->checkState(), Qt::Unchecked);
    }

    void checkListFilterHidesNonMatchingItemsWithoutLosingTheirState()
    {
        QWidget host;
        const fields::ChoiceListField built = fields::makeCheckList(
            &host, {{"Alpha", "a", {}}, {"Beta", "b", {}}}, {QStringLiteral("a")}, true);
        built.filter->setText(QStringLiteral("bet"));
        QVERIFY(built.list->item(0)->isHidden());
        QVERIFY(!built.list->item(1)->isHidden());
        QCOMPARE(built.list->item(0)->checkState(), Qt::Checked); // oculto, mas continua marcado
        built.filter->clear();
        QVERIFY(!built.list->item(0)->isHidden());
    }

    void checkListWithoutFilterHasNoFilterField()
    {
        QWidget host;
        const fields::ChoiceListField built = fields::makeCheckList(&host, {{"One", "1", {}}}, {}, false);
        QVERIFY(!built.filter);
    }

    void datePickFieldWritesPropertiesAndText()
    {
        QWidget host;
        const fields::DatePickField picked = fields::makeDatePickField(
            &host, QString(), QStringLiteral("date"), true,
            [](const QDateTime &dt) { return dt.toString(QStringLiteral("dd/MM/yyyy")); });
        QVERIFY(picked.edit->isReadOnly());
        fields::setDatePickValues(picked.edit, QDateTime(QDate(2024, 1, 2), QTime(0, 0)),
                                  QDateTime(QDate(2024, 1, 5), QTime(0, 0)), true,
                                  [](const QDateTime &dt) { return dt.toString(QStringLiteral("dd/MM/yyyy")); });
        QCOMPARE(picked.edit->property(fields::kDatePropStartFormatted).toString(), QStringLiteral("02/01/2024"));
        QCOMPARE(picked.edit->property(fields::kDatePropEndFormatted).toString(), QStringLiteral("05/01/2024"));
        QVERIFY(picked.edit->text().contains(QStringLiteral("02/01/2024")));
        QVERIFY(picked.edit->text().contains(QStringLiteral("05/01/2024")));
    }

    void dataTableReadOnlyShowsCellsAsTextAndCopiesWithCtrlC()
    {
        QWidget host;
        QJsonArray rows = {QJsonObject{{"name", "alpha"}, {"size", 12}},
                           QJsonObject{{"name", "beta"}, {"size", 1.5}, {"extra", "ignored"}},
                           QJsonObject{{"name", true}}};
        QTableWidget *table = fields::makeDataTable(&host, columns(), rows, QStringLiteral("id"),
                                                    fields::DataTableMode::ReadOnly);
        host.show();
        QCOMPARE(table->rowCount(), 3);
        QCOMPARE(table->columnCount(), 2);
        QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Name"));
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("12"));
        QCOMPARE(table->item(1, 1)->text(), QStringLiteral("1.5"));
        QCOMPARE(table->item(2, 0)->text(), QStringLiteral("true"));
        QCOMPARE(table->item(2, 1)->text(), QString()); // chave ausente = vazio
        QVERIFY(!(table->item(0, 0)->flags() & Qt::ItemIsEditable));

        QApplication::clipboard()->setText(QStringLiteral("before"));
        table->setCurrentCell(1, 0);
        table->setFocus();
        QTest::keyClick(table, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("beta"));
    }

    // ---- KipFieldEditor: texto ----
    void textEditorRoundTripAndEditSignals()
    {
        Built b(field(KipFieldType::Text), QStringLiteral("hello"));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("hello")));
        QSignalSpy changed(b.editor, &KipFieldEditor::valueChanged);
        QSignalSpy edited(b.editor, &KipFieldEditor::userEdited);

        b.editor->setValue(QStringLiteral("programmatic"));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("programmatic")));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(edited.count(), 0); // setValue nunca conta como edição do usuário

        auto *line = qobject_cast<QLineEdit *>(b.editor->widget());
        QVERIFY(line);
        QTest::keyClicks(line, QStringLiteral("!"));
        QVERIFY(edited.count() >= 1);
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("programmatic!")));
    }

    void textEditorRequiredNeedsNonBlankText()
    {
        Built b(field(KipFieldType::Text));
        QVERIFY(!b.editor->isFilled());
        b.editor->setValue(QStringLiteral("   "));
        QVERIFY(!b.editor->isFilled());
        b.editor->setValue(QStringLiteral("x"));
        QVERIFY(b.editor->isFilled());
    }

    void secretEditorIsMaskedAndHasAShowHideToggle()
    {
        Built b(field(KipFieldType::Secret), QStringLiteral("hunter2"));
        auto *line = qobject_cast<QLineEdit *>(b.editor->widget());
        QVERIFY(line);
        QCOMPARE(line->echoMode(), QLineEdit::Password);
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("hunter2"))); // o valor real continua acessível
        QAction *toggle = nullptr;
        for (QAction *a : line->actions()) {
            if (a->isCheckable()) toggle = a;
        }
        QVERIFY(toggle);
        toggle->setChecked(true);
        QCOMPARE(line->echoMode(), QLineEdit::Normal);
        toggle->setChecked(false);
        QCOMPARE(line->echoMode(), QLineEdit::Password);
    }

    void textareaEditorReadsAndWritesMultilineText()
    {
        Built b(field(KipFieldType::Textarea), QStringLiteral("a\nb"));
        QVERIFY(qobject_cast<InlineCodeField *>(b.editor->widget()));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("a\nb")));
        b.editor->setValue(QStringLiteral("x\ny\nz"));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("x\ny\nz")));
    }

    // ---- número ----
    void numberEditorIsNullUntilFilledAndKeepsDecimals()
    {
        KipField f = field(KipFieldType::Number);
        f.min = 0;
        f.max = 10;
        f.decimals = 2;
        f.step = 0.25;
        Built b(f);
        QVERIFY(b.editor->value().isNull());
        QVERIFY(!b.editor->isFilled());
        b.editor->setValue(3.5);
        QCOMPARE(b.editor->value().toDouble(), 3.5);
        QVERIFY(b.editor->isFilled());
        b.editor->setValue(QJsonValue(QJsonValue::Null));
        QVERIFY(b.editor->value().isNull());
    }

    void numberEditorRespectsLimitsAndInitialValue()
    {
        KipField f = field(KipFieldType::Number);
        f.min = 1;
        f.max = 5;
        Built b(f, 3);
        QCOMPARE(b.editor->value().toDouble(), 3.0);
        b.editor->setValue(99);
        QCOMPARE(b.editor->value().toDouble(), 5.0); // preso no máximo
        b.editor->setValue(0);
        QVERIFY(b.editor->value().isNull()); // abaixo do mínimo = campo limpo
    }

    void integerNumberIsSerializedWithoutAFraction()
    {
        Built b(field(KipFieldType::Number), 7);
        const QByteArray json = QJsonDocument(QJsonObject{{"n", b.editor->value()}}).toJson(QJsonDocument::Compact);
        QCOMPARE(json, QByteArray("{\"n\":7}"));
    }

    // ---- select ----
    void selectEditorHasAnEmptyEntryAndResolvesTypedText()
    {
        KipField f = field(KipFieldType::Select);
        f.options = {opt(QStringLiteral("dev")), opt(QStringLiteral("prod"), QStringLiteral("Production"))};
        Built b(f);
        auto *combo = qobject_cast<QComboBox *>(b.editor->widget());
        QVERIFY(combo);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(b.editor->value(), QJsonValue(QString()));
        QVERIFY(!b.editor->isFilled());

        b.editor->setValue(QStringLiteral("prod"));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("prod")));
        QCOMPARE(combo->currentText(), QStringLiteral("Production"));
        QVERIFY(b.editor->isFilled());

        // Texto digitado que não é uma opção não vira valor.
        combo->setEditText(QStringLiteral("something typed"));
        QCOMPARE(b.editor->value(), QJsonValue(QString()));
        // Digitar o rótulo exato escolhe a opção.
        combo->setEditText(QStringLiteral("Production"));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("prod")));
    }

    void selectEditorPreselectsAValidDefault()
    {
        KipField f = field(KipFieldType::Select);
        f.options = {opt(QStringLiteral("dev")), opt(QStringLiteral("prod"))};
        Built b(f, QStringLiteral("prod"));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("prod")));
    }

    void selectEditorUserPickEmitsUserEdited()
    {
        KipField f = field(KipFieldType::Select);
        f.options = {opt(QStringLiteral("a")), opt(QStringLiteral("b"))};
        Built b(f);
        QSignalSpy edited(b.editor, &KipFieldEditor::userEdited);
        auto *combo = qobject_cast<QComboBox *>(b.editor->widget());
        combo->setCurrentIndex(2); // o que um clique do usuário faz
        QVERIFY(edited.count() >= 1);
        b.editor->setValue(QStringLiteral("a"));
        const int after = edited.count();
        b.editor->setValue(QStringLiteral("b"));
        QCOMPARE(edited.count(), after);
    }

    // ---- list ----
    void singleChoiceListSelectsOneValueAndShowsFilterOnlyAboveEight()
    {
        KipField f = field(KipFieldType::List);
        f.options = manyOptions(8);
        {
            Built b(f);
            QVERIFY(!b.editor->widget()->findChild<QLineEdit *>()); // 8 opções: sem filtro
        }
        f.options = manyOptions(9);
        Built b(f);
        QVERIFY(b.editor->widget()->findChild<QLineEdit *>()); // 9: com filtro

        auto *list = b.editor->widget()->findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(b.editor->value(), QJsonValue(QString()));
        QVERIFY(!b.editor->isFilled());
        b.editor->setValue(QStringLiteral("v3"));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("v3")));
        QVERIFY(b.editor->isFilled());
        list->setCurrentRow(5);
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("v5")));
    }

    void singleChoiceListFilterNarrowsTheVisibleItems()
    {
        KipField f = field(KipFieldType::List);
        f.options = manyOptions(12);
        Built b(f);
        auto *filter = b.editor->widget()->findChild<QLineEdit *>();
        auto *list = b.editor->widget()->findChild<QListWidget *>();
        filter->setText(QStringLiteral("option 1"));
        int visible = 0;
        for (int i = 0; i < list->count(); ++i) {
            if (!list->item(i)->isHidden()) ++visible;
        }
        QCOMPARE(visible, 3); // "Option 1", "Option 10" e "Option 11"
    }

    void multipleListReturnsAnArrayOfCheckedValues()
    {
        KipField f = field(KipFieldType::List);
        f.multiple = true;
        f.options = {opt(QStringLiteral("a")), opt(QStringLiteral("b")), opt(QStringLiteral("c"))};
        Built b(f, QJsonArray{QStringLiteral("a"), QStringLiteral("c")});
        QCOMPARE(b.editor->value().toArray(), (QJsonArray{QStringLiteral("a"), QStringLiteral("c")}));
        b.editor->setValue(QJsonArray{QStringLiteral("b")});
        QCOMPARE(b.editor->value().toArray(), QJsonArray{QStringLiteral("b")});
        b.editor->setValue(QJsonArray());
        QVERIFY(!b.editor->isFilled());
    }

    void listOptionDescriptionIsStoredForTheDelegate()
    {
        KipField f = field(KipFieldType::List);
        f.options = {opt(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("the first"))};
        Built b(f);
        auto *list = b.editor->widget()->findChild<QListWidget *>();
        QCOMPARE(list->item(0)->text(), QStringLiteral("Alpha"));
        QCOMPARE(list->item(0)->data(Qt::UserRole + 1).toString(), QStringLiteral("the first"));
    }

    // ---- table ----
    void tableEditorSelectsRowKeysNotLabels()
    {
        KipField f = field(KipFieldType::Table);
        f.columns = columns();
        f.rows = manyRows(5);
        Built b(f);
        auto *table = b.editor->widget()->findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 5);
        QVERIFY(!b.editor->isFilled());
        QCOMPARE(b.editor->value(), QJsonValue(QString()));
        table->selectRow(2);
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("r2")));
        QVERIFY(b.editor->isFilled());
        b.editor->setValue(QStringLiteral("r4"));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("r4")));
        QVERIFY(!b.editor->widget()->findChild<QLineEdit *>()); // 5 linhas: sem filtro
    }

    void tableEditorMultipleReturnsAnArray()
    {
        KipField f = field(KipFieldType::Table);
        f.columns = columns();
        f.rows = manyRows(5);
        f.multiple = true;
        Built b(f, QJsonArray{QStringLiteral("r1"), QStringLiteral("r3")});
        QCOMPARE(b.editor->value().toArray(), (QJsonArray{QStringLiteral("r1"), QStringLiteral("r3")}));
        auto *table = b.editor->widget()->findChild<QTableWidget *>();
        table->selectionModel()->select(table->model()->index(0, 0),
                                        QItemSelectionModel::Select | QItemSelectionModel::Rows); // acrescenta
        QCOMPARE(b.editor->value().toArray().size(), 3);
    }

    void tableEditorUsesACustomRowKey()
    {
        KipField f = field(KipFieldType::Table);
        f.columns = {{QStringLiteral("name"), QStringLiteral("Name")}};
        f.rowKey = QStringLiteral("name");
        f.rows = QJsonArray{QJsonObject{{"name", "alpha"}}, QJsonObject{{"name", "beta"}}};
        Built b(f);
        b.editor->setValue(QStringLiteral("beta"));
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("beta")));
    }

    void tableEditorFilterAppearsAboveEightRowsAndHidesRows()
    {
        KipField f = field(KipFieldType::Table);
        f.columns = columns();
        f.rows = manyRows(12);
        Built b(f);
        auto *filter = b.editor->widget()->findChild<QLineEdit *>();
        auto *table = b.editor->widget()->findChild<QTableWidget *>();
        QVERIFY(filter);
        filter->setText(QStringLiteral("row 7"));
        int visible = 0;
        for (int r = 0; r < table->rowCount(); ++r) {
            if (!table->isRowHidden(r)) ++visible;
        }
        QCOMPARE(visible, 1);
        filter->clear();
        QVERIFY(!table->isRowHidden(0));
    }

    // ---- flags ----
    void flagsEditorAlwaysReportsEveryFlag()
    {
        KipField f = field(KipFieldType::Flags);
        f.flags = {{QStringLiteral("force"), QStringLiteral("Force"), QString(), true},
                   {QStringLiteral("dry"), QStringLiteral("Dry run"), QStringLiteral("no side effects"), false}};
        Built b(f, QJsonObject{{"force", true}, {"dry", false}});
        QCOMPARE(b.editor->value().toObject().size(), 2);
        QCOMPARE(b.editor->value().toObject().value("force").toBool(), true);
        QCOMPARE(b.editor->value().toObject().value("dry").toBool(), false);
        b.editor->setValue(QJsonObject{{"dry", true}});
        QCOMPARE(b.editor->value().toObject().value("force").toBool(), false);
        QCOMPARE(b.editor->value().toObject().value("dry").toBool(), true);
        QVERIFY(b.editor->isFilled());
        b.editor->setValue(QJsonObject());
        QVERIFY(!b.editor->isFilled()); // required em flags = ao menos uma marcada
    }

    void flagsEditorShowsDescriptionsAsMutedLabels()
    {
        KipField f = field(KipFieldType::Flags);
        f.flags = {{QStringLiteral("dry"), QStringLiteral("Dry run"), QStringLiteral("no side effects"), false}};
        Built b(f);
        bool found = false;
        for (QLabel *l : b.editor->widget()->findChildren<QLabel *>()) {
            if (l->text() == QStringLiteral("no side effects")) found = true;
        }
        QVERIFY(found);
    }

    // ---- date ----
    void dateEditorRoundTripsIsoStringsInEveryMode()
    {
        struct Case { QString mode; QString iso; };
        const QVector<Case> cases = {{QStringLiteral("date"), QStringLiteral("2024-03-09")},
                                     {QStringLiteral("time"), QStringLiteral("14:05:09")},
                                     {QStringLiteral("datetime"), QStringLiteral("2024-03-09T14:05:09")}};
        for (const Case &c : cases) {
            KipField f = field(KipFieldType::Date);
            f.dateMode = c.mode;
            Built b(f);
            QVERIFY(b.editor->value().isNull());
            QVERIFY(!b.editor->isFilled());
            b.editor->setValue(c.iso);
            QCOMPARE(b.editor->value(), QJsonValue(c.iso));
            QVERIFY(b.editor->isFilled());
            b.editor->setValue(QJsonValue(QJsonValue::Null));
            QVERIFY(b.editor->value().isNull());
        }
    }

    void dateEditorRangeUsesStartAndEnd()
    {
        KipField f = field(KipFieldType::Date);
        f.range = true;
        Built b(f, QJsonObject{{"start", "2024-01-01"}, {"end", "2024-01-31"}});
        QCOMPARE(b.editor->value().toObject().value("start").toString(), QStringLiteral("2024-01-01"));
        QCOMPARE(b.editor->value().toObject().value("end").toString(), QStringLiteral("2024-01-31"));
        // Uma string simples não é um range válido.
        b.editor->setValue(QStringLiteral("2024-01-01"));
        QVERIFY(b.editor->value().isNull());
    }

    void dateEditorIgnoresGarbage()
    {
        Built b(field(KipFieldType::Date), QStringLiteral("not a date"));
        QVERIFY(b.editor->value().isNull());
    }

    void dateEditorReportsUserPicksThroughTheTextChange()
    {
        Built b(field(KipFieldType::Date));
        QSignalSpy edited(b.editor, &KipFieldEditor::userEdited);
        auto *line = b.editor->widget()->findChild<QLineEdit *>();
        // O que a janelinha faz ao confirmar:
        fields::setDatePickValues(line, QDateTime(QDate(2024, 5, 6), QTime(0, 0)), QDateTime(), false,
                                  [](const QDateTime &dt) { return fields::isoDateText(QStringLiteral("date"), dt); });
        QCOMPARE(edited.count(), 1);
        QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("2024-05-06")));
    }

    // ---- arquivo / pasta ----
    void filepickAndFolderpickEditorsExposeTheTypedPath()
    {
        for (KipFieldType type : {KipFieldType::Filepick, KipFieldType::Folderpick}) {
            Built b(field(type), QStringLiteral("/tmp/x"));
            QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("/tmp/x")));
            auto *line = b.editor->widget()->findChild<QLineEdit *>();
            QVERIFY(line);
            line->setText(QStringLiteral("/tmp/y"));
            QCOMPARE(b.editor->value(), QJsonValue(QStringLiteral("/tmp/y")));
            QVERIFY(b.editor->isFilled());
        }
    }

    // ---- geral ----
    void readOnlyDisablesTheWholeEditor()
    {
        Built b(field(KipFieldType::Text));
        b.editor->setReadOnly(true);
        QVERIFY(!b.editor->widget()->isEnabled());
        b.editor->setReadOnly(false);
        QVERIFY(b.editor->widget()->isEnabled());
    }

    void editorIsDestroyedWithItsWidget()
    {
        QPointer<KipFieldEditor> editor;
        {
            QWidget host;
            editor = new KipFieldEditor(field(KipFieldType::Text), QJsonValue(), &host);
            QVERIFY(editor);
        }
        QVERIFY(!editor);
    }

    // ---- filtro e paginação (list / table) ----
    static int visibleItems(const QListWidget *list)
    {
        int n = 0;
        for (int i = 0; i < list->count(); ++i) n += list->item(i)->isHidden() ? 0 : 1;
        return n;
    }

    static QStringList visibleLabels(const QListWidget *list)
    {
        QStringList out;
        for (int i = 0; i < list->count(); ++i) {
            if (!list->item(i)->isHidden()) out << list->item(i)->text();
        }
        return out;
    }

    void listFilterBoxFollowsSearchableOrTheAutomaticThreshold()
    {
        KipField f = field(KipFieldType::List);
        f.options = manyOptions(3);
        QVERIFY(!Built(f).editor->filterEdit());   // poucos itens: sem filtro
        f.searchable = true;
        QVERIFY(Built(f).editor->filterEdit());    // forçado, mesmo com 3
        f.options = manyOptions(20);
        f.searchable.reset();
        QVERIFY(Built(f).editor->filterEdit());    // automático: acima de 8
        f.searchable = false;
        QVERIFY(!Built(f).editor->filterEdit());   // desligado, mesmo com 20
    }

    void pagedListShowsOnePageAndNavigates()
    {
        KipField f = field(KipFieldType::List);
        f.options = manyOptions(25);
        f.pageSize = 10;
        Built b(f);
        auto *list = b.editor->widget()->findChild<QListWidget *>();
        auto *pager = b.editor->pager();
        QVERIFY(list && pager);
        QCOMPARE(pager->pageCount(), 3);
        QCOMPARE(visibleItems(list), 10);
        QCOMPARE(visibleLabels(list).first(), QStringLiteral("Option 0"));

        auto *bar = b.editor->widget()->findChild<QWidget *>(QStringLiteral("kipPagerBar"));
        QVERIFY(bar && bar->isVisible());
        QToolButton *prev = nullptr, *next = nullptr;
        for (auto *btn : bar->findChildren<QToolButton *>()) {
            (btn->property("kipPagerRole").toString() == QStringLiteral("prev") ? prev : next) = btn;
        }
        QVERIFY(prev && next);
        QVERIFY(!prev->isEnabled());
        QVERIFY(next->isEnabled());
        next->click();
        QCOMPARE(visibleLabels(list).first(), QStringLiteral("Option 10"));
        next->click();
        QCOMPARE(visibleItems(list), 5); // última página
        QCOMPARE(visibleLabels(list).first(), QStringLiteral("Option 20"));
        QVERIFY(!next->isEnabled());
        QVERIFY(prev->isEnabled());
        prev->click();
        QCOMPARE(pager->page(), 1);
        auto *label = bar->findChild<QLabel *>(QStringLiteral("kipPagerLabel"));
        QVERIFY(label->text().contains(QStringLiteral("2")) && label->text().contains(QStringLiteral("3")));
        QVERIFY(label->text().contains(QStringLiteral("25")));
    }

    void pagingAppliesToTheFilteredResultAndFilterResetsThePage()
    {
        KipField f = field(KipFieldType::List);
        f.options = manyOptions(25);
        f.pageSize = 4;
        f.searchable = true;
        Built b(f);
        auto *list = b.editor->widget()->findChild<QListWidget *>();
        auto *pager = b.editor->pager();
        pager->setPage(3);
        QCOMPARE(pager->page(), 3);

        b.editor->filterEdit()->setText(QStringLiteral("option 2"));
        // Option 2, 20..24 = 6 resultados: página 1 de 2, de volta ao começo.
        QCOMPARE(pager->matchCount(), 6);
        QCOMPARE(pager->pageCount(), 2);
        QCOMPARE(pager->page(), 0);
        QCOMPARE(visibleLabels(list), (QStringList{"Option 2", "Option 20", "Option 21", "Option 22"}));
        pager->next();
        QCOMPARE(visibleLabels(list), (QStringList{"Option 23", "Option 24"}));

        b.editor->filterEdit()->setText(QStringLiteral("option 2"));
        b.editor->filterEdit()->setText(QStringLiteral("zzz"));
        QCOMPARE(visibleItems(list), 0);
        auto *bar = b.editor->widget()->findChild<QWidget *>(QStringLiteral("kipPagerBar"));
        QVERIFY(bar->isVisible()); // mostra "Nenhum resultado"
        b.editor->filterEdit()->clear();
        QCOMPARE(pager->matchCount(), 25);
        QCOMPARE(visibleItems(list), 4);
    }

    void pagerBarIsHiddenWhenEverythingFitsOnOnePage()
    {
        KipField f = field(KipFieldType::List);
        f.options = manyOptions(6);
        f.pageSize = 10;
        Built b(f);
        auto *bar = b.editor->widget()->findChild<QWidget *>(QStringLiteral("kipPagerBar"));
        QVERIFY(bar);
        QVERIFY(!bar->isVisible());
        QCOMPARE(visibleItems(b.editor->widget()->findChild<QListWidget *>()), 6);
    }

    void selectionAndValueSurvivePageChanges()
    {
        KipField f = field(KipFieldType::List);
        f.options = manyOptions(25);
        f.pageSize = 10;
        Built b(f, QJsonValue(QStringLiteral("v3")));
        QCOMPARE(b.editor->value().toString(), QStringLiteral("v3"));
        b.editor->pager()->setPage(2);
        QCOMPARE(b.editor->value().toString(), QStringLiteral("v3"));

        KipField multi = f;
        multi.multiple = true;
        Built m(multi, QJsonArray{QStringLiteral("v1"), QStringLiteral("v22")});
        m.editor->pager()->next();
        QCOMPARE(m.editor->value().toArray().size(), 2);
    }

    void listWithoutPageSizeBehavesAsBefore()
    {
        KipField f = field(KipFieldType::List);
        f.options = manyOptions(30);
        Built b(f);
        QVERIFY(!b.editor->widget()->findChild<QWidget *>(QStringLiteral("kipPagerBar")));
        QCOMPARE(b.editor->pager()->pageCount(), 1);
        QCOMPARE(visibleItems(b.editor->widget()->findChild<QListWidget *>()), 30);
    }

    void pagedTableHidesRowsOutsideThePage()
    {
        KipField f = field(KipFieldType::Table);
        f.columns = columns();
        f.rows = manyRows(12);
        f.pageSize = 5;
        Built b(f);
        auto *table = qobject_cast<QTableWidget *>(b.editor->focusTarget());
        QVERIFY(table);
        auto visibleRows = [&]() {
            int n = 0;
            for (int r = 0; r < table->rowCount(); ++r) n += table->isRowHidden(r) ? 0 : 1;
            return n;
        };
        QCOMPARE(visibleRows(), 5);
        QCOMPARE(b.editor->pager()->pageCount(), 3);
        b.editor->pager()->setPage(2);
        QCOMPARE(visibleRows(), 2);
        QVERIFY(!table->isRowHidden(10));
        // filtro dentro da tabela paginada
        b.editor->filterEdit()->setText(QStringLiteral("row 1"));  // Row 1, 10, 11
        QCOMPARE(b.editor->pager()->matchCount(), 3);
        QCOMPARE(visibleRows(), 3);
    }

    void textLikeFieldsAreTheOnesDebouncedByWatch()
    {
        QVERIFY(Built(field(KipFieldType::Text)).editor->isTextLike());
        QVERIFY(Built(field(KipFieldType::Secret)).editor->isTextLike());
        QVERIFY(Built(field(KipFieldType::Number)).editor->isTextLike());
        KipField sel = field(KipFieldType::Select);
        sel.options = {opt(QStringLiteral("a"))};
        QVERIFY(!Built(sel).editor->isTextLike());
        QVERIFY(!Built(field(KipFieldType::Flags)).editor->isTextLike());
        QVERIFY(!Built(field(KipFieldType::Date)).editor->isTextLike());
    }
};

QTEST_MAIN(TestParameterFieldFactory)
#include "test_parameter_field_factory.moc"
