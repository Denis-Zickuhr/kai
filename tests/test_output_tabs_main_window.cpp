#include <QTest>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QKeySequence>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QTemporaryDir>

#include "core/config-manager.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/output/quick-run-popup.h"
#include <QLineEdit>
#include "ui/features/output/terminal-drawer.h"
#include "ui/main-window.h"

using namespace kai;
using namespace kai::ui;

// A barra de guias das saídas ligada ao MainWindow: uma guia por comando executado, trocar de saída pela guia,
// fechar sem parar o processo e a guia que volta ao clicar no comando.
class TestOutputTabsMainWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;

    static core::Command shell(const QString &id, const QString &name, const QString &text)
    {
        core::Command c;
        c.id = id;
        c.folderId = QStringLiteral("f1");
        c.name = name;
        c.type = core::CommandType::Command;
        c.command = text;
        return c;
    }

    void seed(const QVector<core::Command> &commands)
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager config;
        core::CommandsData data;
        core::Folder folder;
        folder.id = QStringLiteral("f1");
        folder.name = QStringLiteral("Tools");
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

    void everyExecutedCommandGetsATabAndTheLastOneIsInFocus()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 2")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out; sleep 2"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        QVERIFY(!bar->isHidden()); // a barra já está lá, vazia, antes de executar qualquer coisa
        QVERIFY(bar->tabs().isEmpty());
        activate(window, QStringLiteral("a"));
        activate(window, QStringLiteral("b"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}), 4000);
        QVERIFY(!bar->isHidden());
        QCOMPARE(bar->currentId(), QStringLiteral("b"));
        QCOMPARE(bar->tabs().at(0).title, QStringLiteral("Alpha"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(0).status, OutputTabStatus::Running, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(1).status, OutputTabStatus::Running, 4000);
        const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); // opcional: um PNG da janela para olhar o visual
        if (!shots.isEmpty()) {
            window.resize(1100, 700);
            QTest::qWait(600);
            window.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/output-tabs-window.png"));
        }
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(0).status, OutputTabStatus::Success, 8000); // terminou
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(1).status, OutputTabStatus::Success, 8000);
    }

    void clickingATabShowsThatOutputAndSelectsTheCommand()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 2")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out; sleep 2"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputTabsBar *bar = drawer->outputTabs();
        activate(window, QStringLiteral("a"));
        activate(window, QStringLiteral("b"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids().size(), 2, 4000);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b"));

        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(QStringLiteral("a")).center());
        QTRY_COMPARE_WITH_TIMEOUT(drawer->currentCommandId(), QStringLiteral("a"), 3000);
        QCOMPARE(tree->currentSelectionId(), QStringLiteral("a"));
        QCOMPARE(bar->currentId(), QStringLiteral("a"));

        // Ctrl+Tab / Ctrl+Shift+Tab alternam entre as guias (o atalho existe e o disparo leva à vizinha).
        auto fire = [&window](const QString &sequence) {
            for (QShortcut *shortcut : window.findChildren<QShortcut *>()) {
                if (shortcut->key() == QKeySequence(sequence)) {
                    emit shortcut->activated();
                    return true;
                }
            }
            return false;
        };
        QVERIFY(fire(QStringLiteral("Ctrl+Tab")));
        QTRY_COMPARE_WITH_TIMEOUT(drawer->currentCommandId(), QStringLiteral("b"), 3000);
        QVERIFY(fire(QStringLiteral("Ctrl+Tab"))); // volta à primeira depois da última
        QTRY_COMPARE_WITH_TIMEOUT(drawer->currentCommandId(), QStringLiteral("a"), 3000);
        QVERIFY(fire(QStringLiteral("Ctrl+Shift+Tab")));
        QTRY_COMPARE_WITH_TIMEOUT(drawer->currentCommandId(), QStringLiteral("b"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(0).status, OutputTabStatus::Success, 8000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(1).status, OutputTabStatus::Success, 8000);
    }

    // O clique direito numa guia abre o menu SEM trazer a guia para a tela: quem está olhando outra saída continua nela.
    void rightClickingATabOpensTheMenuWithoutFocusingIt()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 2")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out; sleep 2"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        activate(window, QStringLiteral("a"));
        activate(window, QStringLiteral("b"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids().size(), 2, 4000);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b"));

        bool menuWasOpen = false;
        QTimer::singleShot(400, [&menuWasOpen]() {
            if (QWidget *popup = QApplication::activePopupWidget()) {
                menuWasOpen = true;
                popup->close();
            }
        });
        emit bar->tabContextRequested(QStringLiteral("a"), bar->mapToGlobal(bar->tabRect(QStringLiteral("a")).center()));
        QVERIFY(menuWasOpen);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b")); // continua na saída que estava na tela
        QCOMPARE(bar->currentId(), QStringLiteral("b"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(0).status, OutputTabStatus::Success, 8000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(1).status, OutputTabStatus::Success, 8000);
    }

    // Rodar de novo (duplo clique/Enter, Play da barra lateral): depois que o comando terminou, uma nova ativação
    // dispara uma execução nova e a guia volta a "rodando".
    void runningAFinishedCommandAgainStartsANewRun()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 1"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        activate(window, QStringLiteral("a"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Running, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);

        activate(window, QStringLiteral("a")); // o "rodar de novo" de sempre
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Running, 4000);
        QCOMPARE(bar->ids(), QStringList{QStringLiteral("a")}); // reaproveita a guia
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);
    }

    // Clique e Enter de VERDADE na árvore (não só a chamada ao slot): rodar, esperar terminar e ativar de novo tem que
    // executar outra vez.
    void activatingAFinishedCommandFromTheTreeRunsItAgain()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 1"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();

        auto rowOf = [&window]() {
            for (QTreeWidget *candidate : window.findChildren<QTreeWidget *>()) {
                QTreeWidgetItemIterator it(candidate);
                while (*it) {
                    if ((*it)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("a") && candidate->isVisible()) {
                        return std::pair<QTreeWidget *, QPoint>(candidate, candidate->visualItemRect(*it).center() - QPoint(100, 0));
                    }
                    ++it;
                }
            }
            return std::pair<QTreeWidget *, QPoint>(nullptr, QPoint());
        };
        const auto row = rowOf();
        QVERIFY(row.first);
        auto activateRow = [&row]() {
            QTest::mouseClick(row.first->viewport(), Qt::LeftButton, Qt::NoModifier, row.second); // seleciona
            QTest::keyClick(row.first, Qt::Key_Return);                                            // e ativa (Enter)
        };
        activateRow();
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Running, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);

        activateRow();
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Running, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);
    }

    // O duplo clique como o Qt o entrega (pressionar, soltar, DblClick, soltar) — inclusive depois de fechar a guia, caso em
    // que o clique no comando faz a barra reaparecer no meio do duplo clique.
    void doubleClickingACommandRunsItEvenWhenItsTabWasClosed()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 1"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        QTreeWidget *tree = nullptr;
        QPoint where;
        for (QTreeWidget *candidate : window.findChildren<QTreeWidget *>()) {
            QTreeWidgetItemIterator it(candidate);
            while (*it) {
                if ((*it)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("a") && candidate->isVisible()) {
                    tree = candidate;
                    where = candidate->visualItemRect(*it).center() - QPoint(100, 0);
                }
                ++it;
            }
        }
        QVERIFY(tree);
        auto doubleClick = [&]() {
            QWidget *viewport = tree->viewport();
            QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, where);
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, where);
            QMouseEvent doubleClickEvent(QEvent::MouseButtonDblClick, QPointF(where), QPointF(viewport->mapToGlobal(where)),
                                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &doubleClickEvent);
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, where);
        };

        doubleClick(); // nunca rodou
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Running, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);

        doubleClick(); // rodou e terminou: de novo
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Running, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);

        // Fecha a guia (a barra some) e roda de novo pelo duplo clique.
        QTest::mouseClick(bar, Qt::MiddleButton, Qt::NoModifier, bar->tabRect(QStringLiteral("a")).center());
        QTRY_VERIFY_WITH_TIMEOUT(bar->tabs().isEmpty(), 3000);
        doubleClick();
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Running, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);
    }

    // Reexecutar um comando que AINDA roda reinicia (o motor encerra o processo antigo e sobe outro): o comando continua
    // "rodando" depois do reinício, e a saída é a da execução nova.
    void rerunningARunningCommandRestartsItAndKeepsItRunning()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo started; sleep 3"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        activate(window, QStringLiteral("a"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Running, 4000);
        QTest::qWait(600);
        activate(window, QStringLiteral("a")); // reinicia
        QTest::qWait(1200); // o processo antigo morreu há muito: o novo segue vivo
        QCOMPARE(bar->tabs().value(0).status, OutputTabStatus::Running);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);
    }

    // Fechar a guia só tira a guia: o processo continua, e clicar no comando de novo traz a guia de volta.
    void closingATabKeepsTheProcessRunningAndClickingTheCommandBringsItBack()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 3")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out; sleep 3"))});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *tree = window.findChild<CommandTreeWidget *>();
        OutputTabsBar *bar = drawer->outputTabs();
        activate(window, QStringLiteral("a"));
        activate(window, QStringLiteral("b"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids().size(), 2, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(0).status, OutputTabStatus::Running, 4000);

        // Fecha a guia da saída que está na tela: vai para a vizinha, e o "Beta" segue rodando.
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->closeRect(QStringLiteral("b")).center());
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), QStringList{QStringLiteral("a")}, 3000);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a"));

        // Clicar no comando na árvore devolve a guia (e a saída dele).
        QVERIFY(tree->selectCommand(QStringLiteral("a")));
        QVERIFY(tree->selectCommand(QStringLiteral("b")));
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}), 3000);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b"));
        QCOMPARE(bar->tabs().at(1).status, OutputTabStatus::Running); // fechar a guia não parou o processo

        // Fechar a última guia que sobra esvazia a saída.
        QTest::mouseClick(bar, Qt::MiddleButton, Qt::NoModifier, bar->tabRect(QStringLiteral("a")).center());
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), QStringList{QStringLiteral("b")}, 3000);
        QTest::mouseClick(bar, Qt::MiddleButton, Qt::NoModifier, bar->tabRect(QStringLiteral("b")).center());
        QTRY_VERIFY_WITH_TIMEOUT(bar->tabs().isEmpty(), 3000);
        QVERIFY(!bar->isHidden()); // vazia, mas continua lá
        QVERIFY(drawer->currentCommandId().isEmpty() || !bar->hasTab(drawer->currentCommandId()));
        QTest::qWait(3500); // deixa os comandos de fundo terminarem antes de destruir a janela
    }

    // O chevron do começo da barra recolhe a lista de comandos: a Saída ocupa tudo e, ao voltar, a divisão é a mesma.
    void theChevronHidesTheCommandListAndGivesTheSameSplitBack()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 1"))});
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *splitter = window.findChild<QSplitter *>(QStringLiteral("outerSplitter"));
        QVERIFY(splitter);
        QWidget *commands = splitter->widget(splitter->indexOf(drawer) == 0 ? 1 : 0);
        OutputTabsBar *bar = drawer->outputTabs();
        QVERIFY(bar->toggleRect().isValid());
        QVERIFY(commands->isVisible());
        const int drawerBefore = drawer->height();

        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->toggleRect().center());
        QVERIFY(!commands->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(drawer->height() > drawerBefore + 100, 2000); // a Saída ficou com o espaço todo
        QVERIFY(drawer->isVisible());

        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->toggleRect().center());
        QVERIFY(commands->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(drawer->height() - drawerBefore) <= 6, 2000);
    }

    // Comando OCULTO em segundo plano (CLI, auto-run, cron): não expande a Saída, não cria guia e não toma a Saída de quem
    // está na tela. Clicar nele na árvore (com "mostrar ocultos") continua mostrando a saída normalmente.
    void hiddenCommandsRunInTheBackgroundWithoutExpandingTheOutputOrCreatingATab()
    {
        core::Command quiet = shell(QStringLiteral("q"), QStringLiteral("Quiet"), QStringLiteral("echo quiet-out"));
        quiet.hidden = true;
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 1")), quiet});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        drawer->setExpanded(false);
        const QString before = drawer->currentCommandId(); // a seleção inicial da árvore, não uma execução
        QString message;
        QVERIFY(window.runCommandByName(QStringLiteral("Quiet"), message)); // o mesmo caminho do `kai run`
        QTest::qWait(1200); // deu tempo de rodar e acabar
        QVERIFY(!drawer->isExpanded());
        QVERIFY(bar->ids().isEmpty());
        QCOMPARE(drawer->currentCommandId(), before);
        QVERIFY(drawer->currentCommandId() != QStringLiteral("q"));

        // Não toma a Saída de quem está olhando outro comando.
        QVERIFY(window.runCommandByName(QStringLiteral("Alpha"), message));
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), QStringList{QStringLiteral("a")}, 4000);
        QVERIFY(drawer->isExpanded());
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a"));
        QVERIFY(window.runCommandByName(QStringLiteral("Quiet"), message));
        QTest::qWait(1200);
        QCOMPARE(bar->ids(), QStringList{QStringLiteral("a")});
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a"));

        // Rodar o oculto pelo clique (caminho do usuário) mostra a saída e cria a guia.
        activate(window, QStringLiteral("q"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), (QStringList{QStringLiteral("a"), QStringLiteral("q")}), 4000);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("q"));
        QTest::qWait(1200);
    }

    // Auto-run de um comando oculto também é em segundo plano; o visível (controle) continua aparecendo.
    void hiddenAutoRunCommandsStayQuietButVisibleOnesStillShow()
    {
        core::Command quiet = shell(QStringLiteral("q"), QStringLiteral("Quiet"), QStringLiteral("echo quiet-out"));
        quiet.hidden = true;
        quiet.autoRun = true;
        quiet.autoRunDelaySec = 0;
        core::Command loud = shell(QStringLiteral("l"), QStringLiteral("Loud"), QStringLiteral("echo loud-out"));
        loud.autoRun = true;
        loud.autoRunDelaySec = 0;
        seed({quiet, loud});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), QStringList{QStringLiteral("l")}, 5000); // só o visível ganhou guia
        QTest::qWait(800);
        QVERIFY(!bar->hasTab(QStringLiteral("q")));
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("l"));
    }

    // Com a lista recolhida, o "+" abre a busca; escolher um comando o executa e mostra a saída dele.
    void thePlusOpensQuickRunAndEnterRunsTheCommandWithTheListHidden()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out"))});
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        QVERIFY(bar->quickRunRect().isValid()); // o "+" aparece por padrão, com a lista aberta
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->toggleRect().center()); // recolhe a lista
        QVERIFY(bar->quickRunRect().isValid());

        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->quickRunRect().center());
        auto *popup = window.findChild<QuickRunPopup *>();
        QVERIFY(popup);
        QTRY_VERIFY_WITH_TIMEOUT(popup->isVisible(), 2000);
        QCOMPARE(popup->visibleIds(), (QStringList{"a", "b"}));
        QTest::keyClicks(popup->searchField(), QStringLiteral("beta"));
        QCOMPARE(popup->visibleIds(), QStringList{"b"});
        QTest::keyClick(popup->searchField(), Qt::Key_Return);
        QVERIFY(!popup->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), QStringList{QStringLiteral("b")}, 4000);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b"));
    }

    // Recolher a própria Saída com a lista recolhida não deixa a tela vazia: a lista volta.
    void collapsingTheOutputWhileTheListIsHiddenBringsTheListBack()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out"))});
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        auto *splitter = window.findChild<QSplitter *>(QStringLiteral("outerSplitter"));
        QWidget *commands = splitter->widget(splitter->indexOf(drawer) == 0 ? 1 : 0);
        QTest::mouseClick(drawer->outputTabs(), Qt::LeftButton, Qt::NoModifier, drawer->outputTabs()->toggleRect().center());
        QVERIFY(!commands->isVisible());
        drawer->setExpanded(false);
        QVERIFY(commands->isVisible());
    }
};

QTEST_MAIN(TestOutputTabsMainWindow)
#include "test_output_tabs_main_window.moc"
