#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "ui/features/collections/project-detection-strategy.h"
#include "ui/features/collections/project-selector.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

namespace {
void writeFile(const QString &path, const QByteArray &content)
{
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(content);
    f.close();
}

const ProjectDetectionStrategy *findStrategy(
    const std::vector<std::unique_ptr<ProjectDetectionStrategy>> &strategies, const QString &name)
{
    for (const auto &s : strategies) {
        if (s->name() == name) {
            return s.get();
        }
    }
    return nullptr;
}
} // namespace

// IMPORTAÇÃO GENÉRICA (feedback do usuário): flag opcional em "Importar
// Projeto" que reconhece definições de execução em formatos que não são
// kai.json. Cada ecossistema é uma ProjectDetectionStrategy isolada — este
// arquivo cobre cada uma individualmente e o fluxo integrado via
// ProjectSelector::importFromDirectory.
class TestProjectDetectionStrategy : public QObject {
    Q_OBJECT

private slots:
    void npmStrategyGeneratesOneCommandPerScript()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("package.json")), R"json({
            "name": "app",
            "scripts": {"build": "tsc", "test": "jest"}
        })json");

        const auto strategies = allDetectionStrategies();
        const auto *npm = findStrategy(strategies, QStringLiteral("npm"));
        QVERIFY(npm);
        QVERIFY(npm->appliesTo(QDir(dir.path())));

        const QVector<DetectedCommand> found = npm->detect(QDir(dir.path()));
        QCOMPARE(found.size(), 2);
        bool hasBuild = false, hasTest = false;
        for (const DetectedCommand &dc : found) {
            if (dc.command.name == QStringLiteral("build")) {
                hasBuild = true;
                QCOMPARE(dc.command.command, QStringLiteral("npm run build"));
            }
            if (dc.command.name == QStringLiteral("test")) {
                hasTest = true;
            }
        }
        QVERIFY(hasBuild);
        QVERIFY(hasTest);
    }

    void npmStrategyPrefersYarnWhenLockfilePresent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("package.json")), R"json({"scripts": {"dev": "vite"}})json");
        writeFile(dir.filePath(QStringLiteral("yarn.lock")), "");

        const auto strategies = allDetectionStrategies();
        const auto *npm = findStrategy(strategies, QStringLiteral("npm"));
        const QVector<DetectedCommand> found = npm->detect(QDir(dir.path()));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.at(0).command.command, QStringLiteral("yarn run dev"));
    }

    void dockerComposeStrategyParsesServiceNames()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("docker-compose.yml")), R"yaml(
services:
  web:
    image: nginx
    ports:
      - "80:80"
  db:
    image: postgres
)yaml");

        const auto strategies = allDetectionStrategies();
        const auto *compose = findStrategy(strategies, QStringLiteral("Docker Compose"));
        QVERIFY(compose);
        QVERIFY(compose->appliesTo(QDir(dir.path())));

        const QVector<DetectedCommand> found = compose->detect(QDir(dir.path()));
        // "Subir tudo" + "Parar tudo" + 2 serviços (logs cada um).
        QCOMPARE(found.size(), 4);
        bool hasWebLogs = false, hasDbLogs = false, hasUp = false, hasDown = false;
        for (const DetectedCommand &dc : found) {
            if (dc.command.name.contains(QStringLiteral("web"))) hasWebLogs = true;
            if (dc.command.name.contains(QStringLiteral("db"))) hasDbLogs = true;
            if (dc.command.command == QStringLiteral("docker compose up -d")) hasUp = true;
            if (dc.command.command == QStringLiteral("docker compose down")) hasDown = true;
        }
        QVERIFY(hasWebLogs);
        QVERIFY(hasDbLogs);
        QVERIFY(hasUp);
        QVERIFY(hasDown);
    }

    void dockerComposeStrategyIgnoresMalformedFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("docker-compose.yml")), QByteArrayLiteral("not: yaml: at: all: {{{"));

        const auto strategies = allDetectionStrategies();
        const auto *compose = findStrategy(strategies, QStringLiteral("Docker Compose"));
        // Sem bloco "services:" reconhecível: nenhum comando, sem crash.
        QVERIFY(compose->detect(QDir(dir.path())).isEmpty());
    }

    void pythonStrategyDetectsDjangoProject()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("requirements.txt")), "django\n");
        writeFile(dir.filePath(QStringLiteral("manage.py")), "");

        const auto strategies = allDetectionStrategies();
        const auto *python = findStrategy(strategies, QStringLiteral("Python"));
        QVERIFY(python->appliesTo(QDir(dir.path())));

        const QVector<DetectedCommand> found = python->detect(QDir(dir.path()));
        bool hasInstall = false, hasRunserver = false, hasMigrate = false;
        for (const DetectedCommand &dc : found) {
            if (dc.command.command.contains(QStringLiteral("pip install -r"))) hasInstall = true;
            if (dc.command.command.contains(QStringLiteral("runserver"))) hasRunserver = true;
            if (dc.command.command.contains(QStringLiteral("migrate"))) hasMigrate = true;
        }
        QVERIFY(hasInstall);
        QVERIFY(hasRunserver);
        QVERIFY(hasMigrate);
    }

    void composerStrategyAlwaysIncludesInstallPlusScripts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("composer.json")), R"json({"scripts": {"test": "phpunit"}})json");

        const auto strategies = allDetectionStrategies();
        const auto *composer = findStrategy(strategies, QStringLiteral("Composer"));
        const QVector<DetectedCommand> found = composer->detect(QDir(dir.path()));
        QCOMPARE(found.size(), 2);
        bool hasInstall = false, hasTest = false;
        for (const DetectedCommand &dc : found) {
            if (dc.command.command == QStringLiteral("composer install")) hasInstall = true;
            if (dc.command.name == QStringLiteral("test")) hasTest = true;
        }
        QVERIFY(hasInstall);
        QVERIFY(hasTest);
    }

    void makefileStrategyExtractsTargetsAndIgnoresVariables()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("Makefile")), QByteArrayLiteral(
            "CC := gcc\n"
            "build:\n"
            "\t$(CC) -o app main.c\n"
            "test:\n"
            "\t./run_tests.sh\n"
            ".PHONY: build test\n"
            "%.o: %.c\n"
            "\t$(CC) -c $<\n"));

        const auto strategies = allDetectionStrategies();
        const auto *make = findStrategy(strategies, QStringLiteral("Makefile"));
        const QVector<DetectedCommand> found = make->detect(QDir(dir.path()));

        QStringList names;
        for (const DetectedCommand &dc : found) {
            names << dc.command.name;
        }
        QVERIFY2(names.contains(QStringLiteral("build")), "target 'build' deveria ser detectado");
        QVERIFY2(names.contains(QStringLiteral("test")), "target 'test' deveria ser detectado");
        QVERIFY2(!names.contains(QStringLiteral("CC")), "'CC := gcc' é variável, não target");
        // Regra implícita "%.o: %.c" não vira comando (sem '%' nos nomes).
        for (const QString &n : names) {
            QVERIFY(!n.contains(QLatin1Char('%')));
        }
    }

    // --- Fluxo integrado (ProjectSelector) ---

    void importFromDirectoryDetectsGenericDefinitionsWithoutKaiJson()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // SEM kai.json — só um package.json.
        writeFile(dir.filePath(QStringLiteral("package.json")), R"json({"scripts": {"start": "node index.js"}})json");

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(
            dir.path(), QString(), /*detectGenericDefinitions=*/true);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.detectedEcosystems.contains(QStringLiteral("npm")));
        QCOMPARE(result.commands.size(), 1);
        QCOMPARE(result.commands.at(0).name, QStringLiteral("start"));
        // Vai para uma subpasta própria (npm), não direto na raiz do projeto.
        QVERIFY(result.commands.at(0).folderId != result.folder.id);
        QCOMPARE(result.subFolders.size(), 1);
        QCOMPARE(result.subFolders.at(0).name, QStringLiteral("npm"));
    }

    void importFromDirectoryWithoutKaiJsonAndWithoutFlagStillErrors()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("package.json")), R"json({"scripts": {"start": "node index.js"}})json");

        ProjectSelector selector;
        // SEM a flag: comportamento antigo preservado (kai.json obrigatório).
        const ProjectImportResult result = selector.importFromDirectory(dir.path());
        QVERIFY(!result.success);
    }

    void importFromDirectoryMergesKaiJsonAndDetectedCommandsInSeparateFolders()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("kai.json")), R"json({
            "project_name": "Misto",
            "commands": [{"name": "Comando do kai.json", "type": "shell", "command": "echo oi"}]
        })json");
        writeFile(dir.filePath(QStringLiteral("package.json")), R"json({"scripts": {"start": "node index.js"}})json");

        ProjectSelector selector;
        const ProjectImportResult result = selector.importFromDirectory(
            dir.path(), QString(), /*detectGenericDefinitions=*/true);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commands.size(), 2);

        QString kaiCmdFolder, npmCmdFolder;
        for (const Command &c : result.commands) {
            if (c.name == QStringLiteral("Comando do kai.json")) kaiCmdFolder = c.folderId;
            if (c.name == QStringLiteral("start")) npmCmdFolder = c.folderId;
        }
        // O comando do kai.json fica na RAIZ do projeto; o detectado (npm)
        // numa subpasta própria — nunca se misturam.
        QCOMPARE(kaiCmdFolder, result.folder.id);
        QVERIFY(!npmCmdFolder.isEmpty());
        QVERIFY(npmCmdFolder != result.folder.id);
    }
};

QTEST_MAIN(TestProjectDetectionStrategy)
#include "test_project_detection_strategy.moc"
