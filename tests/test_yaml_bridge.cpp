#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTest>

#include "core/yaml-bridge.h"

using namespace kai::core;

class TestYamlBridge : public QObject {
    Q_OBJECT

private slots:
    void roundTripsSimpleObject();
    void roundTripsNestedObjectsAndArrays();
    void roundTripsListOfObjects();
    void preservesAccentedCharacters();
    void handlesEmptyContainersAndScalarTypes();
    void parsesFlowSequencesAndMappings();
    void braceTemplatesStayPlainStrings();
    void hashInsideQuotesIsNotAComment();
    void looksLikeJsonDetectsFormat();
    void yamlOutputIsHumanReadableBlockStyle();
    void roundTripsKipLastValuesWithSlashKeysAndMixedValues();
};

void TestYamlBridge::roundTripsSimpleObject()
{
    const QString json = QStringLiteral(R"({"name":"kai","version":1,"active":true,"note":null})");
    const QString yaml = jsonTextToYamlText(json);
    QVERIFY(!yaml.isEmpty());

    bool ok = false;
    QString err;
    const QString backToJson = yamlTextToJsonText(yaml, &ok, &err);
    QVERIFY2(ok, qPrintable(err));

    const QJsonDocument original = QJsonDocument::fromJson(json.toUtf8());
    const QJsonDocument roundTripped = QJsonDocument::fromJson(backToJson.toUtf8());
    QCOMPARE(roundTripped.object(), original.object());
}

void TestYamlBridge::roundTripsNestedObjectsAndArrays()
{
    const QString json = QStringLiteral(
        R"({"folders":[{"id":"f1","name":"Root","tags":["a","b"]}],"settings":{"nested":{"deep":42}}})");
    const QString yaml = jsonTextToYamlText(json);

    bool ok = false;
    const QString backToJson = yamlTextToJsonText(yaml, &ok, nullptr);
    QVERIFY(ok);

    const QJsonDocument original = QJsonDocument::fromJson(json.toUtf8());
    const QJsonDocument roundTripped = QJsonDocument::fromJson(backToJson.toUtf8());
    QCOMPARE(roundTripped.object(), original.object());
}

void TestYamlBridge::roundTripsListOfObjects()
{
    const QString json = QStringLiteral(
        R"([{"id":"c1","name":"Cmd 1","params":[{"name":"p1","optional":true}]},{"id":"c2","name":"Cmd 2"}])");
    const QString yaml = jsonTextToYamlText(json);

    bool ok = false;
    const QString backToJson = yamlTextToJsonText(yaml, &ok, nullptr);
    QVERIFY(ok);

    const QJsonDocument original = QJsonDocument::fromJson(json.toUtf8());
    const QJsonDocument roundTripped = QJsonDocument::fromJson(backToJson.toUtf8());
    QCOMPARE(roundTripped.array(), original.array());
}

void TestYamlBridge::preservesAccentedCharacters()
{
    // Regressão direta do pedido do usuário: "reforçando pra validar o
    // encoding pois ainda não funciona o ç" — garante que o bridge JSON<->YAML
    // não é a origem de qualquer corrupção de caracteres acentuados.
    const QString json = QStringLiteral(R"({"description":"Configuração com ç, ã, é, ü e ñ"})");
    const QString yaml = jsonTextToYamlText(json);
    QVERIFY(yaml.contains(QStringLiteral("ç")));
    QVERIFY(yaml.contains(QStringLiteral("ã")));

    bool ok = false;
    const QString backToJson = yamlTextToJsonText(yaml, &ok, nullptr);
    QVERIFY(ok);
    const QJsonDocument roundTripped = QJsonDocument::fromJson(backToJson.toUtf8());
    QCOMPARE(roundTripped.object().value(QStringLiteral("description")).toString(),
        QStringLiteral("Configuração com ç, ã, é, ü e ñ"));
}

void TestYamlBridge::handlesEmptyContainersAndScalarTypes()
{
    const QString json = QStringLiteral(
        R"({"emptyObj":{},"emptyArr":[],"floatVal":3.5,"intVal":-7,"str":"line1\nline2 \"quoted\""})");
    const QString yaml = jsonTextToYamlText(json);

    bool ok = false;
    QString err;
    const QString backToJson = yamlTextToJsonText(yaml, &ok, &err);
    QVERIFY2(ok, qPrintable(err));

    const QJsonDocument original = QJsonDocument::fromJson(json.toUtf8());
    const QJsonDocument roundTripped = QJsonDocument::fromJson(backToJson.toUtf8());
    QCOMPARE(roundTripped.object(), original.object());
}

void TestYamlBridge::looksLikeJsonDetectsFormat()
{
    QVERIFY(looksLikeJson(QStringLiteral("  {\"a\":1}")));
    QVERIFY(looksLikeJson(QStringLiteral("[1,2,3]")));
    QVERIFY(!looksLikeJson(QStringLiteral("name: kai\nversion: 1\n")));
}

void TestYamlBridge::yamlOutputIsHumanReadableBlockStyle()
{
    // Pedido do usuário: "ymal é mais legível e bonito" — confirma que a
    // saída é bloco indentado, não flow style, e não contém aspas em chaves
    // simples (o visual que o usuário está pedindo).
    const QString json = QStringLiteral(R"({"name":"kai","enabled":true})");
    const QString yaml = jsonTextToYamlText(json);
    QVERIFY(!yaml.contains(QLatin1Char('{')));
    QVERIFY(!yaml.contains(QLatin1Char('}')));
    QVERIFY(yaml.contains(QStringLiteral(": true")));
}

// kip_last_values (spec 11 §7.2): chaves "promptId/campo" e valores de qualquer
// tipo JSON (string, número, array, objeto, null) precisam sobreviver ao
// ciclo exportar para YAML -> reimportar.
void TestYamlBridge::roundTripsKipLastValuesWithSlashKeysAndMixedValues()
{
    const QString json = QStringLiteral(R"({"commands":[{"name":"Deploy","kip":true,"kip_window":true,
        "kip_last_values":{
            "env/target":"prod",
            "env/tags":["a","b c","d: e"],
            "opts/flags":{"force":true,"dry":false},
            "when/range":{"start":"2024-01-02T03:04:05","end":"2024-01-03T03:04:05"},
            "n/count":3.5,
            "n/empty":null,
            "t/rows":[]
        }}]})");
    bool ok = false;
    QString err;
    const QString yaml = jsonTextToYamlText(json);
    const QString back = yamlTextToJsonText(yaml, &ok, &err);
    QVERIFY2(ok, qPrintable(err));
    QCOMPARE(QJsonDocument::fromJson(back.toUtf8()).object(), QJsonDocument::fromJson(json.toUtf8()).object());
}

QTEST_MAIN(TestYamlBridge)
#include "test_yaml_bridge.moc"

// Bug real: `options: ["API:api", "web"]` (lista em estilo fluxo) chegava
// VAZIO — o select ficava sem opções e o autocomplete/ajuda do CLI também.
void TestYamlBridge::parsesFlowSequencesAndMappings()
{
    const QString yaml = QStringLiteral(
        "options: [\"API:api\", web, 'o''k', 3]\n"
        "nested: [[1, 2], {a: x, \"b c\": [y, z]}]\n"
        "map: {name: kai, tags: [cli, \"a, b\"]}\n"
        "empty: []\n");
    bool ok = false;
    QString err;
    const QJsonObject o = QJsonDocument::fromJson(yamlTextToJsonText(yaml, &ok, &err).toUtf8()).object();
    QVERIFY2(ok, qPrintable(err));
    QCOMPARE(o.value("options").toArray(),
             (QJsonArray{QStringLiteral("API:api"), QStringLiteral("web"), QStringLiteral("o'k"), 3}));
    QCOMPARE(o.value("nested").toArray().at(0).toArray(), (QJsonArray{1, 2}));
    QCOMPARE(o.value("nested").toArray().at(1).toObject().value("b c").toArray(),
             (QJsonArray{QStringLiteral("y"), QStringLiteral("z")}));
    QCOMPARE(o.value("map").toObject().value("tags").toArray(),
             (QJsonArray{QStringLiteral("cli"), QStringLiteral("a, b")}));
    QCOMPARE(o.value("empty").toArray(), QJsonArray());
}

// Templates do Kai sem aspas ({{VAR}}) começam com "{" e terminam com "}",
// mas não são mapas — continuam texto.
void TestYamlBridge::braceTemplatesStayPlainStrings()
{
    bool ok = false;
    QString err;
    const QJsonObject o = QJsonDocument::fromJson(
        yamlTextToJsonText(QStringLiteral("dir: {{PROJECT_PATH}}\n"), &ok, &err).toUtf8()).object();
    QVERIFY2(ok, qPrintable(err));
    QCOMPARE(o.value("dir").toString(), QStringLiteral("{{PROJECT_PATH}}"));
}

void TestYamlBridge::hashInsideQuotesIsNotAComment()
{
    bool ok = false;
    QString err;
    const QJsonObject o = QJsonDocument::fromJson(yamlTextToJsonText(
        QStringLiteral("a: 'echo \"# titulo\"'  # comentario\nb: \"x # y\"\n"), &ok, &err).toUtf8()).object();
    QVERIFY2(ok, qPrintable(err));
    QCOMPARE(o.value("a").toString(), QStringLiteral("echo \"# titulo\""));
    QCOMPARE(o.value("b").toString(), QStringLiteral("x # y"));
}
