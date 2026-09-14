#include <QTest>
#include <QLineEdit>
#include <QComboBox>
#include <QCompleter>
#include <QCheckBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QMenu>
#include <QTimer>
#include <QApplication>
#include <QCalendarWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QSignalSpy>

#include "ui/features/command-editor/parameter-form-dialog.h"
#include "ui/shared/grid-param-field.h"
#include "ui/shared/inline-code-field.h"
#include "utils/translation-manager.h"
#include <QJsonArray>
#include <QJsonObject>
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/collection-chip-picker.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Fluxo completo do ParameterFormDialog (preenchimento antes da execução) com parâmetros de cada tipo e teclado
// real (QTest::keyClicks, não setText). Regressão de segfault: uso-após-destruição, ponteiro nulo ou reentrância.
class TestParameterFormDialog : public QObject {
    Q_OBJECT

private slots:
    void fillingTextParameterDoesNotCrash()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("nome");
        p.label = QStringLiteral("Nome");
        p.type = ParameterType::Text;
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QLineEdit *>();
        QVERIFY(field != nullptr);

        field->setFocus();
        QTest::keyClicks(field, QStringLiteral("valor de teste"));

        const QMap<QString, QString> values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("nome")), QStringLiteral("valor de teste"));
    }

    // Sem description o placeholder é genérico; com description, ela vira o placeholder.
    void textFieldPlaceholderFallsBackToGenericTextWithoutDescription()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("nome");
        p.type = ParameterType::Text;
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QLineEdit *>();
        QVERIFY(field != nullptr);
        QVERIFY(!field->placeholderText().isEmpty());
    }

    void textFieldPlaceholderUsesDescriptionWhenPresent()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("ambiente");
        p.type = ParameterType::Text;
        p.description = QStringLiteral("Nome do ambiente alvo (ex: staging)");
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QLineEdit *>();
        QVERIFY(field != nullptr);
        QCOMPARE(field->placeholderText(), QStringLiteral("Nome do ambiente alvo (ex: staging)"));
    }

    void fillingTextParameterWithSpecialCharactersDoesNotCrash()
    {
        // As teclas '/' e '\'' viravam acentos (dead keys; causa de ambiente, Wayland/WSLg IM). O preenchimento desses
        // caracteres não pode crashar, qualquer que seja o resultado visual.
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("path");
        p.type = ParameterType::Text;
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QLineEdit *>();
        QVERIFY(field != nullptr);

        field->setFocus();
        QTest::keyClicks(field, QStringLiteral("/usr/bin/env 'quoted' \"double\""));

        const QMap<QString, QString> values = dialog.values();
        QVERIFY(!values.value(QStringLiteral("path")).isEmpty());
    }

    void fillingSelectParameterDoesNotCrash()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("ambiente");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("staging"), QStringLiteral("prod")};
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QComboBox *>();
        QVERIFY(field != nullptr);
        field->setCurrentIndex(2);

        const QMap<QString, QString> values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("ambiente")), QStringLiteral("prod"));
    }

    // Options "rótulo:valor" EXIBEM o rótulo e INJETAM o valor (ex: "Produção:prod" mostra "Produção", injeta "prod").
    void selectWithLabelValuePairsInjectsValueNotLabel()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("cliente");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("Produção:prod"), QStringLiteral("Qualidade:qa")};
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QComboBox *>();
        QVERIFY(field != nullptr);
        QCOMPARE(field->itemText(0), QStringLiteral("Produção"));
        QCOMPARE(field->itemText(1), QStringLiteral("Qualidade"));

        field->setCurrentIndex(0);
        QCOMPARE(dialog.values().value(QStringLiteral("cliente")), QStringLiteral("prod"));

        field->setCurrentIndex(1);
        QCOMPARE(dialog.values().value(QStringLiteral("cliente")), QStringLiteral("qa"));
    }

    // Options simples (sem ":") exibem e injetam o próprio valor.
    void selectWithPlainOptionsRemainsBackwardCompatible()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("ambiente");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("prod")};
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QComboBox *>();
        QVERIFY(field != nullptr);
        QCOMPARE(field->itemText(0), QStringLiteral("dev"));
        field->setCurrentIndex(1);
        QCOMPARE(dialog.values().value(QStringLiteral("ambiente")), QStringLiteral("prod"));
    }

    // As opções do Select são reordenadas pelo histórico de uso (mais recentes no topo); as demais mantêm a ordem
    // original (stable_sort).
    void selectOptionsAreReorderedByUsageHistory()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("env");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("staging"),
                     QStringLiteral("qa"), QStringLiteral("prod")};
        params << p;

        QMap<QString, QStringList> history;
        history[QStringLiteral("env")] = {QStringLiteral("prod"), QStringLiteral("qa")};

        ParameterFormDialog dialog(params, nullptr, {}, history);
        auto *field = dialog.findChild<QComboBox *>();
        QVERIFY(field != nullptr);
        QCOMPARE(field->itemData(0).toString(), QStringLiteral("prod"));
        QCOMPARE(field->itemData(1).toString(), QStringLiteral("qa"));
        QCOMPARE(field->itemData(2).toString(), QStringLiteral("dev"));
        QCOMPARE(field->itemData(3).toString(), QStringLiteral("staging"));
    }

    // O Select é editável (busca) e updatedUsageHistory promove o valor escolhido ao topo, sem duplicatas.
    void updatedUsageHistoryPromotesChosenValueToTop()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("env");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("staging"), QStringLiteral("prod")};
        params << p;

        QMap<QString, QStringList> history;
        history[QStringLiteral("env")] = {QStringLiteral("prod")};

        ParameterFormDialog dialog(params, nullptr, {}, history);
        auto *field = dialog.findChild<QComboBox *>();
        QVERIFY(field != nullptr);
        const int idx = field->findData(QStringLiteral("staging"));
        QVERIFY(idx >= 0);
        field->setCurrentIndex(idx);

        const QStringList updated = dialog.updatedUsageHistory().value(QStringLiteral("env"));
        QVERIFY(!updated.isEmpty());
        QCOMPARE(updated.first(), QStringLiteral("staging"));
        QVERIFY(updated.contains(QStringLiteral("prod")));
        QCOMPARE(updated.count(QStringLiteral("staging")), 1);
    }

    // O combo do Select é pesquisável (editable + completer contains).
    void selectComboIsSearchable()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("env");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("prod")};
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QComboBox *>();
        QVERIFY(field != nullptr);
        QVERIFY(field->isEditable());
        QVERIFY(field->completer() != nullptr);
        QCOMPARE(field->completer()->filterMode(), Qt::MatchContains);
    }

    void fillingBoolParameterDoesNotCrash()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("force");
        p.type = ParameterType::Bool;
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QCheckBox *>();
        QVERIFY(field != nullptr);
        field->setChecked(true);

        const QMap<QString, QString> values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("force")), QStringLiteral("true"));
    }

    void fillingMultipleParametersOfAllTypesDoesNotCrash()
    {
        // Vários parâmetros de tipos diferentes ao mesmo tempo (um Command real costuma ter mais de um).
        QVector<Parameter> params;

        Parameter textParam;
        textParam.name = QStringLiteral("nome");
        textParam.type = ParameterType::Text;
        params << textParam;

        Parameter selectParam;
        selectParam.name = QStringLiteral("ambiente");
        selectParam.type = ParameterType::Select;
        selectParam.options = {QStringLiteral("dev"), QStringLiteral("prod")};
        params << selectParam;

        Parameter boolParam;
        boolParam.name = QStringLiteral("force");
        boolParam.type = ParameterType::Bool;
        params << boolParam;

        Parameter fileParam;
        fileParam.name = QStringLiteral("arquivo");
        fileParam.type = ParameterType::File;
        params << fileParam;

        ParameterFormDialog dialog(params, nullptr);

        auto *textField = dialog.findChildren<QLineEdit *>().first();
        QVERIFY(textField != nullptr);
        textField->setFocus();
        QTest::keyClicks(textField, QStringLiteral("teste"));

        auto *comboField = dialog.findChild<QComboBox *>();
        QVERIFY(comboField != nullptr);
        comboField->setCurrentIndex(1);

        auto *checkField = dialog.findChild<QCheckBox *>();
        QVERIFY(checkField != nullptr);
        checkField->setChecked(true);

        const QMap<QString, QString> values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("nome")), QStringLiteral("teste"));
        QCOMPARE(values.value(QStringLiteral("ambiente")), QStringLiteral("prod"));
        QCOMPARE(values.value(QStringLiteral("force")), QStringLiteral("true"));
        QVERIFY(values.contains(QStringLiteral("arquivo")));
    }
    // Multi-select de opções fixas: lista checkable; junta os valores marcados por vírgula e pré-marca pelos iniciais.
    void multiSelectJoinsCheckedValues()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("envs");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("qa"), QStringLiteral("prod")};
        p.multiSelect = true;
        params << p;

        QMap<QString, QString> initial;
        initial[QStringLiteral("envs")] = QStringLiteral("dev,prod");

        ParameterFormDialog dialog(params, nullptr, initial);
        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);

        int checked = 0;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->checkState() == Qt::Checked) ++checked;
        }
        QCOMPARE(checked, 2);

        QCOMPARE(dialog.values().value(QStringLiteral("envs")), QStringLiteral("dev,prod"));

        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(Qt::UserRole).toString() == QStringLiteral("qa")) {
                list->item(i)->setCheckState(Qt::Checked);
            }
        }
        QCOMPARE(dialog.values().value(QStringLiteral("envs")), QStringLiteral("dev,qa,prod"));
    }

    // O multi-select de opções fixas também injeta os RÓTULOS marcados (CSV) em "<nome>__labels".
    void multiSelectInjectsCheckedLabels()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("envs");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("Desenvolvimento:dev"), QStringLiteral("Qualidade:qa"),
                     QStringLiteral("Produção:prod")};
        p.multiSelect = true;
        params << p;

        QMap<QString, QString> initial;
        initial[QStringLiteral("envs")] = QStringLiteral("dev,prod");

        ParameterFormDialog dialog(params, nullptr, initial);
        const QMap<QString, QString> values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("envs")), QStringLiteral("dev,prod"));
        QCOMPARE(values.value(QStringLiteral("envs__labels")),
                 QStringLiteral("Desenvolvimento,Produção"));
    }

    // Espaço marca/desmarca os checkboxes sem mouse: foca o item por teclado (seta para baixo) e aperta Espaço.
    void spaceKeyTogglesCheckedStateOfFocusedMultiSelectItem()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("envs");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("qa"), QStringLiteral("prod")};
        p.multiSelect = true;
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        list->setFocus();
        list->setCurrentRow(0);
        QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);

        QTest::keyClick(list, Qt::Key_Space);
        QCOMPARE(list->item(0)->checkState(), Qt::Checked);

        QTest::keyClick(list, Qt::Key_Space);
        QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
    }

    // Expandir o GRUPO que contém o multi-select foca o filtro dele (a lista já fica sempre visível no grupo, sem
    // popover).
    void expandingGroupFocusesItsMultiSelectFilterField()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("envs");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("qa")};
        p.multiSelect = true;
        p.group = QStringLiteral("Acesso");
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        dialog.show();
        QApplication::setActiveWindow(&dialog);
        auto *card = dialog.findChild<CollapsibleSectionCard *>();
        QVERIFY(card != nullptr);
        QVERIFY(!card->isExpanded());

        auto *filter = dialog.findChild<QLineEdit *>();
        QVERIFY(filter != nullptr);
        QVERIFY(!filter->hasFocus());

        card->setExpanded(true);
        QVERIFY(filter->hasFocus());
    }

    // Select de seleção única: injeta "<nome>__label" com o rótulo exibido, distinto do valor em "<nome>".
    void singleSelectInjectsChosenLabel()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("cliente");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("Produção:prod"), QStringLiteral("Qualidade:qa")};
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<QComboBox *>();
        QVERIFY(field != nullptr);
        field->setCurrentIndex(1);

        const QMap<QString, QString> values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("cliente")), QStringLiteral("qa"));
        QCOMPARE(values.value(QStringLiteral("cliente__label")), QStringLiteral("Qualidade"));
    }

    // Regressão: seleção múltipla (opções fixas ou coleção) salvava o CSV inteiro como UMA entrada de histórico, e
    // nenhum valor individual era reconhecido depois (CollectionSelectorDialog e ordenação por uso comparam ids).
    void updatedUsageHistorySplitsMultiSelectValuesIndividually()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("envs");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("qa"), QStringLiteral("prod")};
        p.multiSelect = true;
        params << p;

        QMap<QString, QString> initial;
        initial[QStringLiteral("envs")] = QStringLiteral("dev,prod");

        ParameterFormDialog dialog(params, nullptr, initial);
        const QStringList updated = dialog.updatedUsageHistory().value(QStringLiteral("envs"));
        // Cada valor vira uma entrada individual, não o CSV inteiro.
        QVERIFY(!updated.contains(QStringLiteral("dev,prod")));
        QVERIFY(updated.contains(QStringLiteral("dev")));
        QVERIFY(updated.contains(QStringLiteral("prod")));
        QCOMPARE(updated.count(), 2);
    }

    // Regressão: a pré-seleção de um parâmetro ligado a coleção comparava o initialValue inteiro (CSV "id1,id2") com
    // o id de UMA entrada, então com mais de um valor m_collectionSelectionByParam ficava vazio e values() devolvia "".
    void collectionParamRemembersMultipleLastSelectedValues()
    {
        Collection col;
        col.id = QStringLiteral("col1");
        col.schema = {CollectionField{QStringLiteral("name"), QStringLiteral("Nome"),
            CollectionFieldType::Text, true, false}};
        CollectionEntry e1;
        e1.id = QStringLiteral("id1");
        e1.values[QStringLiteral("name")] = QStringLiteral("Alice");
        CollectionEntry e2;
        e2.id = QStringLiteral("id2");
        e2.values[QStringLiteral("name")] = QStringLiteral("Bob");
        col.entries = {e1, e2};

        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("usuario");
        p.type = ParameterType::Select;
        p.collectionId = col.id;
        p.collectionDisplayField = QStringLiteral("name");
        params << p;

        QMap<QString, QString> initial;
        initial[QStringLiteral("usuario")] = QStringLiteral("id1,id2");

        ParameterFormDialog dialog(params, nullptr, initial, {}, {col});
        const QMap<QString, QString> values = dialog.values();
        // As duas entradas da última execução devem ser lembradas.
        QCOMPARE(values.value(QStringLiteral("usuario")), QStringLiteral("id1,id2"));
    }

    // Regressão: sem collectionDisplayField e com o schema padrão [Key, Value] (Collection::defaultSchema), a chip
    // deve mostrar Value (texto amigável), não o primeiro campo (Key, um identificador).
    void collectionChipShowsValueFieldNotKeyFieldWhenNoDisplayFieldConfigured()
    {
        Collection col;
        col.id = QStringLiteral("col1");
        col.schema = Collection::defaultSchema();
        CollectionEntry e1;
        e1.id = QStringLiteral("id1");
        e1.values[QStringLiteral("key")] = QStringLiteral("ep_1");
        e1.values[QStringLiteral("value")] = QStringLiteral("Alice Employee");
        col.entries = {e1};

        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("usuario");
        p.type = ParameterType::Select;
        p.collectionId = col.id;
        // collectionDisplayField deliberadamente não configurado.
        params << p;

        QMap<QString, QString> initial;
        initial[QStringLiteral("usuario")] = QStringLiteral("id1");

        ParameterFormDialog dialog(params, nullptr, initial, {}, {col});
        auto *picker = dialog.findChild<CollectionChipPickerWidget *>();
        QVERIFY(picker != nullptr);
        const QList<QLabel *> labels = picker->findChildren<QLabel *>();
        QVERIFY2(!labels.isEmpty(), "nenhuma chip renderizada");
        bool foundValueText = false;
        for (QLabel *lbl : labels) {
            if (lbl->text() == QStringLiteral("Alice Employee")) { foundValueText = true; break; }
            QVERIFY2(lbl->text() != QStringLiteral("ep_1"),
                     "chip mostrou o campo Key (parece um id) em vez do Value");
        }
        QVERIFY2(foundValueText, "chip não mostrou o campo Value esperado");
    }

    // Parâmetros com o mesmo `group` (não vazio) caem num único CollapsibleSectionCard com o nome do grupo,
    // colapsado por padrão; parâmetros sem grupo ficam soltos no form.
    void parametersWithSameGroupAreBundledIntoOneCollapsedCard()
    {
        QVector<Parameter> params;
        Parameter ungrouped;
        ungrouped.name = QStringLiteral("solto");
        ungrouped.type = ParameterType::Text;
        params << ungrouped;

        Parameter a;
        a.name = QStringLiteral("a");
        a.type = ParameterType::Text;
        a.group = QStringLiteral("Avançado");
        params << a;

        Parameter b;
        b.name = QStringLiteral("b");
        b.type = ParameterType::Text;
        b.group = QStringLiteral("Avançado");
        params << b;

        ParameterFormDialog dialog(params, nullptr);

        const auto cards = dialog.findChildren<CollapsibleSectionCard *>();
        QCOMPARE(cards.size(), 1);
        CollapsibleSectionCard *card = cards.first();
        QVERIFY(!card->isExpanded());

        // Os campos do grupo estão dentro do card; o solto é filho do diálogo.
        const auto fieldsInCard = card->findChildren<QLineEdit *>();
        QCOMPARE(fieldsInCard.size(), 2);

        auto *soltoField = dialog.findChild<QLineEdit *>();
        QVERIFY(soltoField != nullptr);
        QVERIFY(card->findChildren<QLineEdit *>().indexOf(soltoField) < 0);
    }

    // Regressão: um grupo no meio de parâmetros soltos era renderizado no fim do form. A ordem visual segue `params`,
    // com o card na posição do primeiro parâmetro do grupo.
    void groupPositionRespectsDeclarationOrderAmongLooseParams()
    {
        QVector<Parameter> params;
        Parameter p1;
        p1.name = QStringLiteral("par1");
        p1.type = ParameterType::Text;
        p1.defaultValue = QStringLiteral("v1");
        params << p1;

        Parameter a;
        a.name = QStringLiteral("a");
        a.type = ParameterType::Text;
        a.defaultValue = QStringLiteral("va");
        a.group = QStringLiteral("Acesso");
        params << a;

        Parameter b;
        b.name = QStringLiteral("b");
        b.type = ParameterType::Text;
        b.defaultValue = QStringLiteral("vb");
        b.group = QStringLiteral("Acesso");
        params << b;

        Parameter p4;
        p4.name = QStringLiteral("par4");
        p4.type = ParameterType::Text;
        p4.defaultValue = QStringLiteral("v4");
        params << p4;

        ParameterFormDialog dialog(params, nullptr);
        dialog.show();

        QLineEdit *field1 = nullptr;
        QLineEdit *field4 = nullptr;
        for (QLineEdit *le : dialog.findChildren<QLineEdit *>()) {
            if (le->text() == QStringLiteral("v1")) { field1 = le; }
            if (le->text() == QStringLiteral("v4")) { field4 = le; }
        }
        QVERIFY(field1 != nullptr);
        QVERIFY(field4 != nullptr);

        const auto cards = dialog.findChildren<CollapsibleSectionCard *>();
        QCOMPARE(cards.size(), 1);
        CollapsibleSectionCard *card = cards.first();

        // Ordem visual esperada: par1 (solto) -> card "Acesso" -> par4 (solto).
        const int y1 = field1->mapTo(&dialog, QPoint(0, 0)).y();
        const int yCard = card->mapTo(&dialog, QPoint(0, 0)).y();
        const int y4 = field4->mapTo(&dialog, QPoint(0, 0)).y();
        QVERIFY(y1 < yCard);
        QVERIFY(yCard < y4);
    }

    // `required` é opt-in (não vem de "!optional"): param com `required=true` mostra o asterisco; `optional=true`
    // nunca mostra, mesmo com `required` ligado (optional sempre vence).
    void theFormListsWhichFieldBlocksOkAndWhy()
    {
        QVector<Parameter> params;
        Parameter title;
        title.name = QStringLiteral("title");
        title.label = QStringLiteral("Title");
        title.type = ParameterType::Text;
        title.required = true;
        params << title;
        Parameter rows;
        rows.name = QStringLiteral("rows");
        rows.label = QStringLiteral("Rows");
        rows.type = ParameterType::Grid;
        rows.required = true;
        GridColumn col;
        col.name = QStringLiteral("x");
        rows.gridColumns = {col};
        params << rows;
        Parameter day;
        day.name = QStringLiteral("day");
        day.label = QStringLiteral("Day");
        day.type = ParameterType::Date;
        day.required = true;
        params << day;
        Parameter fine;
        fine.name = QStringLiteral("fine");
        fine.type = ParameterType::Text;
        params << fine;

        ParameterFormDialog dialog(params, nullptr);
        auto *grid = dialog.findChild<GridParamField *>();
        QVERIFY(grid);
        grid->setRows(QJsonArray());
        const auto problems = dialog.validationProblems();
        QCOMPARE(problems.size(), 3);
        QCOMPARE(problems.at(0).label, QStringLiteral("Title"));
        QCOMPARE(problems.at(0).reason, kai::utils::tr(QStringLiteral("params.error.required")));
        QCOMPARE(problems.at(1).label, QStringLiteral("Rows"));
        QCOMPARE(problems.at(1).reason, kai::utils::tr(QStringLiteral("params.error.grid_rows")));
        QCOMPARE(problems.at(2).reason, kai::utils::tr(QStringLiteral("params.error.date")));

        auto *banner = dialog.findChild<QLabel *>(QStringLiteral("paramProblems"));
        QVERIFY(banner);
        QVERIFY(!banner->isHidden());
        QVERIFY(banner->text().contains(QStringLiteral("Title")));
        QVERIFY(banner->text().contains(QStringLiteral("Rows")));
        QVERIFY(!banner->text().contains(QStringLiteral("fine")));

        grid->setRows(QJsonArray{QJsonObject{{QStringLiteral("x"), QStringLiteral("1")}}});
        const auto remaining = dialog.validationProblems();
        QCOMPARE(remaining.size(), 2);
        QCOMPARE(remaining.at(0).label, QStringLiteral("Title"));
        QCOMPARE(remaining.at(1).paramName, QStringLiteral("day"));
        QVERIFY(!banner->text().contains(QStringLiteral("Rows")));
    }

    void theProblemListIsHiddenWhenEverythingIsFilled()
    {
        Parameter p;
        p.name = QStringLiteral("a");
        p.label = QStringLiteral("A");
        p.type = ParameterType::Text;
        p.required = true;
        p.defaultValue = QStringLiteral("ok");
        ParameterFormDialog dialog({p}, nullptr);
        QVERIFY(dialog.validationProblems().isEmpty());
        auto *banner = dialog.findChild<QLabel *>(QStringLiteral("paramProblems"));
        QVERIFY(banner);
        QVERIFY(banner->isHidden());
    }

    static Parameter stepParam(const QString &name, const QString &step, bool required = false)
    {
        Parameter p;
        p.name = name;
        p.label = name.toUpper();
        p.type = ParameterType::Text;
        p.step = step;
        p.required = required;
        return p;
    }

    void withoutStepsTheFormIsASinglePage()
    {
        ParameterFormDialog dialog({stepParam(QStringLiteral("a"), QString())}, nullptr);
        QCOMPARE(dialog.stepCount(), 1);
        QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("paramStepBack")));
        QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("paramStepChip")));
    }

    void stepsSplitTheFormIntoPagesWithBackAndNext()
    {
        const QVector<Parameter> params = {stepParam(QStringLiteral("a"), QStringLiteral("One")),
                                           stepParam(QStringLiteral("b"), QStringLiteral("Two"), true),
                                           stepParam(QStringLiteral("c"), QStringLiteral("Three")),
                                           stepParam(QStringLiteral("d"), QString(), true)};
        ParameterFormDialog dialog(params, nullptr);
        dialog.show();
        QCOMPARE(dialog.stepCount(), 3);
        QCOMPARE(dialog.currentStep(), 0);

        auto *box = dialog.findChild<QDialogButtonBox *>();
        QPushButton *primary = box->button(QDialogButtonBox::Ok);
        auto *back = dialog.findChild<QPushButton *>(QStringLiteral("paramStepBack"));
        QVERIFY(back);
        const QString nextText = kai::utils::tr(QStringLiteral("params.step.next"));
        const QString okText = QStringLiteral("OK");
        QVERIFY(!back->isEnabled());
        QCOMPARE(primary->text(), nextText);

        // O obrigatório sem etapa ("d") mora na primeira página e bloqueia o "Próximo" dela.
        QVERIFY(!primary->isEnabled());
        const auto problems = dialog.validationProblems();
        QCOMPARE(problems.size(), 2);
        QCOMPARE(problems.at(0).paramName, QStringLiteral("b"));
        QCOMPARE(problems.at(0).step, 1);
        QCOMPARE(problems.at(1).paramName, QStringLiteral("d"));
        QCOMPARE(problems.at(1).step, 0);
        auto *banner = dialog.findChild<QLabel *>(QStringLiteral("paramProblems"));
        QVERIFY(banner->text().contains(QStringLiteral("D")));
        QVERIFY(!banner->text().contains(QStringLiteral(">B<")));

        // Preenchida a página 1, "Próximo" abre a 2, que bloqueia por causa do "b".
        for (QLineEdit *edit : dialog.findChildren<QLineEdit *>()) {
            if (edit->isVisibleTo(&dialog) && edit->text().isEmpty()) { edit->setText(QStringLiteral("x")); }
        }
        QVERIFY(primary->isEnabled());
        primary->click();
        QCOMPARE(dialog.currentStep(), 1);
        QVERIFY(back->isEnabled());
        QVERIFY(!primary->isEnabled());
        QVERIFY(banner->text().contains(QStringLiteral("B")));

        for (QLineEdit *edit : dialog.findChildren<QLineEdit *>()) {
            if (edit->isVisibleTo(&dialog) && edit->text().isEmpty()) { edit->setText(QStringLiteral("y")); }
        }
        QVERIFY(primary->isEnabled());
        primary->click();
        QCOMPARE(dialog.currentStep(), 2);
        QCOMPARE(primary->text(), okText); // última página: volta a ser o OK
        QVERIFY(primary->isEnabled());

        QSignalSpy accepted(&dialog, &QDialog::accepted);
        back->click();
        QCOMPARE(dialog.currentStep(), 1);
        QCOMPARE(primary->text(), nextText);
        QCOMPARE(accepted.count(), 0); // "Próximo" nunca aceita o diálogo
        primary->click();
        primary->click();
        QCOMPARE(accepted.count(), 1);

        // Os valores de TODAS as páginas saem juntos.
        const auto values = dialog.values();
        QVERIFY(values.contains(QStringLiteral("a")));
        QCOMPARE(values.value(QStringLiteral("b")), QStringLiteral("y"));
        QCOMPARE(values.value(QStringLiteral("d")), QStringLiteral("x"));
        QCOMPARE(values.value(QStringLiteral("a")), QStringLiteral("x"));
    }

    void aStepChipOnlyOpensPagesWhoseEarlierPagesAreValid()
    {
        const QVector<Parameter> params = {stepParam(QStringLiteral("a"), QStringLiteral("One"), true),
                                           stepParam(QStringLiteral("b"), QStringLiteral("Two"))};
        ParameterFormDialog dialog(params, nullptr);
        dialog.show();
        const auto chips = dialog.findChildren<QPushButton *>(QStringLiteral("paramStepChip"));
        QCOMPARE(chips.size(), 2);
        QVERIFY(chips.at(0)->isEnabled());
        QVERIFY(!chips.at(1)->isEnabled()); // a página 1 ainda tem um obrigatório vazio
        for (QLineEdit *edit : dialog.findChildren<QLineEdit *>()) {
            if (edit->isVisibleTo(&dialog)) { edit->setText(QStringLiteral("ok")); break; }
        }
        QVERIFY(chips.at(1)->isEnabled());
        chips.at(1)->click();
        QCOMPARE(dialog.currentStep(), 1);
        chips.at(0)->click();
        QCOMPARE(dialog.currentStep(), 0);
    }

    void clickingAProblemLinkOpensThePageOfThatField()
    {
        const QVector<Parameter> params = {stepParam(QStringLiteral("a"), QStringLiteral("One")),
                                           stepParam(QStringLiteral("b"), QStringLiteral("Two"), true)};
        ParameterFormDialog dialog(params, nullptr);
        dialog.show();
        QCOMPARE(dialog.currentStep(), 0);
        auto *banner = dialog.findChild<QLabel *>(QStringLiteral("paramProblems"));
        emit banner->linkActivated(QStringLiteral("b"));
        QCOMPARE(dialog.currentStep(), 1);
        emit banner->linkActivated(QStringLiteral("a"));
        QCOMPARE(dialog.currentStep(), 0);
    }

    void requiredParameterLabelShowsAsteriskOptionalDoesNot()
    {
        QVector<Parameter> params;
        Parameter required;
        required.name = QStringLiteral("obrigatorio");
        required.label = QStringLiteral("Obrigatório");
        required.type = ParameterType::Text;
        required.required = true;
        params << required;

        Parameter optional;
        optional.name = QStringLiteral("opcional");
        optional.label = QStringLiteral("Opcional");
        optional.type = ParameterType::Text;
        optional.optional = true;
        optional.required = true;
        params << optional;

        ParameterFormDialog dialog(params, nullptr);
        const auto labels = dialog.findChildren<QLabel *>();

        QLabel *requiredLabel = nullptr;
        QLabel *optionalLabel = nullptr;
        for (QLabel *l : labels) {
            if (l->objectName() == QStringLiteral("paramProblems")) { continue; }
            if (l->text().contains(QStringLiteral("Obrigatório"))) { requiredLabel = l; }
            if (l->text() == QStringLiteral("Opcional")) { optionalLabel = l; }
        }
        QVERIFY(requiredLabel != nullptr);
        QVERIFY(requiredLabel->text().contains(QStringLiteral("*")));
        QVERIFY(optionalLabel != nullptr);
        QVERIFY(!optionalLabel->text().contains(QStringLiteral("*")));
    }

    // Sem default nem lastValue, um Text obrigatório começa vazio -> OK desabilitado; digitar habilita; apagar
    // desabilita de novo.
    void okButtonStaysDisabledUntilRequiredTextFieldIsFilled()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("nome");
        p.type = ParameterType::Text;
        p.required = true;
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *buttonBox = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttonBox != nullptr);
        QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
        QVERIFY(okButton != nullptr);
        QVERIFY(!okButton->isEnabled());

        auto *field = dialog.findChild<QLineEdit *>();
        QVERIFY(field != nullptr);
        QTest::keyClicks(field, QStringLiteral("valor"));
        QVERIFY(okButton->isEnabled());

        field->clear();
        QVERIFY(!okButton->isEnabled());
    }

    // Um parâmetro opcional nunca bloqueia o OK, preenchido ou não, mesmo sem outro parâmetro no form.
    void okButtonStaysEnabledWhenOnlyOptionalParametersExist()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("nome");
        p.type = ParameterType::Text;
        p.optional = true;
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *buttonBox = dialog.findChild<QDialogButtonBox *>();
        QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
        QVERIFY(okButton->isEnabled());
    }

    // O card do grupo não tem badge de contagem (a maioria dos grupos não tem `required`, então mostrava "0" sem
    // utilidade), qualquer que seja o número de parâmetros.
    void groupCardHasNoCountBadge()
    {
        QVector<Parameter> params;
        Parameter a;
        a.name = QStringLiteral("a");
        a.type = ParameterType::Text;
        a.group = QStringLiteral("Acesso");
        a.required = true;
        params << a;

        Parameter b;
        b.name = QStringLiteral("b");
        b.type = ParameterType::Text;
        b.group = QStringLiteral("Acesso");
        params << b;

        ParameterFormDialog dialog(params, nullptr);
        const auto cards = dialog.findChildren<CollapsibleSectionCard *>();
        QCOMPARE(cards.size(), 1);
        CollapsibleSectionCard *card = cards.first();

        auto *badge = card->findChild<QLabel *>(QStringLiteral("sectionCountBadge"));
        QVERIFY(badge != nullptr);
        QVERIFY(!badge->isVisible());
    }

    // Um select obrigatório sem default/último valor válido abre em "nada escolhido" (item vazio), em vez de
    // pré-selecionar a 1ª opção: o valor vazio já mostra que falta escolher e qualquer opção, inclusive a
    // primeira, libera o OK. Com default ou último valor válido, conta como preenchido desde o início.
    void requiredSelectWithoutDefaultStartsEmptyAndBlocksOkUntilChosen()
    {
        Parameter select;
        select.name = QStringLiteral("nivel");
        select.label = QStringLiteral("Nível");
        select.type = ParameterType::Select;
        select.options = {QStringLiteral("Opção 1"), QStringLiteral("Opção 2")};
        select.required = true;

        ParameterFormDialog dialog({select}, nullptr);
        QPushButton *okButton = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        auto *combo = dialog.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 3); // "nada escolhido" + as 2 opções
        QCOMPARE(combo->currentData().toString(), QString());
        QVERIFY(!okButton->isEnabled());
        QCOMPARE(dialog.validationProblems().size(), 1);

        combo->setCurrentIndex(1);
        QVERIFY(okButton->isEnabled());
        QCOMPARE(dialog.values().value(QStringLiteral("nivel")), QStringLiteral("Opção 1"));
        combo->setCurrentIndex(2);
        QVERIFY(okButton->isEnabled());
        QCOMPARE(dialog.values().value(QStringLiteral("nivel")), QStringLiteral("Opção 2"));
        combo->setCurrentIndex(0);
        QVERIFY(!okButton->isEnabled());
    }

    void requiredSelectWithADefaultCountsAsFilledFromTheStart()
    {
        Parameter select;
        select.name = QStringLiteral("env");
        select.type = ParameterType::Select;
        select.options = {QStringLiteral("dev"), QStringLiteral("staging"), QStringLiteral("prod")};
        select.required = true;
        select.defaultValue = QStringLiteral("staging");

        ParameterFormDialog dialog({select}, nullptr);
        QPushButton *okButton = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        auto *combo = dialog.findChild<QComboBox *>();
        QCOMPARE(combo->count(), 3); // sem item "nada escolhido"
        QCOMPARE(combo->currentText(), QStringLiteral("staging"));
        QVERIFY(okButton->isEnabled());
        QVERIFY(dialog.validationProblems().isEmpty());
        QCOMPARE(dialog.values().value(QStringLiteral("env")), QStringLiteral("staging"));
    }

    void requiredSelectRemembersTheLastValueAndIgnoresAStaleOne()
    {
        Parameter select;
        select.name = QStringLiteral("env");
        select.type = ParameterType::Select;
        select.options = {QStringLiteral("dev"), QStringLiteral("prod")};
        select.required = true;

        ParameterFormDialog remembered({select}, nullptr, {{QStringLiteral("env"), QStringLiteral("prod")}});
        QVERIFY(remembered.validationProblems().isEmpty());
        QCOMPARE(remembered.values().value(QStringLiteral("env")), QStringLiteral("prod"));

        // Um último valor que já não é uma opção não conta: pede para escolher.
        ParameterFormDialog stale({select}, nullptr, {{QStringLiteral("env"), QStringLiteral("gone")}});
        QCOMPARE(stale.validationProblems().size(), 1);
    }

    void optionalSelectKeepsPreselectingTheFirstOption()
    {
        Parameter select;
        select.name = QStringLiteral("x");
        select.type = ParameterType::Select;
        select.options = {QStringLiteral("a"), QStringLiteral("b")};

        ParameterFormDialog dialog({select}, nullptr);
        auto *combo = dialog.findChild<QComboBox *>();
        QCOMPARE(combo->count(), 2);
        QCOMPARE(dialog.values().value(QStringLiteral("x")), QStringLiteral("a"));
    }

    // Textarea: InlineCodeField (por baixo um QPlainTextEdit, que o teste encontra via findChild) com o
    // defaultValue multi-linha; cresce conforme linhas são digitadas; values() devolve toPlainText().
    void textareaRendersAsPlainTextEditAndGrowsWithContent()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("descricao");
        p.type = ParameterType::Textarea;
        p.defaultValue = QStringLiteral("linha 1\nlinha 2");
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        // Precisa estar visível: sem largura real de viewport o QPlainTextDocumentLayout não recalcula a altura (falso
        // negativo).
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *field = dialog.findChild<QPlainTextEdit *>();
        QVERIFY(field != nullptr);
        QCOMPARE(field->toPlainText(), QStringLiteral("linha 1\nlinha 2"));

        const int heightBefore = field->height();
        field->setPlainText(field->toPlainText()
            + QStringLiteral("\nlinha 3\nlinha 4\nlinha 5\nlinha 6\nlinha 7\nlinha 8"));
        // textChanged chama setFixedHeight, mas o Qt só aplica o resize no próximo ciclo do event loop
        // (updateGeometry() é assíncrono); sem isso o teste leria a altura antiga.
        QCoreApplication::processEvents();
        QVERIFY2(field->height() > heightBefore,
                 qPrintable(QStringLiteral("altura não cresceu: antes=%1 depois=%2")
                                .arg(heightBefore).arg(field->height())));

        QCOMPARE(dialog.values().value(QStringLiteral("descricao")), field->toPlainText());
    }

    // Json: InlineCodeField editável com realce (setJsonSyntax) seedado com o defaultValue; values() devolve
    // toPlainText(). Reaproveita o widget do Textarea, que traz o botão de expandir.
    void jsonRendersAsInlineCodeFieldAndReadsBackEditedText()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("payload");
        p.type = ParameterType::Json;
        p.defaultValue = QStringLiteral("{\n    \"role\": \"admin\"\n}");
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        auto *field = dialog.findChild<kai::ui::InlineCodeField *>();
        QVERIFY(field != nullptr);
        QVERIFY(!field->editor()->isReadOnly());
        QCOMPARE(field->toPlainText(), p.defaultValue);

        field->setPlainText(QStringLiteral("{\n    \"role\": \"user\"\n}"));
        QCOMPARE(dialog.values().value(QStringLiteral("payload")),
                 QStringLiteral("{\n    \"role\": \"user\"\n}"));
    }

    // Textarea e Json são campos longos: ocupam a linha inteira da grade, sem parear com um compacto (Bool). Ambos
    // são InlineCodeField (um com setJsonSyntax(true)), daí conferir a contagem (2).
    void textareaAndJsonAreFullWidthEvenBesideACompactField()
    {
        QVector<Parameter> params;

        Parameter boolParam;
        boolParam.name = QStringLiteral("force");
        boolParam.type = ParameterType::Bool;
        params << boolParam;

        Parameter textareaParam;
        textareaParam.name = QStringLiteral("descricao");
        textareaParam.type = ParameterType::Textarea;
        params << textareaParam;

        Parameter jsonParam;
        jsonParam.name = QStringLiteral("payload");
        jsonParam.type = ParameterType::Json;
        params << jsonParam;

        ParameterFormDialog dialog(params, nullptr);
        QVERIFY(dialog.findChild<QCheckBox *>() != nullptr);
        QCOMPARE(dialog.findChildren<kai::ui::InlineCodeField *>().size(), 2);
    }

    // Parameter::pickMode == "both": o botão de procurar abre um QMenu (nenhum diálogo nativo escolhe arquivo ou
    // pasta) em vez de um QFileDialog. Fecha o menu via QApplication::activePopupWidget(), só provando que aparece
    // com as duas opções e não crasha.
    void bothPickModeShowsFileOrFolderMenuInsteadOfDialogDirectly()
    {
        QVector<Parameter> params;
        Parameter fileParam;
        fileParam.name = QStringLiteral("caminho");
        fileParam.type = ParameterType::File;
        fileParam.pickMode = QStringLiteral("both");
        params << fileParam;

        ParameterFormDialog dialog(params, nullptr);
        dialog.show();

        auto *browseButton = dialog.findChild<QToolButton *>();
        QVERIFY(browseButton != nullptr);

        int menuActionCount = -1;
        QTimer::singleShot(50, &dialog, [&menuActionCount]() {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (menu) {
                menuActionCount = menu->actions().size();
                menu->close();
            }
        });
        QTest::mouseClick(browseButton, Qt::LeftButton);

        QCOMPARE(menuActionCount, 2);
    }

    // O botão calendário abre o DatePickerDialog; confirmar uma data preenche o campo FORMATADO conforme date_format.
    void datePickerFillsFieldWithFormattedValue()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("quando");
        p.type = ParameterType::Date;
        p.dateMode = QStringLiteral("date");
        p.dateFormat = QStringLiteral("iso_date");
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        dialog.show();

        auto *pickButton = dialog.findChild<QToolButton *>();
        QVERIFY(pickButton != nullptr);

        QTimer::singleShot(50, &dialog, []() {
            auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(picker != nullptr);
            auto *calendar = picker->findChild<QCalendarWidget *>();
            QVERIFY(calendar != nullptr);
            calendar->setSelectedDate(QDate(2024, 3, 20));
            auto *okButton = picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
            QVERIFY(okButton != nullptr);
            QTest::mouseClick(okButton, Qt::LeftButton);
        });
        QTest::mouseClick(pickButton, Qt::LeftButton);

        const QMap<QString, QString> values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("quando")), QStringLiteral("2024-03-20"));
    }

    // Range: {{nome}} carrega o INÍCIO, {{nome.end}} o FIM, nunca o texto "início — fim" de leitura visual.
    void dateRangePickerFillsStartAndEndSeparately()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("janela");
        p.type = ParameterType::Date;
        p.dateMode = QStringLiteral("date");
        p.dateRange = true;
        p.dateFormat = QStringLiteral("iso_date");
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        dialog.show();

        auto *pickButton = dialog.findChild<QToolButton *>();
        QVERIFY(pickButton != nullptr);

        QTimer::singleShot(50, &dialog, []() {
            auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(picker != nullptr);
            const auto calendars = picker->findChildren<QCalendarWidget *>();
            QCOMPARE(calendars.size(), 2);
            calendars.at(0)->setSelectedDate(QDate(2024, 3, 20));
            calendars.at(1)->setSelectedDate(QDate(2024, 3, 25));
            auto *okButton = picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
            QTest::mouseClick(okButton, Qt::LeftButton);
        });
        QTest::mouseClick(pickButton, Qt::LeftButton);

        const QMap<QString, QString> values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("janela")), QStringLiteral("2024-03-20"));
        QCOMPARE(values.value(QStringLiteral("janela.end")), QStringLiteral("2024-03-25"));
    }

    // Cancelar a janelinha não altera o campo (nem crasha).
    void cancelingDatePickerLeavesFieldUnchanged()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("quando");
        p.type = ParameterType::Date;
        params << p;

        QMap<QString, QString> initial;
        initial[QStringLiteral("quando")] = QStringLiteral("2020-01-01");
        ParameterFormDialog dialog(params, nullptr, initial);
        dialog.show();

        auto *pickButton = dialog.findChild<QToolButton *>();
        QVERIFY(pickButton != nullptr);
        QTimer::singleShot(50, &dialog, []() {
            auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(picker != nullptr);
            auto *cancelButton = picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel);
            QTest::mouseClick(cancelButton, Qt::LeftButton);
        });
        QTest::mouseClick(pickButton, Qt::LeftButton);

        QCOMPARE(dialog.values().value(QStringLiteral("quando")), QStringLiteral("2020-01-01"));
    }

    void scriptSelectAsksOwnerForOptionsAndFillsComboFromTheAnswer()
    {
        Parameter p;
        p.name = QStringLiteral("EXT");
        p.type = ParameterType::Select;
        p.optionsScript = QStringLiteral("ls extensions");

        ParameterFormDialog dialog({p}, nullptr);
        QSignalSpy spy(&dialog, &ParameterFormDialog::optionsScriptRequested);
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("EXT"));
        QCOMPARE(spy.first().at(1).toString(), QStringLiteral("ls extensions"));

        // Até o script responder o campo existe, mas não tem valor nem escolha possível.
        QCOMPARE(dialog.values().value(QStringLiteral("EXT")), QString());

        dialog.setScriptOptions(QStringLiteral("EXT"),
            {QStringLiteral("ctx"), QString(), QStringLiteral("Git PR:pr"), QStringLiteral("  sync  ")}, QString());
        QComboBox *combo = nullptr;
        for (QComboBox *c : dialog.findChildren<QComboBox *>()) {
            if (c->isEnabled() && c->count() == 3) { combo = c; }
        }
        QVERIFY(combo != nullptr);
        combo->setCurrentIndex(combo->findText(QStringLiteral("Git PR")));
        QCOMPARE(dialog.values().value(QStringLiteral("EXT")), QStringLiteral("pr"));
    }

    void scriptSelectKeepsLastValueWhenItIsStillAnOption()
    {
        Parameter p;
        p.name = QStringLiteral("EXT");
        p.type = ParameterType::Select;
        p.optionsScript = QStringLiteral("ls");

        ParameterFormDialog dialog({p}, nullptr, {{QStringLiteral("EXT"), QStringLiteral("sync")}});
        dialog.setScriptOptions(QStringLiteral("EXT"), {QStringLiteral("ctx"), QStringLiteral("sync")}, QString());
        QCOMPARE(dialog.values().value(QStringLiteral("EXT")), QStringLiteral("sync"));
    }

    void scriptSelectSupportsMultiSelect()
    {
        Parameter p;
        p.name = QStringLiteral("EXT");
        p.type = ParameterType::Select;
        p.multiSelect = true;
        p.optionsScript = QStringLiteral("ls");

        ParameterFormDialog dialog({p}, nullptr);
        dialog.setScriptOptions(QStringLiteral("EXT"),
            {QStringLiteral("ctx"), QStringLiteral("pr"), QStringLiteral("sync")}, QString());
        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        list->item(0)->setCheckState(Qt::Checked);
        list->item(2)->setCheckState(Qt::Checked);
        QCOMPARE(dialog.values().value(QStringLiteral("EXT")), QStringLiteral("ctx,sync"));
    }

    void scriptFailureShowsTheMessageAndLeavesTheValueEmpty()
    {
        Parameter p;
        p.name = QStringLiteral("EXT");
        p.type = ParameterType::Select;
        p.optionsScript = QStringLiteral("boom");

        ParameterFormDialog dialog({p}, nullptr);
        dialog.show();
        dialog.setScriptOptions(QStringLiteral("EXT"), {}, QStringLiteral("boom: command not found"));
        bool shown = false;
        for (QLabel *label : dialog.findChildren<QLabel *>()) {
            if (label->text().contains(QStringLiteral("command not found")) && label->isVisible()) { shown = true; }
        }
        QVERIFY(shown);
        QCOMPARE(dialog.values().value(QStringLiteral("EXT")), QString());
    }

    void requiredScriptSelectBlocksOkUntilTheUserPicks()
    {
        Parameter p;
        p.name = QStringLiteral("EXT");
        p.type = ParameterType::Select;
        p.required = true;
        p.optionsScript = QStringLiteral("ls");

        ParameterFormDialog dialog({p}, nullptr);
        QVERIFY(!dialog.allRequiredFieldsFilled());
        dialog.setScriptOptions(QStringLiteral("EXT"), {QStringLiteral("ctx"), QStringLiteral("pr")}, QString());
        QVERIFY(!dialog.allRequiredFieldsFilled()); // abre em "nada escolhido"
        QCOMPARE(dialog.values().value(QStringLiteral("EXT")), QString());
        QComboBox *combo = nullptr;
        for (QComboBox *c : dialog.findChildren<QComboBox *>()) {
            if (c->isEnabled() && c->count() == 3) { combo = c; }
        }
        QVERIFY(combo != nullptr);
        combo->setCurrentIndex(1);
        QVERIFY(dialog.allRequiredFieldsFilled());
        QCOMPARE(dialog.values().value(QStringLiteral("EXT")), QStringLiteral("ctx"));
    }
};

QTEST_MAIN(TestParameterFormDialog)
#include "test_parameter_form_dialog.moc"
