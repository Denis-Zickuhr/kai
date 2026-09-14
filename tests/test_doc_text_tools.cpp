#include <QTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ui/features/docs/doc-text-tools.h"

using namespace kai::ui::texttools;

// As ferramentas de texto do editor (formatar/validar JSON e XML, converter, utilitários): puras, sem widgets.
class TestDocTextTools : public QObject {
    Q_OBJECT

private slots:
    // ---- JSON ----
    void validJsonIsAccepted()
    {
        for (const QString &text : {QStringLiteral("{}"), QStringLiteral("[]"), QStringLiteral("  42 "), QStringLiteral("\"x\""),
                                    QStringLiteral("null"), QStringLiteral("{\"a\":[1,2.5e-3,-0,true,false,null,{\"b\":\"\\u00e9\\n\"}]}")}) {
            QVERIFY2(validateJson(text).ok, qPrintable(text));
        }
    }

    void invalidJsonReportsTheExactPosition()
    {
        Issue issue = validateJson(QStringLiteral("{\n  \"a\": 1,\n}"));
        QVERIFY(!issue.ok);
        QCOMPARE(issue.line, 3);
        QVERIFY(issue.message.contains(QStringLiteral("trailing comma")));

        issue = validateJson(QStringLiteral("{\"a\": }"));
        QVERIFY(!issue.ok);
        QCOMPARE(issue.line, 1);
        QCOMPARE(issue.column, 7);

        QVERIFY(!validateJson(QStringLiteral("{'a': 1}")).ok);     // aspas simples
        QVERIFY(!validateJson(QStringLiteral("[1, 2")).ok);        // não fechou
        QVERIFY(!validateJson(QStringLiteral("{\"a\": 01}")).ok);  // zero à esquerda
        QVERIFY(!validateJson(QStringLiteral("{\"a\": 1} x")).ok); // lixo depois
        QVERIFY(!validateJson(QStringLiteral("\"abc")).ok);        // string aberta
        QVERIFY(!validateJson(QStringLiteral("[\"\\x\"]")).ok);    // escape inválido
        QVERIFY(!validateJson(QString()).ok);
        QVERIFY(!validateJson(QStringLiteral("// c\n{}")).ok);      // comentário não é JSON
    }

    // Embelezar mantém a ORDEM das chaves e o texto dos números (o QJsonDocument embaralharia/perderia).
    void prettyKeepsKeyOrderAndNumberText()
    {
        const Result r = prettyJson(QStringLiteral("{\"z\":1.0,\"a\":[1e5,12345678901234567890],\"m\":{\"k\":\"\\u00e9\"},\"e\":[],\"o\":{}}"));
        QVERIFY(r.ok);
        QCOMPARE(r.text, QStringLiteral("{\n  \"z\": 1.0,\n  \"a\": [\n    1e5,\n    12345678901234567890\n  ],\n  \"m\": {\n    \"k\": \"\\u00e9\"\n  },\n  \"e\": [],\n  \"o\": {}\n}\n"));
        QCOMPARE(prettyJson(QStringLiteral("[1]"), 4).text, QStringLiteral("[\n    1\n]\n"));
    }

    void minifyAndSortKeys()
    {
        const QString source = QStringLiteral("{ \"b\": [ {\"y\": 1, \"x\": 2} ], \"A\": \"s p a c e\" }");
        QCOMPARE(minifyJson(source).text, QStringLiteral("{\"b\":[{\"y\":1,\"x\":2}],\"A\":\"s p a c e\"}"));
        // Ordena todos os níveis, sem diferenciar maiúsculas, mantendo os valores.
        QCOMPARE(sortJsonKeys(source).text,
                 QStringLiteral("{\n  \"A\": \"s p a c e\",\n  \"b\": [\n    {\n      \"x\": 2,\n      \"y\": 1\n    }\n  ]\n}\n"));
        const Result bad = prettyJson(QStringLiteral("{\"a\":"));
        QVERIFY(!bad.ok);
        QVERIFY(!bad.error.isEmpty());
        QCOMPARE(bad.line, 1);
    }

    void jsonStringEscapeRoundTrips()
    {
        const QString raw = QStringLiteral("linha 1\nlinha \"2\"\t\\fim é ✓");
        const Result escaped = escapeJsonString(raw);
        QVERIFY(escaped.ok);
        QVERIFY(escaped.text.startsWith(QLatin1Char('"')) && escaped.text.endsWith(QLatin1Char('"')));
        QVERIFY(!escaped.text.contains(QLatin1Char('\n')));
        QCOMPARE(unescapeJsonString(escaped.text).text, raw);
        QCOMPARE(unescapeJsonString(escaped.text.mid(1, escaped.text.size() - 2)).text, raw); // sem as aspas também
        QVERIFY(!unescapeJsonString(QStringLiteral("\"abc\\q\"")).ok);
    }

    // ---- XML ----
    void xmlValidationFindsTheLine()
    {
        QVERIFY(validateXml(QStringLiteral("<a><b/>text</a>")).ok);
        const Issue issue = validateXml(QStringLiteral("<a>\n  <b>\n</a>"));
        QVERIFY(!issue.ok);
        QCOMPARE(issue.line, 3);
        QVERIFY(!validateXml(QStringLiteral("just text")).ok);
        QVERIFY(!validateXml(QString()).ok);
        QVERIFY(!validateXml(QStringLiteral("<a></b>")).ok);
    }

    void xmlPrettyAndMinify()
    {
        const QString compact = QStringLiteral("<?xml version=\"1.0\"?><r a=\"1\"><!-- c --><i>x</i><i><![CDATA[<y>]]></i><e/></r>");
        const Result pretty = prettyXml(compact);
        QVERIFY(pretty.ok);
        QCOMPARE(pretty.text, QStringLiteral("<?xml version=\"1.0\"?>\n<r a=\"1\">\n  <!-- c -->\n  <i>x</i>\n  <i><![CDATA[<y>]]></i>\n  <e/>\n</r>\n"));
        // Minificar de volta tira o recuo e as quebras (e mantém a declaração, o CDATA e o comentário).
        const Result minified = minifyXml(pretty.text);
        QVERIFY(minified.ok);
        QCOMPARE(minified.text, QStringLiteral("<?xml version=\"1.0\"?><r a=\"1\"><!-- c --><i>x</i><i><![CDATA[<y>]]></i><e/></r>"));
        // Sem declaração no original, não inventa uma.
        QVERIFY(!prettyXml(QStringLiteral("<a><b/></a>")).text.startsWith(QStringLiteral("<?xml")));
        QVERIFY(!prettyXml(QStringLiteral("<a>")).ok);
    }

    // ---- conversões ----
    void jsonAndYamlConvertBothWays()
    {
        const Result yaml = jsonToYaml(QStringLiteral("{\"name\":\"kai\",\"list\":[1,2],\"nested\":{\"ok\":true}}"));
        QVERIFY(yaml.ok);
        QVERIFY(yaml.text.contains(QStringLiteral("name:")));
        const Result back = yamlToJson(yaml.text);
        QVERIFY(back.ok);
        QCOMPARE(QJsonDocument::fromJson(back.text.toUtf8()),
                 QJsonDocument::fromJson("{\"name\":\"kai\",\"list\":[1,2],\"nested\":{\"ok\":true}}"));
        QVERIFY(!jsonToYaml(QStringLiteral("{oops")).ok);
        QVERIFY(validateYaml(yaml.text).ok);
        QVERIFY(validateYaml(QString()).ok);
    }

    void xmlAndJsonConvertWithAttributesAndRepeatedElements()
    {
        const Result json = xmlToJson(QStringLiteral("<lib id=\"7\"><book>A</book><book>B</book><name lang=\"pt\">Minha</name><empty/></lib>"));
        QVERIFY(json.ok);
        const QJsonDocument doc = QJsonDocument::fromJson(json.text.toUtf8());
        const QJsonObject lib = doc.object().value(QStringLiteral("lib")).toObject();
        QCOMPARE(lib.value(QStringLiteral("@id")).toString(), QStringLiteral("7"));
        QCOMPARE(lib.value(QStringLiteral("book")).toArray().size(), 2);
        QCOMPARE(lib.value(QStringLiteral("name")).toObject().value(QStringLiteral("#text")).toString(), QStringLiteral("Minha"));
        QCOMPARE(lib.value(QStringLiteral("name")).toObject().value(QStringLiteral("@lang")).toString(), QStringLiteral("pt"));
        QVERIFY(lib.contains(QStringLiteral("empty")));

        const Result xml = jsonToXml(json.text);
        QVERIFY(xml.ok);
        QVERIFY(xml.text.contains(QStringLiteral("<lib id=\"7\">")));
        QVERIFY(xml.text.contains(QStringLiteral("<book>A</book>")));
        QVERIFY(xml.text.contains(QStringLiteral("<name lang=\"pt\">Minha</name>")));
        // Uma lista no topo ou várias chaves ganham a raiz <root>; nomes que o XML não aceita são recusados.
        QVERIFY(jsonToXml(QStringLiteral("{\"a\":1,\"b\":[2,3]}")).text.contains(QStringLiteral("<root>")));
        QVERIFY(!jsonToXml(QStringLiteral("{\"1bad key\":1}")).ok);
        QVERIFY(!jsonToXml(QStringLiteral("{oops")).ok);
        QVERIFY(!xmlToJson(QStringLiteral("<a>")).ok);
    }

    // ---- utilitários ----
    void textUtilities()
    {
        QCOMPARE(base64Encode(QStringLiteral("olá, kai")).text, QStringLiteral("b2zDoSwga2Fp"));
        QCOMPARE(base64Decode(QStringLiteral("b2zDoSwga2Fp")).text, QStringLiteral("olá, kai"));
        QCOMPARE(base64Decode(QStringLiteral("b2zD\noSwg a2Fp")).text, QStringLiteral("olá, kai")); // ignora espaços
        QVERIFY(!base64Decode(QStringLiteral("***")).ok);
        QCOMPARE(urlEncode(QStringLiteral("a b&c=é")).text, QStringLiteral("a%20b%26c%3D%C3%A9"));
        QCOMPARE(urlDecode(QStringLiteral("a%20b%26c+d%C3%A9")).text, QStringLiteral("a b&c dé"));
        QCOMPARE(sortLines(QStringLiteral("b\nC\na\n")).text, QStringLiteral("a\nb\nC\n"));
        QCOMPARE(sortLines(QStringLiteral("b\nC\na"), true).text, QStringLiteral("C\nb\na"));
        QCOMPARE(dedupeLines(QStringLiteral("a\nb\na\nc\nb\n")).text, QStringLiteral("a\nb\nc\n"));
        QCOMPARE(upperCase(QStringLiteral("Olá")).text, QStringLiteral("OLÁ"));
        QCOMPARE(lowerCase(QStringLiteral("OLÁ")).text, QStringLiteral("olá"));
        QCOMPARE(titleCase(QStringLiteral("hello wORLD it's 2nd-time")).text, QStringLiteral("Hello World It's 2nd-Time"));
        QCOMPARE(trimTrailingWhitespace(QStringLiteral("a  \nb\t\r\nc")).text, QStringLiteral("a\nb\nc"));
    }

    void languagesComeFromTheExtension()
    {
        QCOMPARE(languageForExtension(QStringLiteral("JSON")), Language::Json);
        QCOMPARE(languageForExtension(QStringLiteral("yml")), Language::Yaml);
        QCOMPARE(languageForExtension(QStringLiteral("svg")), Language::Xml);
        QCOMPARE(languageForExtension(QStringLiteral("md")), Language::Markdown);
        QCOMPARE(languageForExtension(QString()), Language::Text);
        QCOMPARE(languageName(Language::Yaml), QStringLiteral("YAML"));
    }
};

QTEST_MAIN(TestDocTextTools)
#include "test_doc_text_tools.moc"
