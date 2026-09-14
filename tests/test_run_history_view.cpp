// A tela do histórico de execuções (RunHistoryDialog): grupos por dia, busca, filtro por resultado, detalhe legível,
// reexecutar e o empilhamento em painel estreito.

#include <QTest>

#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "core/run-history.h"
#include "ui/app-stylesheet.h"
#include "ui/features/history/run-history-dialog.h"

using namespace kai;

class TestRunHistoryView : public QObject {
    Q_OBJECT

private:
    static core::RunRecord record(const QString &name, bool success, const QDateTime &when, const QString &output,
                                  const QString &type = QStringLiteral("command"), qint64 ms = 1500)
    {
        core::RunRecord r;
        r.id = QStringLiteral("id-") + name;
        r.commandId = QStringLiteral("cmd-") + name;
        r.commandName = name;
        r.commandType = type;
        r.startedAt = when;
        r.durationMs = ms;
        r.success = success;
        r.output = output;
        return r;
    }

    // Quatro execuções: duas hoje (uma falha), uma ontem e uma de dias atrás, mais recentes primeiro.
    void seed(core::RunHistory &history)
    {
        history.clear();
        const QDateTime now = QDateTime::currentDateTime();
        history.append(record(QStringLiteral("Old build"), true, now.addDays(-5), QStringLiteral("old output")));
        history.append(record(QStringLiteral("Yesterday deploy"), true, now.addDays(-1), QStringLiteral("deployed")));
        history.append(record(QStringLiteral("Lint"), false, now.addSecs(-120),
                              QStringLiteral("\x1b[31mERROR\x1b[0m line one\r\nline two"), QStringLiteral("command"), 800));
        history.append(record(QStringLiteral("Ping API"), true, now.addSecs(-30), QStringLiteral("pong"), QStringLiteral("http"), 95));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void theListGroupsRunsByDayAndShowsTheNewestFirst()
    {
        core::RunHistory history;
        seed(history);
        ui::RunHistoryDialog dialog(&history);
        dialog.show();
        QCOMPARE(dialog.visibleRunCount(), 4);
        // Cabeçalhos de dia (hoje, ontem, a data) + 4 execuções.
        QCOMPARE(dialog.list()->count(), 7);
        QCOMPARE(dialog.list()->item(0)->flags(), Qt::ItemFlags(Qt::NoItemFlags)); // o cabeçalho não é selecionável
        // A mais nova abre sozinha no detalhe.
        QCOMPARE(dialog.detailTitle(), QStringLiteral("Ping API"));
    }

    void searchAndTheResultFilterNarrowTheList()
    {
        core::RunHistory history;
        seed(history);
        ui::RunHistoryDialog dialog(&history);
        dialog.show();
        dialog.searchField()->setText(QStringLiteral("deploy"));
        QCOMPARE(dialog.visibleRunCount(), 1);
        QCOMPARE(dialog.detailTitle(), QStringLiteral("Yesterday deploy"));
        // A busca também olha a saída guardada.
        dialog.searchField()->setText(QStringLiteral("pong"));
        QCOMPARE(dialog.detailTitle(), QStringLiteral("Ping API"));
        dialog.searchField()->clear();
        QCOMPARE(dialog.visibleRunCount(), 4);

        dialog.filterButton(1)->click(); // só as que falharam
        QCOMPARE(dialog.visibleRunCount(), 1);
        QCOMPARE(dialog.detailTitle(), QStringLiteral("Lint"));
        dialog.filterButton(2)->click(); // só os sucessos
        QCOMPARE(dialog.visibleRunCount(), 3);
        dialog.searchField()->setText(QStringLiteral("zzz-nothing"));
        QCOMPARE(dialog.visibleRunCount(), 0);
        QVERIFY(dialog.detailTitle().isEmpty()); // sem nada à vista, o detalhe esvazia
        dialog.filterButton(0)->click();
        dialog.searchField()->clear();
        QCOMPARE(dialog.visibleRunCount(), 4);
    }

    void theDetailShowsReadableOutputAndRerunEmitsTheCommand()
    {
        core::RunHistory history;
        seed(history);
        ui::RunHistoryDialog dialog(&history);
        dialog.show();
        dialog.searchField()->setText(QStringLiteral("lint"));
        QCOMPARE(dialog.detailTitle(), QStringLiteral("Lint"));
        // Sem as sequências de cor, e com quebras de linha normais.
        QCOMPARE(dialog.detailOutput(), QStringLiteral("ERROR line one\nline two"));

        QSignalSpy rerun(&dialog, &ui::RunHistoryDialog::rerunRequested);
        const auto buttons = dialog.findChildren<QPushButton *>();
        QPushButton *rerunButton = nullptr;
        for (QPushButton *button : buttons) {
            if (button->property("kaiRole").toString() == QLatin1String("primary")) {
                rerunButton = button;
            }
        }
        QVERIFY(rerunButton);
        rerunButton->click();
        QCOMPARE(rerun.size(), 1);
        QCOMPARE(rerun.first().first().toString(), QStringLiteral("cmd-Lint"));
    }

    void anEmbeddedPanelStacksListAndDetailWhenNarrow()
    {
        core::RunHistory history;
        seed(history);
        ui::RunHistoryDialog panel(&history, nullptr, true);
        panel.resize(420, 640);
        panel.show();
        QTest::qWait(30);
        QVERIFY(panel.embedded());
        QVERIFY(panel.stacked()); // estreito: lista em cima, detalhe embaixo
        panel.resize(900, 600);
        QTest::qWait(30);
        QVERIFY(!panel.stacked());
        // Esc não fecha o painel.
        QTest::keyClick(&panel, Qt::Key_Escape);
        QVERIFY(panel.isVisible());
    }

    // Só com KAI_TEST_SCREENSHOT_DIR: um PNG do painel dentro de uma janela com o stylesheet do app, largo e estreito.
    void screenshotsForManualReview()
    {
        const QString dir = QString::fromLocal8Bit(qgetenv("KAI_TEST_SCREENSHOT_DIR"));
        if (dir.isEmpty()) {
            QSKIP("defina KAI_TEST_SCREENSHOT_DIR para gerar as imagens");
        }
        core::RunHistory history;
        seed(history);
        for (const QSize size : {QSize(1000, 620), QSize(420, 680)}) {
            QWidget host;
            host.setStyleSheet(ui::buildModernStylesheet());
            auto *layout = new QVBoxLayout(&host);
            auto *panel = new ui::RunHistoryDialog(&history, &host, true);
            layout->addWidget(panel);
            host.resize(size);
            host.show();
            QTest::qWait(150);
            host.grab().save(dir + QStringLiteral("/history-%1x%2.png").arg(size.width()).arg(size.height()));
        }
    }

    void withoutRunsTheListSaysSo()
    {
        core::RunHistory history;
        history.clear();
        ui::RunHistoryDialog dialog(&history);
        dialog.show();
        QCOMPARE(dialog.visibleRunCount(), 0);
        QVERIFY(dialog.detailTitle().isEmpty());
        QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("runListEmpty")) != nullptr);
        QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("runListEmpty"))->isVisible());
    }
};

QTEST_MAIN(TestRunHistoryView)
#include "test_run_history_view.moc"
