#include <QTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/kai-file-validator.h"
#include "engine/kip-session.h"

using namespace kai::core;
using namespace kai::engine;

// Roda os apps de demonstração de sample/kip DE VERDADE (bash puro: os demos
// falam o protocolo na mão, sem o helper `kai kip`) através da KipSession.
// O PATH do processo é reduzido ao mínimo para provar que nada depende do Kai.

namespace {

constexpr int kWait = 15000;

QString repoRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFile::exists(dir.filePath(QStringLiteral("sample/kip/kai.json")))) return dir.absolutePath();
        if (!dir.cdUp()) break;
    }
    return QString();
}

struct Demo {
    ProcessRunner runner;
    KipSession session{&runner};
    QTemporaryDir tmp;

    explicit Demo(const QString &script, const QString &extraPath = QString())
    {
        runner.setKillTimeoutMs(400);
        QMap<QString, QString> env;
        env.insert(QStringLiteral("PATH"), extraPath.isEmpty() ? QStringLiteral("/usr/bin:/bin")
                                                                : extraPath + QStringLiteral(":/usr/bin:/bin"));
        env.insert(QStringLiteral("KIP_VERSION"), QStringLiteral("1"));
        session.setDeclaredEnvVars({DeclaredEnvVar{QStringLiteral("KAI_DEMO_TOKEN")}});
        session.start(QStringLiteral("bash '%1/sample/kip/%2'").arg(repoRoot(), script), QString(), env);
    }

    bool awaiting(const QString &id) const
    {
        const auto &o = session.screen().openScreen();
        return o && o->id == id && session.screen().awaitingInput() && !o->locked;
    }

    void waitFor(const QString &id) { QTRY_VERIFY_WITH_TIMEOUT(awaiting(id), kWait); }
    void waitFinished() { QTRY_VERIFY_WITH_TIMEOUT(session.outcome().finished, kWait); }
    const KipDone &done() const { return *session.screen().done(); }
    bool hasBlock(int index) const
    {
        for (const KipBlock &b : session.screen().blocks()) {
            if (int(b.index()) == index) return true;
        }
        return false;
    }
};

} // namespace

class TestKipSamples : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<kai::engine::KipOutcome>();
        qRegisterMetaType<kai::core::KipSessionState>();
        qRegisterMetaType<kai::core::KipLevel>();
        QVERIFY2(!repoRoot().isEmpty(), "sample/kip não encontrado");
    }

    void projectFileValidatesCleanlyAndScriptsExist()
    {
        QFile f(repoRoot() + QStringLiteral("/sample/kip/kai.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        const ValidationResult result = validateKaiFileText(QString::fromUtf8(bytes));
        QCOMPARE(result.errorCount(), 0);
        QCOMPARE(result.warningCount(), 0);

        int kipCommands = 0;
        int inlineCodeCommands = 0;
        const QJsonArray commands = QJsonDocument::fromJson(bytes).object().value("commands").toArray();
        for (const QJsonValue &v : commands) {
            const QJsonObject c = v.toObject();
            if (!c.value("kip").toBool()) continue;
            // Python/Node: o texto do comando É o programa (sem arquivo); ver test_command_language.
            if (c.value("language").toString() == QStringLiteral("python")
                || c.value("language").toString() == QStringLiteral("node")) {
                ++inlineCodeCommands;
                continue;
            }
            ++kipCommands;
            const QString script = c.value("command").toString().mid(2); // "./x.sh"
            const QFileInfo info(repoRoot() + QStringLiteral("/sample/kip/") + script);
            QVERIFY2(info.exists() && info.isExecutable(), qPrintable(script));
        }
        QVERIFY(kipCommands >= 11);
        QVERIFY(inlineCodeCommands >= 2);
    }

    void demosDoNotDependOnTheKaiBinary()
    {
        const QStringList scripts = QDir(repoRoot() + QStringLiteral("/sample/kip"))
                                        .entryList({QStringLiteral("*.sh")}, QDir::Files);
        QVERIFY(scripts.size() >= 10);
        for (const QString &name : scripts) {
            QFile f(repoRoot() + QStringLiteral("/sample/kip/") + name);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(f.readAll());
            QVERIFY2(!text.contains(QRegularExpression(QStringLiteral("\\bkai\\.exe\\b|(^|[^\\w-])kai\\s+kip\\s"))),
                     qPrintable(name + QStringLiteral(" chama o helper kai")));
        }
    }

    void deployWizard()
    {
        Demo d(QStringLiteral("deploy.sh"));
        d.waitFor(QStringLiteral("target"));
        d.session.setFieldValue(QStringLiteral("env"), QStringLiteral("prod"));
        QVERIFY(d.session.submit());
        d.waitFor(QStringLiteral("sure"));
        QVERIFY(d.session.screen().openScreen()->danger);
        // Voltar devolve o prompt (a demo repete o laço).
        d.session.back();
        d.waitFor(QStringLiteral("target"));
        d.session.setFieldValue(QStringLiteral("env"), QStringLiteral("prod"));
        QVERIFY(d.session.submit());
        d.waitFor(QStringLiteral("sure"));
        QVERIFY(d.session.confirm(true));
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QCOMPARE(d.done().title, QStringLiteral("Deployed to prod"));
        QCOMPARE(d.done().actions.size(), 2);
    }

    // A variante escrita com o helper `kai kip` (helper/deploy.sh): precisa do binário `kai`
    // ao lado dos testes; sem ele, é pulada.
    void deployWrittenWithTheHelperWorks()
    {
        const QString binDir = QCoreApplication::applicationDirPath();
        if (!QFileInfo(binDir + QStringLiteral("/kai")).isExecutable()) {
            QSKIP("binário kai ausente");
        }
        Demo d(QStringLiteral("helper/deploy.sh"), binDir);
        d.waitFor(QStringLiteral("target"));
        QCOMPARE(d.session.screen().openScreen()->fieldNamed(QStringLiteral("env"))->options.size(), 3);
        d.session.setFieldValue(QStringLiteral("env"), QStringLiteral("prod"));
        QVERIFY(d.session.submit());
        d.waitFor(QStringLiteral("sure"));
        QVERIFY(d.session.screen().openScreen()->danger);
        d.session.back();
        d.waitFor(QStringLiteral("target"));
        d.session.setFieldValue(QStringLiteral("env"), QStringLiteral("dev"));
        d.session.setFieldValue(QStringLiteral("opts"), QJsonObject{{"dry", true}, {"notify", false}});
        QVERIFY(d.session.submit());
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QCOMPARE(d.done().title, QStringLiteral("Deployed to dev"));
        QVERIFY(d.done().text.contains(QStringLiteral("dry run")));
        QCOMPARE(d.done().actions.size(), 2);
    }

    void databaseRestore()
    {
        Demo d(QStringLiteral("db-restore.sh"));
        d.waitFor(QStringLiteral("backup"));
        QVERIFY(d.session.submit()); // linha padrão (b2)
        d.waitFor(QStringLiteral("sure"));
        QVERIFY(d.session.confirm(true));
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QVERIFY(d.hasBlock(3)); // KipSteps
        QCOMPARE(d.done().actions.size(), 1);
    }

    void clusterNavigatorCascades()
    {
        Demo d(QStringLiteral("k8s-navigator.sh"));
        d.waitFor(QStringLiteral("nav"));
        const auto &open = *d.session.screen().openScreen();
        QCOMPARE(open.fieldNamed(QStringLiteral("namespace"))->options.size(), 3);

        d.session.setFieldValue(QStringLiteral("context"), QStringLiteral("staging"));
        d.session.sendChange(QStringLiteral("context"));
        // O patch troca as opções de namespace (staging tem 2) e o submit volta.
        QTRY_COMPARE_WITH_TIMEOUT(
            d.session.screen().openScreen()->fieldNamed(QStringLiteral("namespace"))->options.size(), 2, kWait);
        QTRY_VERIFY_WITH_TIMEOUT(!d.session.screen().openScreen()->changePending, kWait);
        d.session.setFieldValue(QStringLiteral("pod"), QStringLiteral("api-dev-1"));
        QVERIFY(d.session.submit());
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QCOMPARE(d.done().title, QStringLiteral("Selected api-dev-1"));
    }

    void scaffoldRejectsInvalidNameThenCreates()
    {
        Demo d(QStringLiteral("scaffold.sh"));
        d.waitFor(QStringLiteral("project"));
        d.session.setFieldValue(QStringLiteral("name"), QStringLiteral("Bad Name"));
        d.session.setFieldValue(QStringLiteral("dest"), d.tmp.path());
        QVERIFY(d.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(
            !d.session.screen().openScreen()->errors.value(QStringLiteral("name")).isEmpty(), kWait);
        d.waitFor(QStringLiteral("project"));
        d.session.setFieldValue(QStringLiteral("name"), QStringLiteral("my-app"));
        QVERIFY(d.session.submit());
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QVERIFY(QFileInfo(d.tmp.filePath(QStringLiteral("my-app/src"))).isDir());
    }

    void onboardingEndsWithWarningAndNotification()
    {
        Demo d(QStringLiteral("onboarding.sh"));
        QSignalSpy notify(&d.session, &KipSession::notifyRequested);
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QCOMPARE(d.done().level, KipLevel::Warning);
        QCOMPARE(notify.count(), 1);
    }

    void loginRetriesAndExportsDeclaredToken()
    {
        Demo d(QStringLiteral("login.sh"));
        QSignalSpy setEnv(&d.session, &KipSession::setEnvRequested);
        d.waitFor(QStringLiteral("login"));
        d.session.setFieldValue(QStringLiteral("password"), QStringLiteral("wrong"));
        QVERIFY(d.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(
            !d.session.screen().openScreen()->errors.value(QStringLiteral("password")).isEmpty(), kWait);
        d.waitFor(QStringLiteral("login"));
        d.session.setFieldValue(QStringLiteral("password"), QStringLiteral("kai"));
        QVERIFY(d.session.submit());
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QCOMPARE(setEnv.count(), 1);
        QCOMPARE(setEnv.first().at(0).toString(), QStringLiteral("KAI_DEMO_TOKEN"));
        QVERIFY(setEnv.first().at(1).toString().startsWith(QStringLiteral("demo-")));
    }

    void convertWritesNextToTheInputAndAsksBeforeOverwriting()
    {
        Demo d(QStringLiteral("convert.sh"));
        QFile in(d.tmp.filePath(QStringLiteral("kipdemo-picture.png")));
        QVERIFY(in.open(QIODevice::WriteOnly));
        in.write("x");
        in.close();
        QFile existing(d.tmp.filePath(QStringLiteral("kipdemo-picture.webp")));
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("old");
        existing.close();

        d.waitFor(QStringLiteral("convert"));
        d.session.setFieldValue(QStringLiteral("input"), in.fileName());
        QVERIFY(d.session.submit());
        // O destino já existe: o mesmo prompt volta com o erro no campo.
        QTRY_VERIFY_WITH_TIMEOUT(
            !d.session.screen().openScreen()->errors.value(QStringLiteral("input")).isEmpty(), kWait);
        d.waitFor(QStringLiteral("convert"));
        d.session.setFieldValue(QStringLiteral("opts"),
                                QJsonObject{{QStringLiteral("strip"), true}, {QStringLiteral("overwrite"), true}});
        QVERIFY(d.session.submit());
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QVERIFY(d.done().actions.at(0).path.endsWith(QStringLiteral("kipdemo-picture.webp")));
        QCOMPARE(QFileInfo(existing.fileName()).size(), qint64(1));
    }

    void lookupShowsTable()
    {
        Demo d(QStringLiteral("lookup.sh"));
        d.waitFor(QStringLiteral("q"));
        d.session.setFieldValue(QStringLiteral("query"), QStringLiteral("zzz"));
        QVERIFY(d.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(
            !d.session.screen().openScreen()->errors.value(QStringLiteral("query")).isEmpty(), kWait);
        d.waitFor(QStringLiteral("q"));
        d.session.setFieldValue(QStringLiteral("query"), QStringLiteral("globex"));
        QVERIFY(d.session.submit());
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QVERIFY(d.hasBlock(4)); // KipTable
        QCOMPARE(d.done().title, QStringLiteral("2 customer(s) found"));
    }

    void branchPickerChipsRunInABoxAndTheListShrinks()
    {
        Demo d(QStringLiteral("branches.sh"));
        d.waitFor(QStringLiteral("pick"));
        const KipField *branch = d.session.screen().openScreen()->fieldNamed(QStringLiteral("branch"));
        QVERIFY(branch);
        QCOMPARE(branch->pageSize, 5);
        QCOMPARE(branch->searchable, std::optional<bool>(true));
        QCOMPARE(branch->options.size(), 12);
        QCOMPARE(d.session.screen().openScreen()->chips.size(), 4);

        // ctx: roda e termina em sucesso, com o prompt ainda aberto
        d.session.setFieldValue(QStringLiteral("branch"), QStringLiteral("feat/payments"));
        QVERIFY(d.session.startChip(QStringLiteral("ctx")));
        QTRY_VERIFY_WITH_TIMEOUT(d.session.screen().openScreen()->chipRun->finished(), kWait);
        QCOMPARE(d.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Success);
        QVERIFY(d.session.screen().openScreen()->chipRun->text.contains(QStringLiteral("feat/payments")));
        d.session.dismissChip();

        // sync numa branch que "conflita": erro, e a sessão segue de pé
        d.session.setFieldValue(QStringLiteral("branch"), QStringLiteral("fix/cart-total"));
        QVERIFY(d.session.startChip(QStringLiteral("sync")));
        QTRY_VERIFY_WITH_TIMEOUT(d.session.screen().openScreen()->chipRun->finished(), kWait);
        QCOMPARE(d.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Error);
        QVERIFY(d.session.screen().openScreen()->chipRun->text.contains(QStringLiteral("CONFLICT")));
        d.session.dismissChip();

        // delete pede confirmação; confirmando, a branch some da lista (patch)
        d.session.setFieldValue(QStringLiteral("branch"), QStringLiteral("perf/images"));
        QVERIFY(d.session.startChip(QStringLiteral("delete")));
        QCOMPARE(d.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Confirming);
        QVERIFY(d.session.confirmChip());
        QTRY_VERIFY_WITH_TIMEOUT(d.session.screen().openScreen()->chipRun->finished(), kWait);
        QCOMPARE(d.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Success);
        QTRY_COMPARE_WITH_TIMEOUT(
            d.session.screen().openScreen()->fieldNamed(QStringLiteral("branch"))->options.size(), 11, kWait);
        d.session.dismissChip();

        // a branch padrão não pode ser apagada
        d.session.setFieldValue(QStringLiteral("branch"), QStringLiteral("main"));
        QVERIFY(d.session.startChip(QStringLiteral("delete")));
        QVERIFY(d.session.confirmChip());
        QTRY_VERIFY_WITH_TIMEOUT(d.session.screen().openScreen()->chipRun->finished(), kWait);
        QCOMPARE(d.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Error);
        d.session.dismissChip();

        // por fim o passo normal: trocar de branch
        d.session.setFieldValue(QStringLiteral("branch"), QStringLiteral("feat/search"));
        QVERIFY(d.session.submit());
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QCOMPARE(d.done().title, QStringLiteral("On branch feat/search"));
    }

    void rawProtocolWorksWithoutHelper()
    {
        Demo d(QStringLiteral("raw-protocol.sh"));
        d.waitFor(QStringLiteral("name"));
        d.session.setFieldValue(QStringLiteral("who"), QStringLiteral("Ana"));
        QVERIFY(d.session.submit());
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QCOMPARE(d.done().title, QStringLiteral("Hello, Ana!"));
    }

    void programThatDoesNotSpeakKipIsUnsupported()
    {
        Demo d(QStringLiteral("not-kip.sh"));
        d.waitFinished();
        QCOMPARE(d.session.state(), KipSessionState::Unsupported);
        QVERIFY(!d.session.outcome().success);
        QVERIFY(d.session.logText().contains(QStringLiteral("unknown option --kip")));
    }
};

QTEST_MAIN(TestKipSamples)
#include "test_kip_samples.moc"
