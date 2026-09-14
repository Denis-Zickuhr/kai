#include <QTest>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QJsonDocument>
#include <QJsonObject>

#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/foldable-json-view.h"
#include "utils/translation-manager.h"

using namespace kai::ui;

// Cobre a extração/parse de JSON do JsonViewerWidget quando o texto vem
// MISTURADO (cabeçalho de log + corpo), especialmente quando o cabeçalho
// contém chaves de template como "{{BASE_URL}}" que confundiriam um parser
// que pegasse o primeiro '{'. Bug reportado (json-erro.png): o viewer
// mostrava "(texto)" em vez do JSON.
//
// Reescrito para a versão em TEXTO LIVRE (FoldableJsonView) — pedido do
// usuário: "quero um editor de json mais bonito e fluido [...] estilo
// insomnia/postman". Os testes originais inspecionavam a árvore
// (QTreeWidget::topLevelItemCount); agora inspecionam o texto
// pretty-printed exibido (QPlainTextEdit::toPlainText), preservando a
// MESMA intenção de cobertura (parse correto / fallback correto).
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

    // O caso da print: log "HTTP GET {{BASE_URL}}/get -> status 200 (...)"
    // seguido do corpo JSON. O '{' de "{{BASE_URL}}" NÃO deve atrapalhar.
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
        // Reconheceu o JSON (as 4 chaves aparecem formatadas)...
        QVERIFY2(text.contains(QStringLiteral("\"args\"")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("\"headers\"")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("\"origin\"")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("\"url\"")), qPrintable(text));
        // ...e não caiu no fallback de texto cru (o cabeçalho do log some,
        // só o JSON pretty-printed fica).
        QVERIFY2(!text.contains(QStringLiteral("HTTP GET")), qPrintable(text));
    }

    void fallsBackToTextWhenNoJson()
    {
        JsonViewerWidget w;
        const QString raw = QStringLiteral("apenas um texto sem json");
        w.setJsonText(raw);
        auto *view = w.findChild<QPlainTextEdit *>();
        QVERIFY(view != nullptr);
        // Fallback: mostra o texto cru tal como veio (sem árvore, sem
        // marcador "(texto)" artificial).
        QCOMPARE(view->toPlainText(), raw);
        QCOMPARE(w.formattedJson(), raw);
    }

    // Bug reportado: com o log contendo o ECO da requisição (um JSON) e a
    // RESPOSTA (outro JSON), o viewer capturava um span com os DOIS objetos
    // e mostrava campos duplicados/quebrados. Deve pegar só o primeiro JSON
    // balanceado (o eco), com suas chaves corretas.
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
        // Primeiro JSON balanceado = { nome, tipo } — não deve conter "echo"
        // nem "ok" (que só existem no segundo objeto).
        QVERIFY2(text.contains(QStringLiteral("\"nome\": \"Kai\"")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("\"tipo\": \"runner\"")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("\"echo\"")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("\"ok\"")), qPrintable(text));
    }

    // REGRESSÃO (achados de auditoria C2/A3): a detecção antiga era só
    // "contém '{'" -> falso positivo (botão acendia e a árvore abria vazia);
    // e com vários JSONs no log o correto é o MAIS RECENTE (o último).
    void extractJsonBlockRejectsNonJsonAndPicksLast()
    {
        // Sem JSON de verdade: chaves de template e log de shell.
        const QString noJson = QStringLiteral("url={{BASE_URL}}/x\nfor i in {1..5}; do echo $i; done\n");
        QVERIFY2(JsonViewerWidget::extractJsonBlock(noJson).isEmpty(),
                 "texto sem JSON valido nao deveria ser detectado como JSON");

        // Dois JSONs no log: deve pegar o ULTIMO (resposta mais recente).
        const QString twoJson = QStringLiteral(
            "HTTP 200\n{\"resposta\": \"antiga\"}\nHTTP 200\n{\"resposta\": \"nova\"}\n");
        const QString block = JsonViewerWidget::extractJsonBlock(twoJson);
        QVERIFY(!block.isEmpty());
        QVERIFY2(block.contains(QStringLiteral("nova")),
                 "deveria extrair o ULTIMO JSON do log (o mais recente)");
        QVERIFY(!block.contains(QStringLiteral("antiga")));
    }

    // REGRESSÃO (bug reportado com /api/docs.json): o log termina com
    // "tags": [] seguido de "[Pipeline concluído...]". Uma varredura de trás
    // para frente casava o ARRAY VAZIO primeiro e a janela abria sem nada.
    // Deve retornar o documento de NÍVEL SUPERIOR inteiro.
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

    // NOVO: cobertura do colapso inline (pedido central do ticket). Um
    // objeto aninhado deve virar um par dobrável, e colapsar deve esconder
    // as linhas internas sem alterar o texto puro do documento (o texto
    // continua todo lá — só ficam blocos invisíveis).
    void nestedObjectIsFoldableAndTogglesVisibility()
    {
        JsonViewerWidget w;
        w.setJsonText(QStringLiteral(
            "{\"user\": {\"id\": 1, \"name\": \"Ana\"}, \"active\": true}"));
        auto *view = w.findChild<FoldableJsonView *>();
        QVERIFY(view != nullptr);

        // Acha a linha que abre o objeto "user" (termina em '{').
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
        // A linha logo após o início (ex: "id": 1) deve estar OCULTA.
        QTextBlock inner = view->document()->findBlockByNumber(startBlock + 1);
        QVERIFY(inner.isValid());
        QVERIFY(!inner.isVisible());

        view->toggleFold(startBlock);
        QVERIFY(!view->isFoldCollapsed(startBlock));
        QVERIFY(view->document()->findBlockByNumber(startBlock + 1).isVisible());
        // Texto puro continua intacto independente do colapso.
        QVERIFY(view->toPlainText().contains(QStringLiteral("\"name\": \"Ana\"")));
    }

    // Bug reportado: "estou fazendo um request que me devolve um JSON bem
    // grande, e o sistema renderiza só um pedacinho desse json, deve exibir
    // TUDO" — um objeto de nível 1 com centenas de linhas nascia
    // AUTO-COLAPSADO (limiar de 40 linhas), sobrando só uma linha visível.
    // JsonViewerWidget não deve mais auto-colapsar nada ao carregar.
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

        // Nenhum bloco deve estar colapsado logo após o carregamento — todo
        // campo (field_0..field_199) precisa estar de fato visível no texto
        // renderizado, não só presente no documento por baixo de um fold.
        for (QTextBlock b = view->document()->firstBlock(); b.isValid(); b = b.next()) {
            QVERIFY2(b.isVisible(), qPrintable(QStringLiteral("bloco oculto por auto-colapso: '%1'").arg(b.text())));
        }
        QVERIFY(view->toPlainText().contains(QStringLiteral("\"field_0\"")));
        QVERIFY(view->toPlainText().contains(QStringLiteral("\"field_199\"")));
    }
};

QTEST_MAIN(TestJsonViewer)
#include "test_json_viewer.moc"
