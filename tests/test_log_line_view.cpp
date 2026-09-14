#include <QtTest>

#include "ui/log-line-view.h"

using namespace kai::ui;

// Testes de UNIDADE da Saída Formatada estilo Grafana/Loki
// (Command::formattedOutput) — só a lógica de parse/filtro/estado, sem
// renderização (o realce visual foi verificado à parte via captura
// offscreen antes de shipar).
class TestLogLineView : public QObject {
    Q_OBJECT

private slots:
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

    // Linha que não é um objeto JSON válido cai pro texto cru — não quebra,
    // não vira card vazio.
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

    // appendText em streaming: uma linha que chega fatiada entre duas
    // chamadas só deve virar UM registro (mesma exigência de robustez que
    // o resto da Saída já tem para chunks quebrados).
    void slicedChunksProduceOneEntry()
    {
        LogLineModel model;
        model.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"star"));
        QCOMPARE(model.rowCount(), 0); // linha ainda incompleta, nada visível
        model.appendText(QStringLiteral("ted\"}\n"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.entryAt(0).message, QStringLiteral("started"));
    }

    // Busca: substring em raw/mensagem/campos extras, case-insensitive — NÃO
    // esconde linha nenhuma (pedido do usuário: "o jump não funcionou na
    // view grafana", filtrar escondia as que não batiam e não sobrava nada
    // pra "pular entre"), só marca quais casam + qual é a atual.
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

    // Filtro por campo estilo Grafana/Loki ("level:error") — pedido do
    // usuário: "filtros por campo detectados na fly de pesquisa, e alguns
    // padrões como level e etc".
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

    // Linha em branco é descartada (pedido do usuário: "descartar linhas
    // vazias") — não vira uma linha crua vazia na lista. E como as duas
    // linhas não-vazias são cruas e CONSECUTIVAS (nenhum JSON entre elas),
    // se fundem num único grupo de "log da aplicação" (pedido do usuário:
    // "agrupar as linhas que não são json em grupo").
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

    // RECONSTRUÇÃO DE LINHA QUEBRADA (bug real reportado, com print): um
    // objeto JSON de uma linha só que o terminal quebrou em várias linhas
    // FÍSICAS (wrap na largura da coluna, cortando até palavras ao meio)
    // deve virar UM registro estruturado só, não vários fragmentos crus.
    void reassemblesJsonWrappedAcrossPhysicalLines()
    {
        LogLineModel model;
        // Simula o wrap: quebra "service" em "servic"+"e" e a mensagem no
        // meio — exatamente o padrão do bug relatado.
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
        // Todas as linhas cruas devolvidas (mais as que vieram depois,
        // já fora do modo de acumulação) são CONSECUTIVAS — se fundem num
        // único grupo de log, não uma por linha (ver comentário de
        // blankLinesAreDiscardedAndConsecutiveRawLinesGroup).
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(!model.entryAt(0).structured);
        QVERIFY(model.entryAt(0).isRawGroup);
        QVERIFY(model.entryAt(0).rawGroupTexts.size() > 1); // nada ficou preso no limbo pra sempre
    }

    // Expandir/colapsar: cards JSON e GRUPOS de log cru alternam (pedido do
    // usuário: "agrupar... e botar uma label" implica poder abrir o grupo
    // pra ver as linhas de dentro) — só uma entrada "nua" sem grupo nem
    // estrutura nenhuma não teria o que expandir, mas isso não existe mais
    // no modelo atual (toda linha crua vira ao menos um grupo de tamanho 1).
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

    // Filtro de nível: multiselect que de fato ESCONDE (pedido do usuário:
    // "o que não está selecionado some do render") — diferente da busca
    // (que só destaca). Grupo de log cru nunca some, mesmo filtrado.
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

        // Novo card "info" chegando DEPOIS do filtro aplicado continua
        // escondido — o filtro vale pra conteúdo novo também.
        model.appendText(QStringLiteral("{\"level\":\"info\",\"msg\":\"d\"}\n"));
        QCOMPARE(model.rowCount(), 2);

        // Limpa o filtro (conjunto vazio) — tudo reaparece, na ordem certa.
        model.setLevelFilter({});
        QCOMPARE(model.rowCount(), 5);
    }
};

QTEST_MAIN(TestLogLineView)
#include "test_log_line_view.moc"
