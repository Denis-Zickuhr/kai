// Prompts do sistema, leitura da resposta do assistente do editor, biblioteca de manifestos e corte do
// histórico ao tamanho do contexto.

#include <QTest>

#include "engine/ai/ai-context.h"
#include "engine/ai/ai-prompts.h"
#include "engine/ai/command-json.h"
#include "engine/ai/manifesto-library.h"

using namespace kai::core;
using namespace kai::engine;

namespace {
AiMessage msg(AiMessage::Role role, const QString &text)
{
    AiMessage m;
    m.role = role;
    m.text = text;
    return m;
}
AiMessage toolMsg(const QString &id, const QString &content)
{
    AiMessage m;
    m.role = AiMessage::Role::Tool;
    AiToolResult r;
    r.toolCallId = id;
    r.content = content;
    m.toolResults.append(r);
    return m;
}
AiMessage callMsg(const QString &id)
{
    AiMessage m;
    m.role = AiMessage::Role::Assistant;
    m.toolCalls.append(AiToolCall{id, QStringLiteral("t"), {}});
    return m;
}
} // namespace

class TestAiPrompts : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ prompts
    void agentPromptKeepsTheFixedRulesAndTheFacts()
    {
        AiPromptContext c;
        c.uiLanguage = QStringLiteral("pt");
        c.today = QStringLiteral("2026-10-16");
        c.activeEnvironment = QStringLiteral("Dev");
        c.extraInstructions = QStringLiteral("Prefira Python.");
        const QString p = buildAgentSystemPrompt(c);
        for (const char *needle : {"Stay inside Kai", "DATA", "Never reveal", "approval", "read_manifesto", "read_samples", "2026-10-16", "'pt'", "'Dev'", "Prefira Python."}) {
            QVERIFY2(p.contains(QString::fromLatin1(needle)), needle);
        }
        // As instruções do usuário vêm DEPOIS das regras e dizem que não as anulam.
        QVERIFY(p.indexOf(QStringLiteral("Prefira Python.")) > p.indexOf(QStringLiteral("Never reveal")));
        QVERIFY(p.contains(QStringLiteral("do not override")));
        QVERIFY(estimateTokens(p.size()) < 1500); // compacto
    }

    void agentPromptVariantsForToolsAndSecrets()
    {
        AiPromptContext c;
        c.toolsEnabled = false;
        c.secretsHidden = true;
        const QString p = buildAgentSystemPrompt(c);
        QVERIFY(p.contains(QStringLiteral("no tool access")));
        QVERIFY(p.contains(QStringLiteral("***hidden***")));
        AiPromptContext plain;
        QVERIFY(!buildAgentSystemPrompt(plain).contains(QStringLiteral("no tool access")));
        QVERIFY(!buildAgentSystemPrompt(plain).contains(QStringLiteral("EXTRA INSTRUCTIONS")));
    }

    void assistPromptCarriesTheCommandModelAndHttpOnlyWhenNeeded()
    {
        ManifestoLibrary lib(QStringLiteral("assets/manifesto"));
        AiPromptContext c;
        c.uiLanguage = QStringLiteral("en");
        const QString shell = buildAssistSystemPrompt(c, false, lib);
        const QString http = buildAssistSystemPrompt(c, true, lib);
        QVERIFY(shell.contains(QStringLiteral("complete command model")));
        QVERIFY(shell.contains(QStringLiteral("```json")));
        QVERIFY(!shell.contains(QStringLiteral("## 6. HTTP commands")));
        QVERIFY(http.contains(QStringLiteral("## 6. HTTP commands")));
        QVERIFY(http.size() > shell.size());
    }

    void assistUserPromptIncludesRequestAndCommand()
    {
        const QString u = buildAssistUserPrompt(QStringLiteral(" troca o timeout "), QJsonObject{{QStringLiteral("name"), QStringLiteral("X")}});
        QVERIFY(u.contains(QStringLiteral("troca o timeout")));
        QVERIFY(u.contains(QStringLiteral("\"name\": \"X\"")));
    }

    // ------------------------------------------------- resposta do assistente
    void assistReplyWithAFence()
    {
        const AssistReply r = parseAssistReply(QStringLiteral("Troquei o timeout.\n```json\n{\"name\": \"A\", \"command\": \"echo {}\"}\n```\n"));
        QVERIFY(r.ok);
        QCOMPARE(r.explanation, QStringLiteral("Troquei o timeout."));
        QCOMPARE(r.command.value(QStringLiteral("command")).toString(), QStringLiteral("echo {}"));
    }

    void assistReplyWithoutAFenceOrWithBracesInStrings()
    {
        const AssistReply r = parseAssistReply(QStringLiteral("Feito: {\"name\":\"A\",\"command\":\"printf '}{'\"} fim"));
        QVERIFY(r.ok);
        QCOMPARE(r.command.value(QStringLiteral("command")).toString(), QStringLiteral("printf '}{'"));
        QVERIFY(r.explanation.startsWith(QStringLiteral("Feito")));
    }

    void assistReplyFailures()
    {
        QCOMPARE(parseAssistReply(QStringLiteral("não sei")).error, QStringLiteral("no_json"));
        QCOMPARE(parseAssistReply(QStringLiteral("```json\n{\"a\": }\n```")).error, QStringLiteral("bad_json"));
        QVERIFY(!parseAssistReply(QString()).ok);
    }

    // ---------------------------------------------------------- command-json
    void commandJsonRoundTripsThroughTheAiShape()
    {
        Command a;
        a.id = QStringLiteral("ca");
        a.folderId = QStringLiteral("f");
        a.name = QStringLiteral("A");
        a.command = QStringLiteral("echo a");
        Command b;
        b.id = QStringLiteral("cb");
        b.folderId = QStringLiteral("f");
        b.name = QStringLiteral("B");
        b.command = QStringLiteral("echo b");
        b.hooks.pre = {QStringLiteral("ca")};
        b.lastParamValues = {{QStringLiteral("x"), QStringLiteral("1")}};
        const QVector<Command> all{a, b};

        const QJsonObject view = commandToAiJson(b, all);
        QCOMPARE(view.value(QStringLiteral("hooks")).toObject().value(QStringLiteral("pre")).toArray().first().toString(), QStringLiteral("A"));
        QVERIFY(!view.contains(QStringLiteral("last_param_values")));

        Command back;
        QString error;
        QVERIFY2(commandFromAiJson(view, QStringLiteral("f"), &b, all, {}, back, error), qPrintable(error));
        QCOMPARE(back.id, QStringLiteral("cb"));
        QCOMPARE(back.hooks.pre, QStringList{QStringLiteral("ca")});
        QCOMPARE(back.lastParamValues.value(QStringLiteral("x")), QStringLiteral("1")); // estado de uso preservado
    }

    // -------------------------------------------------------- manifestos
    void manifestoLibraryParsesHeadingsOutsideCodeFences()
    {
        ManifestoLibrary lib(QStringLiteral("assets/manifesto"));
        const auto &sections = lib.sections(QStringLiteral("kai"));
        QVERIFY(sections.size() > 20);
        QVERIFY(!lib.sectionText(QStringLiteral("kai"), QStringLiteral("3.1")).isEmpty());
        QVERIFY(lib.sectionText(QStringLiteral("kai"), QStringLiteral("3.1")).contains(QStringLiteral("```yaml model command")));
        // Comentários "# ..." dentro de cercas de código não viram seções.
        for (const auto &s : sections) QVERIFY2(!s.title.startsWith(QStringLiteral("Top-level keys")), qPrintable(s.title));
        QVERIFY(lib.outline(QStringLiteral("kai")).contains(QStringLiteral("HTTP commands")));
        // Por título também.
        QVERIFY(!lib.sectionText(QStringLiteral("kai"), QStringLiteral("http commands")).isEmpty());
        QVERIFY(lib.sectionText(QStringLiteral("kai"), QStringLiteral("nope")).isEmpty());
        QVERIFY(lib.sections(QStringLiteral("unknown")).isEmpty());
        QVERIFY(!lib.sections(QStringLiteral("kip")).isEmpty());
        QVERIFY(!lib.sections(QStringLiteral("icons")).isEmpty());
        QVERIFY(!lib.sectionText(QStringLiteral("schema"), QStringLiteral("command")).isEmpty());
    }

    void manifestoLibraryWithoutFilesIsEmptyNotACrash()
    {
        ManifestoLibrary lib(QStringLiteral("/nonexistent/dir"));
        QVERIFY(lib.sections(QStringLiteral("kai")).isEmpty());
        QVERIFY(lib.outline(QStringLiteral("kai")).isEmpty());
    }

    // ----------------------------------------------------------- contexto
    void unlimitedContextKeepsEverything()
    {
        QVector<AiMessage> v{msg(AiMessage::Role::User, QString(10000, QLatin1Char('a')))};
        QCOMPARE(fitToContext(v, 100, 0, 1000).size(), 1);
    }

    void oldToolResultsAreShrunkBeforeAnythingIsDropped()
    {
        QVector<AiMessage> v;
        v << msg(AiMessage::Role::User, QStringLiteral("q1")) << callMsg(QStringLiteral("a")) << toolMsg(QStringLiteral("a"), QString(8000, QLatin1Char('x')))
          << msg(AiMessage::Role::Assistant, QStringLiteral("r1"))
          << msg(AiMessage::Role::User, QStringLiteral("q2")) << callMsg(QStringLiteral("b")) << toolMsg(QStringLiteral("b"), QStringLiteral("small"))
          << msg(AiMessage::Role::Assistant, QStringLiteral("r2")) << msg(AiMessage::Role::User, QStringLiteral("q3"))
          << msg(AiMessage::Role::Assistant, QStringLiteral("r3"));
        const QVector<AiMessage> out = fitToContext(v, 1000, 3000, 500);
        QCOMPARE(out.size(), v.size());
        QVERIFY(out.at(2).toolResults.first().content.contains(QStringLiteral("omitted")));
        QCOMPARE(out.at(6).toolResults.first().content, QStringLiteral("small"));
    }

    void whenStillTooBigTheOldestMessagesGoAndTheStartIsAUserMessage()
    {
        QVector<AiMessage> v;
        for (int i = 0; i < 20; ++i) {
            v << msg(AiMessage::Role::User, QStringLiteral("question %1 ").arg(i) + QString(500, QLatin1Char('q')));
            v << callMsg(QStringLiteral("c%1").arg(i));
            v << toolMsg(QStringLiteral("c%1").arg(i), QStringLiteral("ok"));
            v << msg(AiMessage::Role::Assistant, QStringLiteral("answer %1 ").arg(i) + QString(500, QLatin1Char('a')));
        }
        v << msg(AiMessage::Role::User, QStringLiteral("LAST"));
        const QVector<AiMessage> out = fitToContext(v, 500, 2500, 500);
        QVERIFY(out.size() < v.size());
        QCOMPARE(out.first().role, AiMessage::Role::User);
        QCOMPARE(out.last().text, QStringLiteral("LAST")); // a última pergunta nunca sai
        // Nenhum resultado órfão no começo.
        QVERIFY(out.first().role != AiMessage::Role::Tool);
    }

    void tokenEstimateIsRoughlyCharsOverThreePointFive()
    {
        QVERIFY(estimateTokens(3500) >= 950 && estimateTokens(3500) <= 1050);
        QCOMPARE(estimateTokens(0), 0);
    }
};

QTEST_GUILESS_MAIN(TestAiPrompts)
#include "test_ai_prompts.moc"
