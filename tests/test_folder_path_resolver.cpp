#include <QTest>

#include "core/folder-path-resolver.h"

using namespace kai::core;

// Cobre FolderPathResolver isoladamente — extraído nesta sessão porque o
// Import de Projeto e o Export/Import Configuration tinham cada um sua
// PRÓPRIA implementação da mesma ideia (resolver "folder"/"path" -> id,
// criando pastas intermediárias sob demanda), e elas divergiam em detalhe
// (achado real: só uma tolerava um path com o prefixo do nome do projeto).
// Ter UM lugar só testado evita essa classe de bug se repetir.
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

    // Resolver o MESMO path duas vezes não cria pastas de novo, e um
    // sub-caminho já criado por uma resolução anterior é reaproveitado por
    // outra (ex: "A/B" e depois "A/C" só cria "C", reusa "A").
    void repeatedAndOverlappingPathsReuseCache()
    {
        int counter = 0;
        FolderPathResolver resolver(QStringLiteral("root"), [&]() { return QStringLiteral("f%1").arg(counter++); });
        int totalCreated = 0;
        auto onCreate = [&](auto, auto, auto, auto) { ++totalCreated; };

        const QString first = resolver.resolve(QStringLiteral("A/B"), onCreate);
        QCOMPARE(totalCreated, 2); // A, B

        const QString again = resolver.resolve(QStringLiteral("A/B"), onCreate);
        QCOMPARE(again, first);
        QCOMPARE(totalCreated, 2); // nada novo

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

    // O bug real que motivou extrair esta classe: um path escrito COM o
    // prefixo do nome da raiz (como aparece na árvore) precisa continuar
    // batendo com o path RELATIVO usado pelos comandos.
    void stripRootPrefixRemovesLeadingRootName()
    {
        QCOMPARE(FolderPathResolver::stripRootPrefix(
            QStringLiteral("Amazon Marketplace API/Sincronizar"), QStringLiteral("Amazon Marketplace API")),
            QStringLiteral("Sincronizar"));
        // Sem o prefixo, não mexe em nada.
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
