#include <QTest>

#include <QApplication>
#include <QGuiApplication>
#include <QTabWidget>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include "core/config-manager.h"
#include "core/yaml-bridge.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/main-window.h"
#include "utils/translation-manager.h"

using namespace kai;
using namespace kai::ui;

namespace {

core::Command shellCommand(const QString &id, const QString &folderId, const QString &name)
{
    core::Command c;
    c.id = id;
    c.folderId = folderId;
    c.name = name;
    c.type = core::CommandType::Command;
    c.command = QStringLiteral("echo ") + name;
    return c;
}

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList commandNames()
{
    QStringList names;
    for (const core::Command &c : core::ConfigManager().loadCommands().commands) {
        names << c.name;
    }
    names.sort();
    return names;
}

} // namespace

// A ação "Sincronizar com o arquivo" do menu de pasta-projeto, de ponta a
// ponta com a janela real: plano em segundo plano, caixas de aviso/pergunta
// (respondidas aqui por um timer, como o usuário faria) e persistência.
class TestProjectSyncMainWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;
    QTemporaryDir m_project;
    QTimer m_dialogPump;
    QStringList m_dialogTexts;
    QList<bool> m_dialogWasQuestion;
    bool m_answerYes = true;

    void seed(const QVector<core::Command> &commands, bool projectHasDirectory = true)
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::CommandsData data;
        core::Folder project;
        project.id = QStringLiteral("proj");
        project.name = QStringLiteral("Meu Projeto");
        project.isProject = true;
        if (projectHasDirectory) {
            project.workingDirMode = core::WorkingDirMode::Custom;
            project.workingDir = m_project.path();
        }
        data.folders << project;
        data.commands = commands;
        QVERIFY(core::ConfigManager().saveCommands(data));
    }

    static void sync(MainWindow &window, bool kaiToFile)
    {
        QVERIFY(QMetaObject::invokeMethod(&window, "startProjectSync", Qt::QueuedConnection,
                                          Q_ARG(QString, QStringLiteral("proj")), Q_ARG(bool, kaiToFile)));
    }

    QString projectFile(const QString &name = QStringLiteral("kai.yml")) const { return m_project.filePath(name); }

private slots:
    void initTestCase()
    {
        // Responde as caixas modais: guarda o texto e clica Sim (ou Não) / OK.
        m_dialogPump.setInterval(20);
        connect(&m_dialogPump, &QTimer::timeout, this, [this]() {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) {
                return;
            }
            m_dialogTexts << box->text();
            QAbstractButton *yes = box->button(QMessageBox::Yes);
            m_dialogWasQuestion << (yes != nullptr);
            if (yes) {
                (m_answerYes ? yes : box->button(QMessageBox::No))->click();
            } else if (auto *ok = box->button(QMessageBox::Ok)) {
                ok->click();
            }
        });
        m_dialogPump.start();
    }

    void init()
    {
        QDir(m_config.path()).removeRecursively();
        QDir().mkpath(m_config.path());
        QDir(m_project.path()).removeRecursively();
        QDir().mkpath(m_project.path());
        m_dialogTexts.clear();
        m_dialogWasQuestion.clear();
        m_answerYes = true;
    }

    // A pasta sincronizada mostra um destaque (tooltip na aba) quando o arquivo muda por fora e o
    // Kai ainda não tem; o destaque some depois de sincronizar Arquivo -> Kai.
    void aFolderGetsFlaggedWhenTheFileChangesAndClearsAfterSyncing()
    {
        seed({shellCommand(QStringLiteral("c1"), QStringLiteral("proj"), QStringLiteral("Raiz"))});
        MainWindow window;
        window.show();
        auto *tree = window.findChild<CommandTreeWidget *>();
        QVERIFY(tree);
        auto *tabs = tree->findChild<QTabWidget *>();
        QVERIFY(tabs);
        auto activateApp = [&]() { emit qGuiApp->applicationStateChanged(Qt::ApplicationActive); };

        sync(window, true);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(projectFile()), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 1, 8000);
        QTest::qWait(900); // o fim do sync agenda a checagem
        QVERIFY2(!tabs->tabToolTip(0).contains(kai::utils::tr(QStringLiteral("tree.sync.file_changed"))), "recém-sincronizada não é destaque");

        // O arquivo muda por fora: ao focar a janela, a aba ganha o destaque.
        QString yaml = readAll(projectFile());
        QVERIFY(yaml.contains(QStringLiteral("Raiz")));
        yaml.replace(QStringLiteral("Raiz"), QStringLiteral("Renomeada"));
        QFile out(projectFile());
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(yaml.toUtf8());
        out.close();
        activateApp();
        QTRY_VERIFY_WITH_TIMEOUT(tabs->tabToolTip(0).contains(kai::utils::tr(QStringLiteral("tree.sync.file_changed"))), 8000);

        // Arquivo -> Kai zera o destaque.
        sync(window, false);
        QTRY_VERIFY_WITH_TIMEOUT(commandNames().contains(QStringLiteral("Renomeada")), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(!tabs->tabToolTip(0).contains(kai::utils::tr(QStringLiteral("tree.sync.file_changed"))), 8000);
    }

    void kaiToFileWritesTheFileThenReportsAlreadyInSync()
    {
        seed({shellCommand(QStringLiteral("c1"), QStringLiteral("proj"), QStringLiteral("Raiz"))});
        MainWindow window;
        window.show();

        sync(window, true);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(projectFile()), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 1, 8000);
        QVERIFY(readAll(projectFile()).contains(QStringLiteral("Raiz")));
        QVERIFY2(m_dialogTexts.first().contains(QStringLiteral("Meu Projeto")), qPrintable(m_dialogTexts.first()));
        QVERIFY(!m_dialogWasQuestion.first()); // arquivo novo: nada a perguntar

        // De novo, sem nada mudar: avisa que já está sincronizado (e a pasta não ficou "ocupada").
        sync(window, true);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 2, 8000);
        QVERIFY(!m_dialogWasQuestion.last());
    }

    void fileToKaiPullsAnExternalChangeIntoTheTreeAndSavesIt()
    {
        seed({shellCommand(QStringLiteral("c1"), QStringLiteral("proj"), QStringLiteral("Raiz"))});
        MainWindow window;
        window.show();
        sync(window, true);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 1, 8000);

        // Alguém edita o arquivo por fora (git pull, editor...): entra mais um comando.
        QJsonObject fileObj;
        {
            bool ok = false;
            fileObj = QJsonDocument::fromJson(
                core::yamlTextToJsonText(readAll(projectFile()), &ok, nullptr).toUtf8()).object();
            QVERIFY(ok);
        }
        QJsonArray cmds = fileObj.value(QStringLiteral("commands")).toArray();
        QJsonObject added = cmds.first().toObject();
        added[QStringLiteral("id")] = QStringLiteral("c_ext");
        added[QStringLiteral("name")] = QStringLiteral("Veio do arquivo");
        cmds.append(added);
        fileObj[QStringLiteral("commands")] = cmds;
        {
            QFile f(projectFile());
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(core::jsonTextToYamlText(QString::fromUtf8(QJsonDocument(fileObj).toJson())).toUtf8());
        }

        sync(window, false);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 2, 8000);
        QVERIFY(!m_dialogWasQuestion.last()); // só o arquivo mudou: seguro, sem perguntar
        QCOMPARE(commandNames(), (QStringList{QStringLiteral("Raiz"), QStringLiteral("Veio do arquivo")}));
    }

    // Nunca sincronizado e com conteúdo nos dois lados: pergunta; "Não" não muda nada.
    void declinedConfirmationChangesNothingAndAcceptedReplacesTheFolder()
    {
        seed({shellCommand(QStringLiteral("c1"), QStringLiteral("proj"), QStringLiteral("So no Kai"))});
        {
            core::CommandsData other;
            core::Folder project;
            project.id = QStringLiteral("proj");
            project.name = QStringLiteral("Meu Projeto");
            other.folders << project;
            other.commands << shellCommand(QStringLiteral("c9"), QStringLiteral("proj"), QStringLiteral("So no arquivo"));
            QFile f(projectFile());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(core::jsonTextToYamlText(
                core::ConfigManager::exportFolder(QStringLiteral("proj"), other, {}, {}, false)).toUtf8());
        }

        MainWindow window;
        window.show();

        m_answerYes = false;
        sync(window, false);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 1, 8000);
        QVERIFY(m_dialogWasQuestion.first());
        QCOMPARE(commandNames(), QStringList{QStringLiteral("So no Kai")});

        m_answerYes = true;
        sync(window, false);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 3, 8000); // pergunta + conclusão
        QVERIFY(m_dialogWasQuestion.at(1));
        QCOMPARE(commandNames(), QStringList{QStringLiteral("So no arquivo")});
    }

    // O caso do dia a dia: um kai.yml escrito à mão (sem kai_export) puxado pro Kai.
    void fileToKaiReadsAHandWrittenKaiYmlAndLeavesItUntouched()
    {
        seed({});
        const QString handWritten = QStringLiteral(R"YAML(project_name: "Meu Projeto"
commands:
  - name: "Build"
    type: "command"
    command: "make"
  - name: "Deploy"
    type: "command"
    command: "deploy"
    folder: "Ops"
)YAML");
        {
            QFile f(projectFile());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(handWritten.toUtf8());
        }
        MainWindow window;
        window.show();

        sync(window, false);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 1, 8000);
        QVERIFY(!m_dialogWasQuestion.first()); // pasta vazia: sem perguntar
        QCOMPARE(commandNames(), (QStringList{QStringLiteral("Build"), QStringLiteral("Deploy")}));
        QCOMPARE(readAll(projectFile()), handWritten);

        // Sem mudanças nos dois lados: já sincronizado.
        sync(window, false);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 2, 8000);
        QVERIFY(!m_dialogWasQuestion.last());
    }

    void kaiToFileOverAnUnsyncedFileAsksAndDecliningKeepsTheFile()
    {
        seed({shellCommand(QStringLiteral("c1"), QStringLiteral("proj"), QStringLiteral("Raiz"))});
        const QString original = QStringLiteral("commands: []\nfolders: []\nnote: \"escrito à mão\"\n");
        {
            QFile f(projectFile());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(original.toUtf8());
        }
        MainWindow window;
        window.show();

        m_answerYes = false;
        sync(window, true);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 1, 8000);
        QVERIFY(m_dialogWasQuestion.first());
        QCOMPARE(readAll(projectFile()), original); // intacto
    }

    void aFolderWithoutAWorkingDirectoryExplainsInsteadOfDoingNothing()
    {
        seed({shellCommand(QStringLiteral("c1"), QStringLiteral("proj"), QStringLiteral("Raiz"))}, /*projectHasDirectory=*/false);
        MainWindow window;
        window.show();
        sync(window, true);
        QTRY_VERIFY_WITH_TIMEOUT(m_dialogTexts.size() == 1, 8000);
        QVERIFY2(!m_dialogTexts.first().startsWith(QStringLiteral("project_sync.")), "texto não traduzido");
        QVERIFY(!m_dialogWasQuestion.first());
        QVERIFY(m_project.isValid() && QDir(m_project.path()).entryList(QDir::Files).isEmpty());
    }
};

QTEST_MAIN(TestProjectSyncMainWindow)
#include "test_project_sync_main_window.moc"
