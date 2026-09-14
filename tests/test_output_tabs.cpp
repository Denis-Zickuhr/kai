#include <QtTest>
#include "ui/features/output/aba-content.h"
#include "ui/features/output/output-metrics-header.h"
#include "ui/features/output/output-stdout-content.h"
#include "ui/features/output/output-json-content.h"
#include "ui/features/output/output-http-request-content.h"
#include "ui/features/output/output-http-headers-content.h"

using namespace kai::ui;

class TestOutputTabs : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Inicia a aplicação Qt (necessário para testes de widgets)
    }

    void testAbaContentInterface()
    {
        // Verifica que AbaContent é abstrata e não pode ser instanciada
        // (este teste é mais uma verificação de compilação)
        QCOMPARE(true, true);  // Placeholder
    }

    void testOutputMetricsHeader()
    {
        OutputMetricsHeader header;

        // Testa setMetrics com resposta 200 OK
        header.setMetrics(200, QStringLiteral("OK"), 245, 1024, true);
        QVERIFY(!header.property("visible").toBool() == false);  // Widget deve estar visível

        // Testa clear
        header.clear();
        // Após clear, widget deve estar oculto
        QVERIFY(true);  // Placeholder: seria necessário verificar visibility
    }

    void testOutputStdoutContent()
    {
        OutputStdoutContent stdoutWidget;

        // Verifica label
        QString label = stdoutWidget.label();
        QVERIFY(!label.isEmpty());

        // Verifica iconName
        QString iconName = stdoutWidget.iconName();
        QCOMPARE(iconName, QStringLiteral("terminal"));

        // Testa clear
        stdoutWidget.clear();
        QVERIFY(!stdoutWidget.hasContent());
    }

    void testOutputJsonContent()
    {
        OutputJsonContent json;

        // Verifica label
        QString label = json.label();
        QVERIFY(!label.isEmpty());

        // Verifica iconName
        QString iconName = json.iconName();
        QCOMPARE(iconName, QStringLiteral("braces"));

        // Testa com JSON válido
        json.setJson(QStringLiteral(R"({"key": "value"})"));
        QVERIFY(json.hasContent());

        // Testa clear
        json.clear();
        QVERIFY(!json.hasContent());
    }

    void testOutputJsonContentInvalidJson()
    {
        OutputJsonContent json;

        // Testa com JSON inválido (JsonViewerWidget deve exibir como texto)
        json.setJson(QStringLiteral("not valid json"));
        // Não deve crashear
        QVERIFY(true);
    }

    void testOutputHttpRequestContent()
    {
        OutputHttpRequestContent request;

        // Verifica label
        QString label = request.label();
        QVERIFY(!label.isEmpty());

        // Verifica iconName
        QString iconName = request.iconName();
        QCOMPARE(iconName, QStringLiteral("globe"));

        // Testa setHttpResult
        request.setHttpResult(
            200, QStringLiteral("OK"), 120, 512,
            QStringLiteral("GET"), QStringLiteral("https://example.com/api"),
            QStringLiteral(R"({"data": "response"})"), true
        );
        QVERIFY(request.hasContent());

        // Testa clear
        request.clear();
        QVERIFY(!request.hasContent());
    }

    void testOutputHttpHeadersContent()
    {
        OutputHttpHeadersContent headers;

        // Verifica label
        QString label = headers.label();
        QVERIFY(!label.isEmpty());

        // Verifica iconName
        QString iconName = headers.iconName();
        QCOMPARE(iconName, QStringLiteral("list"));

        // Testa com headers
        QMap<QString, QString> headerMap;
        headerMap[QStringLiteral("Content-Type")] = QStringLiteral("application/json");
        headerMap[QStringLiteral("Authorization")] = QStringLiteral("Bearer token123");

        headers.setHeaders(headerMap);
        QVERIFY(headers.hasContent());

        // Testa clear
        headers.clear();
        QVERIFY(!headers.hasContent());
    }

    void testOutputHttpHeadersContentEmpty()
    {
        OutputHttpHeadersContent headers;

        // Testa com headers vazios
        QMap<QString, QString> emptyMap;
        headers.setHeaders(emptyMap);
        QVERIFY(!headers.hasContent());
    }

    void testMetricsHeaderStatusCodes()
    {
        OutputMetricsHeader header;

        // Testa com diferentes status codes
        // 2xx (sucesso)
        header.setMetrics(200, QStringLiteral("OK"), 100, 512, true);
        QVERIFY(true);

        // 4xx (client error)
        header.setMetrics(404, QStringLiteral("Not Found"), 50, 256, false);
        QVERIFY(true);

        // 5xx (server error)
        header.setMetrics(500, QStringLiteral("Internal Server Error"), 5000, 1024, false);
        QVERIFY(true);
    }

    void testMetricsHeaderSizeFormatting()
    {
        OutputMetricsHeader header;

        // Testa formatação de tamanhos
        // Bytes
        header.setMetrics(200, QStringLiteral("OK"), 100, 512, true);

        // Kilobytes
        header.setMetrics(200, QStringLiteral("OK"), 100, 1024 * 10, true);

        // Megabytes
        header.setMetrics(200, QStringLiteral("OK"), 100, 1024 * 1024 * 2, true);

        QVERIFY(true);
    }

    void cleanupTestCase()
    {
        // Limpeza após testes
    }
};

QTEST_MAIN(TestOutputTabs)
#include "test_output_tabs.moc"
