#include <QTest>
#include <QLineEdit>
#include <QApplication>
#include <QPushButton>
#include <QCheckBox>
#include <QListWidget>
#include <QStackedWidget>

#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "ui/shared/inline-code-field.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using namespace kai::core;

class TestCommandEditorDialog : public QObject {
    Q_OBJECT

private:
    // Troca pra aba "Configuração" (posição 2 — pedido do usuário: "tira a
    // aba de configs do botão, vai virar outra aba agora") pelo item do
    // nav lateral, igual um clique de verdade do usuário faria.
    static void selectConfigurationTab(CommandEditorDialog &dialog)
    {
        auto *nav = dialog.findChild<QListWidget *>();
        QVERIFY(nav);
        for (int i = 0; i < nav->count(); ++i) {
            if (nav->item(i)->text() == kai::utils::tr(QStringLiteral("command.tab.configuration"))) {
                nav->setCurrentRow(i);
                return;
            }
        }
        QFAIL("Aba 'Configuração' não encontrada no nav lateral");
    }

private slots:
    // A aba "Configuração" (era um popup "Configurações Avançadas" — ver
    // buildConfigurationTab, antes handleAdvancedSettingsClicked) abriga
    // agora o campo "Detalhamento" (m_descriptionField) junto de working
    // dir/flags — nenhum hide() manual é mais necessário, o próprio
    // QStackedWidget do sidebar cuida de mostrar só a página atual.
    // Confirma que o campo existe DENTRO dessa aba especificamente (não
    // solto em outro lugar) e que selecionar a aba a traz pra
    // m_sidePages->currentWidget().
    void descriptionFieldLivesInsideConfigurationTab()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);

        // findChild<InlineCodeField*>() sem nome pegaria o PRIMEIRO
        // encontrado na árvore — que seria m_commandField (aba "Geral"),
        // não m_descriptionField.
        auto *descriptionField = dialog.findChild<InlineCodeField *>(QStringLiteral("descriptionField"));
        QVERIFY(descriptionField);

        // objectName específico: o diálogo também tem OUTRO QStackedWidget
        // (Shell/HTTP, m_tabWidget) — findChild sem nome pegaria o
        // primeiro achado, ambíguo.
        auto *stack = dialog.findChild<QStackedWidget *>(QStringLiteral("sidebarTabsPages"));
        QVERIFY(stack);
        QVERIFY2(!descriptionField->isAncestorOf(stack->currentWidget())
                && !stack->currentWidget()->isAncestorOf(descriptionField),
            "Detalhamento não deveria estar na aba inicial ('Geral')");

        selectConfigurationTab(dialog);
        QVERIFY2(stack->currentWidget()->isAncestorOf(descriptionField),
            "Selecionar a aba 'Configuração' deveria trazer o Detalhamento pra página atual");
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

    // Contador no ícone da aba (pedido do usuário: "adicione contador ao
    // icone da abinha na tela de pastas e cmds") — abas com uma LISTA por
    // trás (Headers, Parâmetros...) ganham "(N)" no rótulo do nav quando
    // N>0; sem itens, o rótulo fica limpo (sem "(0)" à toa).
    void navItemShowsCountSuffixOnlyWhenNonEmpty()
    {
        Command existingHttp;
        existingHttp.id = QStringLiteral("c1");
        existingHttp.name = QStringLiteral("API");
        existingHttp.type = CommandType::Http;
        HttpConfig cfg;
        cfg.url = QStringLiteral("https://x/api");
        cfg.headers = {{QStringLiteral("Authorization"), QStringLiteral("Bearer x")},
                       {QStringLiteral("Accept"), QStringLiteral("application/json")}};
        existingHttp.httpConfig = cfg;

        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &existingHttp);
        auto *nav = dialog.findChild<QListWidget *>();
        QVERIFY(nav);

        bool foundHeaders = false;
        bool foundParams = false;
        for (int i = 0; i < nav->count(); ++i) {
            const QString text = nav->item(i)->text();
            if (text.startsWith(QStringLiteral("Headers"))) {
                foundHeaders = true;
                QCOMPARE(text, QStringLiteral("Headers (2)"));
            }
            if (text.startsWith(QStringLiteral("Parameters"))) {
                foundParams = true;
                // Sem parâmetros neste comando: rótulo limpo, sem "(0)".
                QCOMPARE(text, QStringLiteral("Parameters"));
            }
        }
        QVERIFY2(foundHeaders, "Aba 'Headers' não encontrada no nav");
        QVERIFY2(foundParams, "Aba 'Parameters' não encontrada no nav");
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

    // Antiga tela "Configurações Avançadas" virou a aba "Configuração"
    // (pedido do usuário: "tira a aba de configs do botão, vai virar
    // outra aba agora"). Sem popup, não há mais distinção Salvar/Cancelar
    // pra estes campos — são editados AO VIVO, direto nos membros de
    // verdade, e buildCommand() já reflete qualquer mudança na hora,
    // igual a qualquer outro campo do resto do diálogo.
    void configurationTabFlagsAndCliPathReflectLiveInBuildCommand()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        selectConfigurationTab(dialog);

        auto *backgroundField = dialog.findChild<QCheckBox *>(
            QStringLiteral("adv_command.field.background"));
        QVERIFY(backgroundField);
        backgroundField->setChecked(true);

        auto *cliPathField = dialog.findChild<QLineEdit *>(QStringLiteral("cliPathField"));
        QVERIFY(cliPathField);
        cliPathField->setText(QStringLiteral("api"));

        const Command built = dialog.buildCommand();
        QVERIFY(built.isBackground);
        QCOMPARE(built.cliPath, QStringLiteral("api"));
    }

    // HTTP: a aba "Configuração" mostra SÓ o card de CLI Path (pedido do
    // usuário: "com os extras de HTTP lá, acho que só o de caminho por
    // hora") — o container com working dir/flags do Shell some inteiro.
    void configurationTabInHttpModeHidesShellOnlyContainer()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        selectConfigurationTab(dialog);

        auto *cliPathField = dialog.findChild<QLineEdit *>(QStringLiteral("cliPathField"));
        QVERIFY(cliPathField);
        QVERIFY2(!cliPathField->isHidden(), "CLI Path deveria continuar visível em HTTP também");

        // isHidden() só reflete o flag EXPLÍCITO do PRÓPRIO widget, não de
        // um ancestral — checa o container (o que de fato recebe
        // setVisible() em setExecutionMode()), não um campo várias
        // camadas abaixo dele (que nunca é escondido diretamente).
        auto *shellConfigContainer = dialog.findChild<QWidget *>(QStringLiteral("shellConfigContainer"));
        QVERIFY(shellConfigContainer);
        QVERIFY2(!shellConfigContainer->isHidden(), "Flags de Shell deveriam estar visíveis em modo Shell");

        for (auto *b : dialog.findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("HTTP")) {
                b->click();
                break;
            }
        }

        QVERIFY2(!cliPathField->isHidden(), "CLI Path deveria continuar visível em HTTP");
        QVERIFY2(shellConfigContainer->isHidden(),
            "Flags de Shell (ex: 'Executar em background') não deveriam aparecer em modo HTTP");
    }
};

QTEST_MAIN(TestCommandEditorDialog)
#include "test_command_editor_dialog.moc"
