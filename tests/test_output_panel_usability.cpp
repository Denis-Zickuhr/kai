#include <QTest>
#include <QJsonObject>
#include "core/models.h"
#include "core/config-manager.h"
#include "ui/output-panel.h"
#include "ui/command-tree-widget.h"
#include <QSignalSpy>
#include <QLineEdit>
#include <QTemporaryDir>

#include "ui/main-window.h"
#include "ui/terminal-drawer.h"
#include "ui/log-line-view.h"
#include "engine/process-runner.h"

using namespace kai::ui;
using namespace kai::core;
using namespace kai::engine;

// Cobre os ajustes de usabilidade da Saída pedidos pelo usuário: (1) a
// janela principal começa com tamanho mais compacto; (2) a caixa da
// Saída está sempre visível no layout, nunca escondida dinamicamente
// (bug real de sobreposição transitória do QSplitter); (3) o motor de
// execução aceita entrada via stdin mesmo sem nenhum output prévio —
// causa raiz real do bug "read -p não aceita resposta": o campo de input
// da UI só era habilitado dentro de handlePipelineLog (ao chegar o
// primeiro log), mas `read -p` bloqueia esperando stdin sem
// necessariamente emitir output capturável antes disso. Confirmado via
// teste de diagnóstico real com ProcessRunner puro antes da correção.
class TestOutputPanelUsability : public QObject {
    Q_OBJECT

private slots:
    void terminalDrawerIsAlwaysVisibleByDefault()
    {
        MainWindow window;
        auto *terminal = window.findChild<TerminalDrawer *>();
        QVERIFY(terminal != nullptr);
        QVERIFY(!terminal->isHidden());
    }

    void mainWindowDefaultSizeIsCompact()
    {
        MainWindow window;
        // Largura padrão ~2x (feedback do usuário nesta rodada: "aplicativo
        // deve abrir com uma largura maior, cerca de 2x o atual"). O valor
        // anterior (compacto, <=720) foi revertido a pedido; agora a janela
        // nasce larga (1280) para caber a árvore + saída confortavelmente,
        // mantendo um mínimo utilizável para telas pequenas.
        QVERIFY(window.size().width() >= 1000);
        QVERIFY(window.minimumWidth() <= 720);
    }


    // REGRESSÃO CRÍTICA (reportada): depois de colapsar e expandir, "a saída
    // não aparece mais, não consigo mais responder os scripts". Duas causas:
    // (1) o QSplitter não devolvia a altura ao liberar o limite, deixando o
    // painel com altura ~0; (2) a visibilidade da entrada só era decidida em
    // setBodyVisible, então habilitar o stdin DEPOIS de um colapso deixava o
    // campo escondido para sempre.
    void collapseThenExpandRestoresOutputAndInput()
    {
        MainWindow window;
        window.resize(1280, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto *drawer = window.findChild<TerminalDrawer *>();
        QVERIFY(drawer != nullptr);

        drawer->setExpanded(true);
        drawer->setInputEnabled(true);
        qApp->processEvents();
        const int expandedHeight = drawer->height();
        QVERIFY2(expandedHeight > 80,
                 qPrintable(QStringLiteral("altura expandida inicial=%1").arg(expandedHeight)));

        // Colapsa: deve encolher de verdade (não esticar o cabeçalho).
        drawer->setExpanded(false);
        qApp->processEvents();
        QVERIFY2(drawer->height() < expandedHeight,
                 qPrintable(QStringLiteral("apos colapsar altura=%1").arg(drawer->height())));

        // Expande de volta: a altura deve VOLTAR (o bug deixava ~0).
        drawer->setExpanded(true);
        qApp->processEvents();
        QVERIFY2(drawer->height() > 80,
                 qPrintable(QStringLiteral("apos expandir altura=%1 (deveria voltar)").arg(drawer->height())));

        // E a entrada deve estar utilizável para responder ao script.
        auto *input = drawer->findChild<QLineEdit *>(QStringLiteral("outputInput"));
        QVERIFY(input != nullptr);
        QVERIFY(input->isEnabled());
        QVERIFY2(input->isVisible(), "campo de entrada ficou invisivel apos colapsar/expandir");

        // Habilitar o stdin DEPOIS de um colapso/expansão também deve funcionar.
        drawer->setInputEnabled(false);
        drawer->setExpanded(false);
        drawer->setExpanded(true);
        drawer->setInputEnabled(true);
        qApp->processEvents();
        QVERIFY2(input->isVisible(), "entrada habilitada apos colapso continuou escondida");
    }


    // "Compactar saída" (pedido do usuário): colapsa linhas em branco repetidas
    // e apara espaços à direita, deixando a saída densa.
    void compactOutputCollapsesBlankLines()
    {
        OutputPanel panel;
        OutputPanel::ViewOptions options = panel.viewOptions();
        options.compact = true;
        panel.setViewOptions(options);

        panel.appendOutput(QStringLiteral("linha 1   \n\n\n\nlinha 2\n\nlinha 3   \n"));
        const QString out = panel.plainOutput();

        // Sem sequências de 2+ linhas vazias.
        QVERIFY2(!out.contains(QStringLiteral("\n\n\n")), qPrintable(out));
        // Conteúdo preservado.
        QVERIFY(out.contains(QStringLiteral("linha 1")));
        QVERIFY(out.contains(QStringLiteral("linha 2")));
        QVERIFY(out.contains(QStringLiteral("linha 3")));
        // Espaços à direita aparados.
        QVERIFY2(!out.contains(QStringLiteral("linha 1   ")), qPrintable(out));

        // Desligado, a saída passa intacta.
        OutputPanel plain;
        plain.appendOutput(QStringLiteral("a\n\n\n\nb\n"));
        QVERIFY(plain.plainOutput().contains(QStringLiteral("\n\n\n")));
    }

    // O flag compact_output é preferência POR COMANDO e precisa sobreviver ao
    // salvar/carregar (e é aceito no kai.json).
    void compactOutputFlagRoundTripsInCommand()
    {
        kai::core::Command c;
        c.id = QStringLiteral("cmd_compact");
        c.name = QStringLiteral("Teste");
        c.type = kai::core::CommandType::Shell;
        c.command = QStringLiteral("echo oi");
        c.compactOutput = true;

        const QJsonObject obj = c.toJson();
        QCOMPARE(obj.value(QStringLiteral("compact_output")).toBool(), true);

        const kai::core::Command back = kai::core::Command::fromJson(obj);
        QCOMPARE(back.compactOutput, true);

        // Ausente no JSON => false (padrão), sem quebrar arquivos antigos.
        QJsonObject legacy = obj;
        legacy.remove(QStringLiteral("compact_output"));
        QCOMPARE(kai::core::Command::fromJson(legacy).compactOutput, false);
    }

    // (1) Auto-run: flag + delay em SEGUNDOS sobrevivem ao round-trip; ausência
    // no JSON assume desligado/0 (retrocompat).
    void autoRunFlagRoundTripsInCommand()
    {
        kai::core::Command c;
        c.id = QStringLiteral("cmd_autorun");
        c.name = QStringLiteral("Boot");
        c.type = kai::core::CommandType::Shell;
        c.autoRun = true;
        c.autoRunDelaySec = 7;

        const QJsonObject obj = c.toJson();
        QCOMPARE(obj.value(QStringLiteral("auto_run")).toBool(), true);
        QCOMPARE(obj.value(QStringLiteral("auto_run_delay_sec")).toInt(), 7);

        const kai::core::Command back = kai::core::Command::fromJson(obj);
        QCOMPARE(back.autoRun, true);
        QCOMPARE(back.autoRunDelaySec, 7);

        QJsonObject legacy = obj;
        legacy.remove(QStringLiteral("auto_run"));
        legacy.remove(QStringLiteral("auto_run_delay_sec"));
        const kai::core::Command l = kai::core::Command::fromJson(legacy);
        QCOMPARE(l.autoRun, false);
        QCOMPARE(l.autoRunDelaySec, 0);
    }

    // (4) Parameter.multiSelect sobrevive ao round-trip.
    void parameterMultiSelectRoundTrips()
    {
        kai::core::Parameter p;
        p.name = QStringLiteral("envs");
        p.type = kai::core::ParameterType::Select;
        p.options = {QStringLiteral("dev"), QStringLiteral("qa"), QStringLiteral("prod")};
        p.multiSelect = true;

        const kai::core::Parameter back = kai::core::Parameter::fromJson(p.toJson());
        QCOMPARE(back.multiSelect, true);
        QCOMPARE(back.options.size(), 3);

        // Ausente => false.
        QJsonObject obj = p.toJson();
        obj.remove(QStringLiteral("multi_select"));
        QCOMPARE(kai::core::Parameter::fromJson(obj).multiSelect, false);
    }


    // Ligar "Compactar" deve reprocessar o que JÁ está na tela (relatado: as
    // linhas vazias antigas continuavam lá, só a saída nova era compactada).
    void compactRecompactsAlreadyVisibleOutput()
    {
        OutputPanel panel;
        panel.appendOutput(QStringLiteral("a\n\n\n\n\nb\n\n\n\nc\n"));
        QVERIFY2(panel.plainOutput().contains(QStringLiteral("\n\n\n")),
                 "pre-condicao: saida deveria ter linhas vazias em sequencia");

        OutputPanel::ViewOptions options = panel.viewOptions();
        options.compact = true;
        panel.setViewOptions(options);

        const QString out = panel.plainOutput();
        QVERIFY2(!out.contains(QStringLiteral("\n\n\n")), qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("a")));
        QVERIFY(out.contains(QStringLiteral("b")));
        QVERIFY(out.contains(QStringLiteral("c")));
    }

    // Linhas com só espaços contam como vazias, e runs longos de espaço interno
    // são reduzidos.
    void compactTreatsWhitespaceOnlyLinesAsBlank()
    {
        OutputPanel panel;
        OutputPanel::ViewOptions options = panel.viewOptions();
        options.compact = true;
        panel.setViewOptions(options);

        panel.appendOutput(QStringLiteral("x\n   \n\t\n   \ny\n"));
        const QString out = panel.plainOutput();
        QVERIFY2(!out.contains(QStringLiteral("\n\n\n")), qPrintable(out));

        panel.clearAll();
        panel.appendOutput(QStringLiteral("col1          col2\n"));
        QVERIFY2(!panel.plainOutput().contains(QStringLiteral("      ")),
                 qPrintable(panel.plainOutput()));
    }


    // REGRESSÃO (caso real reportado): a saída de um comando com TTY vinha assim
    //   "...deseja continuar? [y/N] " + dezenas de linhas "vazias"
    // e a compactação NÃO removia. Causa: a compactação roda ANTES do parse
    // ANSI, e as linhas "vazias" carregavam sequências de escape (ex: \x1b[0m).
    // Testar trimmed().isEmpty() no texto CRU dava falso — os bytes do escape
    // não são espaço. Agora a vacuidade é avaliada no texto SEM escapes.
    void compactCollapsesBlankLinesCarryingOnlyAnsiEscapes()
    {
        OutputPanel panel;
        OutputPanel::ViewOptions options = panel.viewOptions();
        options.compact = true;
        panel.setViewOptions(options);

        // Reproduz o padrão: prompt seguido de várias linhas só com escape.
        QString payload = QStringLiteral(
            "Você está executando em PRODUÇÃO!!, tem certeza? [y/N] \n");
        for (int i = 0; i < 25; ++i) {
            payload += QStringLiteral("\x1b[0m\n");
        }
        payload += QStringLiteral("fim\n");

        panel.appendOutput(payload);
        const QString out = panel.plainOutput();

        // Não deve sobrar bloco de linhas vazias.
        QVERIFY2(!out.contains(QStringLiteral("\n\n\n")), qPrintable(out));
        // Conteúdo real preservado.
        QVERIFY(out.contains(QStringLiteral("[y/N]")));
        QVERIFY(out.contains(QStringLiteral("fim")));
        // Conta as linhas: prompt + no máximo uma vazia + "fim" (+ possível
        // última quebra) — bem longe das 25 originais.
        const int lineCount = out.split(QLatin1Char('\n')).size();
        QVERIFY2(lineCount <= 5, qPrintable(QStringLiteral("linhas=%1").arg(lineCount)));
    }


    // "Ocultar o Kai ao executar" é preferência POR COMANDO e precisa sobreviver
    // ao salvar/carregar (e é aceita no kai.json como "hide_on_run").
    void hideOnRunFlagRoundTrips()
    {
        kai::core::Command c;
        c.id = QStringLiteral("cmd_hide");
        c.type = kai::core::CommandType::Shell;
        c.command = QStringLiteral("code .");
        c.hideOnRun = true;

        const QJsonObject obj = c.toJson();
        QCOMPARE(obj.value(QStringLiteral("hide_on_run")).toBool(), true);
        QCOMPARE(kai::core::Command::fromJson(obj).hideOnRun, true);

        // Ausente => false, sem quebrar comandos antigos.
        QJsonObject legacy = obj;
        legacy.remove(QStringLiteral("hide_on_run"));
        QCOMPARE(kai::core::Command::fromJson(legacy).hideOnRun, false);
    }


    // A saída NUNCA deve duplicar (pedido explícito: "valide para que a saída
    // não duplique output jamais"). O caminho de risco é a RECONEXÃO: ao
    // selecionar um comando, o histórico guardado é reinjetado no painel — sem
    // limpar antes, ele empilharia sobre o que já estava na tela.
    void outputNeverDuplicatesOnReconnect()
    {
        OutputPanel panel;
        const QString linha = QStringLiteral("resultado importante\n");

        panel.appendOutput(linha);
        QCOMPARE(panel.plainOutput().count(QStringLiteral("resultado importante")), 1);

        // Simula a reconexão: limpa e reinjeta o histórico (a ordem que o
        // MainWindow usa).
        panel.clearAll();
        panel.appendOutput(linha);
        QCOMPARE(panel.plainOutput().count(QStringLiteral("resultado importante")), 1);

        // Reinjetar SEM limpar é o cenário que duplicaria — aqui garantimos que
        // o clearAll de fato zera, então a reconexão é segura.
        panel.clearAll();
        QVERIFY(panel.plainOutput().isEmpty());
    }

    // Bug reportado: "ao selecionar uma PASTA, e se um cmd está rodando
    // dentro dela, o sistema exibe o cmd rodando, não quero isso, se está
    // na pasta não exibe saída alguma". Havia DOIS caminhos que
    // vazavam a saída de um comando em background pra uma pasta
    // selecionada: (1) handleCommandSelectionChanged só limpava o
    // Terminal Drawer ao selecionar pasta/nada se o comando conectado
    // NÃO estivesse mais rodando; (2) mesmo depois de limpar,
    // handlePipelineLog reconectava sozinho ao primeiro log novo que
    // chegasse enquanto nada estivesse conectado — o que uma pasta
    // selecionada sempre deixa "nada conectado".
    void selectingFolderClearsOutputEvenWithBackgroundCommandRunning()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        // "Pasta" precisa ser uma SUBPASTA de verdade (com parentId), não
        // uma raiz/aba — uma raiz é selecionada trocando de aba, não via
        // item de árvore, então não exercitaria o mesmo caminho de
        // currentSelectionIsFolder() que uma subpasta normal usa (o caso
        // real reportado: uma subpasta dentro de um projeto).
        Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Projeto");

        Folder folder;
        folder.id = QStringLiteral("f1");
        folder.name = QStringLiteral("Pasta");
        folder.parentId = root.id;

        Command command;
        command.id = QStringLiteral("c1");
        command.name = QStringLiteral("Dev Server");
        command.folderId = folder.id;
        command.type = CommandType::Shell;
        command.command = QStringLiteral("echo hi");
        command.isBackground = true;

        CommandsData data;
        data.folders << root << folder;
        data.commands << command;
        ConfigManager manager;
        QVERIFY(manager.saveCommands(data));

        MainWindow window;
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *panel = drawer ? drawer->findChild<OutputPanel *>() : nullptr;
        auto *tree = window.findChild<CommandTreeWidget *>();
        QVERIFY(drawer != nullptr && panel != nullptr && tree != nullptr);

        auto selectItemByText = [&](const QString &text) -> QTreeWidgetItem * {
            for (QTreeWidget *w : tree->findChildren<QTreeWidget *>()) {
                const QList<QTreeWidgetItem *> found = w->findItems(text, Qt::MatchExactly | Qt::MatchRecursive);
                if (!found.isEmpty()) {
                    found.first()->treeWidget()->setCurrentItem(found.first());
                    return found.first();
                }
            }
            return nullptr;
        };

        // Seleciona o COMANDO primeiro (estado inicial determinístico —
        // não depende de qual item a árvore auto-seleciona ao abrir).
        QVERIFY(selectItemByText(command.name) != nullptr);
        QVERIFY(!tree->currentSelectionIsFolder());

        // Chega um chunk de log com nada conectado ainda — reconecta
        // automaticamente ao comando selecionado, igual a um comando em
        // background de verdade produzindo output.
        QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection,
            Q_ARG(QString, command.id), Q_ARG(QString, QStringLiteral("Server rodando\n")), Q_ARG(bool, false));
        drawer->flushPendingOutput();
        QVERIFY(panel->plainOutput().contains(QStringLiteral("Server rodando")));

        // Seleciona a PASTA que contém o comando em "execução" — a saída
        // deve sumir completamente, mesmo o comando ainda "rodando".
        QVERIFY(selectItemByText(folder.name) != nullptr);
        QVERIFY(tree->currentSelectionIsFolder());
        QVERIFY(!panel->plainOutput().contains(QStringLiteral("Server rodando")));

        // Novo chunk chega enquanto a pasta AINDA está selecionada — não
        // pode reconectar sozinho e trazer a saída de volta.
        QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection,
            Q_ARG(QString, command.id), Q_ARG(QString, QStringLiteral("Mais uma linha\n")), Q_ARG(bool, false));
        drawer->flushPendingOutput();
        QVERIFY(!panel->plainOutput().contains(QStringLiteral("Mais uma linha")));
    }

    // Bug reportado: "tenho uma saída CMD que roda terminal formatado, o
    // build, se vou em outra saída com term formatado, ele buga e traz a
    // saída do comando pro cara errado" — trocar de um comando com Saída
    // Formatada pra OUTRO comando também com Saída Formatada não pode
    // deixar nenhuma linha do comando anterior visível na view formatada
    // (LogLineView), nem mesmo transitoriamente.
    void switchingBetweenTwoFormattedOutputCommandsNeverMixesContent()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        Command a;
        a.id = QStringLiteral("c_a");
        a.name = QStringLiteral("Build");
        a.type = CommandType::Shell;
        a.command = QStringLiteral("echo build");
        a.formattedOutput = true;

        Command b;
        b.id = QStringLiteral("c_b");
        b.name = QStringLiteral("Testes");
        b.type = CommandType::Shell;
        b.command = QStringLiteral("echo testes");
        b.formattedOutput = true;

        CommandsData data;
        data.commands << a << b;
        ConfigManager manager;
        QVERIFY(manager.saveCommands(data));

        MainWindow window;
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *formattedView = drawer ? drawer->findChild<LogLineView *>() : nullptr;
        QVERIFY(drawer != nullptr && tree != nullptr && formattedView != nullptr);

        auto selectItemByText = [&](const QString &text) -> QTreeWidgetItem * {
            for (QTreeWidget *w : tree->findChildren<QTreeWidget *>()) {
                const QList<QTreeWidgetItem *> found = w->findItems(text, Qt::MatchExactly | Qt::MatchRecursive);
                if (!found.isEmpty()) {
                    found.first()->treeWidget()->setCurrentItem(found.first());
                    return found.first();
                }
            }
            return nullptr;
        };
        auto formattedContains = [&](const QString &needle) -> bool {
            for (int i = 0; i < formattedView->logModel()->rowCount(); ++i) {
                if (formattedView->logModel()->entryAt(i).raw.contains(needle)) {
                    return true;
                }
            }
            return false;
        };

        QVERIFY(selectItemByText(a.name) != nullptr);
        QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection,
            Q_ARG(QString, a.id), Q_ARG(QString, QStringLiteral("linha do BUILD\n")), Q_ARG(bool, false));
        drawer->flushPendingOutput();
        QVERIFY(formattedContains(QStringLiteral("linha do BUILD")));

        QVERIFY(selectItemByText(b.name) != nullptr);
        QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection,
            Q_ARG(QString, b.id), Q_ARG(QString, QStringLiteral("linha dos TESTES\n")), Q_ARG(bool, false));
        drawer->flushPendingOutput();
        QVERIFY(formattedContains(QStringLiteral("linha dos TESTES")));
        QVERIFY2(!formattedContains(QStringLiteral("linha do BUILD")),
            "saida formatada do comando anterior vazou pro comando novo");
    }

    // Bug reportado: "saida formatada, a PRIMEIRA linha de JSON lançada,
    // esta caindo sempre colada junto a saida normal, isso gera perde se
    // o APP SOLTAR um linha de JSON APENAS". Raiz real: HttpRunner emitia
    // a linha de status HTTP e o corpo JSON em duas chamadas logMessage
    // SEPARADAS, mas a linha de status não terminava em "\n" — como as
    // duas chegam no mesmo turno do event loop, o TerminalDrawer as
    // COALESCE (mesmo canal = mesmo erro) por concatenação direta, sem
    // separador. O "{" de abertura do corpo ficava colado no fim da linha
    // de status; a linha física resultante não começava com "{", então
    // LogLineModel nunca reconhecia o corpo como JSON — a resposta
    // inteira virava uma única linha crua, sem estrutura. Reproduz aqui
    // via TerminalDrawer (mesmo caminho real) com as DUAS chamadas que o
    // HttpRunner corrigido agora faz (status já com "\n" final).
    void httpStatusLineAndJsonBodyNeverGlueIntoOneRawLine()
    {
        TerminalDrawer drawer;
        drawer.setFormattedOutputEnabled(true);
        const QString statusLine = QStringLiteral("HTTP GET https://x/api -> 200 OK  •  1.2 KB  •  45 ms");
        const QString body = QStringLiteral("{\n  \"status\": \"ok\"\n}\n");
        // "\n" final na linha de status É o fix (ver HttpRunner::sendRequest) —
        // sem ele, este teste falha exatamente como o bug reportado.
        drawer.appendRawText(statusLine + QStringLiteral("\n"), false);
        drawer.appendRawText(body, false);
        drawer.flushPendingOutput();

        auto *formattedView = drawer.findChild<LogLineView *>();
        QVERIFY(formattedView != nullptr);
        auto *model = formattedView->logModel();
        QCOMPARE(model->rowCount(), 2);
        QVERIFY2(!model->entryAt(0).structured, "linha de status nao e JSON");
        QVERIFY2(model->entryAt(1).structured,
            "corpo JSON tem que virar entrada estruturada PROPRIA, nao grudada na linha de status");
        QVERIFY(model->entryAt(1).raw.contains(QStringLiteral("\"status\": \"ok\"")));
    }

    // seedOutput (usado ao destacar a janela) também não deve acumular: a nova
    // instância começa com o histórico, não com o histórico DUPLICADO.
    void seedOutputReplacesInsteadOfAppending()
    {
        OutputPanel panel;
        panel.seedOutput(QStringLiteral("linha A\n"));
        panel.seedOutput(QStringLiteral("linha A\n"));
        QCOMPARE(panel.plainOutput().count(QStringLiteral("linha A")), 1);
    }

    void terminalInputCanBeEnabledBeforeAnyOutputArrives()
    {
        // Reproduz o núcleo do bug real: TerminalDrawer::setInputEnabled
        // deve poder ser chamado (e o campo ficar de fato habilitado)
        // antes de qualquer texto ter sido escrito no log — é exatamente
        // isso que MainWindow::runCommandWithParams faz agora ao disparar
        // um comando Shell, em vez de esperar o primeiro outputReady.
        TerminalDrawer drawer;
        auto *inputField = drawer.findChild<QLineEdit *>();
        QVERIFY(inputField != nullptr);
        QVERIFY(!inputField->isEnabled());

        drawer.setInputEnabled(true);
        QVERIFY(inputField->isEnabled());
    }

    void processRunnerAcceptsStdinWithoutPriorOutput()
    {
        // Confirma a causa raiz real (validado originalmente via
        // test_read_prompt_diag, removido após a investigação): o motor
        // de execução aceita escrita em stdin mesmo que `read -p` nunca
        // tenha emitido nenhum outputReady antes disso.
        ProcessRunner runner;
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);

        runner.start(QStringLiteral("read -p 'Nome: ' n; echo \"Oi, $n\""), QString(), {});
        QTest::qWait(300);

        runner.writeToStdin(QStringLiteral("KaiTester"));

        QVERIFY(QTest::qWaitFor([&]() { return finishedSpy.count() > 0; }, 3000));
        const ProcessResult result = finishedSpy.first().at(0).value<ProcessResult>();
        QCOMPARE(result.exitCode, 0);
    }
};

QTEST_MAIN(TestOutputPanelUsability)
#include "test_output_panel_usability.moc"
