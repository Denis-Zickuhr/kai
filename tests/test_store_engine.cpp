#include <QTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "core/store-local.h"
#include "engine/store-client.h"
#include "engine/store-installer.h"
#include "engine/store-publisher.h"
#include "engine/store-signature.h"

using namespace kai;
using namespace kai::core;
using namespace kai::engine;

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// Serve uma pasta por HTTP, guarda o que recebeu e sabe responder 304 e redirecionar.
class FolderServer : public QTcpServer {
public:
    explicit FolderServer(const QString &root, QObject *parent = nullptr) : QTcpServer(parent), m_root(root)
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s]() { handle(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/").arg(serverPort()); }
    QStringList authHeaders;
    int conditionalHits = 0;

private:
    void handle(QTcpSocket *s)
    {
        const QByteArray req = s->readAll();
        const QList<QByteArray> lines = req.split('\n');
        const QString path = QString::fromLatin1(lines.value(0).split(' ').value(1));
        QByteArray etagIn;
        for (const QByteArray &l : lines) {
            if (l.toLower().startsWith("authorization:")) authHeaders << QString::fromLatin1(l.mid(14).trimmed());
            if (l.toLower().startsWith("if-none-match:")) etagIn = l.mid(14).trimmed();
        }
        if (path == QLatin1String("/redirect")) {
            s->write(QByteArray("HTTP/1.1 302 Found\r\nLocation: http://localhost:%1/store.yml\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                         .replace("%1", QByteArray::number(serverPort())));
            s->disconnectFromHost();
            return;
        }
        QFile f(QDir(m_root).filePath(path.mid(1)));
        if (!f.open(QIODevice::ReadOnly)) {
            s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        } else {
            const QByteArray body = f.readAll();
            const QByteArray etag = "\"v1\"";
            if (etagIn == etag) {
                ++conditionalHits;
                s->write("HTTP/1.1 304 Not Modified\r\nETag: \"v1\"\r\nConnection: close\r\n\r\n");
            } else {
                s->write("HTTP/1.1 200 OK\r\nETag: \"v1\"\r\nContent-Length: " + QByteArray::number(body.size())
                         + "\r\nConnection: close\r\n\r\n" + body);
            }
        }
        s->disconnectFromHost();
    }
    QString m_root;
};

} // namespace

class TestStoreEngine : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QTemporaryDir m_cache;
    QString m_repo; // pasta do catálogo gerado

    static StoreProviderConfig provider(StoreProviderKind kind, const QString &location, const QString &token = QString())
    {
        StoreProviderConfig p;
        p.id = QStringLiteral("p1");
        p.name = QStringLiteral("Test");
        p.kind = kind;
        p.location = location;
        p.token = token;
        return p;
    }

    void makePackage(const QString &root, const QString &slug, const QString &version, const QString &extra = QString())
    {
        const QString dir = root + QStringLiteral("/packages/demo/") + slug + QLatin1Char('/') + version;
        writeFile(dir + QStringLiteral("/package.yml"),
                  QStringLiteral("package_format: 1\nid: demo/%1\nkind: command\nversion: %2\nname: %1\n%3")
                      .arg(slug, version, extra).toUtf8());
        writeFile(dir + QStringLiteral("/kai.yml"),
                  "commands:\n  - name: Hello\n    type: command\n    command: echo hi\n    cron_expression: \"0 * * * *\"\n    hooks:\n      pre: []\n");
        QVERIFY(packStorePackageDir(dir).ok);
    }

    StoreCatalogResult load(StoreClient &client)
    {
        bool done = false;
        StoreCatalogResult out;
        client.loadCatalog([&](const StoreCatalogResult &r) { out = r; done = true; });
        [&] { QTRY_VERIFY_WITH_TIMEOUT(done, 20000); }();
        return out;
    }

    StoreStagedPackage stage(StoreClient &client, const StoreIndexEntry &entry, StoreManifestResult *manifestOut = nullptr)
    {
        const StoreVersionEntry &v = entry.versions.first();
        bool done = false;
        StoreManifestResult mr;
        client.fetchManifest(v, [&](const StoreManifestResult &r) { mr = r; done = true; });
        [&] { QTRY_VERIFY_WITH_TIMEOUT(done, 20000); }();
        if (manifestOut) *manifestOut = mr;
        StoreStagedPackage staged;
        if (!mr.ok) {
            staged.error = mr.error;
            return staged;
        }
        StoreInstaller installer(&client);
        bool staged_done = false;
        installer.stage(mr.manifest, v.manifest, m_tmp.filePath(QStringLiteral("staging")),
                        [&](const StoreStagedPackage &s) { staged = s; staged_done = true; });
        [&] { QTRY_VERIFY_WITH_TIMEOUT(staged_done, 20000); }();
        return staged;
    }

private slots:
    void initTestCase()
    {
        qputenv("XDG_CACHE_HOME", m_cache.path().toUtf8());
    }

    void init()
    {
        m_repo = m_tmp.filePath(QStringLiteral("repo"));
        QDir(m_repo).removeRecursively();
        writeFile(m_repo + QStringLiteral("/store.yml"), "store_format: 1\nname: Demo\n");
        makePackage(m_repo, QStringLiteral("hello"), QStringLiteral("1.0.0"));
        makePackage(m_repo, QStringLiteral("hello"), QStringLiteral("1.1.0"));
        QVERIFY(buildStoreIndex(m_repo).ok);
    }

    void fileProviderLoadsASingleCatalog()
    {
        const QString catalog = m_repo + QStringLiteral("/catalog.yml");
        QVERIFY(buildStoreIndex(m_repo, catalog).ok);
        StoreClient client(provider(StoreProviderKind::File, catalog), {});
        const StoreCatalogResult r = load(client);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.catalog.packages.size(), 1);
        QCOMPARE(r.catalog.packages.first().latest, QStringLiteral("1.1.0"));
        QCOMPARE(r.signature, StoreSignatureState::None);
    }

    void repoByCloneFallsBackWhenThereIsNoRawAndReadsDist()
    {
        if (StoreGitCheckout::gitExecutable().isEmpty()) QSKIP("git not installed");
        const QString work = m_tmp.filePath(QStringLiteral("gitrepo"));
        QDir(work).removeRecursively();
        QDir().mkpath(work);
        // Catálogo publicado dentro de dist/.
        QDir().rename(m_repo, work + QStringLiteral("/dist"));
        const auto git = [&work](const QStringList &args) {
            QProcess p;
            p.setWorkingDirectory(work);
            p.start(QStringLiteral("git"), QStringList{QStringLiteral("-c"), QStringLiteral("user.name=t"), QStringLiteral("-c"),
                                                      QStringLiteral("user.email=t@t")} + args);
            return p.waitForFinished(30000) && p.exitCode() == 0;
        };
        QVERIFY(git({QStringLiteral("init"), QStringLiteral("-q")}));
        QVERIFY(git({QStringLiteral("add"), QStringLiteral(".")}));
        QVERIFY(git({QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("x")}));

        StoreClient::Options opts;
        opts.cacheDir = m_cache.filePath(QStringLiteral("clone"));
        StoreClient client(provider(StoreProviderKind::Repo, work), opts);
        const StoreCatalogResult r = load(client);
        QVERIFY2(r.ok, qPrintable(r.error + r.errorDetail));
        QCOMPARE(r.catalog.packages.size(), 1);

        // E instala a partir dele (o prefixo dist/ vale para os arquivos também).
        const StoreStagedPackage staged = stage(client, r.catalog.packages.first());
        QVERIFY2(staged.ok, qPrintable(staged.error));
        QCOMPARE(staged.preview.commands.size(), 1);
        StoreInstaller::discard(staged);
    }

    void httpProviderSendsTheTokenOnlyToItsHostAndUsesETag()
    {
        FolderServer server(m_repo);
        StoreClient::Options opts;
        opts.cacheDir = m_cache.filePath(QStringLiteral("http"));
        opts.rawBaseOverride = server.base();
        StoreClient client(provider(StoreProviderKind::Repo, QStringLiteral("https://example.com/o/r.git"), QStringLiteral("sekret")), opts);
        QVERIFY(load(client).ok);
        QVERIFY(!server.authHeaders.isEmpty());
        for (const QString &h : server.authHeaders) QCOMPARE(h, QStringLiteral("Bearer sekret"));
        QTRY_COMPARE_WITH_TIMEOUT(QFile::exists(opts.cacheDir) && !QDir(opts.cacheDir).entryList(QDir::Files).isEmpty(), true, 5000);
        QVERIFY(load(client).ok);
        QVERIFY(server.conditionalHits > 0);
    }

    void httpRefusesRedirectsOffTheProviderHostAndOversizeBodies()
    {
        FolderServer server(m_repo);
        HttpStoreSource source(QUrl(server.base()), QString(), QString());
        StoreFetchResult redirect;
        bool done = false;
        source.fetch(QStringLiteral("redirect"), 1024, [&](const StoreFetchResult &r) { redirect = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(redirect.error, QStringLiteral("off_host"));

        done = false;
        StoreFetchResult big;
        source.fetch(QStringLiteral("index.json"), 10, [&](const StoreFetchResult &r) { big = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(big.error, QStringLiteral("too_large"));
    }

    void plainHttpToARemoteHostIsRefused()
    {
        QVERIFY(!HttpStoreSource::isAllowedUrl(QUrl(QStringLiteral("http://example.com/x"))));
        QVERIFY(HttpStoreSource::isAllowedUrl(QUrl(QStringLiteral("https://example.com/x"))));
        QVERIFY(HttpStoreSource::isAllowedUrl(QUrl(QStringLiteral("http://127.0.0.1:80/x"))));
        QVERIFY(!StoreGitCheckout::isAllowedRepoUrl(QStringLiteral("ext::sh -c evil")));
        QVERIFY(!StoreGitCheckout::isAllowedRepoUrl(QStringLiteral("--upload-pack=evil")));
        QVERIFY(!StoreGitCheckout::isAllowedRepoUrl(QStringLiteral("git@github.com:o/r.git")));
    }

    void aTamperedFileBlocksTheInstall()
    {
        StoreClient fileClient(provider(StoreProviderKind::File, m_repo + QStringLiteral("/catalog.yml")), {});
        QVERIFY(buildStoreIndex(m_repo, m_repo + QStringLiteral("/catalog.yml")).ok);
        const StoreCatalogResult r = load(fileClient);
        QVERIFY(r.ok);
        writeFile(m_repo + QStringLiteral("/packages/demo/hello/1.1.0/kai.yml"), "commands: []\n# tampered\n");
        const StoreIndexEntry &entry = r.catalog.packages.first();
        QCOMPARE(entry.latest, QStringLiteral("1.1.0"));
        StoreIndexEntry only = entry;
        only.versions = {*findVersion(entry, QStringLiteral("1.1.0"))};
        const StoreStagedPackage staged = stage(fileClient, only);
        QVERIFY(!staged.ok);
        QCOMPARE(staged.error, QStringLiteral("hash_mismatch"));
        QVERIFY(staged.dir.isEmpty());
    }

    void aManifestWithATraversalPathNeverTouchesTheDisk()
    {
        StoreClient client(provider(StoreProviderKind::File, m_repo + QStringLiteral("/catalog.yml")), {});
        StoreManifest m;
        m.packageFormat = 1;
        m.id = QStringLiteral("demo/evil");
        m.kind = QStringLiteral("command");
        m.version = QStringLiteral("1.0.0");
        m.name = QStringLiteral("evil");
        m.files = {{QStringLiteral("../../escape.txt"), QString(64, QLatin1Char('a'))}};
        StoreInstaller installer(&client);
        bool done = false;
        StoreStagedPackage out;
        installer.stage(m, QStringLiteral("packages/x/package.yml"), m_tmp.filePath(QStringLiteral("staging")),
                        [&](const StoreStagedPackage &s) { out = s; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QCOMPARE(out.error, QStringLiteral("invalid_manifest"));
        QVERIFY(!QFile::exists(m_tmp.filePath(QStringLiteral("escape.txt"))));
    }

    void signedIndexIsVerifiedAndTamperingIsDetected()
    {
        const auto kp = store_signature::generateKeyPair();
        writeFile(m_repo + QStringLiteral("/store.yml"),
                  QStringLiteral("store_format: 1\nname: Demo\nsigning:\n  algorithm: ed25519\n  key: \"%1\"\n")
                      .arg(store_signature::toBase64(kp.publicKey)).toUtf8());
        writeFile(m_tmp.filePath(QStringLiteral("key")), store_signature::toBase64(kp.secretKey).toUtf8());
        const QString catalog = m_repo + QStringLiteral("/catalog.json");
        QVERIFY(buildStoreIndex(m_repo, catalog, m_tmp.filePath(QStringLiteral("key"))).ok);

        StoreClient ok(provider(StoreProviderKind::File, catalog), {});
        QCOMPARE(load(ok).signature, StoreSignatureState::Ok);

        QFile f(catalog);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QByteArray text = f.readAll();
        text.replace("\"name\": \"hello\"", "\"name\": \"HACKED\"");
        f.resize(0);
        f.write(text);
        f.close();
        StoreClient bad(provider(StoreProviderKind::File, catalog), {});
        QCOMPARE(load(bad).signature, StoreSignatureState::Invalid);
    }

    void aDeclaredKeyWithoutSignatureIsInvalid()
    {
        const auto kp = store_signature::generateKeyPair();
        writeFile(m_repo + QStringLiteral("/store.yml"),
                  QStringLiteral("store_format: 1\nname: Demo\nsigning:\n  key: \"%1\"\n")
                      .arg(store_signature::toBase64(kp.publicKey)).toUtf8());
        const QString catalog = m_repo + QStringLiteral("/catalog.json");
        QVERIFY(buildStoreIndex(m_repo, catalog).ok); // sem --key
        StoreClient client(provider(StoreProviderKind::File, catalog), {});
        QCOMPARE(load(client).signature, StoreSignatureState::Invalid);
    }

    void providersAndInstalledRecordsRoundTripAndTokensAreOwnerOnly()
    {
        QTemporaryDir dir;
        StoreLocalState state(dir.path());
        StoreProviderConfig p = provider(StoreProviderKind::Repo, QStringLiteral("https://github.com/o/r"), QStringLiteral("tok"));
        QVERIFY(state.saveProviders({p}));
        QCOMPARE(state.loadProviders().size(), 1);
        QCOMPARE(state.loadProviders().first().token, QStringLiteral("tok"));
        const auto perms = QFile::permissions(state.providersPath());
        QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));

        StoreInstalledRecord r;
        r.provider = QStringLiteral("p1");
        r.package = QStringLiteral("demo/hello");
        r.folderId = QStringLiteral("f1");
        r.version = QStringLiteral("1.0.0");
        r.installedAt = QDateTime::currentDateTimeUtc();
        QVERIFY(state.recordInstall(r));
        r.version = QStringLiteral("1.1.0");
        QVERIFY(state.recordInstall(r)); // mesma pasta: substitui
        QCOMPARE(state.loadInstalled().size(), 1);
        r.folderId = QStringLiteral("f2"); // cópia: convive
        QVERIFY(state.recordInstall(r));
        QCOMPARE(state.loadInstalled().size(), 2);
        QVERIFY(state.removeRecordForFolder(QStringLiteral("f1")));
        QCOMPARE(state.loadInstalled().size(), 1);
    }

    void aCorruptProvidersFileIsRecoveredNotFatal()
    {
        QTemporaryDir dir;
        StoreLocalState state(dir.path());
        writeFile(state.providersPath(), "{ not json");
        QVERIFY(state.loadProviders().isEmpty());
        QVERIFY(QFile::exists(state.providersPath() + QStringLiteral(".corrupt")));
    }

    // --- sample/store (offline): o que a documentação promete ---------------------------------------------------------

    const StoreIndexEntry *sampleEntry(const StoreCatalogResult &r, const QString &id)
    {
        for (const StoreIndexEntry &e : r.catalog.packages) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }

    void theSampleCatalogLoadsAndGreysOutTheIncompatiblePackage()
    {
        StoreClient client(provider(StoreProviderKind::File, QStringLiteral("sample/store/catalog.yml")), {});
        const StoreCatalogResult r = load(client);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.catalog.packages.size(), 7);
        const StoreIndexEntry *future = sampleEntry(r, QStringLiteral("demo/needs-the-future"));
        QVERIFY(future);
        QVERIFY(!latestCompatible(*future));
        QVERIFY(latestCompatible(*sampleEntry(r, QStringLiteral("demo/hello-command"))));
        QVERIFY(latestCompatible(*sampleEntry(r, QStringLiteral("demo/ship-to-staging"))));
    }

    void theSampleInstallsHelloAndSkipsHooksButAsksAboutCronAndAutoRun()
    {
        StoreClient client(provider(StoreProviderKind::File, QStringLiteral("sample/store/catalog.yml")), {});
        const StoreCatalogResult r = load(client);
        QVERIFY(r.ok);
        const StoreStagedPackage hello = stage(client, *sampleEntry(r, QStringLiteral("demo/hello-command")));
        QVERIFY2(hello.ok, qPrintable(hello.error));
        QCOMPARE(hello.preview.commands.size(), 1);
        StoreInstaller::discard(hello);

        const StoreStagedPackage ping = stage(client, *sampleEntry(r, QStringLiteral("demo/scheduled-ping")));
        QVERIFY2(ping.ok, qPrintable(ping.error));
        QCOMPARE(ping.preview.hooksSkipped, 1);
        QVERIFY(!ping.preview.commands.first().cron.isEmpty());
        QVERIFY(ping.preview.commands.first().autoRun);
        StoreInstaller::discard(ping);
    }

    void theSampleBadHashPackageIsBlocked()
    {
        StoreClient client(provider(StoreProviderKind::File, QStringLiteral("sample/store/catalog.yml")), {});
        const StoreCatalogResult r = load(client);
        QVERIFY(r.ok);
        const StoreStagedPackage bad = stage(client, *sampleEntry(r, QStringLiteral("demo/bad-hash")));
        QVERIFY(!bad.ok);
        QCOMPARE(bad.error, QStringLiteral("hash_mismatch"));
    }

    void theSampleProjectByReferenceClonesALocalBareRepo()
    {
        if (StoreGitCheckout::gitExecutable().isEmpty()) QSKIP("git not installed");
        QProcess fixture;
        fixture.start(QStringLiteral("bash"), {QStringLiteral("sample/store/tools/make-clone-fixture.sh")});
        if (!fixture.waitForFinished(60000) || fixture.exitCode() != 0) QSKIP("could not build the clone fixture");
        StoreClient client(provider(StoreProviderKind::File, QStringLiteral("sample/store/catalog.yml")), {});
        const StoreCatalogResult r = load(client);
        QVERIFY(r.ok);
        const StoreStagedPackage staged = stage(client, *sampleEntry(r, QStringLiteral("demo/app-by-reference")));
        QVERIFY2(staged.ok, qPrintable(staged.error + staged.errorDetail));
        QVERIFY(!staged.refMoves); // v1.0.0 é tag
        QVERIFY(QFile::exists(staged.kaiFilePath));
        StoreInstaller::discard(staged);
    }

    void theSignedSampleVerifiesAndAOneCharacterEditInvalidatesIt()
    {
        StoreClient client(provider(StoreProviderKind::File, QStringLiteral("sample/store/catalog-signed.json")), {});
        QCOMPARE(load(client).signature, StoreSignatureState::Ok);

        QFile src(QStringLiteral("sample/store/catalog-signed.json"));
        QVERIFY(src.open(QIODevice::ReadOnly));
        QByteArray text = src.readAll();
        QVERIFY(text.contains("Hello command"));
        text.replace("Hello command", "Hello commanD");
        const QString tampered = m_tmp.filePath(QStringLiteral("tampered.json"));
        writeFile(tampered, text);
        StoreClient bad(provider(StoreProviderKind::File, tampered), {});
        QCOMPARE(load(bad).signature, StoreSignatureState::Invalid);
    }
};

QTEST_MAIN(TestStoreEngine)
#include "test_store_engine.moc"
