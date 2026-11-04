#include <QTest>
#include "core/models.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QToolButton>
#include <QComboBox>
#include <QApplication>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>

#include "ui/shared/key-value-editor-widget.h"
#include "ui/features/output/terminal-profiles-editor-widget.h"
#include "ui/features/command-editor/parameter-editor-widget.h"
#include "ui/shared/table-utils.h"

using namespace kai::ui;

// Uma coluna de seleção (checkbox) foi inserida na posição 0, deslocando as outras em +1. Como os editores usavam
// índices crus, um deslocamento incompleto leria/gravaria na coluna errada em silêncio. Estes testes fazem o
// round-trip pelo widget (set -> get).
class TestTableWidgets : public QObject {
    Q_OBJECT
private slots:
    void keyValueRoundTripSurvivesSelectionColumn()
    {
        KeyValueEditorWidget editor(nullptr, QStringLiteral("Chave"), QStringLiteral("Valor"));

        QMap<QString, QString> values;
        values.insert(QStringLiteral("API_BASE"), QStringLiteral("http://localhost:3000"));
        values.insert(QStringLiteral("TOKEN"), QStringLiteral("abc123"));
        editor.setValues(values);

        const QMap<QString, QString> back = editor.values();
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.value(QStringLiteral("API_BASE")), QStringLiteral("http://localhost:3000"));
        QCOMPARE(back.value(QStringLiteral("TOKEN")), QStringLiteral("abc123"));
    }

    // Ícones inline de lápis/lixeira por linha (padrão de ParameterEditorWidget/OutputRespondersEditorWidget);
    // "adicionar" fica no botão "+" do widget.
    void keyValueRowHasInlineActions()
    {
        KeyValueEditorWidget editor(nullptr, QStringLiteral("Chave"), QStringLiteral("Valor"));
        QMap<QString, QString> values;
        values.insert(QStringLiteral("A"), QStringLiteral("1"));
        editor.setValues(values);

        auto *table = editor.findChild<QTableWidget *>();
        QVERIFY(table != nullptr);
        QVERIFY(table->rowCount() >= 1);
        QWidget *actionsCell = table->cellWidget(0, table->columnCount() - 1);
        QVERIFY(actionsCell != nullptr);
        QCOMPARE(actionsCell->findChildren<QToolButton *>().size(), 2);
    }

    void terminalProfilesRoundTripSurvivesSelectionColumn()
    {
        TerminalProfilesEditorWidget editor;

        QVector<kai::core::TerminalProfile> targets;
        kai::core::TerminalProfile wsl;
        wsl.name = QStringLiteral("WSL");
        wsl.commandTemplate = QStringLiteral("wsl.exe -- bash -lc '{{command}}'");
        wsl.usePty = true;
        wsl.isDefault = true;
        targets.append(wsl);
        kai::core::TerminalProfile local;
        local.name = QStringLiteral("Local");
        local.commandTemplate = QStringLiteral("bash -lc '{{command}}'");
        local.usePty = false;
        local.isDefault = false;
        targets.append(local);
        editor.setTargets(targets);

        const QVector<kai::core::TerminalProfile> back = editor.targets();
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.at(0).name, QStringLiteral("WSL"));
        QCOMPARE(back.at(0).commandTemplate, QStringLiteral("wsl.exe -- bash -lc '{{command}}'"));
        QVERIFY2(back.at(0).usePty, "flag TTY perdida ou lida da coluna errada");
        QVERIFY2(back.at(0).isDefault, "flag Padrão perdida ou lida da coluna errada");
        QCOMPARE(back.at(1).name, QStringLiteral("Local"));
        QVERIFY(!back.at(1).usePty);
        QVERIFY(!back.at(1).isDefault);
    }

    // Anti-corte: controles embutidos (checkbox de seleção, estrela de favorito) apareciam cortados quando o
    // minimumSizeHint do widget era maior que a altura da linha ou a largura da coluna. O teste mede e falha com os
    // números.
    void embeddedWidgetsFitInsideRow()
    {
        QTableWidget table;
        table.setColumnCount(1);
        table.setRowCount(1);
        QWidget *host = makeSelectionCheckbox(&table);
        QVERIFY(host != nullptr);
        table.setCellWidget(0, 0, host);

        const int rowHeight = standardRowHeight();
        const int columnWidth = selectionColumnWidth();
        const QSize needed = host->minimumSizeHint();

        QVERIFY2(needed.height() <= rowHeight,
                 qPrintable(QStringLiteral("checkbox precisa de %1px de altura, linha tem %2px")
                                .arg(needed.height()).arg(rowHeight)));
        QVERIFY2(needed.width() <= columnWidth,
                 qPrintable(QStringLiteral("checkbox precisa de %1px de largura, coluna tem %2px")
                                .arg(needed.width()).arg(columnWidth)));

        auto *edit = new QLineEdit();
        QWidget *editHost = cellHost(edit, &table);
        QVERIFY2(editHost->minimumSizeHint().height() <= rowHeight,
                 qPrintable(QStringLiteral("editor precisa de %1px, linha tem %2px")
                                .arg(editHost->minimumSizeHint().height()).arg(rowHeight)));
    }

    // Regressão crítica: ao envolver os editores no cellHost, os qobject_cast<QLineEdit*>(cellWidget(...)) de
    // parameters() passaram a receber o host (nullptr), e como parâmetro sem nome é descartado, todos os parâmetros
    // eram apagados ao salvar. Só o round-trip pelo widget pega isso.
    void parameterEditorRoundTrip()
    {
        ParameterEditorWidget editor;

        QVector<kai::core::Parameter> params;
        kai::core::Parameter texto;
        texto.name = QStringLiteral("cliente");
        texto.label = QStringLiteral("Cliente");
        texto.type = kai::core::ParameterType::Text;
        texto.defaultValue = QStringLiteral("acme-corp");
        params.append(texto);

        kai::core::Parameter arquivo;
        arquivo.name = QStringLiteral("file");
        arquivo.label = QStringLiteral("Arquivo");
        arquivo.type = kai::core::ParameterType::File;
        arquivo.defaultValue = QStringLiteral("Todos");
        params.append(arquivo);

        kai::core::Parameter selecao;
        selecao.name = QStringLiteral("modo");
        selecao.type = kai::core::ParameterType::Select;
        selecao.options = QStringList{QStringLiteral("dev"), QStringLiteral("prod")};
        params.append(selecao);

        editor.setParameters(params);
        const QVector<kai::core::Parameter> back = editor.parameters();

        QCOMPARE(back.size(), 3);

        QCOMPARE(back.at(0).name, QStringLiteral("cliente"));
        QCOMPARE(back.at(0).label, QStringLiteral("Cliente"));
        QCOMPARE(back.at(0).defaultValue, QStringLiteral("acme-corp"));
        QCOMPARE(back.at(0).type, kai::core::ParameterType::Text);

        QCOMPARE(back.at(1).name, QStringLiteral("file"));
        QCOMPARE(back.at(1).type, kai::core::ParameterType::File);

        QCOMPARE(back.at(2).name, QStringLiteral("modo"));
        QCOMPARE(back.at(2).type, kai::core::ParameterType::Select);
        QCOMPARE(back.at(2).options, QStringList({QStringLiteral("dev"), QStringLiteral("prod")}));
    }

    // Tipos "text area" e "json": round-trip pelo ParameterEditorWidget e persistência em disco via
    // Parameter::toJson/fromJson, incluindo defaultValue multi-linha (o caso da Textarea).
    void textareaAndJsonParameterTypesRoundTripThroughJson()
    {
        kai::core::Parameter textarea;
        textarea.name = QStringLiteral("descricao");
        textarea.label = QStringLiteral("Descrição");
        textarea.type = kai::core::ParameterType::Textarea;
        textarea.defaultValue = QStringLiteral("linha 1\nlinha 2\nlinha 3");

        const kai::core::Parameter textareaBack = kai::core::Parameter::fromJson(textarea.toJson());
        QCOMPARE(textareaBack.type, kai::core::ParameterType::Textarea);
        QCOMPARE(textareaBack.defaultValue, textarea.defaultValue);
        QCOMPARE(kai::core::parameterTypeToString(kai::core::ParameterType::Textarea), QStringLiteral("textarea"));

        kai::core::Parameter json;
        json.name = QStringLiteral("payload");
        json.label = QStringLiteral("Payload");
        json.type = kai::core::ParameterType::Json;
        json.defaultValue = QStringLiteral("{\n    \"role\": \"admin\"\n}");

        const kai::core::Parameter jsonBack = kai::core::Parameter::fromJson(json.toJson());
        QCOMPARE(jsonBack.type, kai::core::ParameterType::Json);
        QCOMPARE(jsonBack.defaultValue, json.defaultValue);
        QCOMPARE(kai::core::parameterTypeToString(kai::core::ParameterType::Json), QStringLiteral("json"));

        // Também sobrevivem ao round-trip pelo ParameterEditorWidget.
        ParameterEditorWidget editor;
        editor.setParameters({textarea, json});
        const QVector<kai::core::Parameter> back = editor.parameters();
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.at(0).type, kai::core::ParameterType::Textarea);
        QCOMPARE(back.at(1).type, kai::core::ParameterType::Json);
    }

    // Tipo "date" (modo/range/formato/template): round-trip via JSON e via ParameterEditorWidget.
    void dateParameterTypeRoundTripsThroughJsonAndWidget()
    {
        kai::core::Parameter date;
        date.name = QStringLiteral("janela");
        date.label = QStringLiteral("Janela de deploy");
        date.type = kai::core::ParameterType::Date;
        date.dateMode = QStringLiteral("datetime");
        date.dateRange = true;
        date.dateFormat = QStringLiteral("custom");
        date.dateFormatCustom = QStringLiteral("dd.MM.yy HH:mm");

        const kai::core::Parameter back = kai::core::Parameter::fromJson(date.toJson());
        QCOMPARE(back.type, kai::core::ParameterType::Date);
        QCOMPARE(back.dateMode, QStringLiteral("datetime"));
        QCOMPARE(back.dateRange, true);
        QCOMPARE(back.dateFormat, QStringLiteral("custom"));
        QCOMPARE(back.dateFormatCustom, QStringLiteral("dd.MM.yy HH:mm"));
        QCOMPARE(kai::core::parameterTypeToString(kai::core::ParameterType::Date), QStringLiteral("date"));

        // Defaults (campo ausente no JSON) continuam retrocompatíveis.
        QJsonObject bare;
        bare["name"] = QStringLiteral("x");
        bare["type"] = QStringLiteral("date");
        const kai::core::Parameter bareBack = kai::core::Parameter::fromJson(bare);
        QCOMPARE(bareBack.dateMode, QStringLiteral("date"));
        QCOMPARE(bareBack.dateRange, false);
        QCOMPARE(bareBack.dateFormat, QStringLiteral("iso_date"));
        QVERIFY(bareBack.dateFormatCustom.isEmpty());

        ParameterEditorWidget editor;
        editor.setParameters({date});
        const QVector<kai::core::Parameter> widgetBack = editor.parameters();
        QCOMPARE(widgetBack.size(), 1);
        QCOMPARE(widgetBack.at(0).type, kai::core::ParameterType::Date);
        QCOMPARE(widgetBack.at(0).dateMode, QStringLiteral("datetime"));
        QCOMPARE(widgetBack.at(0).dateRange, true);
        QCOMPARE(widgetBack.at(0).dateFormatCustom, QStringLiteral("dd.MM.yy HH:mm"));
    }

    // "group" opcional: round-trip via JSON e via ParameterEditorWidget.
    void groupFieldRoundTripsThroughJsonAndWidget()
    {
        kai::core::Parameter p;
        p.name = QStringLiteral("timeout");
        p.type = kai::core::ParameterType::Number;
        p.group = QStringLiteral("Avançado");

        const kai::core::Parameter back = kai::core::Parameter::fromJson(p.toJson());
        QCOMPARE(back.group, QStringLiteral("Avançado"));

        // Sem grupo (padrão): não aparece no JSON nem quebra o round-trip.
        kai::core::Parameter noGroup;
        noGroup.name = QStringLiteral("x");
        noGroup.type = kai::core::ParameterType::Text;
        const QJsonObject json = noGroup.toJson();
        QVERIFY(!json.contains(QStringLiteral("group")));
        QVERIFY(kai::core::Parameter::fromJson(json).group.isEmpty());

        ParameterEditorWidget editor;
        editor.setParameters({p});
        const QVector<kai::core::Parameter> widgetBack = editor.parameters();
        QCOMPARE(widgetBack.size(), 1);
        QCOMPARE(widgetBack.at(0).group, QStringLiteral("Avançado"));
    }

    // "required" é opt-in: round-trip via JSON e via ParameterEditorWidget, exportável/importável, default false.
    void requiredFieldRoundTripsThroughJsonAndWidget()
    {
        kai::core::Parameter p;
        p.name = QStringLiteral("ambiente");
        p.type = kai::core::ParameterType::Text;
        p.required = true;

        const kai::core::Parameter back = kai::core::Parameter::fromJson(p.toJson());
        QCOMPARE(back.required, true);
        QVERIFY(p.toJson().contains(QStringLiteral("required")));

        // Sem marcar (padrão): não aparece no JSON.
        kai::core::Parameter noRequired;
        noRequired.name = QStringLiteral("x");
        noRequired.type = kai::core::ParameterType::Text;
        const QJsonObject json = noRequired.toJson();
        QVERIFY(!json.contains(QStringLiteral("required")));
        QVERIFY(!kai::core::Parameter::fromJson(json).required);

        ParameterEditorWidget editor;
        editor.setParameters({p});
        const QVector<kai::core::Parameter> widgetBack = editor.parameters();
        QCOMPARE(widgetBack.size(), 1);
        QCOMPARE(widgetBack.at(0).required, true);
    }

    // O toggle "Marcar como secreta" tinha uma faixa de fundo quadrada atrás (QWidget de embrulho com o fundo do estilo
    // global): tem de ficar direto no formulário.
    void secretToggleHasNoWrapperWidgetBehindIt()
    {
        KeyValueEditorWidget editor(nullptr, QStringLiteral("Chave"), QStringLiteral("Valor"));
        editor.enableSecretColumn(QStringLiteral("Secreta"));
        bool dialogOpened = false;
        bool toggleIsDirectChildOfTheDialog = false; // o diálogo some quando o exec() volta: não guarda ponteiros
        QTimer::singleShot(150, [&]() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialogOpened = true;
            if (auto *toggle = dialog->findChild<QCheckBox *>()) toggleIsDirectChildOfTheDialog = toggle->parentWidget() == dialog;
            dialog->reject();
        });
        editor.handleAddRowClicked();
        QVERIFY(dialogOpened);
        QVERIFY(toggleIsDirectChildOfTheDialog);
    }

    // O campo Grupo do ParameterRowDialog é um combo editável: sugere os grupos usados pelos outros parâmetros do mesmo
    // comando e aceita texto livre para criar um novo. Usa o diálogo modal real (QTimer::singleShot +
    // activeModalWidget, como em test_parameter_form_dialog.cpp).
    void groupComboSuggestsNamesAlreadyUsedByOtherParamsInSameCommand()
    {
        kai::core::Parameter existing;
        existing.name = QStringLiteral("timeout");
        existing.type = kai::core::ParameterType::Number;
        existing.group = QStringLiteral("Avançado");

        ParameterEditorWidget editor;
        editor.setParameters({existing});

        // handleAddRowClicked() abre o ParameterRowDialog modal; captura via activeModalWidget().
        QTimer::singleShot(50, [&editor]() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog != nullptr);

            auto *groupCombo = dialog->findChild<QComboBox *>(QStringLiteral("paramGroupCombo"));
            QVERIFY(groupCombo != nullptr);
            QVERIFY(groupCombo->isEditable());

            QVERIFY(groupCombo->findText(QStringLiteral("Avançado")) >= 0);

            groupCombo->setCurrentText(QStringLiteral("Depuração"));

            auto *nameField = dialog->findChild<QLineEdit *>(QStringLiteral("paramNameField"));
            QVERIFY(nameField != nullptr);
            nameField->setText(QStringLiteral("novo_param"));

            auto *buttonBox = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(buttonBox != nullptr);
            buttonBox->button(QDialogButtonBox::Ok)->click();
        });
        editor.handleAddRowClicked();

        const QVector<kai::core::Parameter> params = editor.parameters();
        QCOMPARE(params.size(), 2);
        QCOMPARE(params.at(1).name, QStringLiteral("novo_param"));
        QCOMPARE(params.at(1).group, QStringLiteral("Depuração"));
    }
};

QTEST_MAIN(TestTableWidgets)
#include "test_table_widgets.moc"
