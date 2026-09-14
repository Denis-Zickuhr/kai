#include <QTest>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "engine/kip-session.h"

using namespace kai::core;
using namespace kai::engine;

namespace {

QString fixture(const QString &name)
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFile::exists(dir.filePath(QStringLiteral("sample/kip/fixtures/") + name))) {
            return dir.filePath(QStringLiteral("sample/kip/fixtures/") + name);
        }
        if (!dir.cdUp()) break;
    }
    return QString();
}

// Uma sessão real: ProcessRunner (modo pipe) + KipSession rodando um script de
// sample/kip/fixtures.
struct Harness {
    ProcessRunner runner;
    KipSession session{&runner};
    QTemporaryDir tmp;
    QString outPath;
    QMap<QString, QString> env;

    Harness()
    {
        outPath = tmp.filePath(QStringLiteral("received.jsonl"));
        env.insert(QStringLiteral("KIP_TEST_OUT"), outPath);
        runner.setKillTimeoutMs(400);
    }

    void run(const QString &script)
    {
        const QString path = fixture(script);
        QVERIFY2(!path.isEmpty(), qPrintable(QStringLiteral("fixture ausente: ") + script));
        session.start(QStringLiteral("bash '%1'").arg(path), QString(), env);
    }

    QVector<QJsonObject> received() const
    {
        QVector<QJsonObject> out;
        QFile f(outPath);
        if (f.open(QIODevice::ReadOnly)) {
            for (const QByteArray &line : f.readAll().split('\n')) {
                if (!line.trimmed().isEmpty()) out.append(QJsonDocument::fromJson(line).object());
            }
        }
        return out;
    }

    bool awaiting(const QString &id) const
    {
        const auto &o = session.screen().openScreen();
        return o && o->id == id && session.screen().awaitingInput();
    }
};

constexpr int kWait = 8000;

} // namespace

class TestKipSession : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<kai::engine::KipOutcome>();
        qRegisterMetaType<kai::core::KipSessionState>();
        qRegisterMetaType<kai::core::KipLevel>();
    }

    void happyPathPromptConfirmProgressDone()
    {
        Harness h;
        QSignalSpy notify(&h.session, &KipSession::notifyRequested);
        QSignalSpy remembered(&h.session, &KipSession::answersRemembered);
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("happy.sh"));

        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("env")), kWait);
        QCOMPARE(h.session.state(), KipSessionState::AwaitingInput);
        QCOMPARE(h.session.screen().title(), QStringLiteral("Deploy"));
        QCOMPARE(h.session.screen().programVersion(), QStringLiteral("1.2.3"));
        QCOMPARE(h.session.screen().currentValues().value("env").toString(), QStringLiteral("dev"));

        h.session.setFieldValue(QStringLiteral("env"), QStringLiteral("prod"));
        h.session.setFieldValue(QStringLiteral("token"), QStringLiteral("s3cret"));
        QVERIFY(h.session.submit());
        QVERIFY(!h.session.submit()); // já travado

        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("sure")), kWait);
        QCOMPARE(h.session.screen().answered().size(), 1);
        QVERIFY(h.session.confirm(true));

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Finished);
        QVERIFY(h.session.outcome().success);
        QVERIFY(h.session.screen().done().has_value());
        QCOMPARE(h.session.screen().done()->actions.size(), 1);

        // O que o programa recebeu: o valor secreto REAL, e a confirmação.
        const QVector<QJsonObject> got = h.received();
        QCOMPARE(got.size(), 2);
        QCOMPARE(got.at(0).value("type").toString(), QStringLiteral("response"));
        QCOMPARE(got.at(0).value("id").toString(), QStringLiteral("env"));
        QCOMPARE(got.at(0).value("values").toObject().value("env").toString(), QStringLiteral("prod"));
        QCOMPARE(got.at(0).value("values").toObject().value("token").toString(), QStringLiteral("s3cret"));
        QCOMPARE(got.at(1).value("values").toObject().value("confirmed").toBool(), true);

        // O inspetor mostra as duas direções, com o segredo mascarado.
        bool sawOutgoing = false;
        for (const KipTraceEntry &e : h.session.trace()) {
            QVERIFY2(!e.text.contains(QStringLiteral("s3cret")), "segredo vazou no inspetor");
            if (e.direction == KipTraceEntry::Direction::ToProgram && e.text.contains(QStringLiteral("\"id\":\"env\""))) {
                sawOutgoing = true;
                QVERIFY(e.text.contains(QString::fromUtf16(kKipSecretMask)));
            }
        }
        QVERIFY(sawOutgoing);

        QVERIFY(h.session.logText().contains(QStringLiteral("log line on stderr")));
        QCOMPARE(notify.count(), 1);
        QCOMPARE(notify.first().at(0).toString(), QStringLiteral("Almost there"));

        // Só a resposta aceita é lembrada, e nunca o secret.
        QVERIFY(remembered.count() >= 1);
        QJsonObject all;
        for (const auto &args : remembered) {
            const QJsonObject o = args.at(0).toJsonObject();
            for (auto it = o.begin(); it != o.end(); ++it) all[it.key()] = it.value();
        }
        QCOMPARE(all.value("env/env").toString(), QStringLiteral("prod"));
        QVERIFY(!all.contains("env/token"));
    }

    void commandThatDoesNotSupportKipIsUnsupported()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("unsupported.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Unsupported);
        QCOMPARE(h.session.screen().terminalInfo().reason, KipTerminalReason::ExitedBeforeHello);
        QVERIFY(!h.session.outcome().success);
        QVERIFY(h.session.outcome().exitCode != 0);
        QVERIFY(!h.session.outcome().errorMessage.isEmpty());
        QVERIFY(h.session.logText().contains(QStringLiteral("unknown option --kip")));
    }

    void silentProgramTimesOutTheHandshakeAndIsStopped()
    {
        Harness h;
        h.session.setHandshakeTimeoutMs(250);
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("silent.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(h.session.state(), KipSessionState::Unsupported, kWait);
        QCOMPARE(h.session.screen().terminalInfo().reason, KipTerminalReason::HandshakeTimeout);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait); // o processo foi encerrado
        QVERIFY(!h.session.processAlive());
        QVERIFY(!h.session.outcome().success);
    }

    void wrapperNoiseBeforeHelloGoesToTheLog()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("noise.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("p")), kWait);
        QVERIFY(h.session.logText().contains(QStringLiteral("> pkg@1.0.0 deploy")));
        QCOMPARE(h.session.screen().title(), QStringLiteral("Wrapped"));
        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Finished);
    }

    void malformedAndUnknownLinesNeverEndTheSession()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("malformed.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("p")), kWait);
        QVERIFY(h.session.logText().contains(QStringLiteral("plain text in the middle")));
        QVERIFY(h.session.logText().contains(QStringLiteral("broken"))); // linha JSON truncada = ruído
        // O bloco válido depois do lixo chegou.
        QCOMPARE(h.session.screen().blocks().size(), 1);
        QVERIFY(std::holds_alternative<KipMessageBlock>(h.session.screen().blocks().first()));

        int ignored = 0;
        for (const KipTraceEntry &e : h.session.trace()) {
            if (!e.note.isEmpty()) ++ignored;
        }
        QCOMPARE(ignored, 2); // tipo desconhecido + prompt sem id

        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Finished);
    }

    void exitWhileAwaitingInputIsAFailureEvenWithExitCodeZero()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("exit-awaiting.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Failed);
        QCOMPARE(h.session.screen().terminalInfo().reason, KipTerminalReason::ExitedWhileAwaitingInput);
        QVERIFY(!h.session.outcome().success);
        QVERIFY(h.session.outcome().exitCode != 0);
    }

    void cancelIsDeliveredAndPoliteProgramIsCancelled()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("cancel-polite.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("p")), kWait);
        h.session.cancel();
        QVERIFY(h.session.cancelRequested());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Cancelled);
        QVERIFY(h.session.outcome().stoppedByRequest);
        QVERIFY(!h.session.outcome().success);
        const QVector<QJsonObject> got = h.received();
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.first().value("type").toString(), QStringLiteral("cancel"));
    }

    void cancelThatIsIgnoredIsStoppedAfterTheGracePeriod()
    {
        Harness h;
        h.session.setCancelGraceMs(200);
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("cancel-deaf.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("p")), kWait);
        QElapsedTimer t;
        t.start();
        h.session.cancel();
        // Dentro da graça o processo continua vivo.
        QTest::qWait(80);
        QVERIFY(h.session.processAlive());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QVERIFY(t.elapsed() >= 200);
        QCOMPARE(h.session.state(), KipSessionState::Cancelled);
        QVERIFY(!h.session.processAlive());
    }

    void stoppingTheRunnerFromThePanelCountsAsCancelled()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("cancel-deaf.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("p")), kWait);
        h.runner.forceStop();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Cancelled);
        QVERIFY(h.session.outcome().stoppedByRequest);
    }

    void invalidKeepsThePromptOpenAndOnlyTheAcceptedAnswerIsRemembered()
    {
        Harness h;
        QSignalSpy remembered(&h.session, &KipSession::answersRemembered);
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("invalid.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("name")), kWait);
        h.session.setFieldValue(QStringLiteral("name"), QStringLiteral("taken"));
        QVERIFY(h.session.submit());

        // invalid: o prompt volta editável, com os erros.
        QTRY_VERIFY_WITH_TIMEOUT(h.session.screen().openScreen() && !h.session.screen().openScreen()->errors.isEmpty(), kWait);
        QVERIFY(h.session.screen().awaitingInput());
        QCOMPARE(h.session.state(), KipSessionState::AwaitingInput);
        QCOMPARE(h.session.screen().openScreen()->errors.value("name"), QStringLiteral("already taken"));
        QCOMPARE(h.session.screen().openScreen()->message, QStringLiteral("Fix the name"));
        QCOMPARE(h.session.screen().currentValues().value("name").toString(), QStringLiteral("taken"));
        QVERIFY(h.session.screen().answered().isEmpty());
        QCOMPARE(remembered.count(), 0);

        h.session.setFieldValue(QStringLiteral("name"), QStringLiteral("fresh"));
        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Finished);
        QCOMPARE(remembered.count(), 1);
        QCOMPARE(remembered.first().at(0).toJsonObject().value("name/name").toString(), QStringLiteral("fresh"));
        QCOMPARE(h.received().size(), 2);
    }

    void watchFieldSendsChangeAndPatchReplacesTheDependentField()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("cascade.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("k8s")), kWait);

        h.session.setFieldValue(QStringLiteral("context"), QStringLiteral("a"));
        h.session.sendChange(QStringLiteral("context"));
        QVERIFY(h.session.screen().openScreen()->changePending);

        QTRY_VERIFY_WITH_TIMEOUT(!h.session.screen().openScreen()->changePending, kWait);
        const KipField *pod = h.session.screen().openScreen()->fieldNamed(QStringLiteral("pod"));
        QVERIFY(pod);
        QCOMPARE(pod->options.size(), 2);
        QCOMPARE(pod->options.first().value, QStringLiteral("p1"));

        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);

        const QVector<QJsonObject> got = h.received();
        QCOMPARE(got.at(0).value("type").toString(), QStringLiteral("change"));
        QCOMPARE(got.at(0).value("seq").toInt(), 1);
        QCOMPARE(got.at(0).value("field").toString(), QStringLiteral("context"));
        QCOMPARE(got.at(0).value("values").toObject().value("context").toString(), QStringLiteral("a"));
        QCOMPARE(got.at(1).value("type").toString(), QStringLiteral("response"));
    }

    // ---- chips (§21) ----
    void chipRoundTripLeavesThePromptOpen()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("chips.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("pick")), kWait);

        // `requires`: sem a branch escolhida o chip não roda
        QVERIFY(!h.session.startChip(QStringLiteral("ctx")));
        h.session.setFieldValue(QStringLiteral("branch"), QStringLiteral("feat"));
        h.session.setFieldValue(QStringLiteral("token"), QStringLiteral("hunter2"));
        QVERIFY(h.session.startChip(QStringLiteral("ctx")));
        QCOMPARE(h.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Running);

        QTRY_COMPARE_WITH_TIMEOUT(h.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Success, kWait);
        QCOMPARE(h.session.screen().openScreen()->chipRun->title, QStringLiteral("Done"));
        QCOMPARE(h.session.screen().openScreen()->chipRun->text, QStringLiteral("all good"));
        // o prompt continua aberto e destravado
        QVERIFY(h.awaiting(QStringLiteral("pick")));
        QCOMPARE(h.session.state(), KipSessionState::AwaitingInput);

        const QVector<QJsonObject> got = h.received();
        QCOMPARE(got.at(0).value("type").toString(), QStringLiteral("chip"));
        QCOMPARE(got.at(0).value("id").toString(), QStringLiteral("pick"));
        QCOMPARE(got.at(0).value("chip").toString(), QStringLiteral("ctx"));
        QCOMPARE(got.at(0).value("values").toObject().value("branch").toString(), QStringLiteral("feat"));
        // o segredo vai ao programa (como no response), mas nunca ao inspetor
        QCOMPARE(got.at(0).value("values").toObject().value("token").toString(), QStringLiteral("hunter2"));
        for (const KipTraceEntry &e : h.session.trace()) {
            QVERIFY2(!e.text.contains(QStringLiteral("hunter2")), qPrintable(e.text));
        }

        h.session.dismissChip();
        QVERIFY(!h.session.screen().openScreen()->chipRun);
        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QVERIFY(h.session.outcome().success);
    }

    void chipWithConfirmationSendsNothingUntilConfirmed()
    {
        Harness h;
        h.env.insert(QStringLiteral("CHIP_FAIL"), QStringLiteral("1"));
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("chips.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("pick")), kWait);

        QVERIFY(h.session.startChip(QStringLiteral("nuke")));
        QCOMPARE(h.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Confirming);
        QTest::qWait(150);
        QVERIFY(h.received().isEmpty()); // nada foi ao programa

        // recusar: fecha a caixa, continua sem enviar
        h.session.dismissChip();
        QVERIFY(h.session.startChip(QStringLiteral("nuke")));
        QVERIFY(h.session.confirmChip());
        QTRY_COMPARE_WITH_TIMEOUT(h.session.screen().openScreen()->chipRun->phase, KipChipRun::Phase::Error, kWait);
        QCOMPARE(h.session.screen().openScreen()->chipRun->text, QStringLiteral("boom"));
        QCOMPARE(h.received().size(), 1);
        QCOMPARE(h.received().first().value("chip").toString(), QStringLiteral("nuke"));
        QVERIFY(h.awaiting(QStringLiteral("pick"))); // um chip com erro não derruba a sessão

        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
    }

    void chipsAreRejectedWhileOneIsRunning()
    {
        Harness h;
        h.run(QStringLiteral("chips.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("pick")), kWait);
        h.session.setFieldValue(QStringLiteral("branch"), QStringLiteral("main"));
        QVERIFY(h.session.startChip(QStringLiteral("ctx")));
        QVERIFY(!h.session.startChip(QStringLiteral("nuke")));
        QVERIFY(!h.session.startChip(QStringLiteral("ctx")));
        QTRY_VERIFY_WITH_TIMEOUT(h.session.screen().openScreen()->chipRun->finished(), kWait);
        h.session.cancel();
    }

    void patchWithAStaleSeqIsIgnored()
    {
        Harness h;
        h.env.insert(QStringLiteral("STALE"), QStringLiteral("1"));
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("cascade.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("k8s")), kWait);
        // O script responde ao change (seq 1) primeiro com um patch de seq 0
        // — velho, tem de ser ignorado — e só depois com o patch certo.
        h.session.sendChange(QStringLiteral("context"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.session.screen().openScreen()->changePending, kWait);
        const KipField *pod = h.session.screen().openScreen()->fieldNamed(QStringLiteral("pod"));
        QVERIFY(pod);
        QCOMPARE(pod->options.size(), 2);
        QCOMPARE(pod->options.first().value, QStringLiteral("p1"));
        bool staleNoted = false;
        for (const KipTraceEntry &e : h.session.trace()) {
            if (e.text.contains(QStringLiteral("\"stale\"")) && !e.note.isEmpty()) staleNoted = true;
        }
        QVERIFY(staleNoted);
        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
    }

    // Um chip que repinta a tabela depois de o usuário ter mexido num campo vigiado manda um patch SEM seq:
    // ele vale mesmo depois de vários changes e não libera um change ainda pendente. (Um seq explícito e
    // velho continua sendo ignorado — patchWithAStaleSeqIsIgnored.)
    void aSpontaneousPatchAppliesAfterChangesAndKeepsThePendingChange()
    {
        ProcessRunner runner;
        KipSession session(&runner);
        session.handleOutput(QStringLiteral("{\"kip\":1,\"type\":\"hello\"}\n"
            "{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"f\",\"type\":\"text\",\"watch\":true},"
            "{\"name\":\"l\",\"type\":\"list\",\"options\":[\"a\"]}]}\n"), false);
        QVERIFY(session.screen().openScreen());
        session.sendChange(QStringLiteral("f")); // seq 1
        session.sendChange(QStringLiteral("f")); // seq 2
        QVERIFY(session.screen().openScreen()->changePending);
        auto options = [&]() { return session.screen().openScreen()->fieldNamed(QStringLiteral("l"))->options.size(); };

        session.handleOutput(QStringLiteral("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":0,"
            "\"fields\":[{\"name\":\"l\",\"type\":\"list\",\"options\":[\"a\",\"b\",\"c\"]}]}\n"), false);
        QCOMPARE(options(), 1); // explicit old seq: ignored

        session.handleOutput(QStringLiteral("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\","
            "\"fields\":[{\"name\":\"l\",\"type\":\"list\",\"options\":[\"a\",\"b\"]}]}\n"), false);
        QCOMPARE(options(), 2); // spontaneous: applied...
        QVERIFY(session.screen().openScreen()->changePending); // ...and the wait for the change answer goes on

        session.handleOutput(QStringLiteral("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":2,\"fields\":[]}\n"), false);
        QVERIFY(!session.screen().openScreen()->changePending);
    }

    void aStalePatchDoesNotClearThePendingState()
    {
        // Duas mudanças seguidas: o patch da primeira (seq 1) chega depois da
        // segunda ter sido enviada (seq 2) e NÃO pode liberar o formulário.
        Harness h;
        h.run(QStringLiteral("patch-timeout.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("p")), kWait);
        h.session.sendChange(QStringLiteral("a"));
        h.session.sendChange(QStringLiteral("a"));
        QVERIFY(h.session.screen().openScreen()->changePending);
        KipPatch stale;
        stale.id = QStringLiteral("p");
        stale.seq = 1;
        // Injeta o patch velho como se viesse do programa (via saída crua).
        QMetaObject::invokeMethod(&h.runner, "outputReady", Qt::DirectConnection,
                                  Q_ARG(QString, QString::fromUtf8(kipSerializeMessage(stale))), Q_ARG(bool, false));
        QVERIFY(h.session.screen().openScreen()->changePending);
        bool noted = false;
        for (const KipTraceEntry &e : h.session.trace()) {
            if (e.direction == KipTraceEntry::Direction::FromProgram && !e.note.isEmpty()) noted = true;
        }
        QVERIFY(noted);
        h.runner.forceStop();
    }

    void patchTimeoutReenablesTheForm()
    {
        Harness h;
        h.session.setPatchTimeoutMs(200);
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("patch-timeout.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("p")), kWait);
        h.session.sendChange(QStringLiteral("a"));
        QVERIFY(h.session.screen().openScreen()->changePending);
        QTRY_VERIFY_WITH_TIMEOUT(!h.session.screen().openScreen()->changePending, kWait);
        QVERIFY(h.session.logText().contains(QStringLiteral("[kai]")));
        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Finished);
    }

    void setEnvOnlyForDeclaredVariables()
    {
        Harness h;
        DeclaredEnvVar token;
        token.name = QStringLiteral("TOKEN");
        h.session.setDeclaredEnvVars({token});
        QSignalSpy setEnv(&h.session, &KipSession::setEnvRequested);
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("set-env.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(setEnv.count(), 1);
        QCOMPARE(setEnv.first().at(0).toString(), QStringLiteral("TOKEN"));
        QCOMPARE(setEnv.first().at(1).toString(), QStringLiteral("abc123"));
        bool rejectedInInspector = false;
        for (const KipTraceEntry &e : h.session.trace()) {
            if (e.text.contains(QStringLiteral("UNDECLARED")) && !e.note.isEmpty()) rejectedInInspector = true;
        }
        QVERIFY(rejectedInInspector);
        QVERIFY(h.session.logText().contains(QStringLiteral("UNDECLARED")));
    }

    void noDeclaredVariablesMeansSetEnvIsAlwaysRejected()
    {
        Harness h;
        QSignalSpy setEnv(&h.session, &KipSession::setEnvRequested);
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("set-env.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(setEnv.count(), 0);
    }

    void aLineSplitAcrossChunksIsReassembled()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("split-chunks.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.screen().title(), QStringLiteral("Split"));
        QCOMPARE(h.session.state(), KipSessionState::Finished);
        QCOMPARE(h.session.screen().blocks().size(), 1);
        QCOMPARE(std::get<KipMessageBlock>(h.session.screen().blocks().first()).text, QStringLiteral("one"));
    }

    void helloFromANewerProtocolIsAProtocolError()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("newer-kip.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::ProtocolError);
        QCOMPARE(h.session.screen().terminalInfo().reason, KipTerminalReason::NeedsNewerKai);
        QCOMPARE(h.session.screen().terminalInfo().detail, QStringLiteral("2"));
        QVERIFY(!h.session.outcome().success);
    }

    void firstMessageThatIsNotHelloIsAProtocolError()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("not-hello.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::ProtocolError);
        QCOMPARE(h.session.screen().terminalInfo().reason, KipTerminalReason::FirstMessageNotHello);
    }

    void doneWithANonZeroExitIsStillAFailure()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("done-then-fail.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Failed);
        QCOMPARE(h.session.screen().terminalInfo().reason, KipTerminalReason::NonZeroExit);
        QCOMPARE(h.session.outcome().exitCode, 3);
    }

    void ignoreExitCodeIsRespected()
    {
        Harness h;
        h.session.setIgnoreExitCode(true);
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("done-then-fail.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Finished);
        QVERIFY(h.session.outcome().success);
    }

    void cleanExitWithoutDoneIsAGenericFinish()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("no-done.sh"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Finished);
        QVERIFY(!h.session.screen().done().has_value());
    }

    void backIsDeliveredAndRemovesTheLastAnsweredStep()
    {
        Harness h;
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("back.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("one")), kWait);
        h.session.setFieldValue(QStringLiteral("a"), QStringLiteral("x"));
        QVERIFY(h.session.submit());
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("two")), kWait);
        QCOMPARE(h.session.screen().answered().size(), 1);
        h.session.back();
        QVERIFY(h.session.screen().answered().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("one")), kWait);
        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        const QVector<QJsonObject> got = h.received();
        QCOMPARE(got.at(1).value("type").toString(), QStringLiteral("back"));
        QCOMPARE(got.at(1).value("id").toString(), QStringLiteral("two"));
    }

    void backIsIgnoredWhenThePromptDoesNotAllowIt()
    {
        Harness h;
        h.run(QStringLiteral("cancel-polite.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("p")), kWait);
        h.session.back();
        QVERIFY(h.session.screen().awaitingInput());
        h.session.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!h.session.processAlive(), kWait);
        const QVector<QJsonObject> got = h.received();
        QCOMPARE(got.size(), 1); // só o cancel
    }

    void everythingGoesThroughThePipeMode()
    {
        // Mesmo com o runner configurado para PTY, a sessão força o modo pipe:
        // se houvesse PTY o eco do terminal duplicaria a entrada no stdout.
        Harness h;
        h.runner.setUsePty(true);
        QSignalSpy finished(&h.session, &KipSession::finished);
        h.run(QStringLiteral("noise.sh"));
        QTRY_VERIFY_WITH_TIMEOUT(h.awaiting(QStringLiteral("p")), kWait);
        QVERIFY(h.session.submit());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
        QCOMPARE(h.session.state(), KipSessionState::Finished);
        QVERIFY(!h.session.logText().contains(QStringLiteral("\"type\":\"response\"")));
    }
};

QTEST_MAIN(TestKipSession)
#include "test_kip_session.moc"
