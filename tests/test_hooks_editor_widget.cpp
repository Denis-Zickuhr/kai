#include <QTest>
#include <QListWidget>
#include <QLineEdit>

#include "ui/features/command-editor/hooks-editor-widget.h"

using namespace kai::ui;
using namespace kai::core;

// Cobre o item "hooks: por padrão só a pasta, pesquisando mostra tudo"
// (feedback do usuário): setAvailableCommands() alimenta o padrão (busca
// vazia) e setAllAvailableCommands() alimenta o universo completo, usado só
// quando o usuário digita algo na busca.
//
// O widget tem TRÊS QListWidget internos (pre/post/cleanup), todos filhos
// diretos do mesmo QStackedWidget — só a fase ATIVA (padrão: pre-hooks) é
// repopulada pela busca. Como a ordem de QWidget::findChildren() entre eles
// não é algo que a API pública garanta, os testes checam a condição pelo
// conjunto das três listas (helper allListTexts), em vez de assumir qual
// delas o findChild "de primeira" devolve.
namespace {
QStringList allListTexts(HooksEditorWidget &widget)
{
    QStringList texts;
    for (QListWidget *list : widget.findChildren<QListWidget *>()) {
        for (int i = 0; i < list->count(); ++i) {
            texts << list->item(i)->text();
        }
    }
    return texts;
}
}

class TestHooksEditorWidget : public QObject {
    Q_OBJECT

private slots:
    void emptySearchShowsOnlyFolderScopedCandidates()
    {
        HooksEditorWidget widget;

        kai::core::Command sameFolder;
        sameFolder.id = QStringLiteral("c_local");
        sameFolder.name = QStringLiteral("Local Build");
        sameFolder.folderId = QStringLiteral("f_1");

        kai::core::Command otherFolder;
        otherFolder.id = QStringLiteral("c_other");
        otherFolder.name = QStringLiteral("Deploy Prod");
        otherFolder.folderId = QStringLiteral("f_2");

        widget.setAvailableCommands({sameFolder});
        widget.setAllAvailableCommands({sameFolder, otherFolder});

        // Busca vazia (padrão): as 3 fases foram populadas só com o
        // candidato da pasta — "Deploy Prod" (de fora) não aparece em
        // NENHUMA das listas.
        const QStringList texts = allListTexts(widget);
        QVERIFY(texts.contains(QStringLiteral("Local Build")));
        QVERIFY(!texts.contains(QStringLiteral("Deploy Prod")));
    }

    void searchingSurfacesCommandsOutsideTheFolder()
    {
        HooksEditorWidget widget;

        kai::core::Command sameFolder;
        sameFolder.id = QStringLiteral("c_local");
        sameFolder.name = QStringLiteral("Local Build");
        sameFolder.folderId = QStringLiteral("f_1");

        kai::core::Command otherFolder;
        otherFolder.id = QStringLiteral("c_other");
        otherFolder.name = QStringLiteral("Deploy Prod");
        otherFolder.folderId = QStringLiteral("f_2");

        widget.setAvailableCommands({sameFolder});
        widget.setAllAvailableCommands({sameFolder, otherFolder});

        auto *search = widget.findChild<QLineEdit *>();
        QVERIFY(search);
        // Digita algo que só bate no comando de FORA da pasta.
        search->setText(QStringLiteral("Deploy"));

        QVERIFY(allListTexts(widget).contains(QStringLiteral("Deploy Prod")));
    }

    void clearingSearchRevertsToFolderScope()
    {
        HooksEditorWidget widget;

        kai::core::Command sameFolder;
        sameFolder.id = QStringLiteral("c_local");
        sameFolder.name = QStringLiteral("Local Build");
        sameFolder.folderId = QStringLiteral("f_1");

        kai::core::Command otherFolder;
        otherFolder.id = QStringLiteral("c_other");
        otherFolder.name = QStringLiteral("Deploy Prod");
        otherFolder.folderId = QStringLiteral("f_2");

        widget.setAvailableCommands({sameFolder});
        widget.setAllAvailableCommands({sameFolder, otherFolder});

        auto *search = widget.findChild<QLineEdit *>();
        QVERIFY(search);

        search->setText(QStringLiteral("Deploy"));
        QVERIFY(allListTexts(widget).contains(QStringLiteral("Deploy Prod")));

        search->clear();
        // Voltou ao escopo padrão (só a pasta): "Deploy Prod" some de novo.
        QVERIFY(!allListTexts(widget).contains(QStringLiteral("Deploy Prod")));
    }
    // KIP (spec 11 §15): comando KIP não pode ser hook, então o picker nem o oferece
    // — nem na lista da pasta, nem na busca expandida.
    void kipCommandsAreHiddenFromTheHookPicker()
    {
        HooksEditorWidget widget;
        kai::core::Command plain;
        plain.id = QStringLiteral("c_plain");
        plain.name = QStringLiteral("Plain Build");
        plain.folderId = QStringLiteral("f_1");
        kai::core::Command wizard;
        wizard.id = QStringLiteral("c_wizard");
        wizard.name = QStringLiteral("Deploy Wizard");
        wizard.folderId = QStringLiteral("f_1");
        wizard.kip = true;
        kai::core::Command otherWizard = wizard;
        otherWizard.id = QStringLiteral("c_wizard2");
        otherWizard.name = QStringLiteral("Remote Wizard");
        otherWizard.folderId = QStringLiteral("f_2");

        widget.setAvailableCommands({plain, wizard});
        widget.setAllAvailableCommands({plain, wizard, otherWizard});
        QStringList texts = allListTexts(widget);
        QVERIFY(texts.contains(QStringLiteral("Plain Build")));
        QVERIFY(!texts.contains(QStringLiteral("Deploy Wizard")));

        // Busca expandida: também não aparecem.
        auto *search = widget.findChild<QLineEdit *>();
        QVERIFY(search);
        search->setText(QStringLiteral("wizard"));
        texts = allListTexts(widget);
        QVERIFY(!texts.contains(QStringLiteral("Deploy Wizard")));
        QVERIFY(!texts.contains(QStringLiteral("Remote Wizard")));
    }
};

QTEST_MAIN(TestHooksEditorWidget)
#include "test_hooks_editor_widget.moc"
