#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

#include "core/config-manager.h"
#include "core/field-scopes.h"
#include "core/models.h"

using namespace kai::core;

// Cada campo que os modelos gravam tem uma classe (core/field-scopes): o que vai pro arquivo, o que é
// referência, o que nunca sai. Esta suíte falha quando alguém acrescenta um campo sem decidir isso — a causa
// de "esqueci de levar X" e "levei X que não devia".
class TestFieldScopes : public QObject {
    Q_OBJECT

private:
    static QString readFile(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

    // As chaves JSON que `Owner::toJson()` grava, lidas do próprio fonte de models.cpp.
    static QSet<QString> serializedKeys(const QString &source, const QString &owner)
    {
        const QString header = owner + QStringLiteral("::toJson() const\n{");
        const int start = source.indexOf(header);
        if (start < 0) return {};
        const int end = source.indexOf(QStringLiteral("\n}\n"), start);
        const QString body = source.mid(start, end - start);
        QSet<QString> keys;
        static const QRegularExpression literal(QStringLiteral(R"re((?:obj|o)\["([a-z_0-9]+)"\])re"));
        auto it = literal.globalMatch(body);
        while (it.hasNext()) keys.insert(it.next().captured(1));
        if (body.contains(QStringLiteral("writeWorkingDir("))) keys.insert(QStringLiteral("working_dir"));
        return keys;
    }

private slots:
    void everySerializedFieldHasAScope()
    {
        const QString source = readFile(QStringLiteral("src/core/models.cpp"));
        if (source.isEmpty()) QSKIP("run from the repository root (needs src/core/models.cpp)");
        const struct { const char *owner; FieldOwner kind; } owners[] = {
            {"Command", FieldOwner::Command},
            {"Parameter", FieldOwner::Parameter},
            {"Folder", FieldOwner::Folder},
            {"Collection", FieldOwner::Collection},
            {"CollectionField", FieldOwner::CollectionField},
            {"CollectionEntry", FieldOwner::CollectionEntry},
        };
        for (const auto &o : owners) {
            const QSet<QString> keys = serializedKeys(source, QLatin1String(o.owner));
            QVERIFY2(!keys.isEmpty(), o.owner);
            for (const QString &key : keys) {
                QVERIFY2(scopeOf(o.kind, key).has_value(),
                         qPrintable(QStringLiteral("%1.%2 não tem classe em core/field-scopes.cpp").arg(QLatin1String(o.owner), key)));
            }
            // E nenhuma classe sobrando pra um campo que não existe mais.
            for (const QString &key : declaredKeys(o.kind)) {
                QVERIFY2(keys.contains(key), qPrintable(QStringLiteral("%1.%2 está na tabela mas o modelo não grava").arg(QLatin1String(o.owner), key)));
            }
        }
    }

    // O que o manifesto/schema documenta como conteúdo de arquivo tem de ser uma classe que VAI pro arquivo.
    void everyDocumentedFileKeyIsDeclaredAndTravels()
    {
        const QString text = readFile(QStringLiteral("assets/manifesto/kai.schema.json"));
        if (text.isEmpty()) QSKIP("run from the repository root (needs assets/manifesto/kai.schema.json)");
        const QJsonObject defs = QJsonDocument::fromJson(text.toUtf8()).object().value(QStringLiteral("$defs")).toObject();
        // Nomes que o arquivo usa no lugar da chave interna (caminho/nome em vez de id).
        const QMap<QString, QString> alias{{QStringLiteral("folder"), QStringLiteral("folder_id")},
                                           {QStringLiteral("collection"), QStringLiteral("collection_id")}};
        const struct { const char *def; FieldOwner kind; } checks[] = {
            {"command", FieldOwner::Command}, {"parameter", FieldOwner::Parameter},
            {"collection", FieldOwner::Collection}, {"collectionField", FieldOwner::CollectionField},
        };
        for (const auto &c : checks) {
            const QJsonObject props = defs.value(QLatin1String(c.def)).toObject().value(QStringLiteral("properties")).toObject();
            QVERIFY2(!props.isEmpty(), c.def);
            for (const QString &documented : props.keys()) {
                const QString key = alias.value(documented, documented);
                const auto scope = scopeOf(c.kind, key);
                QVERIFY2(scope.has_value(), qPrintable(QStringLiteral("%1.%2 está no schema mas sem classe").arg(QLatin1String(c.def), documented)));
                const bool travels = *scope != FieldScope::Local && *scope != FieldScope::Secret && *scope != FieldScope::Identity;
                QVERIFY2(travels || documented != key, // um alias (folder, collection) nomeia a forma de arquivo de um id
                         qPrintable(QStringLiteral("%1.%2 é documentado como conteúdo de arquivo mas a classe diz que nunca sai").arg(QLatin1String(c.def), documented)));
            }
        }
    }

    // Nenhum campo Local sai em exportação nenhuma (pasta, comando, global) — a tabela é quem manda.
    void localFieldsNeverReachAnExport()
    {
        Command c;
        c.id = QStringLiteral("c1");
        c.name = QStringLiteral("Cmd");
        c.command = QStringLiteral("echo");
        c.lastParamValues.insert(QStringLiteral("p"), QStringLiteral("valor-lembrado"));
        c.paramUsageHistory.insert(QStringLiteral("p"), QStringList{QStringLiteral("historico-uso")});
        c.kipLastValues.insert(QStringLiteral("k"), QStringLiteral("resposta-kip"));
        Folder f;
        f.id = QStringLiteral("f1");
        f.name = QStringLiteral("Pasta");
        c.folderId = f.id;
        Collection col;
        col.id = QStringLiteral("col1");
        col.folderId = f.id;
        col.name = QStringLiteral("Col");
        col.sourcePath = QStringLiteral("/home/eu/origem-local.csv");
        CommandsData data{{f}, {c}};
        const QStringList exports{
            ConfigManager::exportFolder(f.id, data, {col}, {}, true),
            ConfigManager::exportFolder(f.id, data, {col}, {}, false),
            ConfigManager::exportCommand(c.id, data, {col}, {}, true),
        };
        for (const QString &text : exports) {
            for (const char *leak : {"valor-lembrado", "historico-uso", "resposta-kip", "origem-local"}) {
                QVERIFY2(!text.contains(QLatin1String(leak)), leak);
            }
        }
    }
};

QTEST_MAIN(TestFieldScopes)
#include "test_field_scopes.moc"
