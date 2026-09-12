#include <QRegularExpression>
#include <QTest>
#include <QMap>
#include <QString>
#include <QJsonObject>

#include "core/environment-manager.h"
#include "core/models.h"

using namespace kai::core;

// Resolução da Hierarquia de Variáveis de Ambiente.
class TestEnvironmentManager : public QObject {
    Q_OBJECT

private slots:
    // Cenário exato do caso de uso: Global PORT=3000/NODE_ENV=production,
    // Pasta "E-Commerce" com PORT=8080. Resultado esperado:
    // "Porta: 8080, Env: production".
    void precedenceFolderOverridesGlobal()
    {
        EnvironmentManager env;
        env.setGlobalVars({{"PORT", "3000"}, {"NODE_ENV", "production"}});
        env.setFolderVars({{"PORT", "8080"}});

        const QString output = env.interpolate(QStringLiteral("Porta: {{PORT}}, Env: {{NODE_ENV}}"));
        QCOMPARE(output, QStringLiteral("Porta: 8080, Env: production"));
    }

    void precedenceDynamicOverridesFolderAndGlobal()
    {
        EnvironmentManager env;
        env.setGlobalVars({{"TOKEN", "global-token"}});
        env.setFolderVars({{"TOKEN", "folder-token"}});
        env.setDynamicVar("TOKEN", "dynamic-token");

        QCOMPARE(env.value("TOKEN"), QStringLiteral("dynamic-token"));
    }

    void precedenceParamOverridesEverything()
    {
        EnvironmentManager env;
        env.setGlobalVars({{"ENV_TARGET", "global"}});
        env.setFolderVars({{"ENV_TARGET", "folder"}});
        env.setDynamicVar("ENV_TARGET", "dynamic");
        env.setParamVars({{"ENV_TARGET", "param"}});

        QCOMPARE(env.value("ENV_TARGET"), QStringLiteral("param"));
    }

    // spec 07: variável ausente em qualquer escopo -> fallback "" sem crash.
    void missingVariableFallsBackToEmptyString()
    {
        EnvironmentManager env;
        env.setGlobalVars({{"KNOWN", "value"}});

        QCOMPARE(env.value("UNKNOWN_VAR"), QString());
        QVERIFY(!env.contains("UNKNOWN_VAR"));

        const QString output = env.interpolate(QStringLiteral("X={{UNKNOWN_VAR}}Y"));
        QCOMPARE(output, QStringLiteral("X=Y"));
    }

    void interpolateHandlesMultiplePlaceholders()
    {
        EnvironmentManager env;
        env.setGlobalVars({{"A", "1"}, {"B", "2"}});

        const QString output = env.interpolate(QStringLiteral("{{A}}-{{B}}-{{A}}"));
        QCOMPARE(output, QStringLiteral("1-2-1"));
    }

    void dynamicVarCanBeUpdatedIncrementally()
    {
        EnvironmentManager env;
        env.setDynamicVar("AUTH_TOKEN", "secret_jwt_xyz123");

        QCOMPARE(env.value("AUTH_TOKEN"), QStringLiteral("secret_jwt_xyz123"));

        const QString output = env.interpolate(QStringLiteral("Bearer Token: {{AUTH_TOKEN}}"));
        QCOMPARE(output, QStringLiteral("Bearer Token: secret_jwt_xyz123"));
    }

    // Escopo por pasta-projeto (Folder::isProject): dois projetos usando o
    // MESMO nome de variável dinâmica não podem se sobrescrever.
    void dynamicVarsAreIsolatedPerScope()
    {
        EnvironmentManager env;
        env.setDynamicVarScope("project-a");
        env.setDynamicVar("TOKEN", "token-a");
        env.setDynamicVarScope("project-b");
        env.setDynamicVar("TOKEN", "token-b");

        env.setDynamicVarScope("project-a");
        QCOMPARE(env.value("TOKEN"), QStringLiteral("token-a"));
        env.setDynamicVarScope("project-b");
        QCOMPARE(env.value("TOKEN"), QStringLiteral("token-b"));

        // Escopo Global ("") não vê NENHUM dos dois — escopos não herdam
        // entre si (feedback do usuário: evitar ambiguidade).
        env.setDynamicVarScope(QString());
        QVERIFY(!env.contains("TOKEN"));
    }

    void clearDynamicVarsOnlyAffectsThatScope()
    {
        EnvironmentManager env;
        env.setDynamicVarScope("project-a");
        env.setDynamicVar("TOKEN", "token-a");
        env.setDynamicVarScope("project-b");
        env.setDynamicVar("TOKEN", "token-b");

        env.clearDynamicVars("project-a");

        env.setDynamicVarScope("project-a");
        QVERIFY(!env.contains("TOKEN"));
        env.setDynamicVarScope("project-b");
        QCOMPARE(env.value("TOKEN"), QStringLiteral("token-b"));
    }

    void clearAllDynamicVarsWipesEveryScope()
    {
        EnvironmentManager env;
        env.setDynamicVarScope("project-a");
        env.setDynamicVar("TOKEN", "token-a");
        env.setDynamicVarScope(QString());
        env.setDynamicVar("GLOBAL_TOKEN", "g");

        env.clearAllDynamicVars();

        QVERIFY(env.allDynamicVars().isEmpty());
        env.setDynamicVarScope("project-a");
        QVERIFY(!env.contains("TOKEN"));
    }

    void allDynamicVarsListsEveryScopeForInspection()
    {
        EnvironmentManager env;
        env.setDynamicVarScope("project-a");
        env.setDynamicVar("TOKEN", "token-a");
        env.setDynamicVarScope(QString());
        env.setDynamicVar("GLOBAL_TOKEN", "g");

        const QMap<QString, QMap<QString, QString>> all = env.allDynamicVars();
        QCOMPARE(all.value("project-a").value("TOKEN"), QStringLiteral("token-a"));
        QCOMPARE(all.value(QString()).value("GLOBAL_TOKEN"), QStringLiteral("g"));
    }

    // EnvExtractor::persist (feedback do usuário: refresh token/API key não
    // deveria exigir reautenticar a cada boot) — round-trip JSON.
    void envExtractorPersistRoundTripsThroughJson()
    {
        EnvExtractor e;
        e.jsonPath = "data.token";
        e.envVar = "AUTH_TOKEN";
        e.persist = true;
        const EnvExtractor back = EnvExtractor::fromJson(e.toJson());
        QCOMPARE(back.jsonPath, e.jsonPath);
        QCOMPARE(back.envVar, e.envVar);
        QCOMPARE(back.persist, true);
    }

    // ExecutionCondition::enabled — round-trip JSON e default true pra
    // kai.json antigos sem o campo (feedback do usuário: toggle por linha).
    void executionConditionEnabledRoundTripsThroughJson()
    {
        ExecutionCondition c;
        c.left = "{{TOKEN}}";
        c.op = "exists";
        c.enabled = false;
        const ExecutionCondition back = ExecutionCondition::fromJson(c.toJson());
        QCOMPARE(back.enabled, false);

        QJsonObject legacy;
        legacy["left"] = "{{TOKEN}}";
        legacy["op"] = "exists";
        const ExecutionCondition fromLegacy = ExecutionCondition::fromJson(legacy);
        QCOMPARE(fromLegacy.enabled, true);
    }

    // kai.json antigos (sem o campo "persist") continuam efêmeros.
    void envExtractorPersistDefaultsFalseWhenMissingFromJson()
    {
        QJsonObject obj;
        obj["json_path"] = "data.token";
        obj["env_var"] = "AUTH_TOKEN";
        const EnvExtractor e = EnvExtractor::fromJson(obj);
        QCOMPARE(e.persist, false);
    }

    void seedPersistedDynamicVarsRestoresAtBoot()
    {
        EnvironmentManager env;
        QMap<QString, QMap<QString, QString>> persisted;
        persisted["project-a"] = {{"REFRESH_TOKEN", "restored-value"}};
        env.seedPersistedDynamicVars(persisted);

        env.setDynamicVarScope("project-a");
        QCOMPARE(env.value("REFRESH_TOKEN"), QStringLiteral("restored-value"));
    }

    // Coleções como parâmetro: o replace aceita nomes com ponto,
    // ex {{usuarios.email}}, injetados como "param.campo" pelo MainWindow
    // a partir da entrada escolhida da coleção.
    void interpolateSupportsDottedCollectionFieldNames()
    {
        EnvironmentManager env;
        env.setParamVars({{"usuarios.email", "alice@ex.com"},
                          {"usuarios.value", "Alice"}});

        const QString output = env.interpolate(
            QStringLiteral("enviar para {{usuarios.email}} ({{usuarios.value}})"));
        QCOMPARE(output, QStringLiteral("enviar para alice@ex.com (Alice)"));
    }

    // Feature 5: variáveis dinâmicas/faker resolvidas na interpolação.
    void dynamicUuidIsResolved()
    {
        EnvironmentManager env;
        const QString out = env.interpolate(QStringLiteral("id={{$uuid}}"));
        // Formato uuid: 8-4-4-4-12 hex.
        static const QRegularExpression re(
            QStringLiteral("^id=[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
        QVERIFY2(re.match(out).hasMatch(), qPrintable(out));
    }

    void dynamicTimestampIsNumeric()
    {
        EnvironmentManager env;
        const QString out = env.interpolate(QStringLiteral("{{$timestamp}}"));
        bool ok = false;
        const qlonglong v = out.toLongLong(&ok);
        QVERIFY(ok);
        QVERIFY(v > 1000000000LL); // depois de 2001
    }

    void dynamicRandomIntRespectsBound()
    {
        EnvironmentManager env;
        for (int i = 0; i < 50; ++i) {
            const QString out = env.interpolate(QStringLiteral("{{$randomInt.10}}"));
            bool ok = false;
            const int v = out.toInt(&ok);
            QVERIFY(ok);
            QVERIFY(v >= 0 && v < 10);
        }
    }

    void unknownDynamicFallsBackEmpty()
    {
        EnvironmentManager env;
        QCOMPARE(env.interpolate(QStringLiteral("a{{$naoexiste}}b")), QStringLiteral("ab"));
    }

    void twoUuidsDiffer()
    {
        EnvironmentManager env;
        const QString a = env.interpolate(QStringLiteral("{{$uuid}}"));
        const QString b = env.interpolate(QStringLiteral("{{$uuid}}"));
        QVERIFY(a != b);
    }

    // --- Templates avançados: {% if %}/{% else %}/{% endif %} ------------
    // Caso de uso real do pedido do usuário: um parâmetro Select (A/B)
    // decide qual URL/valor entra no comando final.

    void ifTrueBranchChoosesTrueSide()
    {
        EnvironmentManager env;
        env.setParamVars({{"ENV_TARGET", "prod"}});
        const QString out = env.interpolate(QStringLiteral(
            R"({% if ENV_TARGET == "prod" %}https://api.prod.com{% else %}https://api.dev.com{% endif %}/users)"));
        QCOMPARE(out, QStringLiteral("https://api.prod.com/users"));
    }

    void ifFalseBranchChoosesElseSide()
    {
        EnvironmentManager env;
        env.setParamVars({{"ENV_TARGET", "staging"}});
        const QString out = env.interpolate(QStringLiteral(
            R"({% if ENV_TARGET == "prod" %}https://api.prod.com{% else %}https://api.dev.com{% endif %}/users)"));
        QCOMPARE(out, QStringLiteral("https://api.dev.com/users"));
    }

    void ifFalseWithoutElseBecomesEmpty()
    {
        EnvironmentManager env;
        env.setParamVars({{"DROP_DB", "false"}});
        const QString out = env.interpolate(QStringLiteral("a{% if DROP_DB %}--drop-first{% endif %}b"));
        QCOMPARE(out, QStringLiteral("ab"));
    }

    // Operando no formato {{var}} DENTRO da condição (o usuário escreve
    // {% if {{t}} == "s" %}, não {% if t == "s" %}). Antes o "{{t}}" era
    // buscado literal (com chaves), virava vazio e sempre caía no else
    // (bug relatado). Deve resolver a variável e escolher o ramo certo.
    void ifConditionAcceptsBracedVariableTrue()
    {
        EnvironmentManager env;
        env.setParamVars({{"t", "s"}});
        const QString out = env.interpolate(QStringLiteral(
            R"({% if {{t}} == "s" %}sim{% else %}nao{% endif %})"));
        QCOMPARE(out, QStringLiteral("sim"));
    }

    void ifConditionAcceptsBracedVariableFalse()
    {
        EnvironmentManager env;
        env.setParamVars({{"t", "x"}});
        const QString out = env.interpolate(QStringLiteral(
            R"({% if {{t}} == "s" %}sim{% else %}nao{% endif %})"));
        QCOMPARE(out, QStringLiteral("nao"));
    }

    void ifTruthyAcceptsNonEmptyNonFalseZero()
    {
        EnvironmentManager env;
        env.setParamVars({{"DROP_DB", "true"}});
        QCOMPARE(env.interpolate(QStringLiteral("{% if DROP_DB %}yes{% else %}no{% endif %}")),
                  QStringLiteral("yes"));

        env.setParamVars({{"DROP_DB", "0"}});
        QCOMPARE(env.interpolate(QStringLiteral("{% if DROP_DB %}yes{% else %}no{% endif %}")),
                  QStringLiteral("no"));

        env.setParamVars({{"DROP_DB", ""}});
        QCOMPARE(env.interpolate(QStringLiteral("{% if DROP_DB %}yes{% else %}no{% endif %}")),
                  QStringLiteral("no"));
    }

    void ifComparesNumericallyWhenBothSidesAreNumbers()
    {
        EnvironmentManager env;
        env.setParamVars({{"PORT", "8080"}});
        QCOMPARE(env.interpolate(QStringLiteral("{% if PORT > 1000 %}alto{% else %}baixo{% endif %}")),
                  QStringLiteral("alto"));
        QCOMPARE(env.interpolate(QStringLiteral("{% if PORT >= 8080 %}sim{% else %}nao{% endif %}")),
                  QStringLiteral("sim"));
        QCOMPARE(env.interpolate(QStringLiteral("{% if PORT < 1000 %}baixo{% else %}alto{% endif %}")),
                  QStringLiteral("alto"));
    }

    void ifOrderOperatorOnNonNumericStringFallsBackFalse()
    {
        EnvironmentManager env;
        env.setParamVars({{"ENV_TARGET", "prod"}});
        // "prod" não é número: operador de ordem cai no fallback seguro
        // (falso), sem crash.
        QCOMPARE(env.interpolate(QStringLiteral(R"({% if ENV_TARGET > "1" %}sim{% else %}nao{% endif %})")),
                  QStringLiteral("nao"));
    }

    void ifSupportsSingleAndDoubleQuotedLiterals()
    {
        EnvironmentManager env;
        env.setParamVars({{"ENV_TARGET", "prod"}});
        QCOMPARE(env.interpolate(QStringLiteral(R"({% if ENV_TARGET == 'prod' %}sim{% else %}nao{% endif %})")),
                  QStringLiteral("sim"));
    }

    void ifNestedInElseActsLikeElif()
    {
        EnvironmentManager env;
        env.setParamVars({{"ENV_TARGET", "staging"}});
        const QString tpl = QStringLiteral(
            R"({% if ENV_TARGET == "prod" %}PROD{% else %}{% if ENV_TARGET == "staging" %}STAGING{% else %}DEV{% endif %}{% endif %})");
        QCOMPARE(env.interpolate(tpl), QStringLiteral("STAGING"));

        env.setParamVars({{"ENV_TARGET", "dev"}});
        QCOMPARE(env.interpolate(tpl), QStringLiteral("DEV"));
    }

    void ifMalformedWithoutEndifPreservesTextWithoutCrashing()
    {
        EnvironmentManager env;
        const QString out = env.interpolate(QStringLiteral("a{% if X %}b"));
        QCOMPARE(out, QStringLiteral("a{% if X %}b"));
    }

    void ifBranchContentStillInterpolatesVars()
    {
        EnvironmentManager env;
        env.setParamVars({{"ENV_TARGET", "prod"}, {"TOKEN", "secret123"}});
        const QString out = env.interpolate(QStringLiteral(
            R"({% if ENV_TARGET == "prod" %}Bearer {{TOKEN}}{% endif %})"));
        QCOMPARE(out, QStringLiteral("Bearer secret123"));
    }

    // --- evaluateCondition (Command::executionConditions) ---

    void conditionExistsIsFalseForMissingVar()
    {
        EnvironmentManager env;
        QVERIFY(!env.evaluateCondition(QStringLiteral("{{TOKEN}}"), QStringLiteral("exists"), QString()));
    }

    void conditionNotExistsIsTrueForMissingVar()
    {
        EnvironmentManager env;
        QVERIFY(env.evaluateCondition(QStringLiteral("{{TOKEN}}"), QStringLiteral("not_exists"), QString()));
    }

    void conditionNotExistsIsFalseWhenVarIsSet()
    {
        EnvironmentManager env;
        env.setDynamicVar("TOKEN", "abc123");
        QVERIFY(!env.evaluateCondition(QStringLiteral("{{TOKEN}}"), QStringLiteral("not_exists"), QString()));
    }

    // Caso motivador do usuário: "rodar hook de login se TOKEN for nulo OU
    // EXPIRES_AT for menor que agora" — aqui só a metade numérica, via
    // {{$timestamp}} (epoch "agora" embutido).
    void conditionLessThanNowIsTrueForPastTimestamp()
    {
        EnvironmentManager env;
        env.setDynamicVar("EXPIRES_AT", "1000000000"); // ano 2001, certamente no passado
        QVERIFY(env.evaluateCondition(QStringLiteral("{{EXPIRES_AT}}"), QStringLiteral("lt"), QStringLiteral("{{$timestamp}}")));
    }

    void conditionLessThanNowIsFalseForFutureTimestamp()
    {
        EnvironmentManager env;
        env.setDynamicVar("EXPIRES_AT", "9999999999"); // ano 2286
        QVERIFY(!env.evaluateCondition(QStringLiteral("{{EXPIRES_AT}}"), QStringLiteral("lt"), QStringLiteral("{{$timestamp}}")));
    }

    void conditionEqualsComparesNumericallyWhenBothSidesAreNumbers()
    {
        EnvironmentManager env;
        QVERIFY(env.evaluateCondition(QStringLiteral("1"), QStringLiteral("eq"), QStringLiteral("1.0")));
    }

    void conditionGreaterThanFallsBackFalseForNonNumericStrings()
    {
        EnvironmentManager env;
        QVERIFY(!env.evaluateCondition(QStringLiteral("abc"), QStringLiteral("gt"), QStringLiteral("def")));
    }

    void conditionContainsChecksSubstring()
    {
        EnvironmentManager env;
        env.setDynamicVar("ENV_NAME", "staging-prod");
        QVERIFY(env.evaluateCondition(QStringLiteral("{{ENV_NAME}}"), QStringLiteral("contains"), QStringLiteral("prod")));
        QVERIFY(!env.evaluateCondition(QStringLiteral("{{ENV_NAME}}"), QStringLiteral("not_contains"), QStringLiteral("prod")));
    }
};

QTEST_MAIN(TestEnvironmentManager)
#include "test_environment_manager.moc"
