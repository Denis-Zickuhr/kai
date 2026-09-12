#include <QTest>

#include "ui/command-editor-dialog.h"
#include "ui/inline-code-field.h"

using namespace kai::ui;

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
};

QTEST_MAIN(TestCommandEditorDialog)
#include "test_command_editor_dialog.moc"
