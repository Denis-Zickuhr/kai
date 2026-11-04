#include <QTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "core/models.h"
#include "ui/features/command-editor/parameter-row-dialog.h"
#include "ui/shared/inline-code-field.h"

using namespace kai::ui;
using namespace kai::core;

namespace {
QPushButton *okButton(QDialog &dialog)
{
    return dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
}

QComboBox *combo(QDialog &dialog, const char *name)
{
    return dialog.findChild<QComboBox *>(QString::fromLatin1(name));
}

QCheckBox *toggle(QDialog &dialog, const char *name)
{
    return dialog.findChild<QCheckBox *>(QString::fromLatin1(name));
}

InlineCodeField *code(QDialog &dialog, const char *name)
{
    return dialog.findChild<InlineCodeField *>(QString::fromLatin1(name));
}

// Algum QLabel visível do diálogo contém o texto?
bool shows(QDialog &dialog, const QString &needle)
{
    for (QLabel *label : dialog.findChildren<QLabel *>()) {
        if (label->isVisibleTo(&dialog) && label->text().contains(needle)) {
            return true;
        }
    }
    return false;
}
} // namespace

// Formulário de criação/edição de parâmetro: seções, campos dinâmicos por tipo/fonte e validação inline.
class TestParameterRowDialog : public QObject {
    Q_OBJECT

private slots:
    void newParameterStartsInvalidWithoutShoutingAtTheUser()
    {
        ParameterRowDialog dialog(Parameter{}, {}, {}, {});
        dialog.show();
        QVERIFY(!dialog.isValid());
        QVERIFY(!okButton(dialog)->isEnabled());
    }

    void validNameEnablesOk()
    {
        ParameterRowDialog dialog(Parameter{}, {}, {}, {});
        auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("paramNameField"));
        QVERIFY(name != nullptr);
        QTest::keyClicks(name, QStringLiteral("ambiente_1"));
        QVERIFY(dialog.isValid());
        QVERIFY(okButton(dialog)->isEnabled());
        QCOMPARE(dialog.result().name, QStringLiteral("ambiente_1"));
    }

    void invalidAndDuplicateNamesAreRejected()
    {
        ParameterRowDialog dialog(Parameter{}, {}, {}, {QStringLiteral("TOKEN")});
        dialog.show();
        auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("paramNameField"));
        QTest::keyClicks(name, QStringLiteral("9abc"));
        QVERIFY(!dialog.isValid());
        QVERIFY(shows(dialog, QStringLiteral("digit")) || shows(dialog, QStringLiteral("dígito")));

        name->clear();
        QTest::keyClicks(name, QStringLiteral("com espaco"));
        QVERIFY(!dialog.isValid());

        name->clear();
        QTest::keyClicks(name, QStringLiteral("TOKEN"));
        QVERIFY(!dialog.isValid());
        QVERIFY(!okButton(dialog)->isEnabled());

        name->clear();
        QTest::keyClicks(name, QStringLiteral("OUTRO"));
        QVERIFY(dialog.isValid());
    }

    void selectFixedListNeedsAtLeastOneOption()
    {
        Parameter p;
        p.name = QStringLiteral("ENV");
        p.type = ParameterType::Select;
        ParameterRowDialog dialog(p, {}, {}, {});
        QVERIFY(!dialog.isValid()); // select sem opções

        code(dialog, "paramOptionsField")->setPlainText(QStringLiteral("dev\nstaging, prod:Produção\n\n"));
        QVERIFY(dialog.isValid());
        const Parameter r = dialog.result();
        QCOMPARE(r.options, (QStringList{QStringLiteral("dev"), QStringLiteral("staging"), QStringLiteral("prod:Produção")}));
        QVERIFY(r.optionsScript.isEmpty());
        QVERIFY(r.collectionId.isEmpty());
    }

    void scriptSourceNeedsAScriptAndOnlyKeepsTheScript()
    {
        Parameter p;
        p.name = QStringLiteral("EXT");
        p.type = ParameterType::Select;
        p.options = {QStringLiteral("a"), QStringLiteral("b")};
        ParameterRowDialog dialog(p, {}, {}, {});
        QVERIFY(dialog.isValid());

        combo(dialog, "paramSourceCombo")->setCurrentIndex(combo(dialog, "paramSourceCombo")->findData(QStringLiteral("script")));
        QVERIFY(!dialog.isValid()); // script vazio
        code(dialog, "paramScriptField")->setPlainText(QStringLiteral("   \n  "));
        QVERIFY(!dialog.isValid()); // só espaços também não vale

        code(dialog, "paramScriptField")->setPlainText(QStringLiteral("for d in */; do\n  echo \"${d%/}\"\ndone"));
        QVERIFY(dialog.isValid());
        const Parameter r = dialog.result();
        QVERIFY(r.options.isEmpty()); // a lista fixa não volta a valer
        QVERIFY(r.optionsScript.contains(QStringLiteral("done")));
        QVERIFY(r.optionsScript.contains(QLatin1Char('\n'))); // multi-linha preservado
    }

    void existingScriptParameterOpensOnTheScriptSource()
    {
        Parameter p;
        p.name = QStringLiteral("EXT");
        p.type = ParameterType::Select;
        p.optionsScript = QStringLiteral("ls extensions");
        p.multiSelect = true;
        ParameterRowDialog dialog(p, {}, {}, {});
        QCOMPARE(combo(dialog, "paramSourceCombo")->currentData().toString(), QStringLiteral("script"));
        QVERIFY(dialog.isValid());
        QVERIFY(dialog.result().multiSelect);
        QCOMPARE(dialog.result().optionsScript, QStringLiteral("ls extensions"));
    }

    void collectionSourceNeedsACollection()
    {
        Collection col;
        col.id = QStringLiteral("c1");
        col.name = QStringLiteral("Clientes");
        CollectionField field;
        field.name = QStringLiteral("nome");
        field.label = QStringLiteral("Nome");
        col.schema = {field};

        Parameter p;
        p.name = QStringLiteral("cliente");
        p.type = ParameterType::Select;
        ParameterRowDialog dialog(p, {col}, {}, {});
        auto *source = combo(dialog, "paramSourceCombo");
        source->setCurrentIndex(source->findData(QStringLiteral("collection")));
        QVERIFY(!dialog.isValid());
        auto *collections = combo(dialog, "paramCollectionCombo");
        collections->setCurrentIndex(collections->findData(QStringLiteral("c1")));
        QVERIFY(dialog.isValid());
        QCOMPARE(dialog.result().collectionId, QStringLiteral("c1"));
        QVERIFY(!dialog.result().multiSelect);
    }

    void numberDefaultMustBeANumber()
    {
        Parameter p;
        p.name = QStringLiteral("porta");
        p.type = ParameterType::Number;
        p.defaultValue = QStringLiteral("80a");
        ParameterRowDialog dialog(p, {}, {}, {});
        QVERIFY(!dialog.isValid());
        dialog.findChild<QLineEdit *>(QStringLiteral("paramDefaultField"))->setText(QStringLiteral("8080"));
        QVERIFY(dialog.isValid());
    }

    void customDateFormatCannotBeEmpty()
    {
        Parameter p;
        p.name = QStringLiteral("quando");
        p.type = ParameterType::Date;
        p.dateFormat = QStringLiteral("custom");
        ParameterRowDialog dialog(p, {}, {}, {});
        QVERIFY(!dialog.isValid());
        dialog.findChild<QLineEdit *>(QStringLiteral("paramDateFormatCustomField"))->setText(QStringLiteral("yyyy/MM/dd"));
        QVERIFY(dialog.isValid());
        QCOMPARE(dialog.result().dateFormatCustom, QStringLiteral("yyyy/MM/dd"));
    }

    void optionalAndRequiredAreMutuallyExclusive()
    {
        Parameter p;
        p.name = QStringLiteral("x");
        p.required = true;
        ParameterRowDialog dialog(p, {}, {}, {});
        auto *required = toggle(dialog, "paramRequiredSwitch");
        auto *optional = toggle(dialog, "paramOptionalSwitch");
        QVERIFY(required->isChecked());
        QVERIFY(required->isEnabled());

        optional->setChecked(true);
        QVERIFY(!required->isEnabled());
        QVERIFY(!required->isChecked());
        const Parameter r = dialog.result();
        QVERIFY(r.optional);
        QVERIFY(!r.required);
    }

    void typeSpecificSectionOnlyShowsWhenTheTypeHasSettings()
    {
        Parameter p;
        p.name = QStringLiteral("x");
        p.type = ParameterType::Text;
        ParameterRowDialog dialog(p, {}, {}, {});
        dialog.show();
        auto *type = combo(dialog, "paramTypeCombo");
        auto *scriptField = code(dialog, "paramScriptField");
        QVERIFY(!scriptField->isVisibleTo(&dialog));
        type->setCurrentText(QStringLiteral("select"));
        auto *source = combo(dialog, "paramSourceCombo");
        QVERIFY(source->isVisibleTo(&dialog));
        source->setCurrentIndex(source->findData(QStringLiteral("script")));
        QVERIFY(scriptField->isVisibleTo(&dialog));
        type->setCurrentText(QStringLiteral("bool"));
        QVERIFY(!scriptField->isVisibleTo(&dialog));
        QVERIFY(!source->isVisibleTo(&dialog));
    }
};

QTEST_MAIN(TestParameterRowDialog)
#include "test_parameter_row_dialog.moc"
