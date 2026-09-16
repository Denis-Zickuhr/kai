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

#include "ui/parameter-form-dialog.h"
#include "ui/inline-code-field.h"
#include "ui/collapsible-section-card.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Reprodução real do bug relatado pelo usuário: "ainda crasha ao tentar
// preencher o dado de um parâmetro, erro: Segmentation fault (core
// dumped)". Simula o fluxo completo do ParameterFormDialog (o diálogo real
// de preenchimento de valores antes da execução): cria um
// Command com parâmetros de cada tipo, abre o diálogo, digita valores via
// QTest::keyClicks (evento de teclado real, não setText direto) e chama
// values(). Se houver uso-após-destruição, ponteiro nulo ou crash de
// reentrância em qualquer parte do fluxo, este teste falha com segfault.
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

    void fillingTextParameterWithSpecialCharactersDoesNotCrash()
    {
        // Reprodução direcionada do bug de teclado relatado junto: tecla
        // '/' e '\'' viravam acentos (dead keys) em builds anteriores.
        // Ainda que a causa raiz seja de ambiente (Wayland/WSLg IM
        // module), o preenchimento desses caracteres não deve crashar o
        // Kai independentemente do resultado visual do dead-key.
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

    // Item 3 (seletor CSV de opções): options no formato "rótulo:valor"
    // devem EXIBIR o rótulo mas INJETAR o valor. Ex: uma opção
    // "Produção:prod" mostra "Produção" no combo e injeta "prod".
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
        // Exibe os rótulos, não os valores.
        QCOMPARE(field->itemText(0), QStringLiteral("Produção"));
        QCOMPARE(field->itemText(1), QStringLiteral("Qualidade"));

        field->setCurrentIndex(0);
        // Injeta o VALOR correspondente, não o rótulo.
        QCOMPARE(dialog.values().value(QStringLiteral("cliente")), QStringLiteral("prod"));

        field->setCurrentIndex(1);
        QCOMPARE(dialog.values().value(QStringLiteral("cliente")), QStringLiteral("qa"));
    }

    // Retrocompat: options simples (só "valor", sem ":") continuam
    // exibindo e injetando o próprio valor.
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

    // Item 12: as opções do Select são reordenadas pelo histórico de uso
    // (valores mais recentes no topo). Aqui "prod" e "qa" foram usados por
    // último, então devem aparecer antes de "dev" (que estava primeiro na
    // definição original).
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
        // Ordem esperada: prod, qa (do histórico) e depois dev, staging
        // (ordem original preservada por stable_sort).
        QCOMPARE(field->itemData(0).toString(), QStringLiteral("prod"));
        QCOMPARE(field->itemData(1).toString(), QStringLiteral("qa"));
        QCOMPARE(field->itemData(2).toString(), QStringLiteral("dev"));
        QCOMPARE(field->itemData(3).toString(), QStringLiteral("staging"));
    }

    // Item 12: o Select é editável (busca) e updatedUsageHistory promove o
    // valor escolhido ao topo do histórico, sem duplicatas.
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
        // Escolhe "staging".
        const int idx = field->findData(QStringLiteral("staging"));
        QVERIFY(idx >= 0);
        field->setCurrentIndex(idx);

        const QStringList updated = dialog.updatedUsageHistory().value(QStringLiteral("env"));
        QVERIFY(!updated.isEmpty());
        QCOMPARE(updated.first(), QStringLiteral("staging")); // recém-escolhido no topo
        QVERIFY(updated.contains(QStringLiteral("prod")));     // histórico antigo preservado
        // Sem duplicatas.
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
        // Reprodução com múltiplos parâmetros simultâneos (cenário mais
        // próximo do relatado pelo usuário: um Command real costuma ter
        // mais de um parâmetro de tipos diferentes).
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
    // (4) Multi-select de opções fixas: renderiza uma lista checkable e junta
    // os valores marcados por vírgula. Pré-marca pelos valores iniciais.
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

    // Pedido do usuário: "faça a injeção dos rótulos do select" — além do
    // valor, o multi-select de opções fixas também injeta os RÓTULOS
    // marcados (CSV) em "<nome>__labels".
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

    // Idem para o select de seleção única: injeta "<nome>__label" com o
    // rótulo exibido, distinto do valor injetado em "<nome>".
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

    // Bug reportado: seleção múltipla (opções fixas OU vinculadas a
    // coleção) salvava o CSV inteiro como UMA entrada de histórico, então
    // nenhum valor individual era reconhecido depois (CollectionSelectorDialog
    // e a ordenação por uso comparam ids individuais). updatedUsageHistory
    // deve gravar cada valor escolhido como entrada separada.
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
        // Cada valor escolhido vira uma entrada INDIVIDUAL, não o CSV inteiro.
        QVERIFY(!updated.contains(QStringLiteral("dev,prod")));
        QVERIFY(updated.contains(QStringLiteral("dev")));
        QVERIFY(updated.contains(QStringLiteral("prod")));
        QCOMPARE(updated.count(), 2);
    }

    // Feature pedida pelo usuário: "possibilidade de criar grupo de dados,
    // o que irá começar colapsados". Parâmetros com o mesmo `group` (não
    // vazio) caem dentro de um ÚNICO CollapsibleSectionCard nomeado com o
    // grupo, colapsado por padrão; parâmetros sem grupo continuam soltos
    // no form (fora de qualquer card).
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
        QVERIFY(!card->isExpanded()); // colapsado por padrão

        // Os dois campos do grupo estão DENTRO do card; o campo solto está
        // fora (é filho do diálogo, não do card).
        const auto fieldsInCard = card->findChildren<QLineEdit *>();
        QCOMPARE(fieldsInCard.size(), 2);

        auto *soltoField = dialog.findChild<QLineEdit *>();
        QVERIFY(soltoField != nullptr);
        QVERIFY(card->findChildren<QLineEdit *>().indexOf(soltoField) < 0);
    }

    // Textarea (novo tipo, pedido do usuário: "campo de texto com
    // expansão"): renderiza um InlineCodeField (por baixo, um QPlainTextEdit
    // real — é ele que este teste encontra via findChild<QPlainTextEdit*>)
    // seedado com o defaultValue multi-linha, cresce de altura conforme
    // mais linhas são digitadas, e values() devolve o texto completo via
    // toPlainText().
    void textareaRendersAsPlainTextEditAndGrowsWithContent()
    {
        QVector<Parameter> params;
        Parameter p;
        p.name = QStringLiteral("descricao");
        p.type = ParameterType::Textarea;
        p.defaultValue = QStringLiteral("linha 1\nlinha 2");
        params << p;

        ParameterFormDialog dialog(params, nullptr);
        // PRECISA estar visível: sem largura real de viewport (widget
        // nunca mostrado, width() == 0), o QPlainTextDocumentLayout não
        // recalcula a altura de blocos em modo WidgetWidth — reproduziria
        // um falso-negativo aqui, não um bug real do mecanismo de expansão.
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *field = dialog.findChild<QPlainTextEdit *>();
        QVERIFY(field != nullptr);
        QCOMPARE(field->toPlainText(), QStringLiteral("linha 1\nlinha 2"));

        const int heightBefore = field->height();
        field->setPlainText(field->toPlainText()
            + QStringLiteral("\nlinha 3\nlinha 4\nlinha 5\nlinha 6\nlinha 7\nlinha 8"));
        // textChanged já disparou setFixedHeight (novo tamanho calculado na
        // hora), mas o Qt só aplica o resize de fato no próximo ciclo do
        // event loop (updateGeometry() é assíncrono) — sem isso o teste
        // pegaria a altura ANTIGA e acusaria um falso-negativo.
        QCoreApplication::processEvents();
        QVERIFY2(field->height() > heightBefore,
                 qPrintable(QStringLiteral("altura não cresceu: antes=%1 depois=%2")
                                .arg(heightBefore).arg(field->height())));

        QCOMPARE(dialog.values().value(QStringLiteral("descricao")), field->toPlainText());
    }

    // Json (novo tipo): renderiza um InlineCodeField (mini editor JSON, NÃO
    // read-only, com realce de sintaxe via setJsonSyntax) seedado com o
    // defaultValue, e values() devolve o texto editado via toPlainText().
    // Trocou de FoldableJsonView para InlineCodeField (pedido do usuário:
    // "JSON [...] precisa ter botão pra expandir, além de ter botões de
    // minify e etc." — ver ParameterFormDialog::setupUi, case Json) para
    // ganhar o botão de expandir de graça, mesmo widget reaproveitado pelo
    // Textarea logo abaixo.
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

    // Grade de layout: Textarea e Json são campos LONGOS (largura cheia),
    // não devem parear lado a lado com um campo compacto (Bool) — ambos
    // ocupam a linha inteira da grade mesmo estando junto de um compacto.
    // Ambos renderizam como InlineCodeField agora (mesmo widget, um com
    // setJsonSyntax(true) — ver setupUi), daí conferir a CONTAGEM (2) em
    // vez de tipos C++ diferentes por parâmetro.
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

    // REGRESSÃO/feature (pedido do usuário: "não gostei da cfg pick as
    // folder, queria tipo um select com modo de seleção... arquivo, pastas
    // ou ambos"). Parameter::pickMode == "both": clicar no botão de
    // procurar não abre um QFileDialog direto (nenhum diálogo nativo deixa
    // escolher arquivo OU pasta ao mesmo tempo) - abre um QMenu perguntando
    // qual dos dois. Fecha o menu programaticamente (QApplication::
    // activePopupWidget(), mesmo padrão já usado neste app pra testar
    // QMessageBox::activeModalWidget()) sem escolher nada, só provando que
    // o menu realmente aparece com as duas opções e não crasha.
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

    // Pedido do usuário ("adicione um parâmetro do tipo date picker...
    // esse param abre uma janelinha de pedir data"): o botão calendário
    // abre DatePickerDialog de verdade; escolher uma data e confirmar
    // preenche o campo com o valor FORMATADO conforme date_format — não o
    // texto cru do QDateTime.
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

    // Range: {{nome}} carrega o INÍCIO, {{nome.end}} o FIM — nunca o texto
    // combinado "início — fim" que só existe pra leitura visual do campo.
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
};

QTEST_MAIN(TestParameterFormDialog)
#include "test_parameter_form_dialog.moc"
