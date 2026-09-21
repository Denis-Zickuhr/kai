#include <QTest>

#include "core/kip-screen-state.h"

using namespace kai::core;

namespace {

KipMessage msg(const char *json)
{
    const KipParseResult r = parseKipLine(QString::fromUtf8(json));
    Q_ASSERT(r.kind == KipParseResult::Kind::Message);
    return *r.message;
}

const char *kEnvPrompt =
    "{\"kip\":1,\"type\":\"prompt\",\"id\":\"env\",\"title\":\"Where to?\",\"back\":true,\"fields\":["
    "{\"name\":\"env\",\"type\":\"select\",\"label\":\"Environment\",\"required\":true,"
    "\"options\":[\"dev\",{\"value\":\"prod\",\"label\":\"Production\"}],\"default\":\"dev\"},"
    "{\"name\":\"pass\",\"type\":\"secret\",\"label\":\"Password\"},"
    "{\"name\":\"tags\",\"type\":\"list\",\"multiple\":true,\"options\":[\"a\",\"b\",\"c\"]}]}";

const char *kBranchPrompt =
    "{\"kip\":1,\"type\":\"prompt\",\"id\":\"pick\",\"fields\":["
    "{\"name\":\"branch\",\"type\":\"list\",\"options\":[\"main\",\"feat\"]}],\"chips\":["
    "{\"id\":\"ctx\",\"label\":\"Context\",\"requires\":[\"branch\"]},"
    "{\"id\":\"nuke\",\"danger\":true,\"confirm\":{\"text\":\"Sure?\"}},"
    "{\"id\":\"free\"}]}";

} // namespace

class TestKipScreenState : public QObject {
    Q_OBJECT

private slots:
    void helloSetsHeader()
    {
        KipScreenState s;
        QVERIFY(s.apply(msg("{\"kip\":1,\"type\":\"hello\",\"title\":\"Deploy\",\"version\":\"2.3.0\"}")).changed);
        QCOMPARE(s.title(), QStringLiteral("Deploy"));
        QCOMPARE(s.programVersion(), QStringLiteral("2.3.0"));
    }

    void promptOpensWithInitialValues()
    {
        KipScreenState s;
        s.apply(msg(kEnvPrompt));
        QVERIFY(s.awaitingInput());
        QCOMPARE(s.openScreen()->id, QStringLiteral("env"));
        QCOMPARE(s.currentValues().value("env").toString(), QStringLiteral("dev"));
        QCOMPARE(s.currentValues().value("pass").toString(), QString());
        QVERIFY(s.currentValues().value("tags").isArray());
    }

    void promptWithoutFieldsIsAnAcknowledgeScreen()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"ack\",\"title\":\"Ready\"}"));
        QVERIFY(s.awaitingInput());
        QVERIFY(s.openScreen()->fields.isEmpty());
        const auto values = s.submit();
        QVERIFY(values.has_value());
        QVERIFY(values->isEmpty());
    }

    // ---- lembrar últimas respostas (§7.2) ----
    void rememberedValuesOverrideDefaults()
    {
        KipScreenState s;
        s.setRemembered(QJsonObject{{"env/env", "prod"}, {"env/tags", QJsonArray{"a", "c"}}});
        s.apply(msg(kEnvPrompt));
        QCOMPARE(s.currentValues().value("env").toString(), QStringLiteral("prod"));
        QCOMPARE(s.currentValues().value("tags").toArray().size(), 2);
    }

    void rememberedValueNoLongerValidFallsBackToDefault()
    {
        KipScreenState s;
        s.setRemembered(QJsonObject{{"env/env", "staging"}}); // opção sumiu
        s.apply(msg(kEnvPrompt));
        QCOMPARE(s.currentValues().value("env").toString(), QStringLiteral("dev"));
    }

    void secretFieldsAreNeverRemembered()
    {
        KipScreenState s;
        s.setRemembered(QJsonObject{{"env/pass", "leaked"}});
        s.apply(msg(kEnvPrompt));
        QCOMPARE(s.currentValues().value("pass").toString(), QString());
        s.setFieldValue("pass", QStringLiteral("hunter2"));
        const QJsonObject keep = s.openScreen()->rememberableAnswers(s.currentValues());
        QVERIFY(keep.contains("env/env"));
        QVERIFY(!keep.contains("env/pass"));
    }

    void rememberFalseOnPromptOrFieldOptsOut()
    {
        KipScreenState s;
        s.setRemembered(QJsonObject{{"p/a", "old"}, {"p/b", "old"}});
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":["
                    "{\"name\":\"a\",\"remember\":false},{\"name\":\"b\"}]}"));
        QCOMPARE(s.currentValues().value("a").toString(), QString());
        QCOMPARE(s.currentValues().value("b").toString(), QStringLiteral("old"));

        KipScreenState t;
        t.setRemembered(QJsonObject{{"p/b", "old"}});
        t.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"remember\":false,\"fields\":[{\"name\":\"b\"}]}"));
        QCOMPARE(t.currentValues().value("b").toString(), QString());
        QVERIFY(t.openScreen()->rememberableAnswers(t.currentValues()).isEmpty());
    }

    // ---- submit / resumo de etapas ----
    void submitLocksAndSummarizesWithMaskedSecret()
    {
        KipScreenState s;
        s.apply(msg(kEnvPrompt));
        s.setFieldValue("env", QStringLiteral("prod"));
        s.setFieldValue("pass", QStringLiteral("hunter2"));
        const auto values = s.submit();
        QVERIFY(values.has_value());
        QCOMPARE(values->value("env").toString(), QStringLiteral("prod"));
        QVERIFY(s.openScreen()->locked);
        QVERIFY(!s.awaitingInput());
        QVERIFY(!s.submit().has_value()); // já travado

        // O resumo só ganha a etapa quando a próxima tela chega.
        QVERIFY(s.answered().isEmpty());
        s.apply(msg("{\"kip\":1,\"type\":\"progress\",\"value\":10}"));
        QCOMPARE(s.answered().size(), 1);
        const KipAnsweredStep &step = s.answered().first();
        QCOMPARE(step.title, QStringLiteral("Where to?"));
        QCOMPARE(step.entries.at(0).label, QStringLiteral("Environment"));
        QCOMPARE(step.entries.at(0).value, QStringLiteral("Production"));
        QCOMPARE(step.entries.at(1).value, QString::fromUtf16(kKipSecretMask));
        for (const KipAnswerEntry &e : step.entries) {
            QVERIFY(!e.value.contains(QStringLiteral("hunter2")));
        }
        QVERIFY(!s.openScreen().has_value()); // tela travada some quando o running começa
    }

    void submitClearsDisplayBlocks()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"markdown\",\"text\":\"# hi\"}"));
        s.apply(msg(kEnvPrompt));
        QCOMPARE(s.blocks().size(), 1); // o bloco anterior ao prompt faz parte da tela
        s.submit();
        QVERIFY(s.blocks().isEmpty());
    }

    void invalidUnlocksWithErrorsAndDiscardsThePendingAnswer()
    {
        KipScreenState s;
        s.apply(msg(kEnvPrompt));
        s.submit();
        const auto r = s.apply(msg("{\"kip\":1,\"type\":\"invalid\",\"id\":\"env\",\"errors\":{\"env\":\"nope\"},\"message\":\"fix it\"}"));
        QVERIFY(r.changed);
        QVERIFY(s.awaitingInput());
        QCOMPARE(s.openScreen()->errors.value("env"), QStringLiteral("nope"));
        QCOMPARE(s.openScreen()->message, QStringLiteral("fix it"));
        QVERIFY(s.answered().isEmpty());
        s.apply(msg("{\"kip\":1,\"type\":\"progress\",\"value\":1}"));
        QVERIFY(s.answered().isEmpty()); // a resposta recusada nunca entra no resumo
    }

    void invalidForAnotherIdIsIgnored()
    {
        KipScreenState s;
        s.apply(msg(kEnvPrompt));
        const auto r = s.apply(msg("{\"kip\":1,\"type\":\"invalid\",\"id\":\"zzz\",\"errors\":{\"env\":\"x\"}}"));
        QVERIFY(!r.changed);
        QVERIFY(!r.diagnostics.isEmpty());
        QVERIFY(s.openScreen()->errors.isEmpty());
    }

    // ---- confirm ----
    void confirmAnswerIsAConfirmedFlag()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"confirm\",\"id\":\"c\",\"title\":\"Deploy\",\"text\":\"Sure?\",\"danger\":true}"));
        QCOMPARE(s.openScreen()->kind, KipOpenScreen::Kind::Confirm);
        QVERIFY(s.openScreen()->danger);
        const auto values = s.submitConfirm(false);
        QVERIFY(values.has_value());
        QCOMPARE(values->value("confirmed").toBool(), false);
        s.apply(msg("{\"kip\":1,\"type\":\"message\",\"level\":\"info\",\"text\":\"x\"}"));
        QCOMPARE(s.answered().size(), 1);
        QVERIFY(s.answered().first().isConfirm);
        QVERIFY(!s.answered().first().confirmed);
    }

    void promptCannotBeSubmittedAsConfirmAndViceVersa()
    {
        KipScreenState s;
        s.apply(msg(kEnvPrompt));
        QVERIFY(!s.submitConfirm(true).has_value());
        KipScreenState c;
        c.apply(msg("{\"kip\":1,\"type\":\"confirm\",\"id\":\"c\",\"text\":\"x\"}"));
        QVERIFY(!c.submit().has_value());
    }

    // ---- back ----
    void backRemovesTheLastAnsweredStepAndLocks()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"one\",\"title\":\"One\",\"fields\":[{\"name\":\"a\",\"default\":\"x\"}]}"));
        s.submit();
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"two\",\"title\":\"Two\",\"back\":true}"));
        QCOMPARE(s.answered().size(), 1);
        QVERIFY(s.goBack());
        QVERIFY(s.answered().isEmpty());
        QVERIFY(!s.awaitingInput()); // espera o programa decidir a próxima tela
    }

    void backIsRefusedWhenThePromptDoesNotAllowIt()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"one\"}"));
        QVERIFY(!s.goBack());
        QVERIFY(s.awaitingInput());
    }

    // ---- blocos de exibição ----
    void displayBlocksKeepArrivalOrder()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"markdown\",\"text\":\"a\"}"));
        s.apply(msg("{\"kip\":1,\"type\":\"message\",\"level\":\"warning\",\"text\":\"b\"}"));
        s.apply(msg("{\"kip\":1,\"type\":\"table\",\"columns\":[{\"key\":\"k\"}],\"rows\":[{\"k\":1}]}"));
        QCOMPARE(s.blocks().size(), 3);
        QVERIFY(std::holds_alternative<KipMarkdown>(s.blocks().at(0)));
        QVERIFY(std::holds_alternative<KipMessageBlock>(s.blocks().at(1)));
        QVERIFY(std::holds_alternative<KipTable>(s.blocks().at(2)));
    }

    void laterProgressUpdatesTheSameBar()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"progress\",\"value\":10,\"label\":\"one\"}"));
        s.apply(msg("{\"kip\":1,\"type\":\"markdown\",\"text\":\"between\"}"));
        s.apply(msg("{\"kip\":1,\"type\":\"progress\",\"value\":80,\"label\":\"two\"}"));
        QCOMPARE(s.blocks().size(), 2);
        const auto &p = std::get<KipProgress>(s.blocks().at(0));
        QCOMPARE(*p.value, 80.0);
        QCOMPARE(p.label, QStringLiteral("two"));
    }

    void stepsAreReplacedByIdAndUpdatedByStep()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"steps\",\"id\":\"s\",\"items\":[{\"id\":\"a\",\"label\":\"A\"},{\"id\":\"b\",\"label\":\"B\"}]}"));
        auto r = s.apply(msg("{\"kip\":1,\"type\":\"step\",\"steps\":\"s\",\"id\":\"a\",\"state\":\"running\",\"detail\":\"working\"}"));
        QVERIFY(r.changed);
        const auto &steps = std::get<KipSteps>(s.blocks().at(0));
        QCOMPARE(steps.items.at(0).state, KipStepState::Running);
        QCOMPARE(steps.items.at(0).detail, QStringLiteral("working"));
        QCOMPARE(steps.items.at(1).state, KipStepState::Pending);

        s.apply(msg("{\"kip\":1,\"type\":\"steps\",\"id\":\"s\",\"items\":[{\"id\":\"z\",\"label\":\"Z\"}]}"));
        QCOMPARE(s.blocks().size(), 1);
        QCOMPARE(std::get<KipSteps>(s.blocks().at(0)).items.size(), 1);
    }

    void stepForUnknownListOrItemIsIgnoredWithDiagnostic()
    {
        KipScreenState s;
        auto r = s.apply(msg("{\"kip\":1,\"type\":\"step\",\"steps\":\"nope\",\"id\":\"a\",\"state\":\"success\"}"));
        QVERIFY(!r.changed);
        QVERIFY(!r.diagnostics.isEmpty());
        s.apply(msg("{\"kip\":1,\"type\":\"steps\",\"id\":\"s\",\"items\":[{\"id\":\"a\"}]}"));
        r = s.apply(msg("{\"kip\":1,\"type\":\"step\",\"steps\":\"s\",\"id\":\"zzz\",\"state\":\"success\"}"));
        QVERIFY(!r.changed);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void tablesWithTheSameIdAreReplaced()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"table\",\"id\":\"t\",\"columns\":[{\"key\":\"k\"}],\"rows\":[{\"k\":1}]}"));
        s.apply(msg("{\"kip\":1,\"type\":\"table\",\"id\":\"t\",\"columns\":[{\"key\":\"k\"}],\"rows\":[{\"k\":1},{\"k\":2}]}"));
        s.apply(msg("{\"kip\":1,\"type\":\"table\",\"columns\":[{\"key\":\"k\"}],\"rows\":[]}"));
        QCOMPARE(s.blocks().size(), 2);
        QCOMPARE(std::get<KipTable>(s.blocks().at(0)).rows.size(), 2);
    }

    void newPromptReplacesTheOpenOneKeepingBlocks()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"markdown\",\"text\":\"keep\"}"));
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"a\"}"));
        const auto r = s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"b\"}"));
        QVERIFY(!r.diagnostics.isEmpty()); // substituição é registrada
        QCOMPARE(s.openScreen()->id, QStringLiteral("b"));
        QCOMPARE(s.blocks().size(), 1);
    }

    // ---- patch (§9) ----
    void patchOnlyAppliesToTheOpenPrompt()
    {
        KipScreenState s;
        s.apply(msg(kEnvPrompt));
        const auto r = s.apply(msg("{\"kip\":1,\"type\":\"patch\",\"id\":\"other\",\"seq\":1,\"fields\":[]}"));
        QVERIFY(!r.changed);
        QVERIFY(!r.diagnostics.isEmpty());
    }

    void patchReplacesAppendsAndRemoves()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":["
                    "{\"name\":\"ctx\",\"type\":\"select\",\"options\":[\"a\",\"b\"],\"watch\":true},"
                    "{\"name\":\"ns\",\"type\":\"select\",\"options\":[\"x\"]},"
                    "{\"name\":\"pod\",\"type\":\"select\",\"options\":[\"p1\"]}]}"));
        s.apply(msg("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":1,\"fields\":["
                    "{\"name\":\"ns\",\"type\":\"select\",\"options\":[\"x\",\"y\"]},"
                    "{\"name\":\"extra\",\"type\":\"text\",\"default\":\"hi\"}],\"remove\":[\"pod\"]}"));
        const auto &fields = s.openScreen()->fields;
        QCOMPARE(fields.size(), 3);
        QCOMPARE(fields.at(0).name, QStringLiteral("ctx"));
        QCOMPARE(fields.at(1).options.size(), 2);
        QCOMPARE(fields.at(2).name, QStringLiteral("extra")); // novo = no fim
        QCOMPARE(s.currentValues().value("extra").toString(), QStringLiteral("hi"));
        QVERIFY(!s.currentValues().contains("pod"));
    }

    void patchKeepsTheUsersValueWhenStillValidElseResetsToDefault()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":["
                    "{\"name\":\"ns\",\"type\":\"select\",\"options\":[\"x\",\"y\"],\"default\":\"x\"},"
                    "{\"name\":\"n\",\"type\":\"number\",\"min\":0,\"max\":10,\"default\":1}]}"));
        s.setFieldValue("ns", QStringLiteral("y"));
        s.setFieldValue("n", 8);
        s.apply(msg("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":1,\"fields\":["
                    "{\"name\":\"ns\",\"type\":\"select\",\"options\":[\"x\",\"y\",\"z\"],\"default\":\"x\"},"
                    "{\"name\":\"n\",\"type\":\"number\",\"min\":0,\"max\":5,\"default\":1}]}"));
        QCOMPARE(s.currentValues().value("ns").toString(), QStringLiteral("y")); // continua válido
        QCOMPARE(s.currentValues().value("n").toDouble(), 1.0);                  // fora do intervalo -> default

        s.apply(msg("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":2,\"fields\":["
                    "{\"name\":\"ns\",\"type\":\"select\",\"options\":[\"z\"],\"default\":\"z\"}]}"));
        QCOMPARE(s.currentValues().value("ns").toString(), QStringLiteral("z")); // opção sumiu -> default
    }

    void patchWithEmptyFieldsIsAcceptedAsAnAck()
    {
        KipScreenState s;
        s.apply(msg(kEnvPrompt));
        const auto r = s.apply(msg("{\"kip\":1,\"type\":\"patch\",\"id\":\"env\",\"seq\":3,\"fields\":[]}"));
        QVERIFY(r.changed);
        QCOMPARE(s.openScreen()->fields.size(), 3);
    }

    void patchKeepsFlagsConsistentWithTheNewDefinition()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"fields\":[{\"name\":\"o\",\"type\":\"flags\","
                    "\"options\":[{\"name\":\"a\"},{\"name\":\"b\",\"default\":true}]}]}"));
        s.setFieldValue("o", QJsonObject{{"a", true}, {"b", false}});
        s.apply(msg("{\"kip\":1,\"type\":\"patch\",\"id\":\"p\",\"seq\":1,\"fields\":[{\"name\":\"o\",\"type\":\"flags\","
                    "\"options\":[{\"name\":\"a\"},{\"name\":\"c\",\"default\":true}]}]}"));
        const QJsonObject o = s.currentValues().value("o").toObject();
        QCOMPARE(o.size(), 2);
        QCOMPARE(o.value("a").toBool(), true);
        QCOMPARE(o.value("c").toBool(), true);
        QVERIFY(!o.contains("b"));
    }

    // ---- done e estados ----
    void doneClosesTheOpenScreenAndKeepsBlocks()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"table\",\"columns\":[{\"key\":\"k\"}],\"rows\":[{\"k\":1}]}"));
        s.apply(msg("{\"kip\":1,\"type\":\"done\",\"title\":\"Done\"}"));
        QVERIFY(s.done().has_value());
        QVERIFY(!s.openScreen().has_value());
        QCOMPARE(s.blocks().size(), 1);
    }

    void terminalStateLocksTheOpenScreenAndCommitsTheAnswer()
    {
        KipScreenState s;
        s.apply(msg(kEnvPrompt));
        s.submit();
        s.setSessionState(KipSessionState::Failed, {KipTerminalReason::NonZeroExit, 3, {}});
        QCOMPARE(s.sessionState(), KipSessionState::Failed);
        QCOMPARE(s.terminalInfo().exitCode, 3);
        QCOMPARE(s.answered().size(), 1);
        QVERIFY(!s.awaitingInput());
    }

    void cancellableFollowsPromptThenProgress()
    {
        KipScreenState s;
        QVERIFY(s.cancellable());
        s.apply(msg("{\"kip\":1,\"type\":\"progress\",\"value\":1,\"cancellable\":false}"));
        QVERIFY(!s.cancellable());
        s.apply(msg("{\"kip\":1,\"type\":\"progress\",\"value\":2}"));
        QVERIFY(s.cancellable()); // o último progress manda
        s.apply(msg("{\"kip\":1,\"type\":\"prompt\",\"id\":\"p\",\"cancellable\":false}"));
        QVERIFY(!s.cancellable());
    }

    // ---- chips (§21) ----
    void chipRunsThroughRunningToItsResult()
    {
        KipScreenState s;
        s.apply(msg(kBranchPrompt));
        QCOMPARE(s.openScreen()->chips.size(), 3);
        QCOMPARE(s.startChip(QStringLiteral("free")), KipScreenState::ChipStart::Send);
        QCOMPARE(s.openScreen()->chipRun->phase, KipChipRun::Phase::Running);
        QVERIFY(s.openScreen()->chipRun->busy());

        // outro chip não começa enquanto este roda
        QCOMPARE(s.startChip(QStringLiteral("ctx")), KipScreenState::ChipStart::Rejected);

        auto r = s.apply(msg("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"free\",\"state\":\"running\",\"text\":\"step 1\"}"));
        QVERIFY(r.changed);
        QCOMPARE(s.openScreen()->chipRun->text, QStringLiteral("step 1"));
        QVERIFY(s.openScreen()->chipRun->busy());
        r = s.apply(msg("{\"kip\":1,\"type\":\"chip_result\",\"id\":\"pick\",\"chip\":\"free\",\"state\":\"success\",\"title\":\"Done\",\"text\":\"all good\"}"));
        QVERIFY(r.changed);
        QCOMPARE(s.openScreen()->chipRun->phase, KipChipRun::Phase::Success);
        QCOMPARE(s.openScreen()->chipRun->title, QStringLiteral("Done"));
        QVERIFY(s.openScreen()->chipRun->finished());

        // terminado: um novo chip pode rodar, e a caixa pode ser dispensada
        QVERIFY(s.dismissChip());
        QVERIFY(!s.openScreen()->chipRun);
        QVERIFY(!s.dismissChip());
        QCOMPARE(s.startChip(QStringLiteral("free")), KipScreenState::ChipStart::Send);
    }

    void chipWithConfirmationWaitsForIt()
    {
        KipScreenState s;
        s.apply(msg(kBranchPrompt));
        QCOMPARE(s.startChip(QStringLiteral("nuke")), KipScreenState::ChipStart::NeedsConfirmation);
        QCOMPARE(s.openScreen()->chipRun->phase, KipChipRun::Phase::Confirming);
        // um chip_result não pode adiantar a confirmação
        const auto early = s.apply(msg("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"nuke\",\"state\":\"success\"}"));
        QVERIFY(!early.changed);
        QVERIFY(!early.diagnostics.isEmpty());
        QCOMPARE(s.openScreen()->chipRun->phase, KipChipRun::Phase::Confirming);

        QVERIFY(s.confirmChip());
        QCOMPARE(s.openScreen()->chipRun->phase, KipChipRun::Phase::Running);
        QVERIFY(!s.confirmChip()); // só uma vez

        // recusar a confirmação fecha a caixa
        KipScreenState t;
        t.apply(msg(kBranchPrompt));
        t.startChip(QStringLiteral("nuke"));
        QVERIFY(t.dismissChip());
        QVERIFY(!t.openScreen()->chipRun);
    }

    void chipRequiresFilledFields()
    {
        KipScreenState s;
        s.apply(msg(kBranchPrompt));
        const KipChip *ctx = s.openScreen()->chipNamed(QStringLiteral("ctx"));
        QVERIFY(ctx);
        QVERIFY(!s.openScreen()->chipRequirementsMet(*ctx)); // branch vazio
        QCOMPARE(s.startChip(QStringLiteral("ctx")), KipScreenState::ChipStart::Rejected);
        s.setFieldValue(QStringLiteral("branch"), QStringLiteral("main"));
        QVERIFY(s.openScreen()->chipRequirementsMet(*ctx));
        QCOMPARE(s.startChip(QStringLiteral("ctx")), KipScreenState::ChipStart::Send);
    }

    void chipsAreRejectedWhenTheyCannotRun()
    {
        KipScreenState s;
        QCOMPARE(s.startChip(QStringLiteral("free")), KipScreenState::ChipStart::Rejected); // sem tela
        s.apply(msg(kBranchPrompt));
        QCOMPARE(s.startChip(QStringLiteral("nope")), KipScreenState::ChipStart::Rejected); // chip inexistente
        s.setChangePending(true);
        QCOMPARE(s.startChip(QStringLiteral("free")), KipScreenState::ChipStart::Rejected); // valores em atualização
        s.setChangePending(false);
        s.submit();
        QCOMPARE(s.startChip(QStringLiteral("free")), KipScreenState::ChipStart::Rejected); // tela travada
    }

    void staleOrMisaddressedChipResultsAreIgnored()
    {
        KipScreenState s;
        s.apply(msg(kBranchPrompt));
        // nada rodando
        auto r = s.apply(msg("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"free\",\"state\":\"success\"}"));
        QVERIFY(!r.changed);
        QVERIFY(!r.diagnostics.isEmpty());
        s.startChip(QStringLiteral("free"));
        // outro chip
        r = s.apply(msg("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"ctx\",\"state\":\"success\"}"));
        QVERIFY(!r.changed);
        // outro prompt
        r = s.apply(msg("{\"kip\":1,\"type\":\"chip_result\",\"id\":\"other\",\"chip\":\"free\",\"state\":\"success\"}"));
        QVERIFY(!r.changed);
        QVERIFY(s.openScreen()->chipRun->busy());
        // depois de terminar, um resultado atrasado também é ignorado
        s.apply(msg("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"free\",\"state\":\"error\",\"text\":\"x\"}"));
        r = s.apply(msg("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"free\",\"state\":\"success\"}"));
        QVERIFY(!r.changed);
        QCOMPARE(s.openScreen()->chipRun->phase, KipChipRun::Phase::Error);
    }

    void chipBoxDoesNotSurviveTheScreen()
    {
        KipScreenState s;
        s.apply(msg(kBranchPrompt));
        s.startChip(QStringLiteral("free"));
        s.apply(msg("{\"kip\":1,\"type\":\"chip_result\",\"chip\":\"free\",\"state\":\"success\"}"));
        s.submit();
        QVERIFY(!s.openScreen()->chipRun);
        s.apply(msg(kBranchPrompt));   // outro prompt = caixa nova
        QVERIFY(!s.openScreen()->chipRun);
    }

    void patchCanReplaceTheChipSet()
    {
        KipScreenState s;
        s.apply(msg(kBranchPrompt));
        s.apply(msg("{\"kip\":1,\"type\":\"patch\",\"id\":\"pick\",\"seq\":1,\"fields\":[]}"));
        QCOMPARE(s.openScreen()->chips.size(), 3); // sem a chave: intactos
        s.apply(msg("{\"kip\":1,\"type\":\"patch\",\"id\":\"pick\",\"seq\":2,\"chips\":[{\"id\":\"only\"}]}"));
        QCOMPARE(s.openScreen()->chips.size(), 1);
        QCOMPARE(s.openScreen()->chips.first().id, QStringLiteral("only"));
        s.apply(msg("{\"kip\":1,\"type\":\"patch\",\"id\":\"pick\",\"seq\":3,\"chips\":[]}"));
        QVERIFY(s.openScreen()->chips.isEmpty());
    }

    void failingTheRunningChipShowsTheError()
    {
        KipScreenState s;
        s.apply(msg(kBranchPrompt));
        s.startChip(QStringLiteral("free"));
        s.failRunningChip(QStringLiteral("program ended"));
        QCOMPARE(s.openScreen()->chipRun->phase, KipChipRun::Phase::Error);
        QCOMPARE(s.openScreen()->chipRun->text, QStringLiteral("program ended"));
    }

    void progressDisappearsWhenTheProgramIsDone()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"hello\"}"));
        s.apply(msg("{\"kip\":1,\"type\":\"message\",\"text\":\"kept\"}"));
        s.apply(msg("{\"kip\":1,\"type\":\"progress\",\"value\":null,\"label\":\"Switching…\"}"));
        QCOMPARE(s.blocks().size(), 2);
        s.apply(msg("{\"kip\":1,\"type\":\"done\",\"title\":\"ok\"}"));
        // A barra sai; a mensagem (parte do resultado) fica.
        QCOMPARE(s.blocks().size(), 1);
        QVERIFY(std::holds_alternative<KipMessageBlock>(s.blocks().first()));
    }

    void progressDisappearsWhenTheSessionEndsWithoutDone()
    {
        KipScreenState s;
        s.apply(msg("{\"kip\":1,\"type\":\"hello\"}"));
        s.apply(msg("{\"kip\":1,\"type\":\"progress\",\"value\":40,\"label\":\"Working\"}"));
        QCOMPARE(s.blocks().size(), 1);
        s.setSessionState(KipSessionState::Finished);
        QVERIFY(s.blocks().isEmpty());
    }

    void stateNamesAndTerminalness()
    {
        QVERIFY(!kipIsTerminalState(KipSessionState::Handshaking));
        QVERIFY(!kipIsTerminalState(KipSessionState::AwaitingInput));
        QVERIFY(kipIsTerminalState(KipSessionState::Unsupported));
        QVERIFY(kipIsTerminalState(KipSessionState::Finished));
        QCOMPARE(kipSessionStateToString(KipSessionState::ProtocolError), QStringLiteral("protocol_error"));
    }
};

QTEST_MAIN(TestKipScreenState)
#include "test_kip_screen_state.moc"
