#include <QTest>
#include "core/yaml-bridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

#include "core/kai-file-validator.h"
#include "engine/command-language.h"
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
        if (QFile::exists(dir.filePath(QStringLiteral("sample/kip/kai.yml")))) return dir.absolutePath();
        if (!dir.cdUp()) break;
    }
    return QString();
}

struct Demo {
    ProcessRunner runner;
    KipSession session{&runner};
    QTemporaryDir tmp;

    explicit Demo(const QString &script)
    {
        begin(QStringLiteral("bash '%1/sample/kip/%2'").arg(repoRoot(), script), QString());
    }

    // Um comando nativo com KIP ligado, como o Kai o monta num shell POSIX: o carregador do helper `kip` + o texto
    // do comando, rodando na pasta do projeto (como `kai.json` faz).
    static Demo *injected(const QString &code) { return new Demo(code, true); }

    Demo(const QString &code, bool)
    {
        QFile script(tmp.filePath(QStringLiteral("command.sh")));
        script.open(QIODevice::WriteOnly);
        script.write((kai::engine::kipShellLoader() + QLatin1Char('\n') + code).toUtf8());
        script.close();
        begin(QStringLiteral("bash '%1'").arg(script.fileName()), repoRoot() + QStringLiteral("/sample/kip"));
    }

    void begin(const QString &line, const QString &workingDir)
    {
        runner.setKillTimeoutMs(400);
        QMap<QString, QString> env;
        env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
        env.insert(QStringLiteral("KIP_VERSION"), QStringLiteral("1"));
        session.setDeclaredEnvVars({DeclaredEnvVar{QStringLiteral("KAI_DEMO_TOKEN")}});
        session.start(line, workingDir, env);
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
        QFile f(repoRoot() + QStringLiteral("/sample/kip/kai.yml"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        const ValidationResult result = validateKaiFileText(QString::fromUtf8(bytes));
        QCOMPARE(result.errorCount(), 0);
        QCOMPARE(result.warningCount(), 0);

        int kipCommands = 0;
        int inlineCodeCommands = 0;
        const QJsonArray commands = QJsonDocument::fromJson(kai::core::yamlTextToJsonText(QString::fromUtf8(bytes)).toUtf8()).object().value("commands").toArray();
        for (const QJsonValue &v : commands) {
            const QJsonObject c = v.toObject();
            if (!c.value("kip").toBool()) continue;
            // Python/Node e o shell com o `kip` injetado: o texto do comando É o programa (sem arquivo).
            const QString language = c.value("language").toString();
            const bool inlineShell = !c.value("command").toString().startsWith(QStringLiteral("./"));
            if ((!language.isEmpty() && language != QStringLiteral("native")) || inlineShell) {
                ++inlineCodeCommands;
                continue;
            }
            ++kipCommands;
            const QString script = c.value("command").toString().mid(2); // "./x.sh"
            const QFileInfo info(repoRoot() + QStringLiteral("/sample/kip/") + script);
            QVERIFY2(info.exists() && info.isExecutable(), qPrintable(script));
        }
        QVERIFY(kipCommands >= 11); // os demos em bash puro
        QVERIFY(inlineCodeCommands >= 5); // 1b e 16 (shell), python x2, node
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

    // O sample com o helper injetado também não pode voltar a chamar o binário `kai`.
    void injectedHelperSampleDoesNotCallTheKaiBinary()
    {
        QFile f(repoRoot() + QStringLiteral("/sample/kip/kai.yml"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QString text;
        for (const QJsonValue &v : QJsonDocument::fromJson(kai::core::yamlTextToJsonText(QString::fromUtf8(f.readAll())).toUtf8()).object().value("commands").toArray()) {
            if (v.toObject().value("name").toString().startsWith(QStringLiteral("1b."))) {
                text = v.toObject().value("command").toString();
            }
        }
        QVERIFY(!text.isEmpty());
        QVERIFY(!text.contains(QRegularExpression(QStringLiteral("\\bkai\\.exe\\b|(^|[^\\w-])kai\\s+kip\\s"))));
        QVERIFY(text.contains(QStringLiteral("kip recv")));
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

    // A variante escrita com a função `kip` INJETADA (1b): o texto do próprio comando de kai.yml, nativo com KIP
    // ligado, sem o binário `kai` no PATH.
    void deployWrittenWithTheInjectedHelperWorks()
    {
        QFile file(repoRoot() + QStringLiteral("/sample/kip/kai.yml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString code;
        for (const QJsonValue &v : QJsonDocument::fromJson(kai::core::yamlTextToJsonText(QString::fromUtf8(file.readAll())).toUtf8()).object().value("commands").toArray()) {
            if (v.toObject().value("name").toString().startsWith(QStringLiteral("1b."))) {
                code = v.toObject().value("command").toString();
            }
        }
        QVERIFY(!code.isEmpty());
        std::unique_ptr<Demo> demo(Demo::injected(code));
        Demo &d = *demo;
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

    // O navegador de tabelas (estilo DynamoDB), só com o protocolo de hoje: lista de tabelas -> tabela com
    // páginas do lado do servidor, filtro vigiado, e novo/editar/duplicar/excluir.
    void tableBrowserPagesFiltersAndEditsRows()
    {
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty()) {
            QSKIP("python3 not available");
        }
        ProcessRunner runner;
        KipSession session(&runner);
        runner.setKillTimeoutMs(400);
        QMap<QString, QString> env;
        env.insert(QStringLiteral("PATH"), QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH")));
        env.insert(QStringLiteral("KIP_VERSION"), QStringLiteral("1"));
        env.insert(QStringLiteral("PYTHONPATH"), repoRoot() + QStringLiteral("/src/engine/kip-modules"));
        env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
        session.start(QStringLiteral("%1 -u -c \"import runpy; runpy.run_path('%2/sample/kip/table-browser.py', run_name='__main__')\"")
                          .arg(python, repoRoot()), QString(), env);

        auto awaiting = [&](const QString &id) {
            const auto &o = session.screen().openScreen();
            return o && o->id == id && session.screen().awaitingInput() && !o->locked;
        };
        auto rows = [&]() { return session.screen().openScreen()->fieldNamed(QStringLiteral("rows"))->rows; };
        auto key = [&](int i) { return rows().at(i).toObject().value(QStringLiteral("_key")).toString(); };
        auto chipIds = [&]() {
            QStringList ids;
            for (const KipChip &c : session.screen().openScreen()->chips) ids << c.id;
            return ids;
        };
        auto runChip = [&](const QString &id) {
            QVERIFY(session.startChip(id));
            QTRY_VERIFY_WITH_TIMEOUT(session.screen().openScreen()->chipRun->finished(), kWait);
        };

        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("tables")), kWait);
        session.setFieldValue(QStringLiteral("table"), QStringLiteral("Orders"));
        QVERIFY(session.submit());

        // Primeira página: 6 itens avaliados; sem "Previous", com "Next".
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("browse")), kWait);
        QCOMPARE(rows().size(), 6);
        QCOMPARE(key(0), QStringLiteral("o-001"));
        QCOMPARE(chipIds(), (QStringList{"next", "refresh", "new", "edit", "duplicate", "delete"}));
        QVERIFY(!session.screen().openScreen()->chipRequirementsMet(*session.screen().openScreen()->chipNamed(QStringLiteral("edit"))));

        // Next: a tabela é trocada no lugar (patch) e aparece "Previous".
        runChip(QStringLiteral("next"));
        QCOMPARE(key(0), QStringLiteral("o-007"));
        QCOMPARE(chipIds().first(), QStringLiteral("prev"));
        session.dismissChip();

        // Filtro vigiado: volta à primeira página e só traz o que combina (o filtro vem DEPOIS de ler 6 itens).
        session.setFieldValue(QStringLiteral("filter"), QStringLiteral("shipped"));
        session.sendChange(QStringLiteral("filter"));
        QTRY_COMPARE_WITH_TIMEOUT(rows().size(), 2, kWait);
        QVERIFY(!chipIds().contains(QStringLiteral("prev")));
        QVERIFY(chipIds().contains(QStringLiteral("next"))); // ainda há mais tabela por ler
        session.setFieldValue(QStringLiteral("filter"), QString());
        session.sendChange(QStringLiteral("filter"));
        QTRY_COMPARE_WITH_TIMEOUT(rows().size(), 6, kWait);

        // Duplicar: abre o editor (outra tela) com o item copiado e a chave sugerida; salvar volta à tabela.
        session.setFieldValue(QStringLiteral("rows"), QJsonArray{QStringLiteral("o-002")});
        QVERIFY(session.startChip(QStringLiteral("duplicate")));
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("edit")), kWait);
        QVERIFY(session.screen().openScreen()->back);
        QVERIFY(session.screen().openScreen()->fieldNamed(QStringLiteral("json"))->defaultValue.toString().contains(QStringLiteral("o-002-copy")));
        QVERIFY(session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("browse")), kWait);
        QCOMPARE(key(2), QStringLiteral("o-002-copy"));

        // Editar: muda um atributo e a célula da tabela reflete.
        session.setFieldValue(QStringLiteral("rows"), QJsonArray{QStringLiteral("o-002-copy")});
        QVERIFY(session.startChip(QStringLiteral("edit")));
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("edit")), kWait);
        QJsonObject item = QJsonDocument::fromJson(
            session.screen().openScreen()->fieldNamed(QStringLiteral("json"))->defaultValue.toString().toUtf8()).object();
        item.insert(QStringLiteral("customer"), QStringLiteral("Zed"));
        session.setFieldValue(QStringLiteral("json"), QString::fromUtf8(QJsonDocument(item).toJson(QJsonDocument::Compact)));
        QVERIFY(session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("browse")), kWait);
        QCOMPARE(rows().at(2).toObject().value(QStringLiteral("customer")).toString(), QStringLiteral("Zed"));

        // Novo item com chave repetida: o formulário continua aberto com o erro (invalid); corrigindo, salva.
        QVERIFY(session.startChip(QStringLiteral("new")));
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("edit")), kWait);
        session.setFieldValue(QStringLiteral("json"), QStringLiteral("{\"pk\": \"o-001\"}"));
        QVERIFY(session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(!session.screen().openScreen()->errors.value(QStringLiteral("json")).isEmpty(), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("edit")), kWait);
        session.setFieldValue(QStringLiteral("json"), QStringLiteral("{\"pk\": \"o-100\", \"customer\": \"Neo\"}"));
        QVERIFY(session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("browse")), kWait);

        // Back do editor não salva e volta à tabela.
        session.setFieldValue(QStringLiteral("rows"), QJsonArray{QStringLiteral("o-001")});
        QVERIFY(session.startChip(QStringLiteral("edit")));
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("edit")), kWait);
        session.back();
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("browse")), kWait);
        QCOMPARE(key(0), QStringLiteral("o-001"));

        // Excluir: confirmação inline, depois a tabela é atualizada no lugar.
        session.setFieldValue(QStringLiteral("rows"), QJsonArray{QStringLiteral("o-001")});
        QVERIFY(session.startChip(QStringLiteral("delete")));
        QCOMPARE(session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Confirming);
        QVERIFY(session.confirmChip());
        QTRY_VERIFY_WITH_TIMEOUT(session.screen().openScreen()->chipRun->finished(), kWait);
        QCOMPARE(session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Success);
        QTRY_VERIFY_WITH_TIMEOUT(key(0) != QStringLiteral("o-001"), kWait);

        // Voltar à lista de tabelas e abrir outra, com chave composta (partição + ordenação).
        session.back();
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("tables")), kWait);
        session.setFieldValue(QStringLiteral("table"), QStringLiteral("Events"));
        QVERIFY(session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("browse")), kWait);
        QVERIFY(key(0).contains(QLatin1Char('|')));

        session.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(session.outcome().finished, kWait);
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
