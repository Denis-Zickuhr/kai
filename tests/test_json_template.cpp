#include <QTest>

#include <QJsonDocument>

#include "utils/json-template.h"

using namespace kai::utils;

namespace {

bool validJson(const QString &text)
{
    QJsonParseError e;
    QJsonDocument::fromJson(text.toUtf8(), &e);
    return e.error == QJsonParseError::NoError;
}

} // namespace

class TestJsonTemplate : public QObject {
    Q_OBJECT

private slots:
    void aBareVariableAsAValueBecomesValidJson()
    {
        const QString body = QStringLiteral("{\"products\": {{products}}}");
        QVERIFY(!validJson(body));
        QVERIFY(validJson(maskTemplateVars(body).text));
    }

    void variablesInsideStringsStayInsideTheString()
    {
        const QString body = QStringLiteral("{\"id\": \"user-{{ID}}\", \"n\": {{count|json}}, \"list\": [{{a}}, {{b}}]}");
        const MaskedTemplate masked = maskTemplateVars(body);
        QVERIFY(validJson(masked.text));
        QCOMPARE(masked.entries.size(), 4);
        QVERIFY(!masked.entries.at(0).quoted);
        QVERIFY(masked.entries.at(1).quoted);
    }

    void unmaskRestoresTheOriginalText()
    {
        const QString body = QStringLiteral("{\"id\":\"user-{{ID}}\",\"rows\":{{products}},\"n\":{{c}}}");
        const MaskedTemplate masked = maskTemplateVars(body);
        QCOMPARE(unmaskTemplateVars(masked.text, masked), body);
    }

    void aBrokenBodyIsStillInvalid()
    {
        QVERIFY(!validJson(maskTemplateVars(QStringLiteral("{\"a\": {{x}}")).text));
        QVERIFY(!validJson(maskTemplateVars(QStringLiteral("{\"a\": {{x}}, }")).text));
    }

    void escapedQuotesDoNotConfuseTheScan()
    {
        const QString body = QStringLiteral("{\"a\": \"say \\\"{{x}}\\\"\", \"b\": {{y}}}");
        const MaskedTemplate masked = maskTemplateVars(body);
        QVERIFY(validJson(masked.text));
        QCOMPARE(masked.entries.size(), 2);
        QVERIFY(!masked.entries.at(0).quoted);
        QVERIFY(masked.entries.at(1).quoted);
    }

    void textWithoutVariablesIsUntouched()
    {
        const QString body = QStringLiteral("{\"a\": 1, \"b\": \"{ not a var }\"}");
        const MaskedTemplate masked = maskTemplateVars(body);
        QCOMPARE(masked.text, body);
        QVERIFY(masked.entries.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestJsonTemplate)
#include "test_json_template.moc"
