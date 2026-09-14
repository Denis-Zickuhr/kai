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
#include "core/models.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/features/history/process-list-dialog.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/output/quick-run-popup.h"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include "ui/features/output/terminal-drawer.h"
#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/view-registry.h"
#include "ui/main-window.h"
#include "ui/shared/fuzzy-search.h"
#include "utils/translation-manager.h"

using namespace kai;
using namespace kai::ui;

// A barra de guias das saídas ligada ao MainWindow: uma guia por comando executado, trocar de saída pela guia,
// fechar sem parar o processo e a guia que volta ao clicar no comando.

namespace {
// Foco inteligente ligado: a seleção na árvore leva a Saída e o Documento (o que estes testes conferem).
void enableSmartFocus()
{
    kai::core::ConfigManager config;
    kai::core::SettingsData settings = config.loadSettings();
    settings.smartFocus = true;
    QVERIFY(config.saveSettings(settings));
}
} // namespace

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
    // O painel Processos mostra o NOME de cada comando em execução (bug: aparecia o id cru), com a pasta como contexto.
    void processesPanelShowsCommandNamesNotIds()
    {
        seed({shell(QStringLiteral("cmd_alpha_1"), QStringLiteral("Alpha"), QStringLiteral("sleep 4")),
              shell(QStringLiteral("cmd_beta_2"), QStringLiteral("Beta"), QStringLiteral("sleep 4"))});
        MainWindow window;
        window.resize(1100, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        activate(window, QStringLiteral("cmd_alpha_1"));
        activate(window, QStringLiteral("cmd_beta_2"));
        window.layoutController()->focusView(QStringLiteral("processes"));
        auto *dialog = window.findChild<ProcessListDialog *>();
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->isVisible(), 2000);
        auto *list = dialog->findChild<QListWidget *>(QStringLiteral("processList"));
        QVERIFY(list != nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 2, 4000);
        QStringList names;
        for (int i = 0; i < list->count(); ++i) {
            names << list->item(i)->text();
        }
        names.sort();
        QCOMPARE(names, (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta")}));
        if (const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); !shots.isEmpty()) {
            list->setCurrentRow(0);
            QTest::qWait(400);
            window.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/processes-window.png"));
            dialog->grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/processes-panel.png"));
        }
    }

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
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(0).status, OutputTabStatus::Success, 8000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(1).status, OutputTabStatus::Success, 8000);
    }

    // "Salvar saída como nota": a saída guardada do comando vira uma nota (só neste Kai) na pasta dele, em markdown.
    void savingTheOutputAsANoteKeepsTheCleanText()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("printf '\\033[31mred-text\\033[0m 100%%1 ```fence``` done\\n'"))});
        MainWindow window;
        window.show();
        activate(window, QStringLiteral("a"));
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().size(), 1, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(0).status, OutputTabStatus::Success, 6000);
        QVERIFY(QMetaObject::invokeMethod(&window, "saveOutputAsNote", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("a"))));
        core::ConfigManager manager;
        const QVector<core::Note> notes = manager.loadNotes();
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().folderId, QStringLiteral("f1"));
        QVERIFY(notes.first().local);
        QVERIFY(notes.first().name.startsWith(QStringLiteral("Alpha ")));
        const QString content = notes.first().content;
        QVERIFY(content.startsWith(QStringLiteral("# Alpha\n")));
        QVERIFY(content.contains(QStringLiteral("red-text 100%1 ```fence``` done"))); // sem as cores, sem mexer no texto
        QVERIFY(!content.contains(QChar(0x1B)));
        QVERIFY(content.contains(QStringLiteral("````text\n"))); // a cerca é maior que a da saída
    }

    void clickingATabShowsThatOutputAndSelectsTheCommand()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 2")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out; sleep 2"))});
        enableSmartFocus(); // com o foco inteligente a guia também seleciona o comando
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
        QVERIFY(fire(QStringLiteral("Ctrl+Tab")));
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
            QTest::mouseClick(row.first->viewport(), Qt::LeftButton, Qt::NoModifier, row.second);
            QTest::keyClick(row.first, Qt::Key_Return);
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

        doubleClick();
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Running, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);

        doubleClick();
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
        activate(window, QStringLiteral("a"));
        QTest::qWait(1200); // o processo antigo morreu há muito: o novo segue vivo
        QCOMPARE(bar->tabs().value(0).status, OutputTabStatus::Running);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().value(0).status, OutputTabStatus::Success, 8000);
    }

    // Fechar a guia só tira a guia: o processo continua, e clicar no comando de novo traz a guia de volta.
    void closingATabKeepsTheProcessRunningAndClickingTheCommandBringsItBack()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 3")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out; sleep 3"))});
        enableSmartFocus();
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

    // Bug (foco manual): fechar a guia da saída na tela não fechava — a seleção da vizinha não reconecta a Saída, e a
    // guia fechada voltava porque a Saída ainda era dela.
    void withManualFocusClosingTheShownTabClosesIt()
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
        // Mostra a 1 e depois a 2 pelas guias. Sem o foco inteligente a guia só troca a Saída: a árvore fica onde está.
        const QString selected = tree->currentSelectionId();
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(QStringLiteral("a")).center());
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a"));
        QCOMPARE(tree->shownCommand(), QStringLiteral("a"));
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(QStringLiteral("b")).center());
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b"));
        QCOMPARE(tree->shownCommand(), QStringLiteral("b"));
        QCOMPARE(tree->currentSelectionId(), selected);

        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->closeRect(QStringLiteral("b")).center());
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), QStringList{QStringLiteral("a")}, 3000);
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("a"));
        QCOMPARE(bar->currentId(), QStringLiteral("a"));

        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->closeRect(QStringLiteral("a")).center());
        QTRY_VERIFY_WITH_TIMEOUT(bar->tabs().isEmpty(), 3000);
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
        // No arranjo clássico (lista em cima, Saída embaixo) a Saída ganha a ALTURA toda ao recolher a lista.
        window.dockHost()->applyLayout(layout::LayoutModel::preset(layout::LayoutModel::Preset::Classic));
        QTest::qWait(60);
        auto *drawer = window.findChild<TerminalDrawer *>();
        QWidget *commands = window.viewRegistry()->existingWidget(QStringLiteral("commands"));
        QVERIFY(commands);
        OutputTabsBar *bar = drawer->outputTabs();
        QVERIFY(bar->quickRunRect().isValid());
        QVERIFY(commands->isVisible());
        const int drawerBefore = drawer->height();

        window.dockHost()->toggleMaximize(layout::view_id::kOutput); // o modo foco: a Saída maximizada
        QVERIFY(!commands->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(drawer->height() > drawerBefore + 100, 2000); // a Saída ficou com o espaço todo
        QVERIFY(drawer->isVisible());

        window.dockHost()->toggleMaximize(layout::view_id::kOutput);
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
        QTest::qWait(1200);
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

    // Auto-run (boot) é em segundo plano para QUALQUER comando, oculto ou não: sem guia, sem tomar a Saída e fora da barra de
    // status (nem contagem, nem "último resultado"). Rodar o mesmo comando pelo clique volta ao comportamento normal.
    void autoRunCommandsStayQuietAndOffTheStatusBar()
    {
        core::Command quiet = shell(QStringLiteral("q"), QStringLiteral("Quiet"), QStringLiteral("echo quiet-out"));
        quiet.hidden = true;
        quiet.autoRun = true;
        quiet.autoRunDelaySec = 0;
        core::Command loud = shell(QStringLiteral("l"), QStringLiteral("Loud"), QStringLiteral("echo loud-out"));
        loud.autoRun = true;
        loud.autoRunDelaySec = 0;
        // O primeiro da lista fica selecionado ao abrir (selecionar um comando mostra a saída dele): um comando comum, que
        // não roda sozinho, para a seleção não "puxar" a saída do que rodou em silêncio.
        core::Command first = shell(QStringLiteral("a"), QStringLiteral("Aaa first"), QStringLiteral("echo first"));
        seed({first, quiet, loud});
        MainWindow window;
        window.show();
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        auto *result = window.findChild<QLabel *>(QStringLiteral("statusLineResult"));
        QVERIFY(result);
        QTest::qWait(1500);
        QVERIFY2(bar->ids().isEmpty(), qPrintable(bar->ids().join(QLatin1Char(',')) + QStringLiteral(" current=") + drawer->currentCommandId() + QStringLiteral(" status=") + result->text()));
        QVERIFY(drawer->currentCommandId() != QStringLiteral("l") && drawer->currentCommandId() != QStringLiteral("q"));
        QVERIFY2(!result->isVisibleTo(result->parentWidget()) || result->text().isEmpty(),
                 qPrintable(QStringLiteral("a barra de status mostrou: %1").arg(result->text())));

        // Pelo clique o visível aparece normalmente, com guia e resultado na barra.
        activate(window, QStringLiteral("l"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->ids(), QStringList{QStringLiteral("l")}, 4000);
        QTRY_VERIFY_WITH_TIMEOUT(result->text().contains(QStringLiteral("Loud")), 4000);
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
        window.dockHost()->toggleMaximize(layout::view_id::kOutput); // recolhe a lista (modo foco)
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

    // Ctrl+K abre a paleta com tudo: comando, pasta, ambiente e ações; escolher uma subpasta a seleciona na árvore.
    void ctrlKOpensThePaletteAndChoosingAFolderSelectsItInTheTree()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out"))});
        {
            core::ConfigManager config;
            core::CommandsData data = config.loadCommands();
            core::Folder sub;
            sub.id = QStringLiteral("f2");
            sub.name = QStringLiteral("Deploys");
            sub.parentId = QStringLiteral("f1");
            data.folders << sub;
            QVERIFY(config.saveCommands(data));
        }
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QApplication::setActiveWindow(&window);

        QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
        auto *palette = window.findChild<QuickRunPopup *>(QStringLiteral("commandPalette"));
        QVERIFY(palette);
        QTRY_VERIFY_WITH_TIMEOUT(palette->isVisible(), 2000);
        const QStringList ids = palette->visibleIds();
        QVERIFY(ids.contains(QStringLiteral("a")));
        QVERIFY(ids.contains(QStringLiteral("f2")));
        QVERIFY(ids.contains(QStringLiteral("settings")));

        QTest::keyClicks(palette->searchField(), QStringLiteral("deploys"));
        QCOMPARE(palette->visibleIds().value(0), QStringLiteral("f2"));
        QTest::keyClick(palette->searchField(), Qt::Key_Return);
        QVERIFY(!palette->isVisible());
        auto *tree = window.findChild<CommandTreeWidget *>();
        QVERIFY(tree);
        QCOMPARE(tree->currentSelectionId(), QStringLiteral("f2"));
    }

    // Ctrl+F: a busca inerte. Escolher um comando leva até ele na árvore e abre o menu de ações dele — não executa. A
    // barra de busca da árvore não existe mais.
    void ctrlFFindsAnItemAndOpensItsActionsWithoutRunningIt()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out"))});
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QApplication::setActiveWindow(&window);
        QVERIFY(!window.findChild<FuzzySearchBar *>());

        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        auto *finder = window.findChild<QuickRunPopup *>(QStringLiteral("findPalette"));
        QVERIFY(finder);
        QTRY_VERIFY_WITH_TIMEOUT(finder->isVisible(), 2000);
        QVERIFY(finder->visibleIds().contains(QStringLiteral("a")));
        QVERIFY(!finder->visibleIds().contains(QStringLiteral("settings"))); // só itens: as ações vêm no menu do item
        if (const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); !shots.isEmpty()) {
            QTest::qWait(300);
            window.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/find-palette-window.png"));
            finder->grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/find-palette.png"));
        }

        QStringList menuTexts;
        QTimer::singleShot(300, [&menuTexts]() {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                for (QAction *action : menu->actions()) {
                    menuTexts << action->text();
                }
                menu->close();
            }
        });
        QTest::keyClicks(finder->searchField(), QStringLiteral("beta"));
        QCOMPARE(finder->visibleIds().value(0), QStringLiteral("b"));
        QTest::keyClick(finder->searchField(), Qt::Key_Return);
        QVERIFY(!finder->isVisible());
        auto *tree = window.findChild<CommandTreeWidget *>();
        QTRY_COMPARE_WITH_TIMEOUT(tree->currentSelectionId(), QStringLiteral("b"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(menuTexts.contains(kai::utils::tr(QStringLiteral("tree.run"))), 2000);
        QTest::qWait(300);
        auto *drawer = window.findChild<TerminalDrawer *>();
        QVERIFY(drawer->outputTabs()->ids().isEmpty());
    }

    // Pela paleta também se executa: escolher um comando mostra a saída dele.
    void theCommandPaletteRunsTheChosenCommand()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out"))});
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QApplication::setActiveWindow(&window);
        QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
        auto *palette = window.findChild<QuickRunPopup *>(QStringLiteral("commandPalette"));
        QVERIFY(palette);
        QTest::keyClicks(palette->searchField(), QStringLiteral("beta"));
        QTest::keyClick(palette->searchField(), Qt::Key_Return);
        auto *drawer = window.findChild<TerminalDrawer *>();
        QTRY_COMPARE_WITH_TIMEOUT(drawer->outputTabs()->ids(), QStringList{QStringLiteral("b")}, 4000);
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
        QWidget *commands = window.viewRegistry()->existingWidget(QStringLiteral("commands"));
        window.dockHost()->toggleMaximize(layout::view_id::kOutput); // modo foco
        QVERIFY(!commands->isVisible());
        drawer->setExpanded(false);
        QVERIFY(commands->isVisible());
    }
};

QTEST_MAIN(TestOutputTabsMainWindow)
#include "test_output_tabs_main_window.moc"
