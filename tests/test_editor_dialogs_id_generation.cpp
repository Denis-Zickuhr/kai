#include <QTest>

#include "ui/folder-editor-dialog.h"
#include "ui/command-editor-dialog.h"
#include "ui/name-uniqueness.h"

using namespace kai::ui;

// Testa a lógica pura de geração de ids dos novos editores de Pasta/Comando
// (lacuna de UX corrigida), sem depender de exibição de diálogo.
class TestEditorDialogsIdGeneration : public QObject {
    Q_OBJECT

private slots:
    void folderIdIsSlugifiedFromName()
    {
        QCOMPARE(FolderEditorDialog::generateFolderId(QStringLiteral("Minha Pasta")), QStringLiteral("f_custom_minha_pasta"));
    }

    void folderIdHandlesSpecialCharacters()
    {
        QCOMPARE(FolderEditorDialog::generateFolderId(QStringLiteral("API's & Cia!")), QStringLiteral("f_custom_api_s_cia_"));
    }

    void folderIdFallsBackWhenNameIsEmpty()
    {
        QCOMPARE(FolderEditorDialog::generateFolderId(QString()), QStringLiteral("f_custom_sem_nome"));
    }

    void commandIdIncludesFolderIdAndSlugifiedName()
    {
        const QString id = CommandEditorDialog::generateCommandId(QStringLiteral("f_custom_demo"), QStringLiteral("Dev Server"));
        QCOMPARE(id, QStringLiteral("c_f_custom_demo_dev_server"));
    }

    void commandIdFallsBackWhenNameIsEmpty()
    {
        const QString id = CommandEditorDialog::generateCommandId(QStringLiteral("f_x"), QString());
        QCOMPARE(id, QStringLiteral("c_f_x_sem_nome"));
    }

    void commandIdIsCaseInsensitive()
    {
        const QString id = CommandEditorDialog::generateCommandId(QStringLiteral("f_x"), QStringLiteral("BUILD RELEASE"));
        QCOMPARE(id, QStringLiteral("c_f_x_build_release"));
    }

    // --- Bloqueio de nomes duplicados na mesma pasta (feedback do usuário) ---

    void commandNameCollidesWithSiblingInSameFolder()
    {
        kai::core::Command existing;
        existing.id = QStringLiteral("c_a");
        existing.folderId = QStringLiteral("f_1");
        existing.name = QStringLiteral("Build");
        QVector<kai::core::Command> commands{existing};

        QVERIFY(kai::ui::commandNameCollides(commands, QStringLiteral("f_1"), QStringLiteral("Build"), QString()));
        // Espaços nas pontas não escapam a checagem.
        QVERIFY(kai::ui::commandNameCollides(commands, QStringLiteral("f_1"), QStringLiteral("  Build  "), QString()));
    }

    void commandNameAllowedInDifferentFolder()
    {
        kai::core::Command existing;
        existing.id = QStringLiteral("c_a");
        existing.folderId = QStringLiteral("f_1");
        existing.name = QStringLiteral("Build");
        QVector<kai::core::Command> commands{existing};

        QVERIFY(!kai::ui::commandNameCollides(commands, QStringLiteral("f_2"), QStringLiteral("Build"), QString()));
    }

    void commandNameEditingSelfIsNotACollision()
    {
        kai::core::Command existing;
        existing.id = QStringLiteral("c_a");
        existing.folderId = QStringLiteral("f_1");
        existing.name = QStringLiteral("Build");
        QVector<kai::core::Command> commands{existing};

        // Editando o próprio comando e mantendo o mesmo nome: não é duplicata.
        QVERIFY(!kai::ui::commandNameCollides(commands, QStringLiteral("f_1"), QStringLiteral("Build"), QStringLiteral("c_a")));
    }

    void collectionNameCollidesWithSiblingInSameFolder()
    {
        kai::core::Collection existing;
        existing.id = QStringLiteral("col_a");
        existing.folderId = QStringLiteral("f_1");
        existing.name = QStringLiteral("Endpoints");
        QVector<kai::core::Collection> collections{existing};

        QVERIFY(kai::ui::collectionNameCollides(collections, QStringLiteral("f_1"), QStringLiteral("Endpoints"), QString()));
    }

    void collectionNameAllowedInDifferentFolder()
    {
        kai::core::Collection existing;
        existing.id = QStringLiteral("col_a");
        existing.folderId = QStringLiteral("f_1");
        existing.name = QStringLiteral("Endpoints");
        QVector<kai::core::Collection> collections{existing};

        QVERIFY(!kai::ui::collectionNameCollides(collections, QStringLiteral("f_2"), QStringLiteral("Endpoints"), QString()));
    }

    void collectionNameEditingSelfIsNotACollision()
    {
        kai::core::Collection existing;
        existing.id = QStringLiteral("col_a");
        existing.folderId = QStringLiteral("f_1");
        existing.name = QStringLiteral("Endpoints");
        QVector<kai::core::Collection> collections{existing};

        QVERIFY(!kai::ui::collectionNameCollides(collections, QStringLiteral("f_1"), QStringLiteral("Endpoints"), QStringLiteral("col_a")));
    }
};

QTEST_MAIN(TestEditorDialogsIdGeneration)
#include "test_editor_dialogs_id_generation.moc"
