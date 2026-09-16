#include <QTest>

#include "core/kai-file-validator.h"

using namespace kai::core;

// Cobre o validador estrutural por trás de "kai validate" (pedido do
// usuário: "kai validate file, pra yml e json").
class TestKaiFileValidator : public QObject {
    Q_OBJECT

private slots:
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
        const ValidationResult result = validateKaiFileText(json);
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
        const ValidationResult result = validateKaiFileText(yaml);
        QVERIFY(!result.hasErrors());
    }

    void missingRequiredCommandFieldsAreErrors()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"folder": "X"} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
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
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    void httpCommandRequiresHttpConfigUrl()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "http", "http_config": {"method": "GET"}} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    void invalidEnumValueIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "P", "type": "not-a-real-type"}
            ]} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    void unknownTopLevelKeyIsWarningNotError()
    {
        const QString json = QStringLiteral(R"({
            "projectt_name": "typo"
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 1);
    }

    void unknownCommandKeyIsWarning()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "commnad_typo": "oops"} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 1);
    }

    void rootMustBeObject()
    {
        const ValidationResult result = validateKaiFileText(QStringLiteral("[1, 2, 3]"));
        QVERIFY(result.hasErrors());
    }

    void malformedJsonIsParseError()
    {
        const ValidationResult result = validateKaiFileText(QStringLiteral("{ not valid json"));
        QVERIFY(result.hasErrors());
    }

    void folderEntryNeedsPathOrName()
    {
        const QString json = QStringLiteral(R"({
            "folders": [ {"icon": "server"} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    void collectionFieldInvalidTypeIsError()
    {
        const QString json = QStringLiteral(R"({
            "collections": [ {"name": "Users", "schema": [{"name": "email", "type": "bogus"}]} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    // --- Parâmetro tipo Date (pedido do usuário) ---

    void validDateParameterHasNoIssues()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "quando", "type": "date", "date_mode": "datetime", "date_range": true,
                 "date_format": "custom", "date_format_custom": "dd.MM.yy HH:mm"}
            ]} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    void invalidDateModeIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "quando", "type": "date", "date_mode": "bogus"}
            ]} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    void invalidDateFormatIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "quando", "type": "date", "date_format": "bogus"}
            ]} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    // "group" (agrupamento opcional de parâmetros, pedido do usuário): uma
    // string qualquer é aceita sem gerar aviso de chave desconhecida.
    void validGroupOnParameterHasNoIssues()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "timeout", "type": "number", "group": "Avançado"}
            ]} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    // REGRESSÃO encontrada ao adicionar "date": "textarea"/"json" já eram
    // tipos de parâmetro válidos no app, mas nunca entraram no enum
    // checado aqui — um kai.json real usando qualquer um dos dois era
    // sinalizado como erro por este validador.
    void textareaAndJsonParameterTypesAreValid()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "a", "type": "textarea"},
                {"name": "b", "type": "json"}
            ]} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(!result.hasErrors());
    }

    // --- CLI Paths ---------------------------------------------------------

    void cliPathIsAcceptedAsKnownKey()
    {
        const QString json = QStringLiteral(R"({
            "cli_path": "zephyr",
            "folders": [ {"path": "Zaphyr", "cli_path": "zephyr"} ],
            "commands": [ {"name": "Env", "type": "shell", "command": "up", "folder": "Zaphyr", "cli_path": "env"} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }

    // Duas pastas DIFERENTES, mas ambas na RAIZ (mesmo escopo de CLI) usando
    // o mesmo cli_path — colisão real, um dos dois nunca seria alcançável.
    void duplicateCliPathAtSameLevelIsError()
    {
        const QString json = QStringLiteral(R"({
            "folders": [
                {"path": "A", "cli_path": "x"},
                {"path": "B", "cli_path": "x"}
            ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    // Mesmo cli_path, mas em ESCOPOS DIFERENTES (dentro de pastas-pai
    // distintas, cada uma com seu próprio cli_path) — não é colisão, porque
    // o caminho completo de CLI é diferente em cada caso.
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
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(!result.hasErrors());
    }

    // Uma pasta TRANSPARENTE (sem cli_path próprio) não impede a checagem
    // de colisão dos filhos dela — os filhos "sobem" pro escopo do
    // ancestral opt-in mais próximo (ou a raiz), exatamente como na
    // resolução de verdade.
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
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    void cliPathCollidingWithReservedVerbIsError()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo 1", "cli_path": "validate"} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(result.hasErrors());
    }

    void parameterDescriptionIsAcceptedAsKnownKey()
    {
        const QString json = QStringLiteral(R"({
            "commands": [ {"name": "X", "type": "shell", "command": "echo hi", "params": [
                {"name": "env", "type": "select", "options": ["dev", "prod"], "description": "Ambiente alvo."}
            ]} ]
        })");
        const ValidationResult result = validateKaiFileText(json);
        QVERIFY(!result.hasErrors());
        QCOMPARE(result.warningCount(), 0);
    }
};

QTEST_MAIN(TestKaiFileValidator)
#include "test_kai_file_validator.moc"
