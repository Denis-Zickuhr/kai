#include <QTest>
#include <QLineEdit>
#include <QComboBox>
#include <QCompleter>
#include <QCheckBox>
#include <QListWidget>
#include <QPlainTextEdit>

#include "ui/parameter-form-dialog.h"
#include "ui/inline-code-field.h"
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
};

QTEST_MAIN(TestParameterFormDialog)
#include "test_parameter_form_dialog.moc"
