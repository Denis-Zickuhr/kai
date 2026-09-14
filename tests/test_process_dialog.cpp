// Tela de Processos (ProcessListDialog): lista com estado/PID/tipo, filtro
// Em execução/Todos, ações (parar, forçar, ir para o comando), painel de log
// desligado por ora — e o menu "Processos" fora da barra do topo (o acesso é
// pela barra inferior; o histórico de execuções foi para o menu Arquivo).

#include <QTest>
#include <QSignalSpy>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <memory>

#include "engine/process-manager.h"
#include "engine/process-runner.h"
#include "ui/features/history/process-list-dialog.h"
#include "ui/shared/top-utility-bar.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using namespace kai::engine;

class TestProcessDialog : public QObject {
    Q_OBJECT

private:
    static QList<QPair<QString, qint64>> oneForeground()
    {
        return {{QStringLiteral("c1"), 1234}};
    }

    static int rowCount(QListWidget *list)
    {
        int rows = 0;
        for (int i = 0; i < list->count(); ++i) {
            // cabeçalhos de grupo não são selecionáveis
            if (list->item(i)->flags() & Qt::ItemIsSelectable) {
                ++rows;
            }
        }
        return rows;
    }

private slots:
    void listsAForegroundProcessAndEmitsStopAndGoTo()
    {
        ProcessManager manager;
        ProcessListDialog dialog(&manager);
        dialog.setForegroundProvider([]() { return oneForeground(); });
        dialog.setCommandName(QStringLiteral("c1"), QStringLiteral("Alpha"));
        dialog.refreshProcessList();

        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("processList"));
        auto *stop = dialog.findChild<QPushButton *>(QStringLiteral("processStop"));
        auto *force = dialog.findChild<QPushButton *>(QStringLiteral("processForceStop"));
        auto *goTo = dialog.findChild<QPushButton *>(QStringLiteral("processGoTo"));
        auto *copyPid = dialog.findChild<QPushButton *>(QStringLiteral("processCopyPid"));
        QVERIFY(list && stop && force && goTo && copyPid);
        QCOMPARE(rowCount(list), 1);

        // Nada selecionado: nenhuma ação.
        QVERIFY(!stop->isEnabled() && !force->isEnabled() && !goTo->isEnabled() && !copyPid->isEnabled());

        list->setCurrentRow(0);
        QVERIFY(stop->isEnabled() && force->isEnabled() && goTo->isEnabled() && copyPid->isEnabled());

        QSignalSpy stopSpy(&dialog, &ProcessListDialog::stopRequested);
        QSignalSpy goSpy(&dialog, &ProcessListDialog::commandRequested);
        stop->click();
        force->click();
        goTo->click();
        QCOMPARE(stopSpy.count(), 2);
        QCOMPARE(stopSpy.at(0).at(0).toString(), QStringLiteral("c1"));
        QCOMPARE(stopSpy.at(0).at(1).toBool(), false);
        QCOMPARE(stopSpy.at(1).at(1).toBool(), true);
        QCOMPARE(goSpy.count(), 1);
        QCOMPARE(goSpy.at(0).at(0).toString(), QStringLiteral("c1"));
    }

    void filterAndSummaryCoverFinishedProcesses()
    {
        ProcessManager manager;
        auto runner = std::make_unique<ProcessRunner>();
        ProcessRunner *raw = runner.get();
        manager.track(QStringLiteral("c_done"), std::move(runner));
        QCoreApplication::processEvents();
        raw->start(QStringLiteral("exit 0"), QString(), {});
        QTRY_VERIFY_WITH_TIMEOUT(manager.statusOf(QStringLiteral("c_done")) != ProcessStatus::Running, 5000);

        ProcessListDialog dialog(&manager);
        dialog.setForegroundProvider([]() { return oneForeground(); });
        dialog.refreshProcessList();
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("processList"));
        QVERIFY(list != nullptr);

        // Padrão: só o que está rodando.
        QCOMPARE(rowCount(list), 1);
        QLabel *summary = nullptr;
        for (QLabel *label : dialog.findChildren<QLabel *>()) {
            if (label->text() == kai::utils::tr(QStringLiteral("processes.summary.with_finished")).arg(1).arg(1)) {
                summary = label;
            }
        }
        QVERIFY2(summary != nullptr, "resumo deveria dizer 1 em execução e 1 finalizado");

        // "Todos": o finalizado entra, em grupo próprio (2 cabeçalhos + 2 linhas).
        const auto buttons = dialog.findChildren<QToolButton *>(QStringLiteral("processFilterButton"));
        QCOMPARE(buttons.size(), 2);
        buttons.at(1)->click();
        QCOMPARE(rowCount(list), 2);
        QCOMPARE(list->count(), 4);
        buttons.at(0)->click();
        QCOMPARE(rowCount(list), 1);
    }

    void emptyStateWhenNothingIsRunning()
    {
        ProcessManager manager;
        ProcessListDialog dialog(&manager);
        dialog.refreshProcessList();
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("processList"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 0);
        bool hasEmptyText = false;
        for (QLabel *label : dialog.findChildren<QLabel *>()) {
            hasEmptyText = hasEmptyText || label->text() == kai::utils::tr(QStringLiteral("processes.empty.running"));
        }
        QVERIFY(hasEmptyText);
    }

    // O painel de log fica desligado por ora: escondido e sem acumular texto.
    void logPaneIsOffByDefault()
    {
        ProcessManager manager;
        ProcessListDialog dialog(&manager);
        QVERIFY(!dialog.logsVisible());
        dialog.appendLogFor(QStringLiteral("c1"), QStringLiteral("saída que não deve ser guardada\n"), false);
        dialog.setLogsVisible(true);
        QVERIFY(dialog.logsVisible());
        dialog.setLogsVisible(false);
        QVERIFY(!dialog.logsVisible());
    }

    // O menu "Processos" saiu da barra do topo; o histórico de execuções passou
    // para o menu Arquivo.
    void processesMenuIsGoneAndHistoryMovedToFileMenu()
    {
        TopUtilityBar bar;
        auto *menuBar = bar.findChild<QMenuBar *>();
        QVERIFY(menuBar != nullptr);
        QMenu *fileMenu = nullptr;
        for (QAction *action : menuBar->actions()) {
            QVERIFY2(action->text() != QStringLiteral("Processos") && action->text() != QStringLiteral("Processes"),
                     "o menu Processos deveria ter saído");
            if (action->menu() && fileMenu == nullptr) {
                fileMenu = action->menu();   // o primeiro menu é o Arquivo
            }
        }
        QVERIFY(fileMenu != nullptr);
        bool hasHistory = false;
        for (QAction *action : fileMenu->actions()) {
            hasHistory = hasHistory || action->text() == kai::utils::tr(QStringLiteral("menu.file.run_history"));
        }
        QVERIFY2(hasHistory, "Arquivo deveria ter Histórico de execuções");
    }
};

QTEST_MAIN(TestProcessDialog)
#include "test_process_dialog.moc"
