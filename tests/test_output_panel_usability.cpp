#include <QTest>
#include <QJsonObject>
#include "core/models.h"
#include "ui/output-panel.h"
#include <QSignalSpy>
#include <QLineEdit>

#include "ui/main-window.h"
#include "ui/terminal-drawer.h"
#include "engine/process-runner.h"

using namespace kai::ui;
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
