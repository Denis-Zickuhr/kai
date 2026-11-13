#include <QTest>

#include <QJsonArray>
#include <QJsonObject>

#include "core/store-model.h"
#include "core/store-preview.h"

using namespace kai::core;

class TestStoreModel : public QObject {
    Q_OBJECT

private:
    static QJsonObject manifestJson()
    {
        const QString hash(64, QLatin1Char('a'));
        return QJsonObject{
            {"package_format", 1},
            {"id", "acme/deploy"},
            {"kind", "command"},
            {"version", "1.0.0"},
            {"name", "Deploy"},
            {"files", QJsonArray{QJsonObject{{"path", "kai.yml"}, {"sha256", hash}}}},
        };
    }

private slots:
    void semverOrdersPrereleasesBelowStable()
    {
        QVERIFY(compareVersions(QStringLiteral("1.0.0-beta.2"), QStringLiteral("1.0.0")) < 0);
        QVERIFY(compareVersions(QStringLiteral("1.0.0-beta.2"), QStringLiteral("1.0.0-beta.10")) < 0);
        QVERIFY(compareVersions(QStringLiteral("1.2.0"), QStringLiteral("1.10.0")) < 0);
        QCOMPARE(compareVersions(QStringLiteral("1.0.0"), QStringLiteral("v1.0.0")), 0);
        QVERIFY(compareVersions(QStringLiteral("junk"), QStringLiteral("0.0.1")) < 0);
    }

    void compatibilityReportsTheReason()
    {
        QCOMPARE(checkCompatibility(QStringLiteral("2.0.0"), {}, QStringLiteral("1.0.0"), {}).reason,
                 QStringLiteral("kai_min:2.0.0"));
        QCOMPARE(checkCompatibility({}, {QStringLiteral("quantum")}, QStringLiteral("1.0.0"), {QStringLiteral("http")}).reason,
                 QStringLiteral("capability:quantum"));
        QVERIFY(checkCompatibility(QStringLiteral("1.0.0-beta.2"), {QStringLiteral("http")}, QStringLiteral("1.0.0"),
                                   {QStringLiteral("http")}).ok);
    }

    void latestCompatibleSkipsIncompatibleVersions()
    {
        StoreIndexEntry e;
        e.versions = {{QStringLiteral("1.0.0"), {}, {}, {}, {}},
                      {QStringLiteral("2.0.0"), QStringLiteral("9.0.0"), {}, {}, {}},
                      {QStringLiteral("1.5.0"), {}, {}, {}, {}}};
        const StoreVersionEntry *best = latestCompatible(e, QStringLiteral("1.0.0"), {});
        QVERIFY(best);
        QCOMPARE(best->version, QStringLiteral("1.5.0"));
        e.versions = {{QStringLiteral("2.0.0"), QStringLiteral("9.0.0"), {}, {}, {}}};
        QVERIFY(!latestCompatible(e, QStringLiteral("1.0.0"), {}));
    }

    void safePathsRejectTraversal()
    {
        QVERIFY(isSafeStorePath(QStringLiteral("scripts/deploy.sh")));
        for (const QString &bad : {QStringLiteral("../x"), QStringLiteral("/etc/passwd"), QStringLiteral("a/../b"),
                                   QStringLiteral("C:/x"), QStringLiteral("a\\b"), QStringLiteral(""), QStringLiteral("a//b")}) {
            QVERIFY2(!isSafeStorePath(bad), qPrintable(bad));
        }
    }

    void manifestValidates()
    {
        const auto m = parseStoreManifest(manifestJson());
        QVERIFY(m);
        QVERIFY(!hasStoreErrors(validateStoreManifest(*m)));
    }

    void manifestRejectsBadHashPathAndKind()
    {
        QJsonObject o = manifestJson();
        o["kind"] = "widget";
        o["files"] = QJsonArray{QJsonObject{{"path", "../kai.yml"}, {"sha256", "zz"}}};
        const auto m = parseStoreManifest(o);
        QVERIFY(m);
        const auto issues = validateStoreManifest(*m);
        QVERIFY(hasStoreErrors(issues));
        QStringList codes;
        for (const auto &i : issues) codes << i.message;
        QVERIFY(codes.contains(QStringLiteral("bad_kind")));
        QVERIFY(codes.contains(QStringLiteral("unsafe_path")));
        QVERIFY(codes.contains(QStringLiteral("bad_sha256")));
    }

    void projectNeedsExactlyOneSource()
    {
        QJsonObject o = manifestJson();
        o["kind"] = "project";
        o["source"] = QJsonObject{{"clone", QJsonObject{{"url", "https://x/y.git"}, {"ref", "v1"}}}};
        auto m = parseStoreManifest(o);
        QVERIFY(m);
        QVERIFY(hasStoreErrors(validateStoreManifest(*m))); // files + clone
        o.remove("files");
        m = parseStoreManifest(o);
        QVERIFY(!hasStoreErrors(validateStoreManifest(*m)));
    }

    void newerStoreFormatAsksToUpdateKai()
    {
        StoreParseError err;
        QVERIFY(!parseStoreHeader(QJsonObject{{"store_format", 99}}, &err));
        QVERIFY(err.needsUpdate);
    }

    void badIndexRowsAreSkippedNotFatal()
    {
        QJsonObject good{{"id", "a/b"}, {"kind", "command"}, {"name", "B"},
                         {"versions", QJsonArray{QJsonObject{{"version", "1.0.0"}, {"manifest", "m.yml"}}}}};
        QJsonObject bad{{"id", "NOPE"}, {"kind", "command"}};
        int skipped = 0;
        const auto c = parseStoreIndex(QJsonObject{{"store_format", 1}, {"packages", QJsonArray{good, bad}}}, nullptr, &skipped);
        QVERIFY(c);
        QCOMPARE(c->packages.size(), 1);
        QCOMPARE(skipped, 1);
        QCOMPARE(c->packages.first().latest, QStringLiteral("1.0.0"));
    }

    void kindIsSuggestedFromTheLocation()
    {
        QCOMPARE(suggestStoreKind(QStringLiteral("https://github.com/o/r")), StoreProviderKind::Repo);
        QCOMPARE(suggestStoreKind(QStringLiteral("https://x.org/r.git")), StoreProviderKind::Repo);
        QCOMPARE(suggestStoreKind(QStringLiteral("/home/u/catalog.yml")), StoreProviderKind::File);
        QCOMPARE(suggestStoreKind(QStringLiteral("https://x.org/catalog.json")), StoreProviderKind::File);
        QCOMPARE(suggestStoreKind(QStringLiteral("https://store.example.com")), StoreProviderKind::Api);
    }

    void installRulesStripHooksAndGateCronAndAutoRun()
    {
        Folder root;
        root.id = QStringLiteral("f");
        root.hooks.pre = {QStringLiteral("x")};
        Command a;
        a.name = QStringLiteral("a");
        a.cronExpression = QStringLiteral("* * * * *");
        a.autoRun = true;
        a.hooks.post = {QStringLiteral("y")};
        Command b = a;
        b.name = QStringLiteral("b");
        QVector<Folder> folders{root};
        QVector<Command> commands{a, b};
        StoreInstallChoices choices;
        choices.allowCron.insert(QStringLiteral("b"));
        const int removed = applyStoreInstallRules(folders, commands, QStringLiteral("f"), choices, {QStringLiteral("TOKEN")});
        QCOMPARE(removed, 3);
        QVERIFY(commands[0].cronExpression.isEmpty());
        QVERIFY(!commands[0].autoRun);
        QVERIFY(!commands[1].cronExpression.isEmpty());
        QVERIFY(!commands[1].autoRun);
        QVERIFY(commands[1].hooks.isEmpty());
        QVERIFY(folders[0].secretEnvKeys.contains(QStringLiteral("TOKEN")));
        QCOMPARE(folders[0].envVars.value(QStringLiteral("TOKEN")), QString());
    }

    // Instalar como cópia: a pasta nova não pode apontar para os comandos da instalação anterior.
    void collidingIdsGetNewOnesAndReferencesFollow()
    {
        Folder root;
        root.id = QStringLiteral("f_root");
        Folder sub;
        sub.id = QStringLiteral("f_sub");
        sub.parentId = QStringLiteral("f_root");
        Command a;
        a.id = QStringLiteral("c_a");
        a.folderId = QStringLiteral("f_sub");
        Command b;
        b.id = QStringLiteral("c_b");
        b.folderId = QStringLiteral("f_root");
        b.hooks.post = {QStringLiteral("c_a")};
        root.actions = {QStringLiteral("c_a")};
        QVector<Folder> folders{root, sub};
        QVector<Command> commands{a, b};
        const QSet<QString> existing{QStringLiteral("f_root"), QStringLiteral("f_sub"), QStringLiteral("c_a"), QStringLiteral("c_b")};
        QCOMPARE(remapCollidingStoreIds(folders, commands, QStringLiteral("f_root"), existing), 3);
        QCOMPARE(folders[0].id, QStringLiteral("f_root")); // a raiz é do import
        QVERIFY(!existing.contains(folders[1].id));
        QVERIFY(!existing.contains(commands[0].id));
        QCOMPARE(commands[0].folderId, folders[1].id);
        QCOMPARE(folders[1].parentId.value(), QStringLiteral("f_root"));
        QCOMPARE(commands[1].hooks.post.first(), commands[0].id);
        QCOMPARE(folders[0].actions.first(), commands[0].id);
        // Nada colide: nada muda.
        QCOMPARE(remapCollidingStoreIds(folders, commands, QStringLiteral("f_root"), {}), 0);
    }

    void previewListsHooksAsSkipped()
    {
        const QJsonObject kai{{"commands", QJsonArray{QJsonObject{{"name", "x"}, {"command", "echo"},
                                                                  {"hooks", QJsonObject{{"pre", QJsonArray{"y"}}}}}}}};
        const StorePreview p = buildStorePreview(kai);
        QCOMPARE(p.hooksSkipped, 1);
        QCOMPARE(p.commands.size(), 1);
    }
};

QTEST_MAIN(TestStoreModel)
#include "test_store_model.moc"
