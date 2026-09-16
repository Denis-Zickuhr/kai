#include <QTest>
#include <QTemporaryDir>

#include "core/cli-trust-store.h"

using namespace kai::core;

// Cobre a lista de confiança do modo local de CLI Paths (pedido do
// usuário: "ter uma trust list pro cli") — mesmo espírito do .envrc do
// direnv: confiar uma vez por (diretório, conteúdo exato); conteúdo
// mudado exige confiar de novo.
class TestCliTrustStore : public QObject {
    Q_OBJECT

private slots:
    void untrustedDirectoryIsNotTrustedByDefault()
    {
        QTemporaryDir dir;
        CliTrustStore store(dir.filePath(QStringLiteral("trust.json")));
        QVERIFY(!store.isTrusted(QStringLiteral("/home/x/repo"), QStringLiteral("abc")));
    }

    void trustingPersistsAcrossInstances()
    {
        QTemporaryDir dir;
        const QString storePath = dir.filePath(QStringLiteral("trust.json"));
        const QString hash = CliTrustStore::hashContent(QStringLiteral("project_name: Demo"));

        {
            CliTrustStore store(storePath);
            store.trust(QStringLiteral("/home/x/repo"), hash);
        }
        {
            CliTrustStore store(storePath); // nova instância, mesmo arquivo
            QVERIFY(store.isTrusted(QStringLiteral("/home/x/repo"), hash));
        }
    }

    // Conteúdo mudou desde a última confiança: hash diferente, exige
    // confirmar de novo (não confia "no escuro" num arquivo alterado).
    void changedContentIsNoLongerTrusted()
    {
        QTemporaryDir dir;
        const QString storePath = dir.filePath(QStringLiteral("trust.json"));
        CliTrustStore store(storePath);
        store.trust(QStringLiteral("/home/x/repo"), CliTrustStore::hashContent(QStringLiteral("v1")));

        QVERIFY(!store.isTrusted(QStringLiteral("/home/x/repo"), CliTrustStore::hashContent(QStringLiteral("v2"))));
    }

    // Confiar num diretório não afeta outro.
    void trustIsPerDirectory()
    {
        QTemporaryDir dir;
        const QString storePath = dir.filePath(QStringLiteral("trust.json"));
        const QString hash = CliTrustStore::hashContent(QStringLiteral("same content"));
        CliTrustStore store(storePath);
        store.trust(QStringLiteral("/home/x/repo1"), hash);

        QVERIFY(store.isTrusted(QStringLiteral("/home/x/repo1"), hash));
        QVERIFY(!store.isTrusted(QStringLiteral("/home/x/repo2"), hash));
    }

    void hashIsStableAndDeterministic()
    {
        const QString h1 = CliTrustStore::hashContent(QStringLiteral("abc"));
        const QString h2 = CliTrustStore::hashContent(QStringLiteral("abc"));
        const QString h3 = CliTrustStore::hashContent(QStringLiteral("abd"));
        QCOMPARE(h1, h2);
        QVERIFY(h1 != h3);
        QVERIFY(!h1.isEmpty());
    }
};

QTEST_MAIN(TestCliTrustStore)
#include "test_cli_trust_store.moc"
