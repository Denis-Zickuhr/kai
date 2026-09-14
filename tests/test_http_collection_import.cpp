#include <QTest>

#include <QFile>
#include <QJsonDocument>

#include "core/dotenv.h"
#include "core/http-collection-import.h"

using namespace kai::core;

namespace {

QString fixture(const QString &name)
{
    QFile file(QFINDTESTDATA(QStringLiteral("fixtures/http-import/") + name));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class TestHttpCollectionImport : public QObject {
    Q_OBJECT

private slots:
    void dotenvReadsAssignmentsQuotesAndComments()
    {
        const QMap<QString, QString> env = parseDotenv(QStringLiteral(
            "# comment\n"
            "PLAIN=value\n"
            "export EXPORTED = spaced  \n"
            "DOUBLE=\"line1\\nline2 \\\"q\\\"\"\n"
            "SINGLE='no $expansion \\n'\n"
            "TRAILING=abc # a comment\n"
            "EMPTY=\n"
            "not an assignment\n"
            "WIN=crlf\r\n"));
        QCOMPARE(env.value("PLAIN"), QStringLiteral("value"));
        QCOMPARE(env.value("EXPORTED"), QStringLiteral("spaced"));
        QCOMPARE(env.value("DOUBLE"), QStringLiteral("line1\nline2 \"q\""));
        QCOMPARE(env.value("SINGLE"), QStringLiteral("no $expansion \\n"));
        QCOMPARE(env.value("TRAILING"), QStringLiteral("abc"));
        QVERIFY(env.contains("EMPTY"));
        QCOMPARE(env.value("EMPTY"), QString());
        QCOMPARE(env.value("WIN"), QStringLiteral("crlf"));
        QVERIFY(!env.contains("not"));
        QCOMPARE(env.size(), 7);
    }

    void postmanCollectionBecomesFoldersRequestsAndDefaults()
    {
        const QString json = fixture(QStringLiteral("postman-shop.json"));
        const ImportedHttpCollection c = parsePostmanCollection(json);
        QVERIFY2(c.ok, qPrintable(c.errorMessage));
        QCOMPARE(c.name, QStringLiteral("Shop"));
        QCOMPARE(c.rootDefaults.auth.type, HttpAuthType::Bearer);
        QCOMPARE(c.rootDefaults.auth.token, QStringLiteral("{{TOKEN}}"));
        QCOMPARE(c.variables.value("base"), QStringLiteral("https://api.test"));
        QCOMPARE(c.requests.size(), 3);

        const ImportedRequest &create = c.requests.at(0);
        QCOMPARE(create.name, QStringLiteral("Create"));
        QCOMPARE(create.folderPath, QStringList{QStringLiteral("Users")});
        QCOMPARE(create.config.methodName(), QStringLiteral("POST"));
        QCOMPARE(create.config.url, QStringLiteral("{{base}}/users")); // a query foi para a tabela
        QCOMPARE(create.config.queryParams.size(), 2);
        QVERIFY(!create.config.queryParams.at(1).enabled);
        QCOMPARE(create.config.headers.size(), 2);
        QVERIFY(!create.config.headers.at(1).enabled);
        QCOMPARE(create.config.bodyType, HttpBodyType::Json);
        QCOMPARE(create.config.body, QStringLiteral("{\"id\": \"{{$uuid}}\"}"));

        const ImportedRequest &login = c.requests.at(1);
        QVERIFY(login.folderPath.isEmpty());
        QCOMPARE(login.config.auth.type, HttpAuthType::Basic);
        QCOMPARE(login.config.auth.username, QStringLiteral("u"));
        QCOMPARE(login.config.bodyType, HttpBodyType::Form);
        QCOMPARE(login.config.formFields.size(), 1);

        const ImportedRequest &upload = c.requests.at(2);
        QCOMPARE(upload.config.bodyType, HttpBodyType::Multipart);
        QVERIFY(!upload.config.formFields.at(0).isFile);
        QVERIFY(upload.config.formFields.at(1).isFile);
        QCOMPARE(upload.config.formFields.at(1).value, QStringLiteral("/tmp/a.png"));
        QVERIFY(upload.config.auth.isDefault()); // herda a da coleção
    }

    void postmanFolderAuthBecomesAFolderDefault()
    {
        const QString json = fixture(QStringLiteral("postman-folder-auth.json"));
        const ImportedHttpCollection c = parsePostmanCollection(json);
        QVERIFY(c.ok);
        QCOMPARE(c.folders.size(), 1);
        QCOMPARE(c.folders.first().path, QStringList{QStringLiteral("Admin")});
        QCOMPARE(c.folders.first().defaults.auth.type, HttpAuthType::ApiKey);
        QVERIFY(c.folders.first().defaults.auth.keyInQuery);
    }

    void insomniaExportKeepsGroupsVariablesAndOrder()
    {
        const QString json = fixture(QStringLiteral("insomnia.json"));
        const ImportedHttpCollection c = parseInsomniaExport(json);
        QVERIFY2(c.ok, qPrintable(c.errorMessage));
        QCOMPARE(c.name, QStringLiteral("My API"));
        QCOMPARE(c.variables.value("base_url"), QStringLiteral("https://api.test"));
        QCOMPARE(c.variables.value("nested.k"), QStringLiteral("v"));
        QCOMPARE(c.folders.size(), 1);
        QCOMPARE(c.folders.first().defaults.auth.token, QStringLiteral("{{token}}"));
        QCOMPARE(c.requests.size(), 3);
        // "Gql" tem chave de ordem 0 (ausente), "First" 1, "Second" 2.
        QCOMPARE(c.requests.at(0).name, QStringLiteral("Gql"));
        QCOMPARE(c.requests.at(1).name, QStringLiteral("First"));
        QCOMPARE(c.requests.at(2).name, QStringLiteral("Second"));

        const ImportedRequest &first = c.requests.at(1);
        QCOMPARE(first.folderPath, QStringList{QStringLiteral("Users")});
        QCOMPARE(first.config.url, QStringLiteral("{{base_url}}/a"));
        QCOMPARE(first.config.queryParams.size(), 1);
        QCOMPARE(first.config.bodyType, HttpBodyType::Json);
        QCOMPARE(first.config.auth.type, HttpAuthType::Basic);

        const ImportedRequest &gql = c.requests.at(0);
        QCOMPARE(gql.config.bodyType, HttpBodyType::GraphQl);
        QCOMPARE(gql.config.body, QStringLiteral("{ a }"));
        QCOMPARE(QJsonDocument::fromJson(gql.config.graphqlVariables.toUtf8()).object().value("id").toInt(), 1);
    }

    void harEntriesBecomeRequestsWithoutBrowserNoise()
    {
        const QString json = fixture(QStringLiteral("har.json"));
        const ImportedHttpCollection c = parseHar(json);
        QVERIFY2(c.ok, qPrintable(c.errorMessage));
        QCOMPARE(c.requests.size(), 2);
        QCOMPARE(c.requests.at(0).name, QStringLiteral("POST /api/items"));
        QCOMPARE(c.requests.at(0).config.headers.size(), 2); // só Content-Type e Accept
        QCOMPARE(c.requests.at(0).config.bodyType, HttpBodyType::Json);
        QCOMPARE(c.requests.at(0).config.url, QStringLiteral("https://x.test/api/items?a=1"));
        QCOMPARE(c.requests.at(1).config.bodyType, HttpBodyType::None);
    }

    void formatDetectionAndBadInput()
    {
        QVERIFY(looksLikeHar(QJsonDocument::fromJson("{\"log\":{\"entries\":[]}}").object()));
        QVERIFY(looksLikeInsomniaExport(QJsonDocument::fromJson("{\"_type\":\"export\",\"resources\":[]}").object()));
        QVERIFY(looksLikePostmanCollection(QJsonDocument::fromJson("{\"info\":{\"name\":\"a\"},\"item\":[]}").object()));
        QVERIFY(!looksLikePostmanCollection(QJsonDocument::fromJson("{\"openapi\":\"3.0.0\"}").object()));
        QVERIFY(!parsePostmanCollection(QStringLiteral("not json")).ok);
        QVERIFY(!parseHar(QStringLiteral("{}")).ok);
        QVERIFY(!parseInsomniaExport(QStringLiteral("[]")).ok);
    }
};

QTEST_MAIN(TestHttpCollectionImport)
#include "test_http_collection_import.moc"
