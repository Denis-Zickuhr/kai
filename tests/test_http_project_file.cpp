#include <QTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/config-manager.h"
#include "core/http-resolve.h"
#include "core/kai-file-validator.h"
#include "ui/features/collections/project-selector.h"

using namespace kai;
using namespace kai::core;

// O que uma pasta oferece aos comandos HTTP viaja nos arquivos: kai.yml de projeto, exportação e (por consequência) a sincronização.
class TestHttpProjectFile : public QObject {
    Q_OBJECT

private:
    static ui::ProjectImportResult importYaml(const QString &yaml)
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("kai.yml")));
        file.open(QIODevice::WriteOnly);
        file.write(yaml.toUtf8());
        file.close();
        ui::ProjectSelector selector;
        return selector.importFromDirectory(dir.path(), QString(), false);
    }

private slots:
    void projectDefaultsAndSubfolderDefaultsAreImported()
    {
        const ui::ProjectImportResult r = importYaml(QStringLiteral(
            "http_defaults:\n"
            "  headers:\n"
            "    - name: \"Accept\"\n"
            "      value: \"application/json\"\n"
            "  auth:\n"
            "    type: \"bearer\"\n"
            "    token: \"{{T}}\"\n"
            "  options:\n"
            "    timeout_sec: 20\n"
            "folders:\n"
            "  - path: \"Public\"\n"
            "    http_defaults:\n"
            "      auth:\n"
            "        type: \"none\"\n"
            "commands:\n"
            "  - name: \"Open\"\n"
            "    type: \"http\"\n"
            "    folder: \"Public\"\n"
            "    http_config:\n"
            "      url: \"https://x\"\n"
            "  - name: \"Closed\"\n"
            "    type: \"http\"\n"
            "    http_config:\n"
            "      url: \"https://x\"\n"));
        QVERIFY2(r.success, qPrintable(r.errorMessage));
        QCOMPARE(r.folder.httpDefaults.headers.size(), 1);
        QCOMPARE(r.folder.httpDefaults.auth.type, HttpAuthType::Bearer);
        QCOMPARE(r.folder.httpDefaults.options.timeoutSec.value_or(0), 20);

        QCOMPARE(r.subFolders.size(), 1);
        QCOMPARE(r.subFolders.first().httpDefaults.auth.type, HttpAuthType::None);

        // O comando da subpasta pública não autentica; o outro herda o bearer da raiz.
        QVector<Folder> all = r.subFolders;
        all.prepend(r.folder);
        for (const Command &c : r.commands) {
            const HttpRequestSpec spec = resolveHttpRequest(*c.httpConfig, folderChain(c.folderId, all));
            if (c.name == QStringLiteral("Open")) {
                QCOMPARE(spec.auth.type, HttpAuthType::None);
            } else {
                QCOMPARE(spec.auth.type, HttpAuthType::Bearer);
            }
            QCOMPARE(spec.headers.size(), 1);
            QCOMPARE(spec.options.timeoutSec, 20);
        }
    }

    void theShippedSampleImportsAndValidates()
    {
        QFile file(QStringLiteral("sample/http/kai.yml"));
        if (!file.open(QIODevice::ReadOnly)) {
            QSKIP("run from the repository root (needs sample/http/kai.yml)");
        }
        const QString yaml = QString::fromUtf8(file.readAll());
        const ValidationResult validation = validateKaiFileText(yaml);
        QVERIFY2(!validation.hasErrors() && validation.issues.isEmpty(), "the sample should validate with no warnings");

        const ui::ProjectImportResult r = importYaml(yaml);
        QVERIFY2(r.success, qPrintable(r.errorMessage));
        QVERIFY(r.commands.size() >= 10);
        QCOMPARE(r.folder.httpDefaults.auth.type, HttpAuthType::Bearer);
    }

    void parameterStepsSurviveTheYamlImport()
    {
        const ui::ProjectImportResult r = importYaml(QStringLiteral(
            "commands:\n"
            "  - name: \"Wizard\"\n"
            "    type: \"command\"\n"
            "    command: \"echo {{a}} {{b}}\"\n"
            "    params:\n"
            "      - name: \"a\"\n"
            "        type: \"text\"\n"
            "        step: \"One\"\n"
            "      - name: \"b\"\n"
            "        type: \"text\"\n"
            "        step: \"Two\"\n"));
        QVERIFY2(r.success, qPrintable(r.errorMessage));
        QCOMPARE(r.commands.size(), 1);
        QCOMPARE(r.commands.first().params.at(0).step, QStringLiteral("One"));
        QCOMPARE(r.commands.first().params.at(1).step, QStringLiteral("Two"));
    }

    void exportAndImportKeepTheFolderDefaults()
    {
        CommandsData data;
        Folder root;
        root.id = QStringLiteral("r");
        root.name = QStringLiteral("Api");
        root.httpDefaults.headers = {HttpEntry{QStringLiteral("X-Client"), QStringLiteral("kai"), true, false}};
        root.httpDefaults.auth.type = HttpAuthType::ApiKey;
        root.httpDefaults.auth.keyName = QStringLiteral("k");
        root.httpDefaults.auth.keyValue = QStringLiteral("{{K}}");
        data.folders << root;
        Command c;
        c.id = QStringLiteral("c1");
        c.folderId = QStringLiteral("r");
        c.name = QStringLiteral("Ping");
        c.type = CommandType::Http;
        HttpConfig cfg;
        cfg.url = QStringLiteral("https://x");
        cfg.setMethodName(QStringLiteral("HEAD"));
        c.httpConfig = cfg;
        data.commands << c;

        const QString exported = ConfigManager::exportFolder(QStringLiteral("r"), data);
        const ConfigManager::ImportResult back = ConfigManager::importFromJson(exported);
        QVERIFY2(back.ok, qPrintable(back.errorMessage));
        QCOMPARE(back.folders.size(), 1);
        QCOMPARE(back.folders.first().httpDefaults.headers.size(), 1);
        QCOMPARE(back.folders.first().httpDefaults.auth.keyName, QStringLiteral("k"));
        QCOMPARE(back.commands.size(), 1);
        QCOMPARE(back.commands.first().httpConfig->method, HttpMethod::Head);
    }

    void validatorChecksTheNewHttpKeys()
    {
        auto issues = [](const QString &httpConfig) {
            return validateKaiFileText(QStringLiteral("commands:\n  - name: A\n    type: http\n    http_config:\n") + httpConfig);
        };
        QVERIFY(!issues(QStringLiteral("      url: \"https://x\"\n      method: \"PROPFIND\"\n")).hasErrors());
        QVERIFY(issues(QStringLiteral("      url: \"https://x\"\n      body_type: \"nonsense\"\n")).hasErrors());
        QVERIFY(issues(QStringLiteral("      url: \"https://x\"\n      auth:\n        type: \"magic\"\n")).hasErrors());
        QVERIFY(issues(QStringLiteral("      url: \"https://x\"\n      options:\n        verify_tls: \"no\"\n")).hasErrors());
        // Cabeçalhos como lista, e ainda como o mapa antigo.
        QVERIFY(!issues(QStringLiteral("      url: \"https://x\"\n      headers:\n        - name: \"A\"\n          value: \"1\"\n")).hasErrors());
        QVERIFY(!issues(QStringLiteral("      url: \"https://x\"\n      headers:\n        A: \"1\"\n")).hasErrors());
        // Segredo escrito direto no arquivo é só um aviso.
        const ValidationResult literal = issues(QStringLiteral("      url: \"https://x\"\n      auth:\n        type: \"bearer\"\n        token: \"abc123\"\n"));
        QVERIFY(!literal.hasErrors());
        QVERIFY(!literal.issues.isEmpty());
        QVERIFY(issues(QStringLiteral("      url: \"https://x\"\n      auth:\n        type: \"bearer\"\n        token: \"{{T}}\"\n")).issues.isEmpty());
    }
};

QTEST_MAIN(TestHttpProjectFile)
#include "test_http_project_file.moc"
