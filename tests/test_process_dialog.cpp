// Tela de Processos (ProcessListDialog): lista compacta com estado/nome/tempo, filtro
// Em execução/Todos, ações na linha e no menu (parar, forçar, ir para o comando), painel de log
// desligado por ora — e o menu "Processos" fora da barra do topo (o acesso é
// pela barra inferior; o histórico de execuções foi para o menu Arquivo).

#include <QTest>
#include <QSignalSpy>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
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
    // Uma linha por processo com o nome do comando; as ações ficam na linha (parar, "mais ações") e no menu dela.
    void listsAForegroundProcessAndActsFromTheRow()
    {
        ProcessManager manager;
        ProcessListDialog dialog(&manager);
        dialog.setForegroundProvider([]() { return oneForeground(); });
        dialog.setLabelProvider([](const QString &id) {
            ProcessListDialog::Label label;
            if (id == QStringLiteral("c1")) {
                label.name = QStringLiteral("Alpha");
                label.where = QStringLiteral("Tools");
            }
            return label;
        });
        dialog.resize(480, 300);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("processList"));
        QVERIFY(list != nullptr);
        QCOMPARE(rowCount(list), 1);
        QCOMPARE(list->item(0)->text(), QStringLiteral("Alpha"));
        // a barra de botões embaixo saiu: as ações moram na linha
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("processStop")) == nullptr);

        QSignalSpy stopSpy(&dialog, &ProcessListDialog::stopRequested);
        QSignalSpy goSpy(&dialog, &ProcessListDialog::commandRequested);
        const QRect stopRect = dialog.actionRect(QStringLiteral("c1"), ProcessListDialog::RowAction::Stop);
        QVERIFY(!stopRect.isEmpty());
        QVERIFY(list->visualItemRect(list->item(0)).contains(stopRect));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, stopRect.center());
        QCOMPARE(stopSpy.count(), 1);
        QCOMPARE(stopSpy.at(0).at(0).toString(), QStringLiteral("c1"));
        QCOMPARE(stopSpy.at(0).at(1).toBool(), false);

        std::unique_ptr<QMenu> menu(dialog.createActionsMenu(QStringLiteral("c1"), nullptr));
        QStringList texts;
        for (QAction *action : menu->actions()) {
            if (!action->isSeparator()) {
                texts << action->text();
            }
        }
        QCOMPARE(texts, (QStringList{kai::utils::tr(QStringLiteral("processes.action.stop")),
                                     kai::utils::tr(QStringLiteral("processes.action.force")),
                                     kai::utils::tr(QStringLiteral("processes.action.go_to")),
                                     kai::utils::tr(QStringLiteral("processes.action.copy_pid"))}));
        for (QAction *action : menu->actions()) {
            if (action->text() == kai::utils::tr(QStringLiteral("processes.action.force"))
                || action->text() == kai::utils::tr(QStringLiteral("processes.action.go_to"))) {
                action->trigger();
            }
            if (action->text() == kai::utils::tr(QStringLiteral("processes.action.copy_pid"))) {
                QVERIFY(action->isEnabled()); // tem PID
            }
        }
        QCOMPARE(stopSpy.count(), 2);
        QCOMPARE(stopSpy.at(1).at(1).toBool(), true);
        QCOMPARE(goSpy.count(), 1);
        QCOMPARE(goSpy.at(0).at(0).toString(), QStringLiteral("c1"));

        // duplo clique na linha (fora dos botões) vai para o comando
        const QPoint rowPoint(30, list->visualItemRect(list->item(0)).center().y());
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, rowPoint);
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, {}, rowPoint);
        QCOMPARE(goSpy.count(), 2);

        if (const QByteArray shots = qgetenv("KAI_TEST_SCREENSHOT_DIR"); !shots.isEmpty()) {
            dialog.grab().save(QString::fromLocal8Bit(shots) + QStringLiteral("/process-dialog.png"));
        }
    }

    // Sem nome conhecido a linha cai no id; um processo que já terminou não tem o botão parar.
    void unknownIdsFallBackAndFinishedRowsCannotStop()
    {
        ProcessManager manager;
        auto runner = std::make_unique<ProcessRunner>();
        ProcessRunner *raw = runner.get();
        manager.track(QStringLiteral("c_done"), std::move(runner));
        QCoreApplication::processEvents();
        raw->start(QStringLiteral("exit 0"), QString(), {});
        QTRY_VERIFY_WITH_TIMEOUT(manager.statusOf(QStringLiteral("c_done")) != ProcessStatus::Running, 5000);

        ProcessListDialog dialog(&manager);
        dialog.resize(480, 300);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const auto buttons = dialog.findChildren<QToolButton *>(QStringLiteral("processFilterButton"));
        buttons.at(1)->click(); // Todos
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("processList"));
        QCOMPARE(rowCount(list), 1);
        QCOMPARE(list->item(list->count() - 1)->text(), QStringLiteral("c_done"));
        QVERIFY(dialog.actionRect(QStringLiteral("c_done"), ProcessListDialog::RowAction::Stop).isEmpty());
        QVERIFY(!dialog.actionRect(QStringLiteral("c_done"), ProcessListDialog::RowAction::More).isEmpty());
        std::unique_ptr<QMenu> menu(dialog.createActionsMenu(QStringLiteral("c_done"), nullptr));
        for (QAction *action : menu->actions()) {
            QVERIFY(action->text() != kai::utils::tr(QStringLiteral("processes.action.stop")));
        }
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

    // A barra do topo é só menus e controles da janela: o menu Item, o botão da IA e o seletor de ambientes saíram
    // (os painéis e o menu Exibir cobrem isso).
    void topBarHasNoItemMenuAiButtonOrEnvironmentSelector()
    {
        TopUtilityBar bar;
        auto *menuBar = bar.findChild<QMenuBar *>();
        QVERIFY(menuBar != nullptr);
        // Arquivo, Exibir, Configurações e Ajuda.
        QCOMPARE(menuBar->actions().size(), 4);
        QVERIFY(bar.findChild<QToolButton *>(QStringLiteral("aiChatButton")) == nullptr);
        QVERIFY(bar.findChild<QComboBox *>() == nullptr);
        QVERIFY(bar.findChild<QToolButton *>(QStringLiteral("manageEnvironments")) == nullptr);
    }
};

QTEST_MAIN(TestProcessDialog)
#include "test_process_dialog.moc"
