#include <QTest>
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>

#include "ui/env-var-autocomplete.h"
#include "ui/inline-code-field.h"

using namespace kai::ui;

// Autocomplete de {{var}} (pedido do usuário: "digitar {{ e já aparece uma
// listinha de opções"). Verifica o CONTRATO do componente reutilizável
// (env-var-autocomplete.h) num QPlainTextEdit real (via InlineCodeField,
// o consumidor real em Command/HTTP) e num QLineEdit real — os dois
// overloads existentes.
class TestEnvVarAutocomplete : public QObject {
    Q_OBJECT

private:
    // Acha o popup (um QWidget top-level, filho de nenhum window de teste,
    // contendo um QListWidget) criado por attachEnvVarAutocomplete.
    static QWidget *findPopup(QWidget *excludeWindow)
    {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (w != excludeWindow && w->findChild<QListWidget *>()) {
                return w;
            }
        }
        return nullptr;
    }

private slots:
    void popupAppearsAfterDoubleBrace()
    {
        auto *field = new InlineCodeField();
        field->show();
        attachEnvVarAutocomplete(field->editor(), []() {
            return QStringList{QStringLiteral("API_URL"), QStringLiteral("API_KEY"), QStringLiteral("USER_ID")};
        });
        field->editor()->setFocus();

        QTest::keyClicks(field->editor(), QStringLiteral("{{"));
        QTest::qWait(30);

        QWidget *popup = findPopup(field);
        QVERIFY(popup != nullptr);
        QVERIFY(popup->isVisible());

        delete field;
    }

    void typingWithoutTriggerNeverShowsPopup()
    {
        auto *field = new InlineCodeField();
        field->show();
        attachEnvVarAutocomplete(field->editor(), []() {
            return QStringList{QStringLiteral("API_URL")};
        });
        field->editor()->setFocus();

        QTest::keyClicks(field->editor(), QStringLiteral("just a normal command"));
        QTest::qWait(30);

        QWidget *popup = findPopup(field);
        QVERIFY(popup == nullptr || !popup->isVisible());

        delete field;
    }

    void filtersAsMoreIsTyped()
    {
        auto *field = new InlineCodeField();
        field->show();
        attachEnvVarAutocomplete(field->editor(), []() {
            return QStringList{QStringLiteral("API_URL"), QStringLiteral("API_KEY"), QStringLiteral("USER_ID")};
        });
        field->editor()->setFocus();

        QTest::keyClicks(field->editor(), QStringLiteral("{{a"));
        QTest::qWait(30);

        QWidget *popup = findPopup(field);
        QVERIFY(popup != nullptr);
        auto *list = popup->findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        // "a" casa (fuzzy, case-insensitive) API_URL e API_KEY, mas não USER_ID.
        QCOMPARE(list->count(), 2);

        // Mais um caractere estreita ainda mais o filtro.
        QTest::keyClicks(field->editor(), QStringLiteral("pi_u"));
        QTest::qWait(30);
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->text(), QStringLiteral("API_URL"));

        delete field;
    }

    void selectingItemInsertsTextAndClosesPopup()
    {
        auto *field = new InlineCodeField();
        field->show();
        attachEnvVarAutocomplete(field->editor(), []() {
            return QStringList{QStringLiteral("API_URL"), QStringLiteral("API_KEY")};
        });
        field->editor()->setFocus();

        QTest::keyClicks(field->editor(), QStringLiteral("curl {{a"));
        QTest::qWait(30);

        QWidget *popup = findPopup(field);
        QVERIFY(popup != nullptr);
        QVERIFY(popup->isVisible());

        // Enter confirma o item atualmente selecionado (o primeiro, por padrão).
        QTest::keyClick(field->editor(), Qt::Key_Return);
        QTest::qWait(30);

        QCOMPARE(field->toPlainText(), QStringLiteral("curl {{API_URL}}"));
        QVERIFY(!popup->isVisible());

        delete field;
    }

    void escapeDismissesWithoutInserting()
    {
        auto *field = new InlineCodeField();
        field->show();
        attachEnvVarAutocomplete(field->editor(), []() {
            return QStringList{QStringLiteral("API_URL")};
        });
        field->editor()->setFocus();

        QTest::keyClicks(field->editor(), QStringLiteral("{{a"));
        QTest::qWait(30);
        QWidget *popup = findPopup(field);
        QVERIFY(popup != nullptr);
        QVERIFY(popup->isVisible());

        QTest::keyClick(field->editor(), Qt::Key_Escape);
        QTest::qWait(30);

        QVERIFY(!popup->isVisible());
        // Nada foi inserido além do que o usuário já tinha digitado.
        QCOMPARE(field->toPlainText(), QStringLiteral("{{a"));

        delete field;
    }

    // Autocomplete de bloco condicional (pedido do usuário: "digita um {% e
    // já sugere os comandos possíveis (if, if else)") — só ativo quando
    // supportConditionals=true.
    void percentTriggerSuggestsConditionalBlocksWhenEnabled()
    {
        auto *field = new InlineCodeField();
        field->show();
        attachEnvVarAutocomplete(field->editor(), []() { return QStringList{}; },
            /*supportConditionals=*/true);
        field->editor()->setFocus();

        QTest::keyClicks(field->editor(), QStringLiteral("{%"));
        QTest::qWait(30);

        QWidget *popup = findPopup(field);
        QVERIFY(popup != nullptr);
        QVERIFY(popup->isVisible());
        auto *list = popup->findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 2); // "if" e "if / else"

        delete field;
    }

    // Sem supportConditionals (default), "{%" nunca abre popup — não muda
    // o comportamento dos campos que já usavam este componente antes.
    void percentTriggerDoesNothingWhenConditionalsDisabled()
    {
        auto *field = new InlineCodeField();
        field->show();
        attachEnvVarAutocomplete(field->editor(), []() { return QStringList{QStringLiteral("X")}; });
        field->editor()->setFocus();

        QTest::keyClicks(field->editor(), QStringLiteral("{%"));
        QTest::qWait(30);

        QWidget *popup = findPopup(field);
        QVERIFY(popup == nullptr || !popup->isVisible());

        delete field;
    }

    // Selecionar "if" insere o snippet completo com o cursor posicionado
    // DENTRO dele (logo após "if "), pronto pra digitar a condição — não no
    // fim do texto inserido, diferente do caso de {{var}}.
    void selectingIfInsertsSnippetWithCursorAtCondition()
    {
        auto *field = new InlineCodeField();
        field->show();
        attachEnvVarAutocomplete(field->editor(), []() { return QStringList{}; },
            /*supportConditionals=*/true);
        field->editor()->setFocus();

        QTest::keyClicks(field->editor(), QStringLiteral("{%"));
        QTest::qWait(30);
        QWidget *popup = findPopup(field);
        QVERIFY(popup != nullptr);

        // Primeiro item da lista é "if" (ver conditionalSnippets()).
        QTest::keyClick(field->editor(), Qt::Key_Return);
        QTest::qWait(30);

        QCOMPARE(field->toPlainText(), QStringLiteral("{% if  %}\n{% endif %}"));
        QCOMPARE(field->editor()->textCursor().position(), 6); // logo após "{% if "
        QVERIFY(!popup->isVisible());

        // A condição pode ser digitada normalmente a partir daqui.
        QTest::keyClicks(field->editor(), QStringLiteral("VAR"));
        QCOMPARE(field->toPlainText(), QStringLiteral("{% if VAR %}\n{% endif %}"));

        delete field;
    }

    // "if / else" (segundo item) insere a variante com bloco else.
    void selectingIfElseInsertsSnippetWithElseBranch()
    {
        auto *field = new InlineCodeField();
        field->show();
        attachEnvVarAutocomplete(field->editor(), []() { return QStringList{}; },
            /*supportConditionals=*/true);
        field->editor()->setFocus();

        QTest::keyClicks(field->editor(), QStringLiteral("{%"));
        QTest::qWait(30);
        QWidget *popup = findPopup(field);
        QVERIFY(popup != nullptr);
        auto *list = popup->findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        list->setCurrentRow(1); // "if / else"

        QTest::keyClick(field->editor(), Qt::Key_Return);
        QTest::qWait(30);

        QCOMPARE(field->toPlainText(),
            QStringLiteral("{% if  %}\n\n{% else %}\n\n{% endif %}"));

        delete field;
    }

    // Mesmo contrato no overload de QLineEdit (usado em URL HTTP / Default
    // de parâmetro), não só no QPlainTextEdit do InlineCodeField.
    void lineEditOverloadWorksTheSameWay()
    {
        auto *field = new QLineEdit();
        field->show();
        attachEnvVarAutocomplete(field, []() {
            return QStringList{QStringLiteral("API_URL"), QStringLiteral("API_KEY")};
        });
        field->setFocus();

        QTest::keyClicks(field, QStringLiteral("https://{{a"));
        QTest::qWait(30);

        QWidget *popup = findPopup(field);
        QVERIFY(popup != nullptr);
        QVERIFY(popup->isVisible());

        QTest::keyClick(field, Qt::Key_Return);
        QTest::qWait(30);

        QCOMPARE(field->text(), QStringLiteral("https://{{API_URL}}"));
        QVERIFY(!popup->isVisible());

        delete field;
    }
};

QTEST_MAIN(TestEnvVarAutocomplete)
#include "test_env_var_autocomplete.moc"
