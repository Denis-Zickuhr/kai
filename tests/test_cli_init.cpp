#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "cli/cli-init.h"
#include "core/kai-file-validator.h"
#include "core/yaml-bridge.h"

using namespace kai;

// `kai init`: gera um kai.yml a partir da detecção genérica do projeto — o
// arquivo tem que passar no `kai validate` e ter cli_path em tudo.
class TestCliInit : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString &path, const QByteArray &content)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
    }

private slots:
    void slugIsLowercaseAsciiWithHyphens()
    {
        QCOMPARE(cli::cliSlug(QStringLiteral("Subir tudo (docker compose up)")), QStringLiteral("subir-tudo-docker-compose-up"));
        QCOMPARE(cli::cliSlug(QStringLiteral("Ver logs — Serviço")), QStringLiteral("ver-logs-servico"));
        QCOMPARE(cli::cliSlug(QStringLiteral("build:prod")), QStringLiteral("build-prod"));
    }

    void detectedProjectBecomesAValidManifest()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("package.json")),
                  "{\"name\":\"x\",\"scripts\":{\"dev\":\"vite\",\"build:prod\":\"vite build\","
                  "\"test\":\"echo \\\"# ok\\\"\"}}");
        writeFile(dir.filePath(QStringLiteral("docker-compose.yml")), "services:\n  api:\n    image: x\n");

        const cli::InitManifest m = cli::buildInitManifest(dir.path());
        QVERIFY(m.ecosystems.contains(QStringLiteral("npm")));
        QVERIFY(m.commandCount >= 3);

        const core::ValidationResult validation = core::validateKaiFileText(m.text);
        QVERIFY2(!validation.hasErrors(), qPrintable(m.text));
        QCOMPARE(validation.warningCount(), 0);

        // Relido pelo nosso parser YAML: todo comando com cli_path, e o
        // comando npm é o `npm run <script>` (o corpo do script fica no
        // package.json).
        bool ok = false;
        QString err;
        const QJsonObject root = QJsonDocument::fromJson(core::yamlTextToJsonText(m.text, &ok, &err).toUtf8()).object();
        QVERIFY2(ok, qPrintable(err));
        QStringList cliPaths;
        QString testCommand;
        for (const QJsonValue &v : root.value(QStringLiteral("commands")).toArray()) {
            const QJsonObject c = v.toObject();
            QVERIFY(!c.value(QStringLiteral("cli_path")).toString().isEmpty());
            cliPaths << c.value(QStringLiteral("cli_path")).toString();
            if (c.value(QStringLiteral("name")).toString() == QStringLiteral("test")) {
                testCommand = c.value(QStringLiteral("command")).toString();
            }
        }
        QVERIFY(cliPaths.contains(QStringLiteral("build-prod")));
        QCOMPARE(testCommand, QStringLiteral("npm run test"));
    }

    void emptyProjectGetsAnExample()
    {
        QTemporaryDir dir;
        const cli::InitManifest m = cli::buildInitManifest(dir.path());
        QVERIFY(m.ecosystems.isEmpty());
        bool ok = false;
        const QJsonObject root = QJsonDocument::fromJson(core::yamlTextToJsonText(m.text, &ok).toUtf8()).object();
        QVERIFY(ok);
        QCOMPARE(root.value(QStringLiteral("commands")).toArray().first().toObject()
                     .value(QStringLiteral("cli_path")).toString(), QStringLiteral("hello"));
        QVERIFY(!core::validateKaiFileText(m.text).hasErrors());
    }
};

QTEST_MAIN(TestCliInit)
#include "test_cli_init.moc"
