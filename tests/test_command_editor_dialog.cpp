#include <QTest>
#include <QLineEdit>

#include "ui/command-editor-dialog.h"
#include "ui/folder-editor-dialog.h"
#include "ui/inline-code-field.h"

using namespace kai::ui;
using namespace kai::core;

// Bug relatado (com foto): o campo "Detalhamento" (m_descriptionField)
// aparecia flutuando, sem posição, por cima da janela principal, tanto em
// comandos Shell quanto HTTP. Causa raiz: buildAdvancedSettingsFields()
// esconde explicitamente os campos que só devem aparecer dentro do popup
// "Configurações Avançadas" (aberto sob demanda) — sem isso, um QWidget
// filho do diálogo principal SEM layout que o gerencie fica visível por
// padrão. O m_descriptionField foi adicionado ao grupo sem entrar nessa
// lista de hide() (o mesmo bug já tinha acontecido antes com
// m_autoRunDelayField, e recorreu aqui).
class TestCommandEditorDialog : public QObject {
    Q_OBJECT

private slots:
    void descriptionFieldStaysHiddenUntilAdvancedSettingsOpened()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);

        auto *descriptionField = dialog.findChild<InlineCodeField *>();
        QVERIFY(descriptionField);
        // isHidden() reflete o estado explícito hide()/show() do PRÓPRIO
        // widget (não depende do diálogo estar de fato exibido na tela via
        // exec()/show()) — é a checagem certa pra pegar esta regressão.
        QVERIFY2(descriptionField->isHidden(),
            "Campo de Detalhamento deveria começar escondido (só aparece dentro "
            "do popup Configurações Avançadas), mas está visível por padrão.");
    }

    // CLI Paths: o campo cli_path do formulário precisa ida e volta —
    // preencher e ler de volta via buildCommand(), e um comando existente
    // com cli_path precisa aparecer já preenchido ao abrir pra editar.
    void cliPathFieldRoundTripsThroughBuildCommand()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        auto *cliPathField = dialog.findChild<QLineEdit *>(QStringLiteral("cliPathField"));
        QVERIFY(cliPathField);
        cliPathField->setText(QStringLiteral("env"));
        QCOMPARE(dialog.buildCommand().cliPath, QStringLiteral("env"));
    }

    void existingCommandCliPathIsPrefilledOnEdit()
    {
        Command existing;
        existing.id = QStringLiteral("c1");
        existing.name = QStringLiteral("Subir ambiente");
        existing.type = CommandType::Shell;
        existing.command = QStringLiteral("up.sh");
        existing.cliPath = QStringLiteral("env");

        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &existing);
        auto *cliPathField = dialog.findChild<QLineEdit *>(QStringLiteral("cliPathField"));
        QVERIFY(cliPathField);
        QCOMPARE(cliPathField->text(), QStringLiteral("env"));
    }

    // Mesmo contrato de cli_path, agora em FolderEditorDialog.
    void folderCliPathFieldRoundTripsThroughBuildFolder()
    {
        FolderEditorDialog dialog({}, nullptr);
        auto *cliPathField = dialog.findChild<QLineEdit *>(QStringLiteral("cliPathField"));
        QVERIFY(cliPathField);
        cliPathField->setText(QStringLiteral("zephyr"));
        QCOMPARE(dialog.buildFolder().cliPath, QStringLiteral("zephyr"));
    }

    void existingFolderCliPathIsPrefilledOnEdit()
    {
        Folder existing;
        existing.id = QStringLiteral("f1");
        existing.name = QStringLiteral("Zaphyr");
        existing.cliPath = QStringLiteral("zephyr");

        FolderEditorDialog dialog({existing}, nullptr, &existing);
        auto *cliPathField = dialog.findChild<QLineEdit *>(QStringLiteral("cliPathField"));
        QVERIFY(cliPathField);
        QCOMPARE(cliPathField->text(), QStringLiteral("zephyr"));
    }
};

QTEST_MAIN(TestCommandEditorDialog)
#include "test_command_editor_dialog.moc"
