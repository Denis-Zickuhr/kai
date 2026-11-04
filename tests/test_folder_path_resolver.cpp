#include <QTest>

#include "core/folder-path-resolver.h"

using namespace kai::core;

// FolderPathResolver: resolve "folder"/"path" -> id criando pastas intermediárias. É compartilhado
// por Import de Projeto e Export/Import Configuration, que tinham implementações divergentes.
class TestFolderPathResolver : public QObject {
    Q_OBJECT

private slots:
    void emptyPathResolvesToRoot()
    {
        FolderPathResolver resolver(QStringLiteral("root-id"), []() { return QString(); });
        int calls = 0;
        const QString id = resolver.resolve(QStringLiteral(""), [&](auto, auto, auto, auto) { ++calls; });
        QCOMPARE(id, QStringLiteral("root-id"));
        QCOMPARE(calls, 0);
    }

    void singleSegmentCreatesOneFolder()
    {
        int counter = 0;
        FolderPathResolver resolver(QStringLiteral("root"), [&]() { return QStringLiteral("f%1").arg(counter++); });
        QStringList created;
        const QString id = resolver.resolve(QStringLiteral("Backend"),
            [&](const QString &fid, const QString &name, const QString &parentId, const QString &path) {
                created << QStringLiteral("%1|%2|%3|%4").arg(fid, name, parentId, path);
            });
        QCOMPARE(id, QStringLiteral("f0"));
        QCOMPARE(created.size(), 1);
        QCOMPARE(created.first(), QStringLiteral("f0|Backend|root|Backend"));
    }

    void nestedPathCreatesIntermediateFoldersInOrder()
    {
        int counter = 0;
        FolderPathResolver resolver(QStringLiteral("root"), [&]() { return QStringLiteral("f%1").arg(counter++); });
        QStringList createdNames;
        const QString leafId = resolver.resolve(QStringLiteral("A/B/C"),
            [&](const QString &, const QString &name, const QString &, const QString &) {
                createdNames << name;
            });
        QCOMPARE(createdNames, QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        QCOMPARE(leafId, QStringLiteral("f2")); // C, o último criado
    }

    // Resolver o mesmo path duas vezes não cria pastas; sub-caminhos já criados são reaproveitados
    // ("A/B" e depois "A/C" só cria "C").
    void repeatedAndOverlappingPathsReuseCache()
    {
        int counter = 0;
        FolderPathResolver resolver(QStringLiteral("root"), [&]() { return QStringLiteral("f%1").arg(counter++); });
        int totalCreated = 0;
        auto onCreate = [&](auto, auto, auto, auto) { ++totalCreated; };

        const QString first = resolver.resolve(QStringLiteral("A/B"), onCreate);
        QCOMPARE(totalCreated, 2);

        const QString again = resolver.resolve(QStringLiteral("A/B"), onCreate);
        QCOMPARE(again, first);
        QCOMPARE(totalCreated, 2);

        resolver.resolve(QStringLiteral("A/C"), onCreate);
        QCOMPARE(totalCreated, 3); // só "C" é novo, "A" foi reaproveitado
    }

    void explicitMetadataIsAttachedToMatchingPath()
    {
        QMap<QString, QJsonObject> explicitMeta;
        QJsonObject meta;
        meta[QStringLiteral("icon")] = QStringLiteral("server");
        explicitMeta.insert(QStringLiteral("Backend"), meta);

        int counter = 0;
        FolderPathResolver resolver(QStringLiteral("root"), [&]() { return QStringLiteral("f%1").arg(counter++); },
            explicitMeta);
        QString capturedIcon;
        resolver.resolve(QStringLiteral("Backend"),
            [&](const QString &, const QString &, const QString &, const QString &path) {
                capturedIcon = resolver.explicitMetadataFor(path).value(QStringLiteral("icon")).toString();
            });
        QCOMPARE(capturedIcon, QStringLiteral("server"));
    }

    // Path com o prefixo do nome da raiz (como na árvore) precisa bater com o path relativo dos comandos.
    void stripRootPrefixRemovesLeadingRootName()
    {
        QCOMPARE(FolderPathResolver::stripRootPrefix(
            QStringLiteral("Amazon Marketplace API/Sincronizar"), QStringLiteral("Amazon Marketplace API")),
            QStringLiteral("Sincronizar"));
        QCOMPARE(FolderPathResolver::stripRootPrefix(
            QStringLiteral("Sincronizar"), QStringLiteral("Amazon Marketplace API")),
            QStringLiteral("Sincronizar"));
    }

    void resolveNamesToIdsSkipsUnknownNames()
    {
        const QMap<QString, QString> idByName = {
            {QStringLiteral("Login"), QStringLiteral("c1")},
            {QStringLiteral("Build"), QStringLiteral("c2")},
        };
        const QStringList ids = resolveNamesToIds(
            {QStringLiteral("Login"), QStringLiteral("Nao Existe"), QStringLiteral("Build")}, idByName);
        QCOMPARE(ids, QStringList({QStringLiteral("c1"), QStringLiteral("c2")}));
    }
};

QTEST_MAIN(TestFolderPathResolver)
#include "test_folder_path_resolver.moc"
