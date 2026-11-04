#include <QTest>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QJsonDocument>
#include <QJsonObject>

#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/foldable-json-view.h"
#include "utils/translation-manager.h"

using namespace kai::ui;

// JsonViewerWidget com texto misto (cabeçalho de log + corpo), sobretudo quando o cabeçalho tem chaves
// de template como "{{BASE_URL}}" que confundiriam um parser que pegasse o primeiro '{'.
// Os testes inspecionam o texto pretty-printed (FoldableJsonView, QPlainTextEdit::toPlainText).
class TestJsonViewer : public QObject {
    Q_OBJECT

private slots:
    void parsesPureJson()
    {
        JsonViewerWidget w;
        w.setJsonText(QStringLiteral("{\"a\": 1, \"b\": {\"c\": 2}}"));
        auto *view = w.findChild<QPlainTextEdit *>();
        QVERIFY(view != nullptr);
        const QString text = view->toPlainText();
        QVERIFY2(text.contains(QStringLiteral("\"a\": 1")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("\"c\": 2")), qPrintable(text));
    }

    // Log "HTTP GET {{BASE_URL}}/get -> status 200 (...)" seguido do corpo JSON: o '{' do template não atrapalha.
    void parsesJsonAfterHeaderWithTemplateBraces()
    {
        const QString mixed = QStringLiteral(
            "HTTP GET {{BASE_URL}}/get -> status 200 (318 bytes)\n"
            "{\n"
            "  \"args\": {},\n"
            "  \"headers\": { \"Host\": \"httpbin.org\" },\n"
            "  \"origin\": \"179.223.204.207\",\n"
            "  \"url\": \"https://httpbin.org/get\"\n"
            "}");
        JsonViewerWidget w;
        w.setJsonText(mixed);
        auto *view = w.findChild<QPlainTextEdit *>();
        QVERIFY(view != nullptr);
        const QString text = view->toPlainText();
        QVERIFY2(text.contains(QStringLiteral("\"args\"")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("\"headers\"")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("\"origin\"")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("\"url\"")), qPrintable(text));
        // Não caiu no fallback de texto cru (o cabeçalho do log some).
        QVERIFY2(!text.contains(QStringLiteral("HTTP GET")), qPrintable(text));
    }

    void fallsBackToTextWhenNoJson()
    {
        JsonViewerWidget w;
        const QString raw = QStringLiteral("apenas um texto sem json");
        w.setJsonText(raw);
        auto *view = w.findChild<QPlainTextEdit *>();
        QVERIFY(view != nullptr);
        // Fallback: texto cru como veio.
        QCOMPARE(view->toPlainText(), raw);
        QCOMPARE(w.formattedJson(), raw);
    }

    // Log com o eco da requisição (um JSON) e a resposta (outro JSON): pega só o primeiro JSON
    // balanceado (o eco), não um span com os dois.
    void extractsOnlyFirstBalancedJsonNotTwoConcatenated()
    {
        const QString twoJsons = QStringLiteral(
            "Enviando corpo:\n"
            "{ \"nome\": \"Kai\", \"tipo\": \"runner\" }\n"
            "Resposta:\n"
            "{ \"ok\": true, \"echo\": { \"nome\": \"Kai\" } }");
        JsonViewerWidget w;
        w.setJsonText(twoJsons);
        auto *view = w.findChild<QPlainTextEdit *>();
        QVERIFY(view != nullptr);
        const QString text = view->toPlainText();
        // Primeiro JSON = { nome, tipo }: sem "echo" nem "ok" (do segundo objeto).
        QVERIFY2(text.contains(QStringLiteral("\"nome\": \"Kai\"")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("\"tipo\": \"runner\"")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("\"echo\"")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("\"ok\"")), qPrintable(text));
    }

    // A detecção não pode ser só "contém '{'" (falso positivo: botão acendia e a árvore abria vazia);
    // com vários JSONs no log vale o mais recente (último).
    void extractJsonBlockRejectsNonJsonAndPicksLast()
    {
        // Sem JSON de verdade: chaves de template e log de shell.
        const QString noJson = QStringLiteral("url={{BASE_URL}}/x\nfor i in {1..5}; do echo $i; done\n");
        QVERIFY2(JsonViewerWidget::extractJsonBlock(noJson).isEmpty(),
                 "texto sem JSON valido nao deveria ser detectado como JSON");

        const QString twoJson = QStringLiteral(
            "HTTP 200\n{\"resposta\": \"antiga\"}\nHTTP 200\n{\"resposta\": \"nova\"}\n");
        const QString block = JsonViewerWidget::extractJsonBlock(twoJson);
        QVERIFY(!block.isEmpty());
        QVERIFY2(block.contains(QStringLiteral("nova")),
                 "deveria extrair o ULTIMO JSON do log (o mais recente)");
        QVERIFY(!block.contains(QStringLiteral("antiga")));
    }

    // Log terminando em `"tags": []` seguido de "[Pipeline concluído...]": a varredura de trás para frente
    // casava o array vazio. Deve retornar o documento de nível superior inteiro.
    void picksOuterDocumentNotTrailingEmptyArray()
    {
        const QString log = QStringLiteral(
            "HTTP GET http://x/api/docs.json -> status 200 (13252 bytes){\n"
            "    \"openapi\": \"3.0.0\",\n"
            "    \"info\": {\"title\": \"Exemplo-API-Notifications\"},\n"
            "    \"tags\": [\n    ]\n"
            "}\n\n[Pipeline concluido com sucesso]\n");
        const QString block = JsonViewerWidget::extractJsonBlock(log);
        QVERIFY(!block.isEmpty());
        QVERIFY2(block.contains(QStringLiteral("Exemplo-API-Notifications")),
                 "deveria extrair o documento inteiro, nao o 'tags: []' do fim");
        QVERIFY2(block.startsWith(QLatin1Char('{')), "deve comecar no objeto raiz");
        const QJsonDocument d = QJsonDocument::fromJson(block.toUtf8());
        QVERIFY(d.isObject());
        QCOMPARE(d.object().value(QStringLiteral("openapi")).toString(), QStringLiteral("3.0.0"));
    }

    // Colapso inline: objeto aninhado vira par dobrável; colapsar esconde as linhas internas sem alterar o texto puro.
    void nestedObjectIsFoldableAndTogglesVisibility()
    {
        JsonViewerWidget w;
        w.setJsonText(QStringLiteral(
            "{\"user\": {\"id\": 1, \"name\": \"Ana\"}, \"active\": true}"));
        auto *view = w.findChild<FoldableJsonView *>();
        QVERIFY(view != nullptr);

        int startBlock = -1;
        for (QTextBlock b = view->document()->firstBlock(); b.isValid(); b = b.next()) {
            if (b.text().trimmed().endsWith(QLatin1Char('{')) && b.text().contains(QStringLiteral("user"))) {
                startBlock = b.blockNumber();
                break;
            }
        }
        QVERIFY2(startBlock >= 0, "deveria haver uma linha de abertura pro objeto 'user'");
        QVERIFY(view->foldStartAtBlock(startBlock));
        QVERIFY(!view->isFoldCollapsed(startBlock));

        view->toggleFold(startBlock);
        QVERIFY(view->isFoldCollapsed(startBlock));
        QTextBlock inner = view->document()->findBlockByNumber(startBlock + 1);
        QVERIFY(inner.isValid());
        QVERIFY(!inner.isVisible());

        view->toggleFold(startBlock);
        QVERIFY(!view->isFoldCollapsed(startBlock));
        QVERIFY(view->document()->findBlockByNumber(startBlock + 1).isVisible());
        QVERIFY(view->toPlainText().contains(QStringLiteral("\"name\": \"Ana\"")));
    }

    // Objeto de nível 1 com centenas de linhas nascia auto-colapsado (limiar de 40 linhas):
    // JsonViewerWidget não deve auto-colapsar nada ao carregar.
    void largeJsonBodyIsNotAutoCollapsedOnLoad()
    {
        QJsonObject root;
        for (int i = 0; i < 200; ++i) {
            root.insert(QStringLiteral("field_%1").arg(i), QStringLiteral("value_%1").arg(i));
        }
        const QString bigJson = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));

        JsonViewerWidget w;
        w.setJsonText(bigJson);
        auto *view = w.findChild<FoldableJsonView *>();
        QVERIFY(view != nullptr);

        // Nenhum bloco colapsado após o carregamento: todo campo (field_0..field_199) visível no texto renderizado.
        for (QTextBlock b = view->document()->firstBlock(); b.isValid(); b = b.next()) {
            QVERIFY2(b.isVisible(), qPrintable(QStringLiteral("bloco oculto por auto-colapso: '%1'").arg(b.text())));
        }
        QVERIFY(view->toPlainText().contains(QStringLiteral("\"field_0\"")));
        QVERIFY(view->toPlainText().contains(QStringLiteral("\"field_199\"")));
    }
};

QTEST_MAIN(TestJsonViewer)
#include "test_json_viewer.moc"
