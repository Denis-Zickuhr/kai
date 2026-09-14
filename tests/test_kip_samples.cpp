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
#include "core/text-diff.h"
#include "core/unified-diff.h"
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

QString commandText(const QString &prefix)
{
    QFile file(repoRoot() + QStringLiteral("/sample/kip/kai.yml"));
    if (!file.open(QIODevice::ReadOnly)) return QString();
    for (const QJsonValue &v : QJsonDocument::fromJson(kai::core::yamlTextToJsonText(QString::fromUtf8(file.readAll())).toUtf8()).object().value("commands").toArray()) {
        if (v.toObject().value("name").toString().startsWith(prefix)) return v.toObject().value("command").toString();
    }
    return QString();
}

// Um comando Python de kai.yml (o texto é o programa), com o módulo `kip` no PYTHONPATH e a pasta do projeto.
struct PythonDemo {
    ProcessRunner runner;
    KipSession session{&runner};
    QTemporaryDir tmp;
    bool start(const QString &code)
    {
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty() || code.isEmpty()) return false;
        QFile script(tmp.filePath(QStringLiteral("demo.py")));
        script.open(QIODevice::WriteOnly);
        script.write(code.toUtf8());
        script.close();
        runner.setKillTimeoutMs(400);
        QMap<QString, QString> env;
        env.insert(QStringLiteral("PATH"), QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH")));
        env.insert(QStringLiteral("KIP_VERSION"), QStringLiteral("1"));
        env.insert(QStringLiteral("PYTHONPATH"), repoRoot() + QStringLiteral("/src/engine/kip-modules"));
        env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
        session.start(QStringLiteral("%1 -u '%2'").arg(python, script.fileName()), repoRoot() + QStringLiteral("/sample/kip"), env);
        return true;
    }
    bool awaiting(const QString &id) const
    {
        const auto &o = session.screen().openScreen();
        return o && o->id == id && session.screen().awaitingInput() && !o->locked;
    }
    template <typename T>
    const T *block() const
    {
        for (const KipBlock &b : session.screen().blocks()) {
            if (const auto *t = std::get_if<T>(&b)) return t;
        }
        return nullptr;
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
            const QString script = c.value("command").toString().mid(2);
            const QFileInfo info(repoRoot() + QStringLiteral("/sample/kip/") + script);
            QVERIFY2(info.exists() && info.isExecutable(), qPrintable(script));
        }
        QVERIFY(kipCommands >= 11);
        QVERIFY(inlineCodeCommands >= 5);
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
        QVERIFY(d.session.submit());
        d.waitFor(QStringLiteral("sure"));
        QVERIFY(d.session.confirm(true));
        d.waitFinished();
        QVERIFY(d.session.outcome().success);
        QVERIFY(d.hasBlock(3));
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
        QVERIFY(d.hasBlock(4));
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

    // Demo 19: a rodada 2 inteira — ações por linha (com a confirmação na linha), diff, campo diff, log de passo,
    // várias barras e o resultado com stats / kv / code.
    void gitReviewUsesTheRoundTwoComponents()
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
        session.start(QStringLiteral("%1 -u -c \"import runpy; runpy.run_path('%2/sample/kip/git-review.py', run_name='__main__')\"")
                          .arg(python, repoRoot()), QString(), env);
        const auto awaiting = [&](const QString &id) {
            const auto &o = session.screen().openScreen();
            return o && o->id == id && session.screen().awaitingInput() && !o->locked;
        };
        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("branches")), kWait);
        const auto rows = [&]() { return session.screen().openScreen()->fieldNamed(QStringLiteral("branch"))->rows.size(); };
        QCOMPARE(rows(), 5);
        // `main` só sincroniza
        QVERIFY(!session.startRowAction(QStringLiteral("branch"), {}, QStringLiteral("main"), QStringLiteral("delete")));
        QVERIFY(session.startRowAction(QStringLiteral("branch"), {}, QStringLiteral("fix/cart-total"), QStringLiteral("delete")));
        QCOMPARE(session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Confirming);
        QVERIFY(session.confirmChip());
        QTRY_COMPARE_WITH_TIMEOUT(session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Success, kWait);
        QCOMPARE(rows(), 4); // o programa repintou a tabela
        session.dismissChip();
        QVERIFY(session.submit());

        QTRY_VERIFY_WITH_TIMEOUT(awaiting(QStringLiteral("commit")), kWait);
        bool diffBlock = false;
        for (const KipBlock &b : session.screen().blocks()) diffBlock = diffBlock || std::holds_alternative<KipDiff>(b);
        QVERIFY(diffBlock);
        QCOMPARE(session.screen().openScreen()->values.value("hunks").toArray().size(), 4);
        session.setFieldValue(QStringLiteral("hunks"), QJsonArray{"src/auth/session.ts@0", "config/deploy.yml@0"});
        QVERIFY(session.submit());

        QTRY_VERIFY_WITH_TIMEOUT(session.outcome().finished, kWait);
        QVERIFY(session.outcome().success);
        QCOMPARE(session.screen().done()->title, QStringLiteral("Committed to feat/login"));
        int stats = 0, kv = 0, code = 0;
        for (const KipBlock &b : session.screen().blocks()) {
            stats += std::holds_alternative<KipStats>(b);
            kv += std::holds_alternative<KipKv>(b);
            code += std::holds_alternative<KipCode>(b);
            if (const auto *steps = std::get_if<KipSteps>(&b)) {
                QCOMPARE(steps->items.at(1).log.size(), 6);
                QCOMPARE(steps->items.at(1).state, KipStepState::Success);
            }
        }
        QCOMPARE(stats + kv + code, 3);
        QCOMPARE(std::get<KipStats>(session.screen().blocks().at(1)).items.first().value, QStringLiteral("2"));
    }

    // Demo 20: o `kip` injetado lê os dois arquivos onde o script roda e manda os textos.
    void compareTwoFilesWithTheInjectedHelper()
    {
        QFile file(repoRoot() + QStringLiteral("/sample/kip/kai.yml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString code;
        for (const QJsonValue &v : QJsonDocument::fromJson(kai::core::yamlTextToJsonText(QString::fromUtf8(file.readAll())).toUtf8()).object().value("commands").toArray()) {
            if (v.toObject().value("name").toString().startsWith(QStringLiteral("20."))) {
                code = v.toObject().value("command").toString();
            }
        }
        QVERIFY(!code.isEmpty());
        std::unique_ptr<Demo> demo(Demo::injected(code));
        demo->waitFinished();
        QVERIFY(demo->session.outcome().success);
        const KipDiff *diff = nullptr;
        for (const KipBlock &b : demo->session.screen().blocks()) {
            if (const auto *d = std::get_if<KipDiff>(&b)) diff = d;
        }
        QVERIFY(diff);
        QVERIFY(diff->left && diff->right);
        QCOMPARE(diff->left->name, QStringLiteral("staging.json"));
        QVERIFY(diff->right->text.contains(QStringLiteral("\"replicas\": 6")));
    }

    // Demos 21–27: um por componente da rodada 2.
    void commitViewerShowsTheDiffOfThePickedCommit()
    {
        PythonDemo d;
        if (!d.start(commandText(QStringLiteral("21.")))) QSKIP("python3 not available");
        QTRY_VERIFY_WITH_TIMEOUT(d.awaiting(QStringLiteral("pick")), kWait);
        d.session.setFieldValue(QStringLiteral("commit"), QStringLiteral("585439b"));
        QVERIFY(d.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(d.awaiting(QStringLiteral("more")), kWait);
        const KipDiff *diff = d.block<KipDiff>();
        QVERIFY(diff);
        const QVector<DiffFile> files = parseUnifiedDiff(diff->patch);
        QCOMPARE(files.size(), 3);
        QCOMPARE(files.at(0).status, DiffFileStatus::Renamed);
        QVERIFY(files.at(1).binary);
        QCOMPARE(files.at(2).status, DiffFileStatus::Added);
        QVERIFY(d.session.confirm(false));
        QTRY_VERIFY_WITH_TIMEOUT(d.session.outcome().finished, kWait);
        QVERIFY(d.session.outcome().success);
    }

    void compareTwoTextsSendsBothSides()
    {
        PythonDemo d;
        if (!d.start(commandText(QStringLiteral("22.")))) QSKIP("python3 not available");
        QTRY_VERIFY_WITH_TIMEOUT(d.awaiting(QStringLiteral("texts")), kWait);
        d.session.setFieldValue(QStringLiteral("opts"), QJsonObject{{"ws", true}, {"json", true}});
        QVERIFY(d.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(d.session.outcome().finished, kWait);
        const KipDiff *diff = d.block<KipDiff>();
        QVERIFY(diff && diff->left && diff->right);
        QVERIFY(diff->ignoreWhitespace);
        // sem espaços, a linha reindentada é igual: sobram replicas, memory, log_level e new_checkout
        const DiffFile f = diffTexts(diff->left->text, diff->right->text, 3, true);
        QCOMPARE(f.deletions, 4);
    }

    void deployHistoryRollsBackFromTheTableBlock()
    {
        PythonDemo d;
        if (!d.start(commandText(QStringLiteral("23.")))) QSKIP("python3 not available");
        QTRY_VERIFY_WITH_TIMEOUT(d.awaiting(QStringLiteral("history")), kWait);
        QVERIFY(!d.session.startRowAction({}, QStringLiteral("deploys"), QStringLiteral("d-1039"), QStringLiteral("rollback")));
        QVERIFY(d.session.startRowAction({}, QStringLiteral("deploys"), QStringLiteral("d-1042"), QStringLiteral("rollback")));
        QVERIFY(d.session.confirmChip());
        QTRY_COMPARE_WITH_TIMEOUT(d.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Success, kWait);
        const KipTable *table = d.block<KipTable>();
        QVERIFY(table);
        QCOMPARE(table->rows.first().toObject().value("status").toObject().value("text").toString(), QStringLiteral("rolled back"));
        d.session.dismissChip();
        QVERIFY(d.session.startRowAction({}, QStringLiteral("deploys"), QStringLiteral("d-1041"), QStringLiteral("logs")));
        QTRY_COMPARE_WITH_TIMEOUT(d.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Success, kWait);
        QVERIFY(d.session.screen().openScreen()->chipRun->text.contains(QStringLiteral("health check ok")));
        d.session.dismissChip();
        QVERIFY(d.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(d.session.outcome().finished, kWait);
        QVERIFY(d.session.outcome().success);
    }

    void releaseSummaryIsMadeOfResultBlocks()
    {
        const QString code = commandText(QStringLiteral("24."));
        QVERIFY(!code.isEmpty());
        std::unique_ptr<Demo> demo(Demo::injected(code));
        demo->waitFinished();
        QVERIFY(demo->session.outcome().success);
        int stats = 0, kv = 0, codeBlocks = 0;
        for (const KipBlock &b : demo->session.screen().blocks()) {
            if (const auto *s = std::get_if<KipStats>(&b)) { ++stats; QCOMPARE(s->items.size(), 3); }
            if (const auto *k = std::get_if<KipKv>(&b)) { ++kv; QCOMPARE(k->items.at(3).format, QStringLiteral("link")); }
            if (const auto *c = std::get_if<KipCode>(&b)) { ++codeBlocks; QCOMPARE(c->highlight, (QVector<int>{3, 4})); }
        }
        QCOMPARE(stats + kv + codeBlocks, 3);
    }

    void choicesAnswerWithPlainValues()
    {
        PythonDemo d;
        if (!d.start(commandText(QStringLiteral("25.")))) QSKIP("python3 not available");
        QTRY_VERIFY_WITH_TIMEOUT(d.awaiting(QStringLiteral("land")), kWait);
        QCOMPARE(d.session.screen().openScreen()->values.value("tests"), QJsonValue(QJsonArray{"src/auth"}));
        d.session.setFieldValue(QStringLiteral("strategy"), QStringLiteral("squash"));
        d.session.setFieldValue(QStringLiteral("tests"), QJsonArray{"src/auth", "docs"});
        QVERIFY(d.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(d.session.outcome().finished, kWait);
        const KipKv *kv = d.block<KipKv>();
        QVERIFY(kv);
        QCOMPARE(kv->items.at(0).value.toString(), QStringLiteral("squash"));
        QCOMPARE(kv->items.at(2).value.toString(), QStringLiteral("src/auth, docs"));
    }

    void partialCommitKeepsOnlyThePickedHunks()
    {
        PythonDemo d;
        if (!d.start(commandText(QStringLiteral("26.")))) QSKIP("python3 not available");
        QTRY_VERIFY_WITH_TIMEOUT(d.awaiting(QStringLiteral("commit")), kWait);
        QCOMPARE(d.session.screen().openScreen()->values.value("hunks").toArray().size(), 4);
        d.session.setFieldValue(QStringLiteral("hunks"), QJsonArray{"src/auth/session.ts@1", "config/deploy.yml@0"});
        QVERIFY(d.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(d.session.outcome().finished, kWait);
        const KipCode *code = d.block<KipCode>();
        QVERIFY(code);
        const QVector<DiffFile> staged = parseUnifiedDiff(code->text);
        QCOMPARE(staged.size(), 2); // debug.log ficou de fora
        QCOMPARE(staged.at(0).hunks.size(), 1);
        QVERIFY(staged.at(0).hunks.first().lines.at(1).text.contains(QStringLiteral("clear();")));
        QCOMPARE(d.block<KipStats>()->items.first().value, QStringLiteral("2 / 4"));
    }

    void buildShowsStepOutputAndSeveralBars()
    {
        const QString code = commandText(QStringLiteral("27."));
        QVERIFY(!code.isEmpty());
        std::unique_ptr<Demo> demo(Demo::injected(code));
        demo->waitFinished();
        QVERIFY(demo->session.outcome().success);
        const KipSteps *steps = nullptr;
        for (const KipBlock &b : demo->session.screen().blocks()) {
            if (const auto *s = std::get_if<KipSteps>(&b)) steps = s;
        }
        QVERIFY(steps);
        QCOMPARE(steps->items.at(1).log.size(), 12);
        QCOMPARE(steps->items.at(2).state, KipStepState::Error);
        QCOMPARE(steps->items.at(2).log.size(), 2);
        QCOMPARE(steps->items.at(3).state, KipStepState::Success);
        QCOMPARE(demo->done().level, KipLevel::Warning);
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
