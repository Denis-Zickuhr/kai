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
    void looksLikeJsonDetectsFormat();
    void yamlOutputIsHumanReadableBlockStyle();
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

QTEST_MAIN(TestYamlBridge)
#include "test_yaml_bridge.moc"
