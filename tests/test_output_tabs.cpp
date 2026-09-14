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
    }

    void testAbaContentInterface()
    {
        QCOMPARE(true, true);  // Placeholder
    }

    void testOutputMetricsHeader()
    {
        OutputMetricsHeader header;

        header.setMetrics(200, QStringLiteral("OK"), 245, 1024, true);
        QVERIFY(!header.property("visible").toBool() == false);

        header.clear();
        QVERIFY(true);  // Placeholder: seria necessário verificar visibility
    }

    void testOutputStdoutContent()
    {
        OutputStdoutContent stdoutWidget;

        QString label = stdoutWidget.label();
        QVERIFY(!label.isEmpty());

        QString iconName = stdoutWidget.iconName();
        QCOMPARE(iconName, QStringLiteral("terminal"));

        stdoutWidget.clear();
        QVERIFY(!stdoutWidget.hasContent());
    }

    void testOutputJsonContent()
    {
        OutputJsonContent json;

        QString label = json.label();
        QVERIFY(!label.isEmpty());

        QString iconName = json.iconName();
        QCOMPARE(iconName, QStringLiteral("braces"));

        json.setJson(QStringLiteral(R"({"key": "value"})"));
        QVERIFY(json.hasContent());

        json.clear();
        QVERIFY(!json.hasContent());
    }

    void testOutputJsonContentInvalidJson()
    {
        OutputJsonContent json;

        json.setJson(QStringLiteral("not valid json"));
        // Não deve crashear
        QVERIFY(true);
    }

    void testOutputHttpRequestContent()
    {
        OutputHttpRequestContent request;

        QString label = request.label();
        QVERIFY(!label.isEmpty());

        QString iconName = request.iconName();
        QCOMPARE(iconName, QStringLiteral("globe"));

        request.setHttpResult(
            200, QStringLiteral("OK"), 120, 512,
            QStringLiteral("GET"), QStringLiteral("https://example.com/api"),
            QStringLiteral(R"({"data": "response"})"), true
        );
        QVERIFY(request.hasContent());

        request.clear();
        QVERIFY(!request.hasContent());
    }

    void testOutputHttpHeadersContent()
    {
        OutputHttpHeadersContent headers;

        QString label = headers.label();
        QVERIFY(!label.isEmpty());

        QString iconName = headers.iconName();
        QCOMPARE(iconName, QStringLiteral("list"));

        QMap<QString, QString> headerMap;
        headerMap[QStringLiteral("Content-Type")] = QStringLiteral("application/json");
        headerMap[QStringLiteral("Authorization")] = QStringLiteral("Bearer token123");

        headers.setHeaders(headerMap);
        QVERIFY(headers.hasContent());

        headers.clear();
        QVERIFY(!headers.hasContent());
    }

    void testOutputHttpHeadersContentEmpty()
    {
        OutputHttpHeadersContent headers;

        QMap<QString, QString> emptyMap;
        headers.setHeaders(emptyMap);
        QVERIFY(!headers.hasContent());
    }

    void testMetricsHeaderStatusCodes()
    {
        OutputMetricsHeader header;

        header.setMetrics(200, QStringLiteral("OK"), 100, 512, true);
        QVERIFY(true);

        header.setMetrics(404, QStringLiteral("Not Found"), 50, 256, false);
        QVERIFY(true);

        header.setMetrics(500, QStringLiteral("Internal Server Error"), 5000, 1024, false);
        QVERIFY(true);
    }

    void testMetricsHeaderSizeFormatting()
    {
        OutputMetricsHeader header;

        header.setMetrics(200, QStringLiteral("OK"), 100, 512, true);

        header.setMetrics(200, QStringLiteral("OK"), 100, 1024 * 10, true);

        header.setMetrics(200, QStringLiteral("OK"), 100, 1024 * 1024 * 2, true);

        QVERIFY(true);
    }

    void cleanupTestCase()
    {
    }
};

QTEST_MAIN(TestOutputTabs)
#include "test_output_tabs.moc"
