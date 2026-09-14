#include <QTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/config-manager.h"
#include "core/yaml-bridge.h"
#include "ui/main-window.h"

using namespace kai;
using namespace kai::ui;

// `kai import` com um pacote exportado: traz as AÇÕES (e o resto) como o diálogo de "Importar configuração" com tudo
// marcado, sem perguntar nada. Antes só entendia o kai.yml de um projeto e ignorava o que o pacote carregava.
class TestCliImportPackage : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;
    QTemporaryDir m_files;

    static core::CommandsData repoWithActions()
    {
        core::Folder repo;
        repo.id = QStringLiteral("f_repo");
        repo.name = QStringLiteral("Repo");
        repo.actions = {QStringLiteral("c_pull")};
        core::Command pull;
        pull.id = QStringLiteral("c_pull");
        pull.name = QStringLiteral("Pull");
        pull.folderId = QStringLiteral("f_repo");
        pull.command = QStringLiteral("git pull");
        return {{repo}, {pull}};
    }

    QString writePackage()
    {
        const QString json = core::ConfigManager::exportFolder(QStringLiteral("f_repo"), repoWithActions());
        const QString path = m_files.filePath(QStringLiteral("repo-export.yml"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return QString();
        file.write(core::jsonTextToYamlText(json).toUtf8());
        return path;
    }

private slots:
    void init()
    {
        QDir(m_config.path()).removeRecursively();
        QDir().mkpath(m_config.path());
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    }

    void anExportedPackageIsImportedWholeIncludingTheFolderActions()
    {
        const QString path = writePackage();
        QVERIFY(!path.isEmpty());
        MainWindow window;
        QString message;
        QVERIFY2(window.importProjectFromCli(path, {}, message), qPrintable(message));

        const core::CommandsData saved = core::ConfigManager().loadCommands();
        const core::Folder *repo = nullptr;
        for (const core::Folder &f : saved.folders) if (f.name == QStringLiteral("Repo")) repo = &f;
        QVERIFY(repo);
        const core::Command *pull = nullptr;
        for (const core::Command &c : saved.commands) if (c.name == QStringLiteral("Pull")) pull = &c;
        QVERIFY(pull);
        QCOMPARE(repo->actions, QStringList{pull->id}); // a ação voltou ligada ao comando importado
    }

    void onlyChoosesTheCategories()
    {
        const QString path = writePackage();
        MainWindow window;
        QString message;
        QVERIFY2(window.importProjectFromCli(path, {QStringLiteral("collections")}, message), qPrintable(message));
        QVERIFY(core::ConfigManager().loadCommands().folders.isEmpty()); // só coleções: nenhuma pasta veio

        QVERIFY2(window.importProjectFromCli(path, {QStringLiteral("commands")}, message), qPrintable(message));
        QVERIFY(!core::ConfigManager().loadCommands().folders.isEmpty());
    }

    void anUnknownOnlyNameIsRefusedWithTheValidOnes()
    {
        const QString path = writePackage();
        MainWindow window;
        QString message;
        QVERIFY(!window.importProjectFromCli(path, {QStringLiteral("everything")}, message));
        QVERIFY2(message.contains(QStringLiteral("everything")) && message.contains(QStringLiteral("commands")), qPrintable(message));
        QVERIFY(core::ConfigManager().loadCommands().folders.isEmpty());
    }

    void onlyDoesNotApplyToAProjectFile()
    {
        const QString path = m_files.filePath(QStringLiteral("kai.yml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("project_name: \"P\"\ncommands:\n  - name: \"A\"\n    command: \"echo a\"\n");
        file.close();
        MainWindow window;
        QString message;
        QVERIFY(!window.importProjectFromCli(path, {QStringLiteral("commands")}, message));
        QVERIFY(core::ConfigManager().loadCommands().folders.isEmpty());
        // Sem --only continua sendo o import de projeto de sempre.
        QVERIFY2(window.importProjectFromCli(path, {}, message), qPrintable(message));
        QVERIFY(!core::ConfigManager().loadCommands().folders.isEmpty());
    }
};

QTEST_MAIN(TestCliImportPackage)
#include "test_cli_import_package.moc"
