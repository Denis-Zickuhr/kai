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
