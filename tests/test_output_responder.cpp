#include <QtTest>

#include "engine/output-responder-matcher.h"
#include "core/environment-manager.h"

using namespace kai::engine;
using namespace kai::core;

// Unidade do matcher de auto-responsores, sem processo real: casamento sobre chunks, captura de grupo, limite e buffer.
class TestOutputResponder : public QObject {
    Q_OBJECT

private slots:
    // Um prompt que chega FATIADO entre dois chunks deve casar UMA vez só
    // (não zero por chegar quebrado, não duas por reprocessar o buffer).
    void slicedChunksMatchOnce()
    {
        OutputResponder r;
        r.name = QStringLiteral("t");
        r.pattern = QStringLiteral("Continuar\\? \\[y/N\\]");
        r.response = QStringLiteral("y");
        OutputResponderMatcher m({r});

        auto a = m.feed(QStringLiteral("Deseja Contin"));
        QCOMPARE(a.size(), 0);
        auto b = m.feed(QStringLiteral("uar? [y/N] "));
        QCOMPARE(b.size(), 1);
        QCOMPARE(b.at(0).text, QStringLiteral("y"));
        auto c = m.feed(QStringLiteral("\nfazendo coisas...\n"));
        QCOMPARE(c.size(), 0);
    }

    // Captura de grupo: o nome vem NA própria pergunta e é ecoado na resposta.
    void capturedGroupInResponse()
    {
        OutputResponder r;
        r.name = QStringLiteral("t");
        r.pattern = QStringLiteral("Base de dados \\[(\\w+)\\]\\?");
        r.response = QStringLiteral("\\1");
        OutputResponderMatcher m({r});

        auto out = m.feed(QStringLiteral("Base de dados [producao]? "));
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.at(0).text, QStringLiteral("producao"));
    }

    // {{VAR}} na resposta, junto com \1 no mesmo texto.
    void interpolatesEnvVarsInResponse()
    {
        EnvironmentManager env;
        env.setGlobalVars({{"DB_PASSWORD", "s3cr3t"}});

        OutputResponder r;
        r.name = QStringLiteral("t");
        r.pattern = QStringLiteral("Senha para (\\w+):");
        r.response = QStringLiteral("{{DB_PASSWORD}} (\\1)");
        OutputResponderMatcher m({r}, &env);

        auto out = m.feed(QStringLiteral("Senha para producao: "));
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.at(0).text, QStringLiteral("s3cr3t (producao)"));
    }

    // Sem EnvironmentManager (nullptr, padrão) {{VAR}} passa como texto literal, sem crash.
    void withoutEnvironmentManagerLeavesPlaceholdersLiteral()
    {
        OutputResponder r;
        r.name = QStringLiteral("t");
        r.pattern = QStringLiteral("Continuar\\?");
        r.response = QStringLiteral("{{X}}");
        OutputResponderMatcher m({r});

        auto out = m.feed(QStringLiteral("Continuar? "));
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.at(0).text, QStringLiteral("{{X}}"));
    }

    // Vários prompts no mesmo chunk casam na ORDEM de aparição.
    void multiplePromptsInOneChunk()
    {
        OutputResponder r;
        r.name = QStringLiteral("t");
        r.pattern = QStringLiteral("valor=(\\d+)");
        r.response = QStringLiteral("<\\1>");
        OutputResponderMatcher m({r});

        auto out = m.feed(QStringLiteral("valor=1 e valor=2 e valor=3"));
        QCOMPARE(out.size(), 3);
        QCOMPARE(out.at(0).text, QStringLiteral("<1>"));
        QCOMPARE(out.at(1).text, QStringLiteral("<2>"));
        QCOMPARE(out.at(2).text, QStringLiteral("<3>"));
    }

    // Limite de disparos: com limitTriggers e maxTriggers=1, casa uma vez só
    // mesmo com o padrão aparecendo várias vezes.
    void respectsTriggerLimit()
    {
        OutputResponder r;
        r.name = QStringLiteral("t");
        r.pattern = QStringLiteral("prompt");
        r.response = QStringLiteral("ok");
        r.limitTriggers = true;
        r.maxTriggers = 1;
        OutputResponderMatcher m({r});

        auto out = m.feed(QStringLiteral("prompt prompt prompt"));
        QCOMPARE(out.size(), 1);
        auto out2 = m.feed(QStringLiteral("prompt de novo"));
        QCOMPARE(out2.size(), 0);
    }

    // Sem limite configurado, o teto de sanidade impede loop infinito.
    void sanityCapPreventsInfiniteLoop()
    {
        OutputResponder r;
        r.name = QStringLiteral("t");
        r.pattern = QStringLiteral("x");
        r.response = QStringLiteral("y");
        OutputResponderMatcher m({r});

        auto out = m.feed(QString(200, QLatin1Char('x')));
        QCOMPARE(out.size(), OutputResponderMatcher::kSanityCap);
    }

    // Regex inválida é ignorada silenciosamente (não casa, não crasha).
    void invalidRegexIsIgnored()
    {
        OutputResponder r;
        r.name = QStringLiteral("t");
        r.pattern = QStringLiteral("[invalido(");
        r.response = QStringLiteral("nao");
        OutputResponderMatcher m({r});

        auto out = m.feed(QStringLiteral("qualquer coisa [invalido("));
        QCOMPARE(out.size(), 0);
    }

    // Responsor desabilitado não dispara.
    void disabledResponderDoesNotFire()
    {
        OutputResponder r;
        r.name = QStringLiteral("t");
        r.enabled = false;
        r.pattern = QStringLiteral("prompt");
        r.response = QStringLiteral("ok");
        OutputResponderMatcher m({r});

        QCOMPARE(m.feed(QStringLiteral("prompt")).size(), 0);
    }

    // Nome é OBRIGATÓRIO: responsor sem nome não dispara.
    void unnamedResponderDoesNotFire()
    {
        OutputResponder r;
        r.name = QString();
        r.pattern = QStringLiteral("prompt");
        r.response = QStringLiteral("ok");
        OutputResponderMatcher m({r});

        QCOMPARE(m.feed(QStringLiteral("prompt")).size(), 0);
    }

    // O buffer é cortado ao exceder o teto, sem perder um casamento recente.
    void bufferIsTrimmedButStillMatchesRecent()
    {
        OutputResponder r;
        r.name = QStringLiteral("t");
        r.pattern = QStringLiteral("MARCA_FINAL");
        r.response = QStringLiteral("achou");
        OutputResponderMatcher m({r});

        m.feed(QString(OutputResponderMatcher::kMaxBuffer * 2, QLatin1Char('a')));
        auto out = m.feed(QStringLiteral("...MARCA_FINAL..."));
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.at(0).text, QStringLiteral("achou"));
    }
};

QTEST_MAIN(TestOutputResponder)
#include "test_output_responder.moc"
