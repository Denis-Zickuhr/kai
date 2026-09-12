#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonObject>

#include "core/config-manager.h"
#include "core/models.h"

using namespace kai::core;

// Cobre o modelo Collection (feature "Coleções"): schema extensível,
// entries com tags/favoritos, e persistência em arquivo SEPARADO
// (collections.json), isolando o diretório de config num temporário.
class TestCollectionModel : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        m_tempDir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tempDir->isValid());
        qputenv("XDG_CONFIG_HOME", m_tempDir->path().toUtf8());
    }

    void cleanup() { m_tempDir.reset(); }

    void defaultSchemaIsKeyValue()
    {
        const QVector<CollectionField> schema = Collection::defaultSchema();
        QCOMPARE(schema.size(), 2);
        QCOMPARE(schema.at(0).name, QStringLiteral("key"));
        QCOMPARE(schema.at(0).type, CollectionFieldType::Key);
        QCOMPARE(schema.at(1).name, QStringLiteral("value"));
        QCOMPARE(schema.at(1).type, CollectionFieldType::Value);
    }

    void fieldTypeRoundTripsThroughString()
    {
        QCOMPARE(collectionFieldTypeFromString(collectionFieldTypeToString(CollectionFieldType::Email)),
                 CollectionFieldType::Email);
        // Tipo desconhecido cai em Text (fallback seguro).
        QCOMPARE(collectionFieldTypeFromString(QStringLiteral("inexistente")), CollectionFieldType::Text);
    }

    // Round-trip completo: cria uma coleção de usuários com schema
    // estendido (key, value, email), entradas com tags e favorito, salva e
    // recarrega — tudo deve ser preservado, no arquivo collections.json
    // (separado do commands.json).
    void collectionRoundTripPreservesSchemaEntriesTagsFavorites()
    {
        ConfigManager manager;

        Collection col;
        col.id = QStringLiteral("col_users");
        col.folderId = QStringLiteral("f_root");
        col.name = QStringLiteral("Usuários");
        col.icon = QStringLiteral("database");
        col.order = 2;
        col.sourcePath = QStringLiteral("/tmp/users.csv");
        col.schema = {
            {QStringLiteral("key"), QStringLiteral("Chave"), CollectionFieldType::Key},
            {QStringLiteral("value"), QStringLiteral("Nome"), CollectionFieldType::Value},
            {QStringLiteral("email"), QStringLiteral("E-mail"), CollectionFieldType::Email},
        };

        CollectionEntry e1;
        e1.id = QStringLiteral("e1");
        e1.values = {{QStringLiteral("key"), QStringLiteral("u1")},
                     {QStringLiteral("value"), QStringLiteral("Alice")},
                     {QStringLiteral("email"), QStringLiteral("alice@ex.com")}};
        e1.favorite = true;

        CollectionEntry e2;
        e2.id = QStringLiteral("e2");
        e2.values = {{QStringLiteral("key"), QStringLiteral("u2")},
                     {QStringLiteral("value"), QStringLiteral("Bob")}};

        col.entries = {e1, e2};

        QVERIFY(manager.saveCollections({col}));
        QVERIFY(QFile::exists(manager.collectionsFilePath()));
        // Não deve tocar o commands.json.
        QVERIFY(!QFile::exists(manager.commandsFilePath()));

        const QVector<Collection> reloaded = manager.loadCollections();
        QCOMPARE(reloaded.size(), 1);
        const Collection &r = reloaded.at(0);
        QCOMPARE(r.id, QStringLiteral("col_users"));
        QCOMPARE(r.name, QStringLiteral("Usuários"));
        QCOMPARE(r.order, 2);
        QCOMPARE(r.sourcePath, QStringLiteral("/tmp/users.csv"));
        QCOMPARE(r.schema.size(), 3);
        QCOMPARE(r.schema.at(2).name, QStringLiteral("email"));
        QCOMPARE(r.schema.at(2).type, CollectionFieldType::Email);
        QCOMPARE(r.entries.size(), 2);
        QCOMPARE(r.entries.at(0).values.value(QStringLiteral("value")), QStringLiteral("Alice"));
        QCOMPARE(r.entries.at(0).values.value(QStringLiteral("email")), QStringLiteral("alice@ex.com"));
        QVERIFY(r.entries.at(0).favorite);
        QVERIFY(!r.entries.at(1).favorite);
    }

    void collectionWithoutSchemaFallsBackToDefault()
    {
        // fromJson de um objeto sem schema deve popular o schema default.
        QJsonObject obj;
        obj["id"] = QStringLiteral("c1");
        obj["name"] = QStringLiteral("Vazia");
        const Collection c = Collection::fromJson(obj);
        QCOMPARE(c.schema.size(), 2); // default key/value
    }

    void loadingMissingCollectionsFileReturnsEmpty()
    {
        ConfigManager manager;
        QCOMPARE(manager.loadCollections().size(), 0);
    }

    // (2) Visibilidade por campo do schema: precisa sobreviver ao salvar/
    // carregar; schema antigo (sem a chave) assume visível.
    void schemaFieldVisibilityRoundTrip()
    {
        Collection col;
        col.id = QStringLiteral("c_vis");
        col.name = QStringLiteral("Clientes");
        CollectionField f1;
        f1.name = QStringLiteral("nome"); f1.label = QStringLiteral("Nome"); f1.visible = true;
        CollectionField f2;
        f2.name = QStringLiteral("cnpj"); f2.label = QStringLiteral("CNPJ"); f2.visible = false;
        col.schema = {f1, f2};

        const Collection back = Collection::fromJson(col.toJson());
        QCOMPARE(back.schema.size(), 2);
        QCOMPARE(back.schema.at(0).visible, true);
        QCOMPARE(back.schema.at(1).visible, false);

        // Schema legado (sem "visible") assume visível.
        QJsonObject legacyField;
        legacyField["name"] = QStringLiteral("x");
        legacyField["label"] = QStringLiteral("X");
        QVERIFY(CollectionField::fromJson(legacyField).visible);
    }

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;

};

QTEST_MAIN(TestCollectionModel)
#include "test_collection_model.moc"
