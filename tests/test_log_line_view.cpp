#include <QtTest>

#include <QElapsedTimer>
#include <QMouseEvent>

#include "ui/features/output/log-line-view.h"

using namespace kai::ui;

// Unidade da Saída Formatada estilo Grafana/Loki (Command::formattedOutput): só parse/filtro/estado,
// sem renderização.
class TestLogLineView : public QObject {
    Q_OBJECT

private slots:
    void logfmtLineBecomesAStructuredEntry()
    {
        const LogLineEntry e = LogLineModel::parseLine(
            QStringLiteral("time=2026-01-10T12:00:00Z level=warn msg=\"slow \\\"query\\\" a=b\" duration_ms=1840 empty="));
        QVERIFY(e.structured);
        QCOMPARE(e.level, QStringLiteral("warn"));
        QCOMPARE(e.message, QStringLiteral("slow \"query\" a=b"));
        QCOMPARE(e.timestampText, QStringLiteral("2026-01-10T12:00:00Z"));
        QCOMPARE(e.extraFields.value(QStringLiteral("duration_ms")).toString(), QStringLiteral("1840"));
        QVERIFY(e.extraFields.contains(QStringLiteral("empty")));
    }

    void plainTextWithAnEqualsSignIsNotLogfmt()
    {
        QVERIFY(!LogLineModel::parseLine(QStringLiteral("retries = 3")).structured);
        QVERIFY(!LogLineModel::parseLine(QStringLiteral("export PATH=/usr/bin now")).structured);
        QVERIFY(!LogLineModel::parseLine(QStringLiteral("a=1 b=2")).structured); // nenhum nível, mensagem ou hora
        QVERIFY(!LogLineModel::parseLine(QStringLiteral("level=info msg=\"unterminated")).structured);
    }

    // Cards colapsados têm todos a mesma altura; expandir cresce só aquele card, e o clique no chevron (no começo da linha)
    // é o que alterna — o resto da linha só seleciona.
    void expandingACardGrowsOnlyThatCardAndTheChevronToggles()
    {
        LogLineView view;
        view.resize(640, 400);
        view.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"first\",\"time\":\"2026-01-01T10:00:00Z\",\"a\":1,\"b\":2}\n"
                                       "{\"level\":\"error\",\"msg\":\"second\",\"time\":\"2026-01-01T10:00:01Z\"}\n"));
        view.show();
        QCOMPARE(view.logModel()->rowCount(), 2);
        const int collapsed = view.sizeHintForRow(0);
        QCOMPARE(view.sizeHintForRow(1), collapsed);

        const QRect first = view.visualRect(view.logModel()->index(0));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, first.center() + QPoint(200, 0)); // fora do chevron
        QVERIFY(!view.logModel()->entryAt(0).expanded);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(first.left() + 14, first.top() + collapsed / 2));
        QVERIFY(view.logModel()->entryAt(0).expanded);
        QVERIFY(view.sizeHintForRow(0) > collapsed + 40); // mensagem + horário completo + 2 campos
        QCOMPARE(view.sizeHintForRow(1), collapsed);
    }

    // Linha JSON com os aliases mais comuns (level/msg/time) reconhecidos.
    void parsesStructuredLineWithCommonAliases()
    {
        const LogLineEntry e = LogLineModel::parseLine(
            QStringLiteral(R"({"level":"warn","msg":"slow query","time":"12:00:05","duration_ms":342})"));
        QVERIFY(e.structured);
        QCOMPARE(e.level, QStringLiteral("warn"));
        QCOMPARE(e.levelRaw, QStringLiteral("warn"));
        QCOMPARE(e.message, QStringLiteral("slow query"));
        QCOMPARE(e.timestampText, QStringLiteral("12:00:05"));
        QCOMPARE(e.extraFields.size(), 1);
        QVERIFY(e.extraFields.contains(QStringLiteral("duration_ms")));
    }

    // Aliases alternativos (severity/message/@timestamp) também são
    // reconhecidos, e a comparação de chave ignora capitalização.
    void recognizesAlternateKeyAliasesCaseInsensitively()
    {
        const LogLineEntry e = LogLineModel::parseLine(
            QStringLiteral(R"({"Severity":"ERROR","Message":"db down","@timestamp":"2026-01-01T00:00:00Z"})"));
        QVERIFY(e.structured);
        QCOMPARE(e.level, QStringLiteral("error"));
        QCOMPARE(e.message, QStringLiteral("db down"));
        QCOMPARE(e.timestampText, QStringLiteral("2026-01-01T00:00:00Z"));
    }

    // Linha que não é um objeto JSON válido cai pro texto cru, sem card vazio.
    void nonJsonLineStaysRaw()
    {
        const LogLineEntry e = LogLineModel::parseLine(QStringLiteral("plain shell output, not json"));
        QVERIFY(!e.structured);
        QCOMPARE(e.raw, QStringLiteral("plain shell output, not json"));
    }

    // JSON válido mas de tipo array/escalar (não objeto) também cai pro
    // texto cru — só objetos viram "registro de log".
    void jsonArrayIsNotTreatedAsStructured()
    {
        const LogLineEntry e = LogLineModel::parseLine(QStringLiteral("[1,2,3]"));
        QVERIFY(!e.structured);
    }

    // Muitos shells emitem uma sequência OSC (título da janela) no início do processo; só CSI era removida
    // antes do parse, então o "{" deixava de ser o primeiro caractere e a linha caía pro texto cru.
    // Variantes de terminador: BEL (\x07) e ST (ESC \\).
    void jsonLineWithLeadingOscSequenceStillParsesAsStructured()
    {
        const QString withBel = QStringLiteral("\x1b]0;My Terminal Title\x07")
            + QStringLiteral(R"({"level":"info","msg":"started"})");
        const LogLineEntry e1 = LogLineModel::parseLine(withBel);
        QVERIFY(e1.structured);
        QCOMPARE(e1.level, QStringLiteral("info"));
        QCOMPARE(e1.message, QStringLiteral("started"));

        const QString withSt = QStringLiteral("\x1b]0;My Terminal Title\x1b\\")
            + QStringLiteral(R"({"level":"info","msg":"started"})");
        const LogLineEntry e2 = LogLineModel::parseLine(withSt);
        QVERIFY(e2.structured);
        QCOMPARE(e2.level, QStringLiteral("info"));
    }

    // Loggers colam um prefixo antes do objeto (timestamp cru, tag de nível) e parseLine exigia
    // front()=='{'. Agora acha o primeiro '{' e valida dali pra frente.
    void jsonLineWithTextPrefixBeforeBraceStillParsesAsStructured()
    {
        const LogLineEntry e = LogLineModel::parseLine(
            QStringLiteral(R"(2024-01-01T12:00:00Z {"level":"info","msg":"started"})"));
        QVERIFY(e.structured);
        QCOMPARE(e.level, QStringLiteral("info"));
        QCOMPARE(e.message, QStringLiteral("started"));
    }

    // Mesmo caso via appendText (o gate de entrada no parse também aceita contains('{')).
    void appendTextRecognizesJsonLineWithTextPrefix()
    {
        LogLineModel model;
        model.appendText(QStringLiteral(R"(INFO: {"level":"warn","msg":"disk low"})") + QStringLiteral("\n"));
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.entryAt(0).structured);
        QCOMPARE(model.entryAt(0).message, QStringLiteral("disk low"));
    }

    // Uma chave solta no meio de texto comum (ex: um trecho de código
    // colado no log) não deve ser tratada como início de um JSON quebrado
    // por wrap — só entra em modo de acumulação quando o balanço de chaves
    // da linha fica REALMENTE aberto no fim dela.
    void looseBraceInPlainTextDoesNotTriggerAccumulation()
    {
        LogLineModel model;
        model.appendText(QStringLiteral("void foo() { return bar(); }\n"));
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(!model.entryAt(0).structured);
        // A próxima linha normal não foi engolida pela acumulação: funde no mesmo grupo de log cru (ver insertRawLine).
        model.appendText(QStringLiteral("next line of output\n"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.entryAt(0).rawGroupTexts.size(), 2);
    }

    // appendText em streaming: linha fatiada entre duas chamadas vira um só registro.
    void slicedChunksProduceOneEntry()
    {
        LogLineModel model;
        model.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"star"));
        QCOMPARE(model.rowCount(), 0); // linha ainda incompleta, nada visível
        model.appendText(QStringLiteral("ted\"}\n"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.entryAt(0).message, QStringLiteral("started"));
    }

    // Busca: substring em raw/mensagem/campos extras, case-insensitive. Não esconde linha nenhuma (filtrar
    // não deixava o que "pular entre"), só marca as que casam e a atual.
    void searchTermMarksMatchesWithoutHidingRows()
    {
        LogLineModel model;
        model.appendText(QStringLiteral(
            "{\"level\":\"info\",\"msg\":\"server started\"}\n"
            "{\"level\":\"warn\",\"msg\":\"slow query\",\"host\":\"db-1\"}\n"
            "plain line mentioning db-1 too\n"));
        QCOMPARE(model.rowCount(), 3);

        model.setSearchTerm(QStringLiteral("DB-1")); // maiúsculo de propósito
        QCOMPARE(model.rowCount(), 3); // nenhuma linha some
        QCOMPARE(model.matchCount(), 2); // o registro com host=db-1 + a linha crua
        QVERIFY(!model.rowMatches(0));
        QVERIFY(model.rowMatches(1));
        QVERIFY(model.rowMatches(2));
        QCOMPARE(model.currentMatchOrdinal(), 1);
        QVERIFY(model.isCurrentMatchRow(1));

        QCOMPARE(model.goToNextMatch(), 2);
        QCOMPARE(model.currentMatchOrdinal(), 2);
        QVERIFY(model.isCurrentMatchRow(2));
        QCOMPARE(model.goToNextMatch(), 1); // dá a volta (wrap-around)

        QCOMPARE(model.goToPreviousMatch(), 2);

        model.setSearchTerm(QString());
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.matchCount(), 0);
        QCOMPARE(model.currentMatchOrdinal(), 0);
    }

    // Filtro por campo estilo Grafana/Loki ("level:error").
    void fieldValueQuerySyntaxFiltersByNamedField()
    {
        LogLineModel model;
        model.appendText(QStringLiteral(
            "{\"level\":\"info\",\"msg\":\"server started\",\"service\":\"api\"}\n"
            "{\"level\":\"error\",\"msg\":\"db down\",\"service\":\"api\"}\n"
            "{\"level\":\"error\",\"msg\":\"disk full\",\"service\":\"worker\"}\n"));
        QCOMPARE(model.knownFieldNames(), (QStringList{QStringLiteral("level"), QStringLiteral("service")}));
        QCOMPARE(model.knownLevelValues(), (QStringList{QStringLiteral("error"), QStringLiteral("info")}));

        model.setSearchTerm(QStringLiteral("level:error"));
        QCOMPARE(model.matchCount(), 2);
        QVERIFY(!model.rowMatches(0));
        QVERIFY(model.rowMatches(1));
        QVERIFY(model.rowMatches(2));

        // Dois tokens combinam em E: só a entrada com AMBOS os campos bate.
        model.setSearchTerm(QStringLiteral("level:error service:worker"));
        QCOMPARE(model.matchCount(), 1);
        QVERIFY(model.rowMatches(2));
        QVERIFY(!model.rowMatches(1));
    }

    // Linha em branco é descartada; linhas cruas não-vazias consecutivas (sem JSON entre elas) se fundem
    // num único grupo de "log da aplicação".
    void blankLinesAreDiscardedAndConsecutiveRawLinesGroup()
    {
        LogLineModel model;
        model.appendText(QStringLiteral("linha 1\n\n   \nlinha 2\n"));
        QCOMPARE(model.rowCount(), 1);
        const LogLineEntry &e = model.entryAt(0);
        QVERIFY(e.isRawGroup);
        QCOMPARE(e.rawGroupTexts, (QStringList{QStringLiteral("linha 1"), QStringLiteral("linha 2")}));
        QCOMPARE(e.raw, QStringLiteral("linha 1\nlinha 2"));
    }

    // Uma linha crua entre dois cards JSON NÃO funde com nada (não há
    // grupo aberto adjacente) — vira um grupo de tamanho 1 sozinho.
    void rawLineBetweenJsonEntriesFormsItsOwnGroup()
    {
        LogLineModel model;
        model.appendText(QStringLiteral(
            "{\"level\":\"info\",\"msg\":\"a\"}\n"
            "linha solta\n"
            "{\"level\":\"info\",\"msg\":\"b\"}\n"));
        QCOMPARE(model.rowCount(), 3);
        QVERIFY(model.entryAt(0).structured);
        QVERIFY(model.entryAt(1).isRawGroup);
        QCOMPARE(model.entryAt(1).rawGroupTexts.size(), 1);
        QVERIFY(model.entryAt(2).structured);
    }

    // Linha quebrada pelo terminal: um objeto JSON de uma linha que o wrap quebrou em várias linhas físicas
    // (cortando até palavras) vira UM registro estruturado.
    void reassemblesJsonWrappedAcrossPhysicalLines()
    {
        LogLineModel model;
        // Simula o wrap: quebra "service" em "servic"+"e" e a mensagem no meio.
        model.appendText(QStringLiteral(
            "{\"level\":\"info\",\"message\":\"Verificando Pedidos Cance\n"
            "lados. Luzzoo x Shopee\",\"servic\n"
            "e\":\"mz_pedido_marketplace_shopee_job\"}\n"));
        QCOMPARE(model.rowCount(), 1);
        const LogLineEntry &e = model.entryAt(0);
        QVERIFY(e.structured);
        QCOMPARE(e.level, QStringLiteral("info"));
        QCOMPARE(e.message, QStringLiteral("Verificando Pedidos Cancelados. Luzzoo x Shopee"));
        QVERIFY(e.extraFields.contains(QStringLiteral("service")));
    }

    // Um "{" solto que nunca fecha (não é JSON quebrado, só texto comum
    // começando com chave) não trava pra sempre acumulando — desiste depois
    // de um teto de linhas e devolve como linhas cruas, sem perder dado.
    void giveUpAccumulatingAfterTooManyLinesFallsBackToRaw()
    {
        LogLineModel model;
        QString text = QStringLiteral("{ isto nunca fecha\n");
        for (int i = 0; i < 90; ++i) {
            text += QStringLiteral("linha %1\n").arg(i);
        }
        model.appendText(text);
        // As linhas cruas devolvidas (e as seguintes, já fora da acumulação) são consecutivas: fundem num único
        // grupo (ver blankLinesAreDiscardedAndConsecutiveRawLinesGroup).
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(!model.entryAt(0).structured);
        QVERIFY(model.entryAt(0).isRawGroup);
        QVERIFY(model.entryAt(0).rawGroupTexts.size() > 1);
    }

    // Expandir/colapsar: cards JSON e grupos de log cru alternam (toda linha crua vira ao menos um grupo de tamanho 1).
    void toggleExpandedWorksForBothJsonCardsAndRawGroups()
    {
        LogLineModel model;
        model.appendText(QStringLiteral(
            "{\"level\":\"info\",\"msg\":\"ok\",\"x\":1}\n"
            "plain line\n"));
        QVERIFY(!model.entryAt(0).expanded);
        model.toggleExpanded(0);
        QVERIFY(model.entryAt(0).expanded);
        model.toggleExpanded(0);
        QVERIFY(!model.entryAt(0).expanded);

        QVERIFY(model.entryAt(1).isRawGroup);
        QVERIFY(!model.entryAt(1).expanded);
        model.toggleExpanded(1);
        QVERIFY(model.entryAt(1).expanded);
        model.toggleExpanded(1);
        QVERIFY(!model.entryAt(1).expanded);
    }

    void clearLogResetsEverything()
    {
        LogLineModel model;
        model.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"a\"}\n"));
        QCOMPARE(model.rowCount(), 1);
        model.clearLog();
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(model.isEmpty());
    }

    // Filtro de nível: multiselect que esconde (a busca só destaca). Grupo de log cru nunca some.
    void levelFilterHidesNonMatchingStructuredRowsButNeverRawGroups()
    {
        LogLineModel model;
        model.appendText(QStringLiteral(
            "{\"level\":\"info\",\"msg\":\"a\"}\n"
            "{\"level\":\"warn\",\"msg\":\"b\"}\n"
            "{\"level\":\"error\",\"msg\":\"c\"}\n"
            "linha crua\n"));
        QCOMPARE(model.rowCount(), 4);

        model.setLevelFilter({QStringLiteral("error")});
        QCOMPARE(model.rowCount(), 2); // só o card "error" + o grupo cru (nunca escondido)
        QVERIFY(model.entryAt(0).structured);
        QCOMPARE(model.entryAt(0).level, QStringLiteral("error"));
        QVERIFY(model.entryAt(1).isRawGroup);

        // Card "info" chegando depois do filtro continua escondido.
        model.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"d\"}\n"));
        QCOMPARE(model.rowCount(), 2);

        // Limpa o filtro (conjunto vazio) — tudo reaparece, na ordem certa.
        model.setLevelFilter({});
        QCOMPARE(model.rowCount(), 5);
    }

    // O MODEL só sinaliza cada linha de erro estruturada (o "primeira vez" é de
    // OutputPanel::m_firstErrorNotifiedThisRun): uma vez por linha de erro e nenhuma para níveis não-erro
    // (predicado de colorForLevel: error/err/fatal/crit).
    void errorLineDetectedFiresOncePerStructuredErrorLine()
    {
        LogLineModel model;
        QSignalSpy spy(&model, &LogLineModel::errorLineDetected);
        model.appendText(QStringLiteral(
            "{\"level\":\"error\",\"msg\":\"a\"}\n"
            "{\"level\":\"error\",\"msg\":\"b\"}\n"));
        QCOMPARE(spy.count(), 2);
    }

    void errorLineDetectedIgnoresNonErrorLevelsAndRawLines()
    {
        LogLineModel model;
        QSignalSpy spy(&model, &LogLineModel::errorLineDetected);
        model.appendText(QStringLiteral(
            "{\"level\":\"info\",\"msg\":\"a\"}\n"
            "{\"level\":\"warn\",\"msg\":\"b\"}\n"
            "{\"level\":\"debug\",\"msg\":\"c\"}\n"
            "linha crua qualquer\n"));
        QCOMPARE(spy.count(), 0);
    }

    // "fatal"/"crit" contam como erro também (mesmo predicado de
    // colorForLevel), não só o literal "error".
    void errorLineDetectedAlsoFiresForFatalAndCritLevels()
    {
        LogLineModel model;
        QSignalSpy spy(&model, &LogLineModel::errorLineDetected);
        model.appendText(QStringLiteral(
            "{\"level\":\"fatal\",\"msg\":\"a\"}\n"
            "{\"level\":\"crit\",\"msg\":\"b\"}\n"));
        QCOMPARE(spy.count(), 2);
    }

    // O texto em espera (rajada agrupada em ~50 ms) é entregue a quem lê o modelo: nada some nem chega fora de ordem.
    void burstsAreCoalescedButNeverLost()
    {
        LogLineView view;
        view.resize(640, 400);
        view.show();
        view.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"one\"}\n"));
        view.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"two\"}\n"));
        view.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"thr"));
        view.appendText(QStringLiteral("ee\"}\n"));
        QCOMPARE(view.logModel()->rowCount(), 3);
        QCOMPARE(view.logModel()->entryAt(2).message, QStringLiteral("three"));
        view.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"four\"}\n"));
        view.clearLog();
        QTest::qWait(120);
        QCOMPARE(view.logModel()->rowCount(), 0); // clearLog descarta o que estava em espera
    }

    // Saída longa em trechos pequenos (como a leitura do processo): o custo não pode crescer com o quadrado das linhas.
    // Antes, 20 mil linhas JSON em trechos de 10 levavam ~30 s (layout de todas as linhas a cada trecho).
    void aLongFormattedLogInSmallChunksStaysFast()
    {
        LogLineView view;
        view.resize(800, 500);
        view.show();
        QElapsedTimer timer;
        timer.start();
        constexpr int kLines = 20000;
        for (int i = 0; i < kLines; i += 10) {
            QString chunk;
            for (int j = 0; j < 10; ++j) {
                chunk += QStringLiteral("{\"level\":\"info\",\"msg\":\"request %1 handled\",\"time\":\"2026-01-01T12:00:00Z\",\"id\":%1}\n").arg(i + j);
            }
            view.appendText(chunk);
            QCoreApplication::processEvents();
        }
        QCOMPARE(view.logModel()->rowCount(), kLines);
        QVERIFY2(timer.elapsed() < 8000, qPrintable(QStringLiteral("%1 ms").arg(timer.elapsed())));
    }

    // Linhas cruas seguidas formam um grupo só; o texto juntado (busca/copiar) continua igual à junção das linhas.
    void aRawGroupKeepsItsJoinedTextAsLinesArrive()
    {
        LogLineModel model;
        model.appendText(QStringLiteral("alpha\nbeta\n"));
        model.appendText(QStringLiteral("gamma\n"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.entryAt(0).rawGroupTexts.size(), 3);
        QCOMPARE(model.entryAt(0).raw, QStringLiteral("alpha\nbeta\ngamma"));
    }
};

QTEST_MAIN(TestLogLineView)
#include "test_log_line_view.moc"
