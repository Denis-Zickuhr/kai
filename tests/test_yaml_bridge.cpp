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
    void literalBlockScalarsKeepTheirTextAndChompingRules();
    void foldedBlockScalarsJoinLines();
    void blockScalarsWorkInListsAtAnyDepthAndStopWhereTheIndentEnds();
    void pipeInsideAValueIsNotABlockScalar();
    void listsWrittenAtTheSameColumnAsTheirKeyAreRead();
    void looksLikeJsonDetectsFormat();
    void yamlOutputIsHumanReadableBlockStyle();
    void multiLineStringsAreWrittenAsBlockScalarsAndComeBackIdentical();
    void stringsThatCannotBeBlockScalarsStayQuoted();
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

// Bug real: `command: |` chegava como o texto "|" — o comando multilinha inteiro sumia sem erro.
void TestYamlBridge::literalBlockScalarsKeepTheirTextAndChompingRules()
{
    const QString yaml = QStringLiteral(
        "commands:\n"
        "  - name: A\n"
        "    command: |\n"
        "      echo one\n"
        "        indented\n"
        "      # not a comment\n"
        "\n"
        "      echo two\n"
        "    type: command\n"
        "  - name: B\n"
        "    command: |-\n"
        "      x\n"
        "  - name: C\n"
        "    command: |+\n"
        "      y\n"
        "\n"
        "\n"
        "  - name: D\n"
        "    command: |\n"
        "  - name: E\n");
    bool ok = false;
    QString err;
    const QJsonArray c = QJsonDocument::fromJson(yamlTextToJsonText(yaml, &ok, &err).toUtf8()).object().value("commands").toArray();
    QVERIFY2(ok, qPrintable(err));
    QCOMPARE(c.size(), 5);
    QCOMPARE(c.at(0).toObject().value("command").toString(), QStringLiteral("echo one\n  indented\n# not a comment\n\necho two\n"));
    QCOMPARE(c.at(0).toObject().value("type").toString(), QStringLiteral("command")); // a chave seguinte continua valendo
    QCOMPARE(c.at(1).toObject().value("command").toString(), QStringLiteral("x"));      // |- sem quebra final
    QCOMPARE(c.at(2).toObject().value("command").toString(), QStringLiteral("y\n\n\n")); // |+ guarda as quebras
    QCOMPARE(c.at(3).toObject().value("command").toString(), QString());                // bloco vazio
    QCOMPARE(c.at(4).toObject().value("name").toString(), QStringLiteral("E"));
}

void TestYamlBridge::foldedBlockScalarsJoinLines()
{
    const QString yaml = QStringLiteral(
        "a: >\n"
        "  one\n"
        "  two\n"
        "\n"
        "  three\n"
        "b: >-\n"
        "  only\n"
        "  line\n"
        "c: end\n");
    bool ok = false;
    QString err;
    const QJsonObject o = QJsonDocument::fromJson(yamlTextToJsonText(yaml, &ok, &err).toUtf8()).object();
    QVERIFY2(ok, qPrintable(err));
    QCOMPARE(o.value("a").toString(), QStringLiteral("one two\nthree\n"));
    QCOMPARE(o.value("b").toString(), QStringLiteral("only line"));
    QCOMPARE(o.value("c").toString(), QStringLiteral("end"));
}

void TestYamlBridge::blockScalarsWorkInListsAtAnyDepthAndStopWhereTheIndentEnds()
{
    const QString yaml = QStringLiteral(
        "items:\n"
        "  - |\n"
        "    first\n"
        "    second\n"
        "  - command: |\n"
        "      inline start\n"
        "    name: N\n"
        "nested:\n"
        "  deep:\n"
        "    text: |\n"
        "      a\r\n"
        "      b\r\n"
        "  after: 1\n"
        "top: ok\n");
    bool ok = false;
    QString err;
    const QJsonObject o = QJsonDocument::fromJson(yamlTextToJsonText(yaml, &ok, &err).toUtf8()).object();
    QVERIFY2(ok, qPrintable(err));
    const QJsonArray items = o.value("items").toArray();
    QCOMPARE(items.at(0).toString(), QStringLiteral("first\nsecond\n"));
    QCOMPARE(items.at(1).toObject().value("command").toString(), QStringLiteral("inline start\n"));
    QCOMPARE(items.at(1).toObject().value("name").toString(), QStringLiteral("N"));
    QCOMPARE(o.value("nested").toObject().value("deep").toObject().value("text").toString(), QStringLiteral("a\nb\n"));
    QCOMPARE(o.value("nested").toObject().value("after").toDouble(), 1.0);
    QCOMPARE(o.value("top").toString(), QStringLiteral("ok"));
}

// Só um "|" ou ">" SOZINHO no lugar do valor é block scalar.
void TestYamlBridge::pipeInsideAValueIsNotABlockScalar()
{
    bool ok = false;
    QString err;
    const QJsonObject o = QJsonDocument::fromJson(yamlTextToJsonText(
        QStringLiteral("a: \"|\"\nb: ls | grep x\nc: 'a > b'\nd: \"x\"\n"), &ok, &err).toUtf8()).object();
    QVERIFY2(ok, qPrintable(err));
    QCOMPARE(o.value("a").toString(), QStringLiteral("|"));
    QCOMPARE(o.value("b").toString(), QStringLiteral("ls | grep x"));
    QCOMPARE(o.value("c").toString(), QStringLiteral("a > b"));
    QCOMPARE(o.value("d").toString(), QStringLiteral("x"));
}

// Estilo muito comum: a lista na MESMA coluna da chave. Antes virava lixo sem erro.
void TestYamlBridge::listsWrittenAtTheSameColumnAsTheirKeyAreRead()
{
    const QString yaml = QStringLiteral(
        "project_name: P\n"
        "commands:\n"
        "- name: A\n"
        "  hooks:\n"
        "    pre:\n"
        "    - X\n"
        "    - Y\n"
        "  params:\n"
        "  - name: N\n"
        "    type: text\n"
        "  type: command\n"
        "- name: B\n"
        "collections:\n"
        "- name: C\n");
    bool ok = false;
    QString err;
    const QJsonObject o = QJsonDocument::fromJson(yamlTextToJsonText(yaml, &ok, &err).toUtf8()).object();
    QVERIFY2(ok, qPrintable(err));
    QCOMPARE(o.value("project_name").toString(), QStringLiteral("P"));
    const QJsonArray commands = o.value("commands").toArray();
    QCOMPARE(commands.size(), 2);
    const QJsonObject a = commands.at(0).toObject();
    QCOMPARE(a.value("hooks").toObject().value("pre").toArray(), (QJsonArray{QStringLiteral("X"), QStringLiteral("Y")}));
    QCOMPARE(a.value("params").toArray().at(0).toObject().value("name").toString(), QStringLiteral("N"));
    QCOMPARE(a.value("type").toString(), QStringLiteral("command"));
    QCOMPARE(commands.at(1).toObject().value("name").toString(), QStringLiteral("B"));
    QCOMPARE(o.value("collections").toArray().at(0).toObject().value("name").toString(), QStringLiteral("C"));
}

// Texto com quebra de linha sai como `|` (mais limpo que "a\\nb") e volta idêntico, em qualquer posição.
void TestYamlBridge::multiLineStringsAreWrittenAsBlockScalarsAndComeBackIdentical()
{
    const QStringList samples = {
        QStringLiteral("echo a\necho b"),                 // sem quebra final: |-
        QStringLiteral("echo a\necho b\n"),               // uma quebra final: |
        QStringLiteral("echo a\n\n"),                     // várias: |+
        QStringLiteral("  indentada\nnormal"),            // 1ª linha começa por espaço: precisa do indicador
        QStringLiteral("\nlinha depois de uma vazia"),    // começa com linha vazia
        QStringLiteral("a\n\nb\n\n\nc"),                 // linhas vazias no meio
        QStringLiteral("if x:\n    y()\n  z()\n"),       // indentação interna
        QStringLiteral("# não é comentário\nkey: value\n- item"), // sintaxe YAML dentro do texto
        QStringLiteral("tab\tno meio\n\tinicio com tab"),
        QStringLiteral("acentuação: ção\nemoji ✓"),
    };
    for (const QString &text : samples) {
        QJsonObject command{{"name", "Cmd"}, {"command", text}};
        QJsonObject root{{"commands", QJsonArray{command}}, {"note", text},
                         {"list", QJsonArray{text, QStringLiteral("simples")}},
                         {"nested", QJsonObject{{"deep", QJsonObject{{"text", text}}}}}};
        const QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
        const QString yaml = jsonTextToYamlText(json);
        QVERIFY2(yaml.contains(QLatin1String(": |")), qPrintable(text + QStringLiteral("\n---\n") + yaml));
        QVERIFY2(!yaml.contains(QLatin1String("\\n")), qPrintable(text + QStringLiteral("\n---\n") + yaml));
        bool ok = false;
        QString err;
        const QString back = yamlTextToJsonText(yaml, &ok, &err);
        QVERIFY2(ok, qPrintable(err + QStringLiteral("\n") + yaml));
        QCOMPARE(QJsonDocument::fromJson(back.toUtf8()).object(), root);
    }
}

// Se o parser daqui não devolveria o texto exato (retorno de carro, controle, linha só de espaços),
// continua entre aspas — nunca perde conteúdo.
void TestYamlBridge::stringsThatCannotBeBlockScalarsStayQuoted()
{
    const QStringList samples = {
        QStringLiteral("a\r\nb"),
        QStringLiteral("a\n   \nb"),
        QStringLiteral("a\u0001b\nc"),
        QStringLiteral("\n\n"),
        QStringLiteral("sem quebra"),
    };
    for (const QString &text : samples) {
        const QJsonObject root{{"v", text}};
        const QString yaml = jsonTextToYamlText(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
        QVERIFY2(!yaml.contains(QLatin1String(": |")), qPrintable(yaml));
        bool ok = false;
        const QString back = yamlTextToJsonText(yaml, &ok, nullptr);
        QVERIFY(ok);
        QCOMPARE(QJsonDocument::fromJson(back.toUtf8()).object(), root);
    }
}
