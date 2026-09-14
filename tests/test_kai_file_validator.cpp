#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "core/kai-file-validator.h"
#include "core/yaml-bridge.h"
#include "core/models.h"

using namespace kai::core;

// Os fixtures são JSON por legibilidade; o validador lê YAML (o único formato de arquivo). Texto que
// não é JSON (um YAML escrito de propósito, ou lixo) vai como está.
static ValidationResult validateJson(const QString &text)
{
    const bool isJson = !QJsonDocument::fromJson(text.toUtf8()).isNull();
    return validateKaiFileText(isJson ? jsonTextToYamlText(text) : text);
}

// Validador estrutural por trás de "kai validate".
class TestKaiFileValidator : public QObject {
    Q_OBJECT

private slots:
    // options_script é uma chave conhecida (sem aviso) e precisa ser texto.
    void optionsScriptIsAKnownTextKey()
    {
        const QString ok = QStringLiteral(R"({"commands": [ {"name": "X", "type": "command", "command": "echo",
            "params": [ {"name": "EXT", "type": "select", "options_script": "ls extensions"} ]} ]})");
        const ValidationResult good = validateJson(ok);
        QVERIFY(!good.hasErrors());
        QCOMPARE(good.warningCount(), 0);

        const QString bad = QStringLiteral(R"({"commands": [ {"name": "X", "type": "command", "command": "echo",
            "params": [ {"name": "EXT", "type": "select", "options_script": 42} ]} ]})");
        QVERIFY(validateJson(bad).hasErrors());
    }

    void validProjectManifestHasNoIssues()
    {
        const QString json = QStringLiteral(R"({
            "project_name": "Demo",
            "icon": "rocket",
            "commands": [
                {"name": "Build", "type": "shell", "command": "make", "folder": "Build"}
            ],
            "folders": [
                {"path": "Build", "icon": "hammer"}
            ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    // "command" é o tipo; "shell" (o nome anterior) segue válido.
    void commandTypeAndTheLegacyShellAreBothAccepted()
    {
        for (const char *type : {"command", "shell"}) {
            const QString json = QStringLiteral(R"({"commands": [ {"name": "X", "type": "%1", "command": "echo hi"} ]})")
                                     .arg(QLatin1String(type));
            const ValidationResult result = validateJson(json);
            QVERIFY2(!result.hasErrors(), type);
            QCOMPARE(result.warningCount(), 0);
        }
        // Um command sem texto continua sendo erro, como o shell era.
        QVERIFY(validateJson(QStringLiteral(R"({"commands": [ {"name": "X", "type": "command"} ]})")).hasErrors());
    }

    // `initial_dir` de um parâmetro (a "pasta inicial" foi cortada) em arquivo antigo: aceito, sem aviso, e ignorado.
    void anOldParameterInitialDirIsStillAcceptedAndIgnored()
    {
        const QString json = QStringLiteral(
            R"({"commands": [ {"name": "X", "type": "command", "command": "ls", "params": [
                 {"name": "f", "type": "file", "initial_dir": "/srv/certs"} ]} ]})");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
        const kai::core::Parameter p = kai::core::Parameter::fromJson(
            QJsonDocument::fromJson(json.toUtf8()).object().value("commands").toArray().at(0).toObject()
                .value("params").toArray().at(0).toObject());
        QCOMPARE(p.name, QStringLiteral("f"));
        QVERIFY(!p.toJson().contains(QStringLiteral("initial_dir")));
    }

    void languageAndInterpreterAreValidated()
    {
        const auto validate = [](const QString &extra) {
            return validateJson(
                QStringLiteral(R"JSON({"commands": [ {"name": "X", "type": "command", "command": "print(1)", %1} ]})JSON").arg(extra));
        };
        for (const char *language : {"native", "python", "node"}) {
            const ValidationResult ok = validate(QStringLiteral(R"("language": "%1")").arg(QLatin1String(language)));
            QVERIFY2(!ok.hasErrors(), language);
            QCOMPARE(ok.warningCount(), 0);
        }
        // Linguagem desconhecida é erro (typo "pyhton" não pode virar shell em silêncio).
        QVERIFY(validate(QStringLiteral(R"("language": "pyhton")")).hasErrors());

        // `interpreter` só vale com python/node.
        QCOMPARE(validate(QStringLiteral(R"("interpreter": "uv run python")")).warningCount(), 1);
        QCOMPARE(validate(QStringLiteral(R"("language": "python", "interpreter": "uv run python")")).warningCount(), 0);

        // `capture_env` não faz nada em python/node.
        QCOMPARE(validate(QStringLiteral(R"("language": "node", "capture_env": true)")).warningCount(), 1);
        QCOMPARE(validate(QStringLiteral(R"("capture_env": true)")).warningCount(), 0);
    }

    // working_dir: texto (próprio), null (nenhum) e ausente (herda) são válidos,
    // em comandos E em pastas; outro tipo seria lido como "herdar" em silêncio.
    void workingDirAcceptsStringAndNullButNotOtherTypes()
    {
        const auto command = [](const QString &extra) {
            return validateJson(
                QStringLiteral(R"JSON({"commands": [ {"name": "X", "type": "command", "command": "ls", %1} ]})JSON").arg(extra));
        };
        const auto folder = [](const QString &extra) {
            return validateJson(
                QStringLiteral(R"JSON({"folders": [ {"name": "F", %1} ], "commands": []})JSON").arg(extra));
        };
        for (const char *value : {"\"/srv/app\"", "\"api\"", "null"}) {
            const QString field = QStringLiteral(R"("working_dir": %1)").arg(QLatin1String(value));
            QVERIFY2(!command(field).hasErrors(), value);
            QCOMPARE(command(field).warningCount(), 0);
            QVERIFY2(!folder(field).hasErrors(), value);
            QCOMPARE(folder(field).warningCount(), 0);
        }
        QVERIFY(command(QStringLiteral(R"("working_dir": 42)")).hasErrors());
        QVERIFY(command(QStringLiteral(R"("working_dir": ["a"])")).hasErrors());
        QVERIFY(folder(QStringLiteral(R"("working_dir": true)")).hasErrors());
    }

    // actions (por pasta): lista de nomes de comando; chave conhecida, outro tipo é erro.
    void folderActionsAreKnownAndMustBeAListOfStrings()
    {
        const auto folder = [](const QString &extra) {
            return validateJson(
                QStringLiteral(R"JSON({"folders": [ {"name": "F", %1} ], "commands": []})JSON").arg(extra));
        };
        const ValidationResult ok = folder(QStringLiteral(R"("actions": ["Fetch", "Pull"])"));
        QVERIFY(!ok.hasErrors());
        QCOMPARE(ok.warningCount(), 0);
        QVERIFY(folder(QStringLiteral(R"("actions": "Fetch")")).hasErrors());
        QVERIFY(folder(QStringLiteral(R"("actions": [1, 2])")).hasErrors());
    }

    void cliWorkingDirIsKnownAndAnInvalidValueIsFlagged()
    {
        const auto validate = [](const QString &extra) {
            return validateJson(
                QStringLiteral(R"JSON({"commands": [ {"name": "X", "type": "command", "command": "ls", %1} ]})JSON").arg(extra));
        };
        for (const char *mode : {"default", "invocation"}) {
            const ValidationResult ok = validate(QStringLiteral(R"("cli_working_dir": "%1")").arg(QLatin1String(mode)));
            QVERIFY2(!ok.hasErrors(), mode);
            QCOMPARE(ok.warningCount(), 0);
        }
        QVERIFY(validate(QStringLiteral(R"("cli_working_dir": "invokation")")).hasErrors());
    }

    // cron_expression/cron_notify_on_run são chaves de verdade do comando: não podem
    // virar "chave desconhecida", e um cron inválido nunca dispara — o validador avisa.
    void cronKeysAreKnownAndAnInvalidExpressionIsFlagged()
    {
        const auto validate = [](const QString &extra) {
            return validateJson(
                QStringLiteral(R"JSON({"commands": [ {"name": "X", "type": "command", "command": "echo hi", %1} ]})JSON").arg(extra));
        };
        for (const char *cron : {"0 9 * * 1-5", "*/15 * * * *", "30 2 1 * *", "0 8 * * mon,fri"}) {
            const ValidationResult ok = validate(QStringLiteral(R"("cron_expression": "%1", "cron_notify_on_run": true)").arg(QLatin1String(cron)));
            QVERIFY2(!ok.hasErrors() && ok.warningCount() == 0, cron);
        }
        const ValidationResult bad = validate(QStringLiteral(R"("cron_expression": "every day")"));
        QVERIFY(!bad.hasErrors());
        QCOMPARE(bad.warningCount(), 1);
        QVERIFY(validate(QStringLiteral(R"("cron_expression": "99 * * * *")")).warningCount() == 1);
    }

    void kipAutoCloseIsKnownAndWarnsWithoutKip()
    {
        const auto validate = [](const QString &extra) {
            return validateJson(
                QStringLiteral(R"JSON({"commands": [ {"name": "X", "type": "command", "command": "echo hi", %1} ]})JSON").arg(extra));
        };
        const ValidationResult ok = validate(QStringLiteral(R"("kip": true, "kip_window": true, "kip_auto_close": true, "kip_auto_close_delay_sec": 0)"));
        QVERIFY(!ok.hasErrors());
        QCOMPARE(ok.warningCount(), 0);
        QCOMPARE(validate(QStringLiteral(R"("kip_auto_close": true)")).warningCount(), 1);
    }

    void kipIsAcceptedOnACodeLanguageCommand()
    {
        const ValidationResult result = validateJson(QStringLiteral(
            R"({"commands": [ {"name": "X", "type": "command", "language": "python", "kip": true, "command": "import kip"} ]})"));
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    void validYamlEquivalentHasNoIssues()
    {
        const QString yaml = QStringLiteral(
            "project_name: \"Demo\"\n"
            "commands:\n"
            "  - name: \"Build\"\n"
            "    type: \"shell\"\n"
            "    command: \"make\"\n");
        const ValidationResult result = validateJson(yaml);
        QVERIFY(!result.hasErrors());
    }

    void missingRequiredCommandFieldsAreErrors()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"folder": "X"} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
        bool foundName = false, foundType = false;
        for (const ValidationIssue &issue : result.issues) {
            if (issue.message.contains(QStringLiteral("'name'"))) foundName = true;
            if (issue.message.contains(QStringLiteral("'type'"))) foundType = true;
        }
        QVERIFY(foundName);
        QVERIFY(foundType);
    }

    void shellCommandWithoutCommandFieldIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell"} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    void httpCommandRequiresHttpConfigUrl()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "http", "http_config": {"method": "GET"}} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    void invalidEnumValueIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "P", "type": "not-a-real-type"}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    void unknownTopLevelKeyIsWarningNotError()
    {
        const QString json = QStringLiteral(R"({
            "projectt_name": "typo"
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 1);
    }

    void unknownCommandKeyIsWarning()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "commnad_typo": "oops"} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 1);
    }

    void rootMustBeObject()
    {
        const ValidationResult result = validateJson(QStringLiteral("[1, 2, 3]"));
        QVERIFY(result.hasErrors());
    }

    void malformedJsonIsParseError()
    {
        const ValidationResult result = validateJson(QStringLiteral("{ not valid json"));
        QVERIFY(result.hasErrors());
    }

    void folderEntryNeedsPathOrName()
    {
        const QString json = QStringLiteral(R"({
            "folders": [ {"icon": "server"} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    void collectionFieldInvalidTypeIsError()
    {
        const QString json = QStringLiteral(R"({
            "collections": [ {"name": "Users", "schema": [{"name": "email", "type": "bogus"}]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    void validDateParameterHasNoIssues()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "quando", "type": "date", "date_mode": "datetime", "date_range": true,
                 "date_format": "custom", "date_format_custom": "dd.MM.yy HH:mm"}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    void validGridParameterHasNoIssues()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "p", "type": "grid", "row_template": "{{sku}}", "row_separator": ",",
                 "columns": [{"name": "sku"}, {"name": "qty", "type": "number", "default": "1"},
                             {"name": "when", "type": "date", "date_mode": "datetime"}]}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    void gridWithoutColumnsIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "p", "type": "grid"}
            ]} ]
        })");
        QVERIFY(validateJson(json).hasErrors());
    }

    void gridColumnWithAnUnknownTypeIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "p", "type": "grid", "columns": [{"name": "a", "type": "bogus"}]}
            ]} ]
        })");
        QVERIFY(validateJson(json).hasErrors());
    }

    void gridColumnWithAnUnknownKeyIsWarned()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "p", "type": "grid", "columns": [{"name": "a", "colour": "red"}]}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 1);
    }

    void invalidDateModeIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "quando", "type": "date", "date_mode": "bogus"}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    void invalidDateFormatIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "quando", "type": "date", "date_format": "bogus"}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    // "group" (agrupamento opcional de parâmetros): string qualquer aceita sem aviso de chave desconhecida.
    void validGroupOnParameterHasNoIssues()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "timeout", "type": "number", "group": "Avançado"}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    // "step" (etapas do formulário de execução): uma string qualquer é aceita sem aviso.
    void validStepOnParameterHasNoIssues()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "user", "type": "text", "step": "Access"},
                {"name": "retries", "type": "number", "step": "Tuning"}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    // "required" (opt-in): aceito sem aviso de chave desconhecida.
    void validRequiredOnParameterHasNoIssues()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "ambiente", "type": "text", "required": true}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    // "textarea"/"json" já eram tipos válidos no app mas faltavam no enum checado aqui: um kai.json real
    // com eles era sinalizado como erro.
    void textareaAndJsonParameterTypesAreValid()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "a", "type": "textarea"},
                {"name": "b", "type": "json"}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
    }

    void cliPathIsAcceptedAsKnownKey()
    {
        const QString json = QStringLiteral(R"({
            "cli_path": "zephyr",
            "folders": [ {"path": "Zaphyr", "cli_path": "zephyr"} ],
            "commands": [ {"name": "Env", "type": "shell", "command": "up", "folder": "Zaphyr", "cli_path": "env"} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    // Duas pastas diferentes, ambas na raiz (mesmo escopo de CLI), com o mesmo cli_path: colisão real.
    void duplicateCliPathAtSameLevelIsError()
    {
        const QString json = QStringLiteral(R"({
            "folders": [
                {"path": "A", "cli_path": "x"},
                {"path": "B", "cli_path": "x"}
            ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    // Mesmo cli_path em escopos diferentes (pais distintos com cli_path próprio): não colide.
    void sameCliPathInDifferentScopesIsNotError()
    {
        const QString json = QStringLiteral(R"({
            "folders": [
                {"path": "A", "cli_path": "a"},
                {"path": "B", "cli_path": "b"}
            ],
            "commands": [
                {"name": "X1", "type": "shell", "command": "echo 1", "folder": "A", "cli_path": "run"},
                {"name": "X2", "type": "shell", "command": "echo 2", "folder": "B", "cli_path": "run"}
            ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
    }

    // Pasta transparente (sem cli_path) não impede a checagem dos filhos, que sobem ao escopo do ancestral
    // opt-in mais próximo (ou à raiz).
    void collisionIsDetectedThroughTransparentFolder()
    {
        const QString json = QStringLiteral(R"({
            "folders": [
                {"path": "API Vendas", "icon": "server"},
                {"path": "API Vendas/Zaphyr", "cli_path": "zephyr"}
            ],
            "commands": [
                {"name": "X1", "type": "shell", "command": "echo 1", "folder": "API Vendas/Zaphyr", "cli_path": "env"},
                {"name": "X2", "type": "shell", "command": "echo 2", "folder": "API Vendas/Zaphyr", "cli_path": "env"}
            ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    void cliPathCollidingWithReservedVerbIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo 1", "cli_path": "validate"} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(result.hasErrors());
    }

    void parameterDescriptionIsAcceptedAsKnownKey()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "env", "type": "select", "options": ["dev", "prod"], "description": "Ambiente alvo."}
            ]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }
    void kipCommandAloneHasNoWarnings()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "Deploy", "type": "shell", "command": "deploy --kip", "kip": true, "kip_window": true,
                           "declared_env_vars": [{"name": "TOKEN"}]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    void kipWithIncompatibleFeaturesWarnsOncePerFeature()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "Deploy", "type": "shell", "command": "deploy --kip", "kip": true,
                           "interactive_terminal": true, "formatted_output": true,
                           "compact_output": true, "open_last_link": true, "capture_env": true,
                           "is_background": true, "auto_run": true, "cron_expression": "* * * * *",
                           "responders": [{"pattern": "x", "response": "y"}]} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        // 9 recursos incompatíveis; cron_expression também não é chave conhecida do manifesto (+1 aviso).
        int kipWarnings = 0;
        for (const ValidationIssue &issue : result.issues) {
            if (issue.message.contains(QStringLiteral("kip"))) {
                ++kipWarnings;
            }
        }
        QCOMPARE(kipWarnings, 9);
    }

    void kipFeaturesOffDoNotWarnWithoutKip()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "Htop", "type": "shell", "command": "htop", "interactive_terminal": true,
                           "is_background": true} ]
        })");
        QCOMPARE(validateJson(json).warningCount(), 0);
    }

    void kipOnHttpCommandWarns()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "Ping", "type": "http", "kip": true, "http_config": {"url": "x.y", "method": "GET"}} ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 1);
    }

    void kipWindowWithoutKipWarns()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "x", "kip_window": true} ]
        })");
        QCOMPARE(validateJson(json).warningCount(), 1);
    }

    void kipCommandUsedAsHookWarns()
    {
        const QString json = QStringLiteral(R"({
            "commands": [
                {"name": "Wizard", "type": "shell", "command": "wiz --kip", "kip": true},
                {"name": "Build", "type": "shell", "command": "make", "hooks": {"pre": ["Wizard"], "cleanup": ["Wizard"]}}
            ]
        })");
        const ValidationResult result = validateJson(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 2);
    }

    void kipYamlManifestIsAccepted()
    {
        const QString yaml = QStringLiteral(
            "commands:\n"
            "  - name: \"Deploy\"\n"
            "    type: \"shell\"\n"
            "    command: \"deploy --kip\"\n"
            "    kip: true\n"
            "    kip_window: true\n");
        const ValidationResult result = validateJson(yaml);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    // YAML: número/true sem aspas onde o Kai lê TEXTO vira vazio em silêncio — o validador tem que avisar.
    void unquotedNumbersAndBooleansWhereKaiReadsTextAreErrors()
    {
        const QString yaml = QStringLiteral(
            "env_vars:\n  PORT: 8080\n  DEBUG: true\n"
            "commands:\n"
            "  - name: A\n    type: command\n    command: x\n"
            "    params:\n"
            "      - name: N\n        type: select\n        default: 3\n        options: [1, 2]\n"
            "  - name: B\n    type: http\n    http_config:\n      url: http://x\n      headers:\n        X-Retries: 3\n"
            "collections:\n  - name: C\n    entries:\n      - values:\n          key: 10\n");
        const ValidationResult result = validateJson(yaml);
        QStringList paths;
        for (const ValidationIssue &issue : result.issues) {
            QVERIFY2(issue.severity == ValidationSeverity::Error, qPrintable(issue.path + issue.message));
            paths << issue.path;
        }
        for (const char *expected : {"env_vars.PORT", "env_vars.DEBUG", "commands[0].params[0]", "commands[1].http_config.headers.X-Retries",
                                     "collections[0].entries[0].values.key"}) {
            QVERIFY2(paths.contains(QLatin1String(expected)), expected);
        }
        // Com aspas, nada a reclamar (e um default true num parâmetro bool continua valendo).
        const QString fixed = QStringLiteral(
            "env_vars:\n  PORT: \"8080\"\n"
            "commands:\n  - name: A\n    type: command\n    command: x\n    params:\n"
            "      - name: N\n        type: bool\n        default: true\n"
            "      - name: M\n        type: select\n        default: \"3\"\n        options: [\"1\", \"2\"]\n");
        QVERIFY2(validateJson(fixed).issues.isEmpty(), "quoted values and a bool default are fine");
    }

    // Flags e números entre aspas são ignorados pelo Kai (ficam no padrão) — erro no validador.
    void quotedFlagsAndNumbersAreErrors()
    {
        const ValidationResult result = validateJson(QStringLiteral(
            "commands:\n  - name: A\n    type: command\n    command: x\n"
            "    is_background: \"true\"\n    order: \"2\"\n    auto_run_delay_sec: \"5\"\n"));
        QCOMPARE(result.errorCount(), 3);
        QVERIFY(!validateJson(QStringLiteral(
            "commands:\n  - name: A\n    type: command\n    command: x\n"
            "    is_background: true\n    order: 2\n    auto_run_delay_sec: 5\n")).hasErrors());
    }

    // O QUERY existe no app e no manifesto; um validador que o recusasse reprovaria um arquivo bom. E o `order`
    // de uma coleção (lido pelo importador) não é "chave desconhecida".
    void queryMethodAndCollectionOrderAreAccepted()
    {
        const ValidationResult result = validateJson(QStringLiteral(
            "commands:\n  - name: Q\n    type: http\n    http_config:\n      method: QUERY\n      url: http://x\n"
            "collections:\n  - name: C\n    order: 1\n    hidden: true\n"));
        QVERIFY2(result.issues.isEmpty(), result.issues.isEmpty() ? "" : qPrintable(result.issues.first().message));
    }

    // Um bloco `|` (multilinha) é lido inteiro: o comando chega e o arquivo valida sem avisos.
    void blockScalarCommandsValidate()
    {
        const ValidationResult result = validateJson(QStringLiteral(
            "commands:\n  - name: A\n    type: command\n    command: |\n      npm ci\n      npm test\n    is_background: true\n"));
        QVERIFY(result.issues.isEmpty());
    }
};

QTEST_MAIN(TestKaiFileValidator)
#include "test_kai_file_validator.moc"
