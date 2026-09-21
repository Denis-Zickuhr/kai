#include <QTest>
#include <QLineEdit>
#include <QApplication>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QListWidget>
#include <QStackedWidget>
#include <QDialogButtonBox>
#include <QDialog>
#include <QComboBox>
#include <QSpinBox>

#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "ui/shared/inline-code-field.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/dialog-frame.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using namespace kai::core;

class TestCommandEditorDialog : public QObject {
    Q_OBJECT

private:
    // Troca de aba pelo item do nav lateral, igual um clique de verdade do
    // usuário faria.
    static void selectTab(CommandEditorDialog &dialog, const QString &titleKey)
    {
        auto *nav = dialog.findChild<QListWidget *>();
        QVERIFY(nav);
        for (int i = 0; i < nav->count(); ++i) {
            if (nav->item(i)->text() == kai::utils::tr(titleKey)) {
                nav->setCurrentRow(i);
                return;
            }
        }
        QFAIL(qPrintable(QStringLiteral("Aba '%1' não encontrada no nav lateral").arg(titleKey)));
    }
    static void selectConfigurationTab(CommandEditorDialog &dialog)
    {
        selectTab(dialog, QStringLiteral("command.tab.configuration"));
    }

private slots:
    // Aba "Interface CLI" (pedido do usuário): caminho de CLI + Descrição
    // saíram da aba Configuração pra uma aba própria. A Descrição estava no
    // card "Detalhe" da Configuração, que some em HTTP — aí comando HTTP não
    // tinha como ser descrito pelo formulário. Confirma que os dois campos
    // moram na aba nova e que selecioná-la os traz pra página atual.
    void cliFieldsLiveInsideCliInterfaceTab()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);

        // findChild<InlineCodeField*>() sem nome pegaria o PRIMEIRO
        // encontrado na árvore — que seria m_commandField (aba "Geral"),
        // não m_descriptionField.
        auto *descriptionField = dialog.findChild<InlineCodeField *>(QStringLiteral("descriptionField"));
        auto *cliPathField = dialog.findChild<QLineEdit *>(QStringLiteral("cliPathField"));
        QVERIFY(descriptionField);
        QVERIFY(cliPathField);

        // objectName específico: o diálogo também tem OUTRO QStackedWidget
        // (Shell/HTTP, m_tabWidget) — findChild sem nome pegaria o
        // primeiro achado, ambíguo.
        auto *stack = dialog.findChild<QStackedWidget *>(QStringLiteral("sidebarTabsPages"));
        QVERIFY(stack);
        QVERIFY2(!stack->currentWidget()->isAncestorOf(descriptionField),
            "Descrição não deveria estar na aba inicial ('Geral')");

        selectTab(dialog, QStringLiteral("editor.tab.cli"));
        QVERIFY(stack->currentWidget()->isAncestorOf(descriptionField));
        QVERIFY(stack->currentWidget()->isAncestorOf(cliPathField));
    }

    // Em HTTP a aba Configuração ficaria vazia (só tem opções de Shell):
    // some do nav. A "Interface CLI" continua lá — HTTP também tem caminho e
    // descrição.
    void httpCommandHidesConfigurationButKeepsCliTab()
    {
        Command existingHttp;
        existingHttp.id = QStringLiteral("c1");
        existingHttp.name = QStringLiteral("API");
        existingHttp.type = CommandType::Http;
        existingHttp.description = QStringLiteral("Autentica na API");
        HttpConfig cfg;
        cfg.url = QStringLiteral("https://x/api");
        existingHttp.httpConfig = cfg;

        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &existingHttp);
        auto *nav = dialog.findChild<QListWidget *>();
        QVERIFY(nav);
        bool configurationVisible = false;
        bool cliVisible = false;
        for (int i = 0; i < nav->count(); ++i) {
            const QString text = nav->item(i)->text();
            if (text == kai::utils::tr(QStringLiteral("command.tab.configuration"))) {
                configurationVisible = !nav->isRowHidden(i);
            }
            if (text == kai::utils::tr(QStringLiteral("editor.tab.cli"))) {
                cliVisible = !nav->isRowHidden(i);
            }
        }
        QVERIFY(!configurationVisible);
        QVERIFY(cliVisible);
        QCOMPARE(dialog.buildCommand().description, QStringLiteral("Autentica na API"));
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
        existing.type = CommandType::Command;
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

    // Bug relatado: dar Tab até "Cancelar" passava o gradiente de ação
    // primária pra ele (autoDefault fazia o botão focado virar :default) e
    // deixava "Salvar" cinza. O primário tem que continuar o default — e
    // marcado kaiRole="primary" — com o foco em qualquer outro botão.
    void dialogPrimaryButtonStaysDefaultWhenFocusMoves()
    {
        QDialog dialog;
        auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        stripDialogButtonIcons(box);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QPushButton *ok = box->button(QDialogButtonBox::Ok);
        QPushButton *cancel = box->button(QDialogButtonBox::Cancel);

        cancel->setFocus(Qt::TabFocusReason);
        QApplication::processEvents();
        QVERIFY(ok->isDefault());
        QVERIFY(!cancel->isDefault());
        QCOMPARE(ok->property("kaiRole").toString(), QStringLiteral("primary"));
        QVERIFY(!cancel->property("kaiRole").isValid());
    }

    // Descrição de CLI da pasta (campo novo, aba "Interface CLI"): ida e
    // volta, pré-preenchida ao editar, e desabilitada sem caminho de CLI
    // (sem caminho a pasta nem aparece na listagem do terminal).
    void folderCliDescriptionRoundTripsAndNeedsACliPath()
    {
        Folder existing;
        existing.id = QStringLiteral("f_api");
        existing.name = QStringLiteral("API");
        existing.cliPath = QStringLiteral("api");
        existing.cliDescription = QStringLiteral("Comandos da API");
        FolderEditorDialog dialog({existing}, nullptr, &existing);

        auto *cliPathField = dialog.findChild<QLineEdit *>(QStringLiteral("cliPathField"));
        auto *descriptionField = dialog.findChild<QLineEdit *>(QStringLiteral("cliDescriptionField"));
        QVERIFY(cliPathField);
        QVERIFY(descriptionField);
        QCOMPARE(descriptionField->text(), QStringLiteral("Comandos da API"));
        QVERIFY(descriptionField->isEnabled());

        descriptionField->setText(QStringLiteral("Pagamentos"));
        QCOMPARE(dialog.buildFolder().cliDescription, QStringLiteral("Pagamentos"));

        cliPathField->clear();
        QVERIFY(!descriptionField->isEnabled());
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
    // ---- KIP (spec 11 §2/§15) ----
    static QCheckBox *adv(CommandEditorDialog &dialog, const char *labelKey)
    {
        return dialog.findChild<QCheckBox *>(QStringLiteral("adv_") + QString::fromLatin1(labelKey));
    }

    // ---- Linguagem do comando (Nativa / Python / Node) ----
    static QComboBox *languageCombo(CommandEditorDialog &d)
    {
        return d.findChild<QComboBox *>(QStringLiteral("commandLanguageCombo"));
    }
    static QLineEdit *interpreterField(CommandEditorDialog &d)
    {
        return d.findChild<QLineEdit *>(QStringLiteral("commandInterpreterField"));
    }
    static void chooseLanguage(CommandEditorDialog &d, CommandLanguage language)
    {
        QComboBox *combo = languageCombo(d);
        const int idx = combo->findData(commandLanguageToString(language));
        QVERIFY(idx >= 0);
        combo->setCurrentIndex(idx);
    }

    void newCommandIsNativeAndHidesTheInterpreter()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        QVERIFY(languageCombo(dialog));
        QVERIFY(interpreterField(dialog));
        QCOMPARE(languageCombo(dialog)->count(), 3);
        QVERIFY(interpreterField(dialog)->parentWidget()->isHidden());
        QCOMPARE(dialog.buildCommand().language, CommandLanguage::Native);
        QVERIFY(dialog.buildCommand().interpreter.isEmpty());
    }

    void languageAndInterpreterAreSavedOnTheCommand()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        chooseLanguage(dialog, CommandLanguage::Python);
        QVERIFY(!interpreterField(dialog)->parentWidget()->isHidden());
        interpreterField(dialog)->setText(QStringLiteral("  uv run python "));
        const Command python = dialog.buildCommand();
        QCOMPARE(python.language, CommandLanguage::Python);
        QCOMPARE(python.interpreter, QStringLiteral("uv run python"));

        // Voltar a Nativa não deixa um interpretador órfão no comando.
        chooseLanguage(dialog, CommandLanguage::Native);
        QVERIFY(interpreterField(dialog)->parentWidget()->isHidden());
        const Command native = dialog.buildCommand();
        QCOMPARE(native.language, CommandLanguage::Native);
        QVERIFY(native.interpreter.isEmpty());
    }

    void existingPythonCommandIsLoadedBackIntoTheEditor()
    {
        Command node;
        node.id = QStringLiteral("n1");
        node.name = QStringLiteral("Node thing");
        node.type = CommandType::Command;
        node.command = QStringLiteral("console.log(1)");
        node.language = CommandLanguage::Node;
        node.interpreter = QStringLiteral("~/.nvm/node");
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &node);
        QCOMPARE(languageCombo(dialog)->currentData().toString(), QStringLiteral("node"));
        QCOMPARE(interpreterField(dialog)->text(), QStringLiteral("~/.nvm/node"));
        QVERIFY(!interpreterField(dialog)->parentWidget()->isHidden());
        const Command built = dialog.buildCommand();
        QCOMPARE(built.language, CommandLanguage::Node);
        QCOMPARE(built.interpreter, QStringLiteral("~/.nvm/node"));
        QCOMPARE(built.command, QStringLiteral("console.log(1)"));
    }

    // O campo mostra o interpretador GLOBAL como placeholder e explica que o
    // {{VAR}} não vale em código.
    void interpreterPlaceholderAndHintExplainTheLanguage()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        chooseLanguage(dialog, CommandLanguage::Python);
        QVERIFY(interpreterField(dialog)->placeholderText().contains(QStringLiteral("python")));
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("commandLanguageHint"));
        QVERIFY(hint);
        QVERIFY(!hint->isHidden());
        QVERIFY2(hint->text().contains(QStringLiteral("os.environ")), qPrintable(hint->text()));
        chooseLanguage(dialog, CommandLanguage::Node);
        QVERIFY2(hint->text().contains(QStringLiteral("process.env")), qPrintable(hint->text()));
        chooseLanguage(dialog, CommandLanguage::Native);
        QVERIFY(hint->isHidden());
    }

    // Python/Node não têm `export`: "Exportar variáveis" fica desabilitado (com o
    // motivo), é gravado como desligado, e voltar a Nativa o devolve.
    void exportVariablesIsUnavailableForCodeLanguages()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        selectConfigurationTab(dialog);
        QCheckBox *capture = adv(dialog, "command.field.capture_env");
        QVERIFY(capture);
        capture->setChecked(true);
        QVERIFY(capture->isEnabled());
        const QString normalTip = capture->toolTip();

        chooseLanguage(dialog, CommandLanguage::Python);
        QVERIFY(!capture->isEnabled());
        QVERIFY(capture->toolTip() != normalTip);
        QVERIFY(!dialog.buildCommand().captureEnv);

        chooseLanguage(dialog, CommandLanguage::Native);
        QVERIFY(capture->isEnabled());
        QCOMPARE(capture->toolTip(), normalTip);
        QVERIFY(dialog.buildCommand().captureEnv); // a escolha anterior não foi apagada
    }

    void kipTabMentionsTheInjectedModuleOnlyForCodeLanguages()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("kipModuleHint"));
        QVERIFY(hint);
        QVERIFY(hint->isHidden());
        chooseLanguage(dialog, CommandLanguage::Python);
        QVERIFY(!hint->isHidden());
        QVERIFY(hint->text().contains(QStringLiteral("import kip")));
        chooseLanguage(dialog, CommandLanguage::Node);
        QVERIFY(hint->text().contains(QStringLiteral("require('kip')")));
    }

    // KIP é coisa de comando SHELL: o editor só o oferece no shell e um comando HTTP
    // (mesmo vindo de um arquivo editado à mão com "kip": true) nunca sai KIP.
    void kipIsOnlyOfferedAndSavedForShellCommands()
    {
        {
            CommandEditorDialog shell(QStringLiteral("f1"), {}, {}, nullptr);
            QCheckBox *kip = adv(shell, "command_editor.kip");
            QVERIFY(kip);
            auto *container = shell.findChild<QWidget *>(QStringLiteral("shellConfigContainer"));
            QVERIFY(container);
            QVERIFY(!container->isHidden());          // comando novo é shell: o cartão KIP existe
            QVERIFY(kip->isEnabled());
        }
        Command http;
        http.id = QStringLiteral("c1");
        http.name = QStringLiteral("API");
        http.type = CommandType::Http;
        HttpConfig cfg;
        cfg.url = QStringLiteral("https://x/api");
        http.httpConfig = cfg;
        http.kip = true;                 // arquivo editado à mão
        http.kipOpenInWindow = true;
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &http);
        auto *container = dialog.findChild<QWidget *>(QStringLiteral("shellConfigContainer"));
        QVERIFY(container);
        QVERIFY2(container->isHidden(), "a configuração KIP não pode aparecer num comando HTTP");
        const Command built = dialog.buildCommand();
        QVERIFY(!built.kip);
        QVERIFY(!built.kipOpenInWindow);
    }

    // As coisas do KIP têm aba PRÓPRIA no editor (só shell) em vez de um cartão
    // espremido na aba Configuração.
    void kipHasItsOwnTabForShellCommandsOnly()
    {
        auto navRow = [](CommandEditorDialog &dialog, const QString &title) {
            auto *nav = dialog.findChild<QListWidget *>();
            for (int i = 0; nav && i < nav->count(); ++i) {
                if (nav->item(i)->text() == title) return i;
            }
            return -1;
        };
        const QString kipTitle = kai::utils::tr(QStringLiteral("command.tab.kip"));

        CommandEditorDialog shell(QStringLiteral("f1"), {}, {}, nullptr);
        auto *nav = shell.findChild<QListWidget *>();
        const int row = navRow(shell, kipTitle);
        QVERIFY2(row >= 0, "falta a aba KIP no editor");
        QVERIFY(!nav->isRowHidden(row));
        // O cartão saiu da aba Configuração: a caixa "Interface KIP" mora na aba nova.
        QCheckBox *kip = adv(shell, "command_editor.kip");
        QVERIFY(kip);
        auto *configuration = shell.findChild<QWidget *>(QStringLiteral("shellConfigContainer"));
        QVERIFY(configuration);
        QVERIFY(!configuration->isAncestorOf(kip));
        QVERIFY(!configuration->isAncestorOf(adv(shell, "command_editor.kip_window")));
        nav->setCurrentRow(row);
        QVERIFY(kip->isVisibleTo(&shell));

        Command http;
        http.id = QStringLiteral("c1");
        http.name = QStringLiteral("API");
        http.type = CommandType::Http;
        HttpConfig cfg;
        cfg.url = QStringLiteral("https://x/api");
        http.httpConfig = cfg;
        CommandEditorDialog httpDialog(QStringLiteral("f1"), {}, {}, nullptr, &http);
        auto *httpNav = httpDialog.findChild<QListWidget *>();
        const int httpRow = navRow(httpDialog, kipTitle);
        QVERIFY(httpRow >= 0);
        QVERIFY2(httpNav->isRowHidden(httpRow), "a aba KIP não existe para comandos HTTP");
    }

    void kipTabListsRememberedAnswersAndCanForgetThem()
    {
        Command existing;
        existing.id = QStringLiteral("c1");
        existing.name = QStringLiteral("Wizard");
        existing.type = CommandType::Command;
        existing.command = QStringLiteral("wizard --kip");
        existing.kip = true;
        existing.kipLastValues.insert(QStringLiteral("env/target"), QStringLiteral("prod"));
        existing.kipLastValues.insert(QStringLiteral("opts/flags"), QJsonObject{{"force", true}});
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &existing);
        auto *label = dialog.findChild<QLabel *>(QStringLiteral("kipRememberedLabel"));
        auto *forget = dialog.findChild<QPushButton *>(QStringLiteral("kipForgetButton"));
        QVERIFY(label && forget);
        QVERIFY(label->text().contains(QStringLiteral("2")));
        QVERIFY(forget->isEnabled());
        QCOMPARE(dialog.buildCommand().kipLastValues.size(), 2); // editar não perde as respostas

        forget->click();
        QVERIFY(!forget->isEnabled());
        QVERIFY(dialog.buildCommand().kipLastValues.isEmpty());
        QCOMPARE(label->text(), kai::utils::tr(QStringLiteral("command_editor.kip.remembered.none")));
    }

    void kipTabShortcutOpensTheExportableVariablesTab()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        auto *nav = dialog.findChild<QListWidget *>();
        auto *go = dialog.findChild<QPushButton *>(QStringLiteral("kipGoToVariablesButton"));
        QVERIFY(go);
        go->click();
        QCOMPARE(nav->item(nav->currentRow())->text(), kai::utils::tr(QStringLiteral("command.tab.exportable_vars")));
    }

    void kipCheckboxAndOwnWindowAreSavedOnTheCommand()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        selectConfigurationTab(dialog);
        QCheckBox *kip = adv(dialog, "command_editor.kip");
        QCheckBox *window = adv(dialog, "command_editor.kip_window");
        QVERIFY(kip && window);
        // Janela própria só faz sentido com KIP ligado.
        QVERIFY(!window->isEnabled());
        QVERIFY(!dialog.buildCommand().kip);

        kip->setChecked(true);
        QVERIFY(window->isEnabled());
        window->setChecked(true);
        const Command built = dialog.buildCommand();
        QVERIFY(built.kip);
        QVERIFY(built.kipOpenInWindow);

        // Desligando o KIP, a janela própria não vaza para o comando salvo.
        kip->setChecked(false);
        const Command off = dialog.buildCommand();
        QVERIFY(!off.kip);
        QVERIFY(!off.kipOpenInWindow);
    }

    void kipAutoCloseIsSavedLoadedAndOnlyEnabledWithKip()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        QCheckBox *autoClose = adv(dialog, "command_editor.kip_auto_close");
        auto *delay = dialog.findChild<QSpinBox *>(QStringLiteral("kipAutoCloseDelay"));
        QVERIFY(autoClose && delay);
        QVERIFY(!autoClose->isEnabled());                   // sem KIP não tem o que fechar
        adv(dialog, "command_editor.kip")->setChecked(true);
        QVERIFY(autoClose->isEnabled());
        QVERIFY(!delay->isEnabled());                       // o atraso só vale marcado
        autoClose->setChecked(true);
        QVERIFY(delay->isEnabled());
        delay->setValue(5);
        const Command saved = dialog.buildCommand();
        QVERIFY(saved.kipAutoCloseWindow);
        QCOMPARE(saved.kipAutoCloseDelaySec, 5);

        CommandEditorDialog reopened(QStringLiteral("f1"), {}, {}, nullptr, &saved);
        QVERIFY(adv(reopened, "command_editor.kip_auto_close")->isChecked());
        QCOMPARE(reopened.findChild<QSpinBox *>(QStringLiteral("kipAutoCloseDelay"))->value(), 5);

        // Desligar o KIP antes de salvar não deixa o auto-fechar para trás.
        adv(dialog, "command_editor.kip")->setChecked(false);
        QVERIFY(!dialog.buildCommand().kipAutoCloseWindow);
    }

    void kipDisablesTheFeaturesThatCompeteForStdio()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        selectConfigurationTab(dialog);
        const QVector<const char *> excluded = {
            "command.field.background", "command_editor.interactive_terminal", "command_editor.formatted_output",
            "command_editor.render_markdown", "command.field.capture_env", "command_editor.open_last_link",
            "command.field.autorun", "command.field.cron_notify_on_run"};
        for (const char *key : excluded) {
            QVERIFY2(adv(dialog, key), key);
            QVERIFY2(adv(dialog, key)->isEnabled(), key);
        }
        auto *cron = dialog.findChild<QLineEdit *>(QString());
        Q_UNUSED(cron);

        adv(dialog, "command_editor.kip")->setChecked(true);
        for (const char *key : excluded) {
            QVERIFY2(!adv(dialog, key)->isEnabled(), key);
            QVERIFY2(!adv(dialog, key)->toolTip().isEmpty(), key); // diz POR QUÊ
        }
        // "Ignorar código de saída" e "ocultar ao executar" continuam valendo.
        QVERIFY(adv(dialog, "command_editor.ignore_exit_code")->isEnabled());
        QVERIFY(adv(dialog, "command_editor.hide_on_run")->isEnabled());

        // Desligar devolve tudo (e o tooltip original de cada controle).
        adv(dialog, "command_editor.kip")->setChecked(false);
        for (const char *key : excluded) {
            QVERIFY2(adv(dialog, key)->isEnabled(), key);
        }
        QVERIFY(adv(dialog, "command_editor.interactive_terminal")->toolTip()
                == kai::utils::tr(QStringLiteral("command_editor.interactive_terminal.tip")));
    }

    void savingWithKipOnStoresTheCompetingFeaturesAsOff()
    {
        Command existing;
        existing.id = QStringLiteral("c1");
        existing.folderId = QStringLiteral("f1");
        existing.name = QStringLiteral("Wizard");
        existing.type = CommandType::Command;
        existing.command = QStringLiteral("wizard --kip");
        existing.interactiveTerminal = true;
        existing.formattedOutput = true;
        existing.renderMarkdown = true;
        existing.openLastLink = true;
        existing.isBackground = true;
        existing.captureEnv = true;
        existing.autoRun = true;
        existing.autoRunDelaySec = 5;
        existing.cronExpression = QStringLiteral("* * * * *");
        OutputResponder responder;
        responder.name = QStringLiteral("yes");
        responder.pattern = QStringLiteral("\\?");
        responder.response = QStringLiteral("y");
        existing.responders << responder;
        existing.kip = true;

        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &existing);
        const Command built = dialog.buildCommand();
        QVERIFY(built.kip);
        QVERIFY(!built.interactiveTerminal);
        QVERIFY(!built.formattedOutput);
        QVERIFY(!built.renderMarkdown);
        QVERIFY(!built.openLastLink);
        QVERIFY(!built.isBackground);
        QVERIFY(!built.captureEnv);
        QVERIFY(!built.autoRun);
        QCOMPARE(built.autoRunDelaySec, 0);
        QVERIFY(built.cronExpression.isEmpty());
        QVERIFY(built.responders.isEmpty());
    }

    void kipKeepsTheDeclaredExportableVariablesEditable()
    {
        Command existing;
        existing.id = QStringLiteral("c1");
        existing.folderId = QStringLiteral("f1");
        existing.name = QStringLiteral("Login");
        existing.type = CommandType::Command;
        existing.command = QStringLiteral("login --kip");
        existing.kip = true;
        existing.declaredEnvVars << DeclaredEnvVar{QStringLiteral("TOKEN")};

        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &existing);
        // A aba segue disponível no nav (o título ganha "(N)" com itens).
        auto *nav = dialog.findChild<QListWidget *>();
        QVERIFY(nav);
        bool tabAvailable = false;
        for (int i = 0; i < nav->count(); ++i) {
            if (nav->item(i)->text().startsWith(kai::utils::tr(QStringLiteral("command.tab.exportable_vars")))) {
                tabAvailable = !nav->isRowHidden(i) && (nav->item(i)->flags() & Qt::ItemIsEnabled);
            }
        }
        QVERIFY(tabAvailable);
        const Command built = dialog.buildCommand();
        QCOMPARE(built.declaredEnvVars.size(), 1);
        QCOMPARE(built.declaredEnvVars.first().name, QStringLiteral("TOKEN"));
        QVERIFY(built.kip);
    }

    void editingKeepsTheRememberedKipAnswers()
    {
        Command existing;
        existing.id = QStringLiteral("c1");
        existing.folderId = QStringLiteral("f1");
        existing.name = QStringLiteral("Wizard");
        existing.type = CommandType::Command;
        existing.command = QStringLiteral("wizard --kip");
        existing.kip = true;
        existing.kipLastValues.insert(QStringLiteral("env/target"), QStringLiteral("prod"));
        existing.kipLastValues.insert(QStringLiteral("opts/flags"), QJsonObject{{"force", true}});

        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &existing);
        QCOMPARE(dialog.buildCommand().kipLastValues, existing.kipLastValues);
    }

    void loadingAnExistingKipCommandChecksTheBoxesAndAppliesExclusions()
    {
        Command existing;
        existing.id = QStringLiteral("c1");
        existing.folderId = QStringLiteral("f1");
        existing.name = QStringLiteral("Wizard");
        existing.type = CommandType::Command;
        existing.command = QStringLiteral("wizard --kip");
        existing.kip = true;
        existing.kipOpenInWindow = true;

        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr, &existing);
        selectConfigurationTab(dialog);
        QVERIFY(adv(dialog, "command_editor.kip")->isChecked());
        QVERIFY(adv(dialog, "command_editor.kip_window")->isChecked());
        QVERIFY(!adv(dialog, "command_editor.interactive_terminal")->isEnabled());
    }

    void httpCommandsNeverCarryKip()
    {
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        selectConfigurationTab(dialog);
        adv(dialog, "command_editor.kip")->setChecked(true);
        for (auto *b : dialog.findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("HTTP")) {
                b->click();
                break;
            }
        }
        const Command built = dialog.buildCommand();
        QCOMPARE(built.type, CommandType::Http);
        QVERIFY(!built.kip);
        QVERIFY(!built.kipOpenInWindow);
    }

    // Bug visual reportado: ao abrir a tela de edição, o campo de comando
    // nascia estreito (largura do sizeHint da página) e o combo "Linguagem"
    // colado ao rótulo, em vez de ocupar a largura do card. Só acontecia com
    // a moldura própria dos diálogos (que redimensiona no primeiro Show) e
    // vinha de um adjustSize() na página, que o QStackedLayout não desfazia.
    void commandFieldFillsCardWidthOnFirstShow()
    {
        installDialogFrames();
        CommandEditorDialog dialog(QStringLiteral("f1"), {}, {}, nullptr);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::qWait(50);

        InlineCodeField *field = nullptr;
        for (auto *candidate : dialog.findChildren<InlineCodeField *>(QStringLiteral("inlineCodeField"))) {
            if (candidate->isVisible()) {
                field = candidate;
                break;
            }
        }
        QVERIFY(field);
        auto *stack = dialog.findChild<QStackedWidget *>(QStringLiteral("execModeStack"));
        QVERIFY(stack);
        QVERIFY2(stack->currentWidget()->width() >= stack->width() - 2,
                 qPrintable(QStringLiteral("página %1px, pilha %2px")
                                .arg(stack->currentWidget()->width()).arg(stack->width())));
        QVERIFY2(field->width() >= stack->width() - 8,
                 qPrintable(QStringLiteral("campo %1px, pilha %2px")
                                .arg(field->width()).arg(stack->width())));
    }
};

QTEST_MAIN(TestCommandEditorDialog)
#include "test_command_editor_dialog.moc"
