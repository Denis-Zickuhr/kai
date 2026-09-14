#include <QTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/config-manager.h"
#include "core/kip-settings.h"
#include "ui/external-run-session.h"
#include "ui/features/docs/doc-viewer.h"
#include "ui/features/kip/kip-details-drawer.h"
#include "ui/features/kip/kip-result-card.h"
#include "ui/features/kip/kip-view.h"
#include "ui/features/output/output-panel.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/main-window.h"

using namespace kai;
using namespace kai::ui;

namespace {

QString fixture(const QString &name)
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFile::exists(dir.filePath(QStringLiteral("sample/kip/fixtures/") + name))) {
            return dir.filePath(QStringLiteral("sample/kip/fixtures/") + name);
        }
        if (!dir.cdUp()) break;
    }
    return QString();
}

core::Command kipCommand(const QString &id, const QString &name, const QString &script)
{
    core::Command c;
    c.id = id;
    c.folderId = QStringLiteral("f1");
    c.name = name;
    c.type = core::CommandType::Command;
    c.kip = true;
    c.command = QStringLiteral("bash '%1'").arg(fixture(script));
    return c;
}

} // namespace

// A fiação da feature KIP no MainWindow, de ponta a ponta com processos reais:
// painel de saída em modo KIP, respostas lembradas, janela própria, `kai -g`.
class TestKipMainWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;

    void seed(const QVector<core::Command> &commands)
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager config;
        core::CommandsData data;
        core::Folder folder;
        folder.id = QStringLiteral("f1");
        folder.name = QStringLiteral("KIP");
        data.folders << folder;
        data.commands = commands;
        QVERIFY(config.saveCommands(data));
    }

    static void activate(MainWindow &window, const QString &id)
    {
        QVERIFY(QMetaObject::invokeMethod(&window, "handleCommandActivated", Qt::DirectConnection, Q_ARG(QString, id)));
    }

private slots:
    void init()
    {
        QDir(m_config.path()).removeRecursively();
        QDir().mkpath(m_config.path());
    }

    void clickingAKipCommandShowsTheViewAndAnswersAreRemembered()
    {
        seed({kipCommand(QStringLiteral("deploy"), QStringLiteral("Deploy"), QStringLiteral("happy.sh"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        QVERIFY(drawer);
        activate(window, QStringLiteral("deploy"));

        QTRY_VERIFY_WITH_TIMEOUT(drawer->kipMode(), 8000);
        KipView *view = drawer->embeddedPanel()->kipView();
        QVERIFY(view->session());
        QTRY_VERIFY_WITH_TIMEOUT(view->session()->screen().awaitingInput(), 8000);
        view->flushRefresh();
        QVERIFY(view->editorFor(QStringLiteral("env")));
        // Sem o campo de resposta de texto do terminal no meio do caminho.
        QVERIFY(view->isVisible());

        auto *session = view->session();
        session->setFieldValue(QStringLiteral("env"), QStringLiteral("prod"));
        QVERIFY(session->submit());
        QTRY_VERIFY_WITH_TIMEOUT(session->screen().openScreen() && session->screen().openScreen()->id == QStringLiteral("sure")
                                     && session->screen().awaitingInput(), 8000);
        QVERIFY(session->confirm(true));
        QTRY_VERIFY_WITH_TIMEOUT(session->isTerminal(), 8000);
        QCOMPARE(session->state(), core::KipSessionState::Finished);

        // A resposta aceita foi gravada no comando (e sobrevive a reabrir o app).
        QTRY_VERIFY_WITH_TIMEOUT(
            core::ConfigManager().loadCommands().commands.first().kipLastValues.value(QStringLiteral("env/env")).toString()
                == QStringLiteral("prod"), 4000);
        QVERIFY(!core::ConfigManager().loadCommands().commands.first().kipLastValues.contains(QStringLiteral("env/token")));
    }

    // A documentação da pasta (página própria do corpo da Saída) não atrapalha a view KIP: com o README já mostrado,
    // executar um comando KIP troca para a view do app e o modo documento sai.
    void aKipCommandReplacesTheFolderDocumentationInTheOutput()
    {
        QTemporaryDir project;
        QFile readme(project.filePath(QStringLiteral("README.md")));
        QVERIFY(readme.open(QIODevice::WriteOnly));
        readme.write("# Docs da pasta\n\ntexto\n");
        readme.close();

        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager config;
        core::CommandsData data;
        core::Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Root");
        data.folders << root;
        core::Folder folder;
        folder.id = QStringLiteral("f1");
        folder.name = QStringLiteral("KIP");
        folder.parentId = QStringLiteral("root");
        folder.workingDirMode = core::WorkingDirMode::Custom;
        folder.workingDir = project.path();
        data.folders << folder;
        data.commands = {kipCommand(QStringLiteral("deploy"), QStringLiteral("Deploy"), QStringLiteral("happy.sh"))};
        QVERIFY(config.saveCommands(data));

        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = drawer->embeddedPanel();
        QVERIFY(tree->selectCommand(QStringLiteral("f1")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);

        activate(window, QStringLiteral("deploy"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->kipMode(), 8000);
        QVERIFY(!panel->documentMode());
        KipView *view = panel->kipView();
        QVERIFY(view);
        QTRY_VERIFY_WITH_TIMEOUT(view->isVisible(), 3000); // a página do corpo é a da view, não a do documento
        QVERIFY(!panel->docViewer()->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(view->session()->screen().awaitingInput(), 8000);
        // Termina o app antes de destruir a janela (um processo vivo na hora de fechar é outro assunto).
        auto *session = view->session();
        QVERIFY(session->submit());
        QTRY_VERIFY_WITH_TIMEOUT(session->screen().openScreen() && session->screen().openScreen()->id == QStringLiteral("sure")
                                     && session->screen().awaitingInput(), 8000);
        QVERIFY(session->confirm(true));
        QTRY_VERIFY_WITH_TIMEOUT(session->isTerminal(), 8000);
    }

    // Um comando KIP como ação de pasta: a view KIP aparece, a sessão sobrevive ao fim do
    // processo (o estado final fica na tela) e a resposta lembrada vai pro comando de verdade.
    void aKipCommandRunsAsAFolderActionAndRemembersAnswersOnTheRealCommand()
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::CommandsData data;
        core::Folder root;
        root.id = QStringLiteral("f1");
        root.name = QStringLiteral("KIP");
        core::Folder repo;
        repo.id = QStringLiteral("repo");
        repo.name = QStringLiteral("Repo");
        repo.parentId = QStringLiteral("f1");
        repo.actions = {QStringLiteral("deploy")};
        data.folders << root << repo;
        data.commands << kipCommand(QStringLiteral("deploy"), QStringLiteral("Deploy"), QStringLiteral("happy.sh"));
        QVERIFY(core::ConfigManager().saveCommands(data));

        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        QVERIFY(drawer && tree);
        QVERIFY(QMetaObject::invokeMethod(tree, "folderActionActivated", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("deploy")), Q_ARG(QString, QStringLiteral("repo"))));

        QTRY_VERIFY_WITH_TIMEOUT(drawer->kipMode(), 8000);
        KipView *view = drawer->embeddedPanel()->kipView();
        QTRY_VERIFY_WITH_TIMEOUT(view->session() && view->session()->screen().awaitingInput(), 8000);
        auto *session = view->session();
        session->setFieldValue(QStringLiteral("env"), QStringLiteral("prod"));
        QVERIFY(session->submit());
        QTRY_VERIFY_WITH_TIMEOUT(session->screen().openScreen() && session->screen().openScreen()->id == QStringLiteral("sure")
                                     && session->screen().awaitingInput(), 8000);
        QVERIFY(session->confirm(true));
        QTRY_VERIFY_WITH_TIMEOUT(session->isTerminal(), 8000);

        QTRY_VERIFY_WITH_TIMEOUT(
            core::ConfigManager().loadCommands().commands.first().kipLastValues.value(QStringLiteral("env/env")).toString()
                == QStringLiteral("prod"), 4000);
        QVERIFY(!core::ConfigManager().loadCommands().commands.first().id.contains(QLatin1Char('|')));
        QCOMPARE(core::ConfigManager().loadCommands().commands.size(), 1); // nada de comando virtual no disco

        // Depois do fim do processo a view continua com a sessão (o pipeline da ação não foi destruído).
        QTest::qWait(300);
        QCOMPARE(drawer->embeddedPanel()->kipView()->session(), session);
    }

    void rerunPrefillsTheRememberedAnswerAndGetsAFreshSession()
    {
        core::Command c = kipCommand(QStringLiteral("deploy"), QStringLiteral("Deploy"), QStringLiteral("happy.sh"));
        c.kipLastValues.insert(QStringLiteral("env/env"), QStringLiteral("prod"));
        seed({c});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("deploy"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->kipMode(), 8000);
        KipView *view = drawer->embeddedPanel()->kipView();
        QTRY_VERIFY_WITH_TIMEOUT(view->session() && view->session()->screen().awaitingInput(), 8000);
        QCOMPARE(view->session()->screen().currentValues().value("env").toString(), QStringLiteral("prod"));
        view->session()->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(view->session()->isTerminal(), 8000);

        engine::KipSession *first = view->session();
        activate(window, QStringLiteral("deploy")); // "Run again"
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->kipView()->session() != first, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->kipView()->session()->screen().awaitingInput(), 8000);
        drawer->embeddedPanel()->kipView()->session()->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->kipView()->session()->isTerminal(), 8000);
    }

    void unsupportedCommandShowsTheCardInTheOutputPanel()
    {
        seed({kipCommand(QStringLiteral("old"), QStringLiteral("Old tool"), QStringLiteral("unsupported.sh"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("old"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->kipMode(), 8000);
        KipView *view = drawer->embeddedPanel()->kipView();
        QTRY_VERIFY_WITH_TIMEOUT(view->session() && view->session()->state() == core::KipSessionState::Unsupported, 8000);
        view->flushRefresh();
        QVERIFY(view->resultCard()->isVisible());
        QVERIFY(view->details()->isExpanded());
        QVERIFY(view->details()->logText().contains(QStringLiteral("unknown option --kip")));
    }

    void kipWindowFlagOpensTheViewInItsOwnWindowAndLeavesAPlaceholder()
    {
        core::Command c = kipCommand(QStringLiteral("win"), QStringLiteral("Windowed"), QStringLiteral("cancel-polite.sh"));
        c.kipOpenInWindow = true;
        seed({c});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("win"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->hasDetachedWindow(), 8000);
        QVERIFY(drawer->detachedPanel()->kipMode());
        QVERIFY(drawer->embeddedPanel()->kipDetachedPlaceholder());
        QTRY_VERIFY_WITH_TIMEOUT(drawer->detachedPanel()->kipSession()->screen().awaitingInput(), 8000);

        // Fechar a janela devolve a view; o processo segue vivo.
        drawer->detachedPanel()->window()->close();
        QTRY_VERIFY(!drawer->hasDetachedWindow());
        QVERIFY(!drawer->embeddedPanel()->kipDetachedPlaceholder());
        QVERIFY(drawer->embeddedPanel()->kipSession()->processAlive());
        drawer->embeddedPanel()->kipSession()->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->kipSession()->isTerminal(), 8000);
    }

    // Com a view KIP numa janela própria, voltar à janela principal e selecionar uma pasta que mostra README não pode
    // derrubar o app (relatado: crash ao sair da tela do KIP para uma pasta com documentação).
    void selectingAFolderWithDocumentationWhileTheKipIsInItsOwnWindowIsSafe()
    {
        QTemporaryDir project;
        QFile readme(project.filePath(QStringLiteral("README.md")));
        QVERIFY(readme.open(QIODevice::WriteOnly));
        readme.write("# Docs da pasta\n\ntexto\n");
        readme.close();

        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager config;
        core::CommandsData data;
        core::Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Root");
        data.folders << root;
        core::Folder folder;
        folder.id = QStringLiteral("f1");
        folder.name = QStringLiteral("KIP");
        folder.parentId = QStringLiteral("root");
        folder.workingDirMode = core::WorkingDirMode::Custom;
        folder.workingDir = project.path();
        data.folders << folder;
        core::Command c = kipCommand(QStringLiteral("win"), QStringLiteral("Windowed"), QStringLiteral("cancel-polite.sh"));
        c.kipOpenInWindow = true;
        data.commands = {c};
        QVERIFY(config.saveCommands(data));

        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = drawer->embeddedPanel();
        activate(window, QStringLiteral("win"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->hasDetachedWindow(), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(drawer->detachedPanel()->kipSession()->screen().awaitingInput(), 8000);

        QVERIFY(tree->selectCommand(QStringLiteral("win"))); // o comando em foco; depois sai dele para a pasta
        QVERIFY(tree->selectCommand(QStringLiteral("f1")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QVERIFY(!panel->kipMode());
        QVERIFY(drawer->hasDetachedWindow()); // a janela do KIP segue lá, com o processo vivo
        QVERIFY(drawer->detachedPanel()->kipMode());

        // Voltar ao comando traz o KIP de volta ao painel principal (a janela está aberta: placeholder).
        QVERIFY(tree->selectCommand(QStringLiteral("win")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->kipMode(), 4000);
        QVERIFY(!panel->documentMode());

        drawer->detachedPanel()->kipSession()->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(drawer->detachedPanel()->kipSession()->isTerminal(), 8000);
    }

    // O caso do crash relatado, na ordem real: roda um KIP até o fim, destaca a saída (botão "janela própria"), volta à
    // janela principal e seleciona uma pasta com README; depois alterna entre pasta, comando e fecha a janela.
    void detachingAFinishedKipAndThenBrowsingFoldersWithDocumentationIsSafe()
    {
        QTemporaryDir project;
        QFile readme(project.filePath(QStringLiteral("README.md")));
        QVERIFY(readme.open(QIODevice::WriteOnly));
        readme.write("# Docs da pasta\n\ntexto\n");
        readme.close();

        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager config;
        core::CommandsData data;
        core::Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Root");
        data.folders << root;
        core::Folder folder;
        folder.id = QStringLiteral("f1");
        folder.name = QStringLiteral("KIP");
        folder.parentId = QStringLiteral("root");
        folder.workingDirMode = core::WorkingDirMode::Custom;
        folder.workingDir = project.path();
        data.folders << folder;
        data.commands = {kipCommand(QStringLiteral("quick"), QStringLiteral("Quick"), QStringLiteral("quick.sh"))};
        QVERIFY(config.saveCommands(data));

        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputPanel *panel = drawer->embeddedPanel();
        activate(window, QStringLiteral("quick"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->kipMode(), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(panel->kipSession()->isTerminal(), 8000);

        drawer->showDetachedOutput();
        QVERIFY(drawer->hasDetachedWindow());
        QVERIFY(tree->selectCommand(QStringLiteral("quick")));
        QVERIFY(tree->selectCommand(QStringLiteral("f1")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("quick")));
        QTRY_VERIFY_WITH_TIMEOUT(!panel->documentMode(), 4000);
        QVERIFY(tree->selectCommand(QStringLiteral("f1")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->documentMode(), 4000);
        drawer->detachedPanel()->window()->close();
        QTRY_VERIFY(!drawer->hasDetachedWindow());
        QVERIFY(panel->documentMode()); // fechar a janela do KIP não tira a documentação da tela
        QVERIFY(tree->selectCommand(QStringLiteral("quick")));
        QTRY_VERIFY_WITH_TIMEOUT(panel->kipMode(), 4000);
    }

    // kip_auto_close: terminou com SUCESSO e a view está na janela própria -> a janela
    // fecha sozinha depois do atraso. Falha deixa aberta (é onde está o erro).
    void windowClosesByItselfAfterASuccessfulSessionOnlyWhenAsked()
    {
        core::Command closing = kipCommand(QStringLiteral("closing"), QStringLiteral("Closing"), QStringLiteral("quick.sh"));
        closing.kipOpenInWindow = true;
        closing.kipAutoCloseWindow = true;
        closing.kipAutoCloseDelaySec = 0;
        core::Command staying = kipCommand(QStringLiteral("staying"), QStringLiteral("Staying"), QStringLiteral("quick.sh"));
        staying.kipOpenInWindow = true; // sem auto-fechar
        core::Command failing = kipCommand(QStringLiteral("failing"), QStringLiteral("Failing"), QStringLiteral("done-then-fail.sh"));
        failing.kipOpenInWindow = true;
        failing.kipAutoCloseWindow = true;
        failing.kipAutoCloseDelaySec = 0;
        seed({closing, staying, failing});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();

        activate(window, QStringLiteral("closing"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->hasDetachedWindow(), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(!drawer->hasDetachedWindow(), 8000); // fechou sozinha
        QVERIFY(!drawer->embeddedPanel()->kipDetachedPlaceholder());

        activate(window, QStringLiteral("staying"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->hasDetachedWindow(), 8000);
        QTest::qWait(1200);
        QVERIFY2(drawer->hasDetachedWindow(), "sem kip_auto_close a janela fica aberta");
        drawer->detachedPanel()->window()->close();
        QTRY_VERIFY(!drawer->hasDetachedWindow());

        activate(window, QStringLiteral("failing"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->hasDetachedWindow(), 8000);
        QTest::qWait(1500);
        QVERIFY2(drawer->hasDetachedWindow(), "uma falha mantém a janela aberta");
        drawer->detachedPanel()->window()->close();
        QTRY_VERIFY(!drawer->hasDetachedWindow());
    }

    // O atraso é respeitado: a janela ainda está lá antes dele e some depois.
    void autoCloseWaitsForTheConfiguredDelay()
    {
        core::Command c = kipCommand(QStringLiteral("delayed"), QStringLiteral("Delayed"), QStringLiteral("quick.sh"));
        c.kipOpenInWindow = true;
        c.kipAutoCloseWindow = true;
        c.kipAutoCloseDelaySec = 2;
        seed({c});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("delayed"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->hasDetachedWindow(), 8000);
        QTest::qWait(900);
        QVERIFY2(drawer->hasDetachedWindow(), "ainda dentro do atraso de 2 s");
        QTRY_VERIFY_WITH_TIMEOUT(!drawer->hasDetachedWindow(), 6000);
    }

    // A view KIP na janela própria é o que o usuário vê: rodar o comando (`kai -gw`, `kai run`,
    // o módulo kai) NÃO traz a janela principal sozinha. Sem janela própria, continua trazendo.
    void runningAKipCommandInItsOwnWindowDoesNotShowTheMainWindow()
    {
        core::Command windowed = kipCommand(QStringLiteral("w1"), QStringLiteral("Windowed"), QStringLiteral("cancel-polite.sh"));
        windowed.kipOpenInWindow = true;
        core::Command docked = kipCommand(QStringLiteral("d1"), QStringLiteral("Docked"), QStringLiteral("cancel-polite.sh"));
        seed({windowed, docked});
        MainWindow window; // nunca mostrada: o app vive na bandeja
        auto *drawer = window.findChild<TerminalDrawer *>();
        QString error;

        // `kai -gw` num comando KIP comum: janela própria, principal escondida.
        ExternalRunSession *run = window.startExternalRun(QStringLiteral("d1"), {}, error, /*openOutputWindow=*/true);
        QVERIFY2(run, qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->hasDetachedWindow(), 8000);
        QVERIFY2(!window.isVisible(), "-gw não deve trazer a janela principal");
        QPointer<QWidget> own = drawer->detachedPanel()->window();
        QPointer<engine::KipSession> session = drawer->detachedPanel()->kipSession();
        QTRY_VERIFY_WITH_TIMEOUT(session->screen().awaitingInput(), 8000);
        session->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!session || session->isTerminal(), 8000);
        if (own) own->close();
        QTRY_VERIFY(!drawer->hasDetachedWindow());

        // Comando com kip_window pelo nome (kai run / kai.run): idem.
        QString message;
        QVERIFY(window.runCommandByName(QStringLiteral("Windowed"), message));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->hasDetachedWindow(), 8000);
        QVERIFY2(!window.isVisible(), "kip_window não deve trazer a janela principal");
        own = drawer->detachedPanel()->window();
        session = drawer->detachedPanel()->kipSession();
        QTRY_VERIFY_WITH_TIMEOUT(session->screen().awaitingInput(), 8000);
        session->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!session || session->isTerminal(), 8000);
        if (own) own->close();
        QTRY_VERIFY(!drawer->hasDetachedWindow());

        // Sem janela própria a view KIP está NA janela principal: ela aparece.
        QVERIFY(window.runCommandByName(QStringLiteral("Docked"), message));
        QVERIFY2(window.isVisible(), "a view embutida precisa da janela principal à frente");
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->kipSession()
                                     && drawer->embeddedPanel()->kipSession()->screen().awaitingInput(), 8000);
        drawer->embeddedPanel()->kipSession()->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->kipSession()->isTerminal(), 8000);
    }

    // Configurações → KIP → "Tamanho da janela própria": decide só para a view KIP.
    void kipWindowModeSettingDecidesHowTheOwnWindowOpens()
    {
        core::Command c = kipCommand(QStringLiteral("m1"), QStringLiteral("ModeWin"), QStringLiteral("cancel-polite.sh"));
        c.kipOpenInWindow = true;
        seed({c});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        const core::KipSettings saved = core::kipSettings();

        struct Case { const char *mode; bool fullscreen; bool maximized; };
        for (const Case &test : {Case{"fullscreen", true, false}, Case{"maximized", false, true},
                                 Case{"normal", false, false}, Case{"preference", false, false}}) {
            core::KipSettings prefs = saved;
            prefs.detachedWindowMode = QString::fromLatin1(test.mode);
            core::setKipSettings(prefs);
            activate(window, QStringLiteral("m1"));
            QTRY_VERIFY_WITH_TIMEOUT(drawer->hasDetachedWindow(), 8000);
            QWidget *own = drawer->detachedPanel()->window();
            QTRY_VERIFY_WITH_TIMEOUT(own->isVisible(), 4000);
            QVERIFY2(own->isFullScreen() == test.fullscreen, test.mode);
            QVERIFY2(own->isMaximized() == test.maximized, test.mode);
            drawer->detachedPanel()->kipSession()->cancel();
            QTRY_VERIFY_WITH_TIMEOUT(drawer->detachedPanel()->kipSession()->isTerminal(), 8000);
            own->close();
            QTRY_VERIFY(!drawer->hasDetachedWindow());
            QTRY_VERIFY_WITH_TIMEOUT(!window.findChild<TerminalDrawer *>()->embeddedPanel()->kipSession()
                                         || window.findChild<TerminalDrawer *>()->embeddedPanel()->kipSession()->isTerminal(), 4000);
        }
        core::setKipSettings(saved);
    }

    void nonKipCommandLeavesKipModeAndSelectingBackReconnectsTheSession()
    {
        core::Command plain;
        plain.id = QStringLiteral("plain");
        plain.folderId = QStringLiteral("f1");
        plain.name = QStringLiteral("Plain");
        plain.type = core::CommandType::Command;
        plain.command = QStringLiteral("echo hi");
        seed({kipCommand(QStringLiteral("k"), QStringLiteral("Kip"), QStringLiteral("cancel-polite.sh")), plain});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("k"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->kipMode(), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->kipSession()->screen().awaitingInput(), 8000);
        engine::KipSession *session = drawer->embeddedPanel()->kipSession();

        QVERIFY(window.findChild<CommandTreeWidget *>()->selectCommand(QStringLiteral("plain")));
        QTRY_VERIFY_WITH_TIMEOUT(!drawer->kipMode(), 4000);

        QVERIFY(window.findChild<CommandTreeWidget *>()->selectCommand(QStringLiteral("k")));
        QVERIFY(drawer->kipMode());
        QCOMPARE(drawer->embeddedPanel()->kipSession(), session);
        session->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(session->isTerminal(), 8000);
    }

    // Bug reportado: com uma sessão KIP aberta na saída, clicar num comando comum que JÁ estava rodando não
    // trocava a saída no primeiro clique (era preciso clicar num terceiro e voltar). Clique de verdade na árvore.
    void firstClickOnARunningCommandLeavesTheKipViewAndShowsItsOutput()
    {
        core::Command dev;
        dev.id = QStringLiteral("dev");
        dev.folderId = QStringLiteral("f1");
        dev.name = QStringLiteral("Dev");
        dev.type = core::CommandType::Command;
        dev.command = QStringLiteral("echo dev-up; sleep 6");
        seed({kipCommand(QStringLiteral("k"), QStringLiteral("Kip"), QStringLiteral("cancel-polite.sh")), dev});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("dev"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->currentCommandId() == QStringLiteral("dev"), 4000);
        // A árvore fica em "Dev" enquanto o KIP é iniciado por outro caminho (busca, atalho, "executar de novo"):
        // a saída mostra o KIP, mas o item selecionado continua sendo o "Dev".
        QVERIFY(window.findChild<CommandTreeWidget *>()->selectCommand(QStringLiteral("dev")));
        activate(window, QStringLiteral("k"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->kipMode(), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->kipSession()->screen().awaitingInput(), 8000);
        QTest::qWait(300);

        QTreeWidget *tree = nullptr;
        QRect rect;
        for (QTreeWidget *candidate : window.findChildren<QTreeWidget *>()) {
            QTreeWidgetItemIterator it(candidate);
            while (*it) {
                if ((*it)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("dev") && candidate->isVisible()) {
                    tree = candidate;
                    rect = candidate->visualItemRect(*it);
                }
                ++it;
            }
        }
        QVERIFY(tree);
        QVERIFY(rect.isValid());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(rect.left() + 40, rect.center().y()));
        QTRY_VERIFY_WITH_TIMEOUT(!drawer->kipMode(), 2000);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("dev"));
        window.findChild<CommandTreeWidget *>()->selectCommand(QStringLiteral("k"));
        drawer->embeddedPanel()->kipSession()->cancel();
        QTest::qWait(6500); // deixa o comando de fundo terminar antes de destruir a janela
    }

    void externalRunOfAKipCommandRunsInTheAppAndReportsTheRealExitCode()
    {
        seed({kipCommand(QStringLiteral("g"), QStringLiteral("Global"), QStringLiteral("done-then-fail.sh"))});
        MainWindow window;
        window.show();
        QString error;
        ExternalRunSession *run = window.startExternalRun(QStringLiteral("g"), {}, error, /*openOutputWindow=*/true);
        QVERIFY2(run, qPrintable(error));
        QSignalSpy finished(run, &ExternalRunSession::finished);
        auto *drawer = window.findChild<TerminalDrawer *>();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 8000);
        QCOMPARE(finished.first().at(0).toInt(), 3); // o código REAL do programa
        QVERIFY(drawer->hasDetachedWindow()); // `-w`: view na janela própria
        QVERIFY(window.isVisible());
        drawer->detachedPanel()->window()->close();
    }
};

QTEST_MAIN(TestKipMainWindow)
#include "test_kip_main_window.moc"
