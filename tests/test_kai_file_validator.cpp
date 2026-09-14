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
};

QTEST_MAIN(TestKaiFileValidator)
#include "test_kai_file_validator.moc"
