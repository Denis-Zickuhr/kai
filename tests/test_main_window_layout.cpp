#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QShortcut>
#include <QSplitter>
#include <QWindow>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include "core/ai-profile.h"
#include "core/config-manager.h"
#include "core/models.h"
#include "non-fresh-config.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/ai/ai-chat-window.h"
#include "ui/features/ai/main-window-agent-host.h"
#include "ui/features/environments/dynamic-vars-inspector-widget.h"
#include "ui/features/environments/environment-manager-dialog.h"
#include "ui/features/history/notification-history-dialog.h"
#include "ui/features/history/process-list-dialog.h"
#include "ui/features/history/run-history-dialog.h"
#include "ui/features/output/output-panel.h"
#include "ui/features/output/quick-run-popup.h"
#include "ui/features/output/run-mirror-panel.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/features/settings/settings-dialog.h"
#include "ui/features/settings/tabs/layout-tab.h"
#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/view-registry.h"
#include "ui/layout/view-zone.h"
#include "ui/main-window.h"
#include "ui/shared/action-sidebar.h"
#include "ui/shared/status-line.h"
#include "ui/shared/top-utility-bar.h"
#include "utils/action-shortcuts.h"
#include "utils/translation-manager.h"

using namespace kai;
using namespace kai::ui;
using namespace kai::ui::layout;

// As células do padrão 2×2: a lista na primeira coluna, a Saída (e o Documento) na segunda; as de baixo começam vazias.
const Zone kLeftCell{0, 0};
const Zone kRightCell{0, 1};
const Zone kLowerLeft{1, 0};
const Zone kLowerRight{1, 1};

// O layout flexível ligado ao MainWindow: a Saída e a lista de comandos como painéis que mudam de zona sem perder estado,
// fechar/reabrir pelo menu Exibir, o layout.json (gravação, migração, arquivo corrompido) e as flutuantes.
class TestMainWindowLayout : public QObject {
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

    QString layoutPath() const { return core::ConfigManager().layoutFilePath(); }

    static QAction *actionTitled(QMenu *menu, const QString &title)
    {
        for (QAction *action : menu->actions()) {
            if (action->text() == title) {
                return action;
            }
        }
        return nullptr;
    }

private slots:
    void init()
    {
        QDir(m_config.path()).removeRecursively();
        QDir().mkpath(m_config.path());
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    }

    // O ponto da arquitetura: mover o painel só o reparenta. Uma execução em andamento, as guias e o texto da Saída
    // sobrevivem a qualquer mudança de zona.
    void movingTheOutputKeepsARunningCommandItsTabsAndItsText()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; sleep 2; echo alpha-end"))});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        OutputTabsBar *bar = drawer->outputTabs();
        activate(window, QStringLiteral("a"));
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().size(), 1, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(0).status, OutputTabStatus::Running, 4000);
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("alpha-out")), 4000);

        DockHost *dock = window.dockHost();
        const QList<std::pair<Zone, bool>> moves = {{kRightCell, false}, {kLowerLeft, false}, {kLowerRight, false}};
        for (const auto &[zone, floating] : moves) {
            Q_UNUSED(floating);
            dock->moveView(view_id::kOutput, zone);
            QTest::qWait(60);
            QCOMPARE(window.findChild<TerminalDrawer *>(), drawer); // o mesmo painel, nunca recriado
            QCOMPARE(bar->ids(), QStringList{QStringLiteral("a")});
            QCOMPARE(bar->tabs().at(0).status, OutputTabStatus::Running);
            QVERIFY(drawer->isVisible());
            QVERIFY(dock->zoneWidget(zone)->isAncestorOf(drawer));
        }
        dock->floatView(view_id::kOutput);
        QTest::qWait(60);
        QVERIFY(dock->floatingWindow(view_id::kOutput) != nullptr);
        QCOMPARE(bar->tabs().at(0).status, OutputTabStatus::Running);
        dock->dockView(view_id::kOutput);
        QTest::qWait(60);
        QCOMPARE(dock->zoneOf(view_id::kOutput), std::optional<Zone>(kLowerRight));

        // O processo seguiu vivo durante toda a dança e terminou normalmente.
        QTRY_COMPARE_WITH_TIMEOUT(bar->tabs().at(0).status, OutputTabStatus::Success, 8000);
        QVERIFY(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("alpha-end")));
    }

    void theViewMenuListsThePanelsAndClosesAndReopensThem()
    {
        seed({});
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QMenu *menu = window.findChild<TopUtilityBar *>()->viewMenu();
        QVERIFY(menu != nullptr);
        emit menu->aboutToShow();

        const QString outputTitle = utils::tr(QStringLiteral("layout.view.output"));
        const QString commandsTitle = utils::tr(QStringLiteral("layout.view.commands"));
        QAction *output = actionTitled(menu, outputTitle);
        QAction *commands = actionTitled(menu, commandsTitle);
        QVERIFY(output != nullptr);
        QVERIFY(commands != nullptr);
        QVERIFY(output->isCheckable() && output->isChecked());
        QVERIFY(commands->isChecked());

        DockHost *dock = window.dockHost();
        dock->moveView(view_id::kOutput, kRightCell);
        output->trigger(); // desmarca = fecha
        QTest::qWait(30);
        QVERIFY(!dock->model().isPlaced(view_id::kOutput));
        QVERIFY(!window.findChild<TerminalDrawer *>()->isVisible());

        emit menu->aboutToShow();
        output = actionTitled(menu, outputTitle);
        QVERIFY(!output->isChecked());
        output->trigger();
        QTest::qWait(30);
        // Volta para o último lugar onde estava, não para o padrão.
        QCOMPARE(dock->zoneOf(view_id::kOutput), std::optional<Zone>(kRightCell));
        QVERIFY(window.findChild<TerminalDrawer *>()->isVisible());
    }

    void runningACommandBringsTheClosedOutputBack()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out"))});
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        dock->closeView(view_id::kOutput);
        QTest::qWait(30);
        QVERIFY(!dock->model().isPlaced(view_id::kOutput));
        activate(window, QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(dock->model().isPlaced(view_id::kOutput), 2000);
        QVERIFY(window.findChild<TerminalDrawer *>()->isVisible());
    }

    void theLayoutIsWrittenToLayoutJsonOffTheMainThread()
    {
        seed({});
        {
            MainWindow window;
            window.resize(1200, 760);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QVERIFY(!QFile::exists(layoutPath())); // padrão intocado: nada a gravar
            window.dockHost()->moveView(view_id::kOutput, kLowerRight);
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(layoutPath()), 3000);
        }
        QFile file(layoutPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), LayoutModel::kVersion);
        const QJsonObject cells = root.value(QStringLiteral("cells")).toObject();
        QCOMPARE(cells.value(QStringLiteral("1,1")).toObject().value(QStringLiteral("views")).toArray().first().toString(),
                 view_id::kOutput);
        QCOMPARE(root.value(QStringLiteral("grid")).toObject().value(QStringLiteral("cols")).toInt(), 2);
        QVERIFY(root.value(QStringLiteral("maximized")).isNull()); // o modo foco não vai para o layout.json

        // E a próxima janela nasce assim.
        MainWindow again;
        QCOMPARE(again.dockHost()->zoneOf(view_id::kOutput), std::optional<Zone>(kLowerRight));
    }

    void aCorruptLayoutFileFallsBackToTheDefaultAndKeepsABackup()
    {
        seed({});
        {
            QFile file(layoutPath());
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{ this is not json ::");
        }
        MainWindow window;
        QCOMPARE(window.dockHost()->zoneOf(view_id::kOutput), std::optional<Zone>(kRightCell));
        QCOMPARE(window.dockHost()->zoneOf(view_id::kCommands), std::optional<Zone>(kLeftCell));
        const QStringList backups = QDir(QFileInfo(layoutPath()).absolutePath()).entryList({QStringLiteral("layout.json.corrupt-*")});
        QCOMPARE(backups.size(), 1);
    }

    void aLayoutFileWithGhostPanelsIsRepaired()
    {
        seed({});
        {
            QFile file(layoutPath());
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(R"({"version":3,"grid":{"cols":2,"rows":2},"cells":{"0,0":{"views":["ghost","commands"]},"1,0":{"views":["commands","output"]}}})");
        }
        MainWindow window;
        DockHost *dock = window.dockHost();
        QVERIFY(!dock->model().isPlaced(QStringLiteral("ghost")));
        QCOMPARE(dock->zoneOf(view_id::kCommands), std::optional<Zone>(kLeftCell));
        QCOMPARE(dock->zoneOf(view_id::kOutput), std::optional<Zone>(kLowerLeft));
    }

    void layoutJsonWinsOverTheOldOutputPositionSetting()
    {
        seed({});
        {
            core::ConfigManager config;
            core::SettingsData settings = config.loadSettings();
            settings.outputPosition = QStringLiteral("left");
            QVERIFY(config.saveSettings(settings));
            QFile file(layoutPath());
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(R"({"version":3,"grid":{"cols":1,"rows":2},"cells":{"0,0":{"views":["commands"]},"1,0":{"views":["output"]}}})");
        }
        MainWindow window;
        QCOMPARE(window.dockHost()->zoneOf(view_id::kOutput), std::optional<Zone>(kLowerLeft));
    }

    void theOldOutputPositionMigratesAndIsWrittenToTheNewFile()
    {
        seed({});
        {
            core::ConfigManager config;
            core::SettingsData settings = config.loadSettings();
            settings.outputPosition = QStringLiteral("left"); // "bottom" e "right" já são o padrão novo: só "left" muda algo
            QVERIFY(config.saveSettings(settings));
        }
        {
            MainWindow window;
            QCOMPARE(window.dockHost()->zoneOf(view_id::kOutput), std::optional<Zone>(kLeftCell));
        }
        QVERIFY(QFile::exists(layoutPath())); // a migração é gravada: a partir daí manda o layout.json
    }

    void restoringTheLayoutBringsBackTheExactDefault()
    {
        seed({});
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        dock->moveView(view_id::kOutput, kLowerLeft);
        dock->closeView(view_id::kCommands);
        dock->floatView(view_id::kOutput);
        window.layoutController()->restoreDefault();
        QTest::qWait(60);
        QCOMPARE(dock->model(), LayoutModel::defaultLayout());
        QVERIFY(!dock->floatingWindow(view_id::kOutput));
        QVERIFY(window.findChild<TerminalDrawer *>()->isVisible());
    }

    void closingEveryPanelLeavesTheWayBack()
    {
        seed({});
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        dock->closeView(view_id::kOutput);
        dock->closeView(view_id::kDocument);
        dock->closeView(view_id::kCommands);
        QTest::qWait(30);
        QVERIFY(dock->emptyStateShown());
        QMenu *menu = window.findChild<TopUtilityBar *>()->viewMenu();
        emit menu->aboutToShow();
        QAction *restore = actionTitled(menu, utils::tr(QStringLiteral("menu.view.restore_layout")));
        QVERIFY(restore != nullptr);
        restore->trigger();
        QTest::qWait(30);
        QVERIFY(!dock->emptyStateShown());
    }

    // Relato: com Variáveis e Histórico na zona de baixo, o divisor quase não andava (o painel pedia 404 px de altura mínima).
    void theBottomZoneKeepsResizingAfterManyDrags_data()
    {
        QTest::addColumn<QStringList>("bottomViews");
        QTest::addColumn<bool>("withOutput");
        QTest::newRow("output-only") << QStringList{} << false;
        QTest::newRow("output-with-log") << QStringList{} << true;
        QTest::newRow("variables-history") << QStringList{view_id::kVariables, view_id::kHistory} << false;
        QTest::newRow("history") << QStringList{view_id::kHistory} << false;
        QTest::newRow("variables") << QStringList{view_id::kVariables} << false;
    }

    void theBottomZoneKeepsResizingAfterManyDrags()
    {
        QFETCH(QStringList, bottomViews);
        QFETCH(bool, withOutput);
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha"))});
        MainWindow window;
        window.resize(1300, 850);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        if (withOutput) {
            auto *tree = window.findChild<CommandTreeWidget *>();
            QVERIFY(tree->selectCommand(QStringLiteral("a")));
            for (int i = 0; i < 40; ++i) {
                QMetaObject::invokeMethod(&window, "handlePipelineLog", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("a")),
                                          Q_ARG(QString, QStringLiteral("linha %1\n").arg(i)), Q_ARG(bool, false));
            }
            window.findChild<TerminalDrawer *>()->flushPendingOutput();
        }
        DockHost *dock = window.dockHost();
        dock->applyLayout(LayoutModel::preset(LayoutModel::Preset::Classic));
        for (const QString &id : bottomViews) {
            dock->moveView(id, kLowerLeft);
        }
        dock->revealView(bottomViews.isEmpty() ? view_id::kOutput : bottomViews.last());
        QTest::qWait(150);
        QSplitter *vertical = dock->stackSplitter(0);
        QVERIFY(vertical && vertical->count() == 2);
        ViewZone *bottom = dock->zoneWidget(kLowerLeft);
        QVERIFY(bottom);
        QWindow *handleWindow = window.windowHandle();
        QVERIFY(handleWindow);
        int stuck = 0;
        QStringList trace;
        for (int i = 0; i < 12; ++i) {
            QSplitterHandle *handle = vertical->handle(1);
            const QPoint center = handle->mapTo(&window, handle->rect().center());
            const int before = bottom->height();
            const int delta = (i % 2 == 0) ? -90 : 60; // sobe (aumenta o fundo) e desce
            QTest::mousePress(handleWindow, Qt::LeftButton, Qt::NoModifier, center);
            QTest::mouseMove(handleWindow, center + QPoint(0, delta / 2));
            QTest::mouseMove(handleWindow, center + QPoint(0, delta));
            QTest::mouseRelease(handleWindow, Qt::LeftButton, Qt::NoModifier, center + QPoint(0, delta));
            QTest::qWait(40);
            trace << QStringLiteral("%1:%2->%3").arg(i).arg(before).arg(bottom->height());
            if (qAbs(bottom->height() - before) < 10) {
                ++stuck;
            }
        }
        // Os extremos: o divisor vai de quase todo o miolo até um fundo pequeno (nenhum painel impõe um piso alto).
        int smallest = 100000;
        int largest = 0;
        for (int extreme : {-700, 700, -700, 700}) {
            QSplitterHandle *handle = vertical->handle(1);
            const QPoint center = handle->mapTo(&window, handle->rect().center());
            QTest::mousePress(handleWindow, Qt::LeftButton, Qt::NoModifier, center);
            QTest::mouseMove(handleWindow, center + QPoint(0, extreme / 2));
            QTest::mouseMove(handleWindow, center + QPoint(0, extreme));
            QTest::mouseRelease(handleWindow, Qt::LeftButton, Qt::NoModifier, center + QPoint(0, extreme));
            QTest::qWait(40);
            smallest = qMin(smallest, bottom->height());
            largest = qMax(largest, bottom->height());
        }
        QVERIFY2(smallest < 250, qPrintable(QStringLiteral("o fundo não passa de %1 px para baixo").arg(smallest)));
        QVERIFY2(largest > 450, qPrintable(QStringLiteral("o fundo não passa de %1 px para cima").arg(largest)));
        QVERIFY2(stuck <= 1, qPrintable(trace.join(QLatin1Char(' '))));
    }

    // "Ir para o comando" (processos): a árvore volta à tela, mesmo fechada, recolhida numa faixa ou atrás de um maximizado.
    void goToCommandBringsTheCommandTreeBack()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha"))});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        auto *tree = window.findChild<CommandTreeWidget *>();
        auto *processes = window.findChild<ProcessListDialog *>();
        QVERIFY(processes && tree);

        dock->closeView(view_id::kCommands);
        QVERIFY(!dock->model().isPlaced(view_id::kCommands));
        emit processes->commandRequested(QStringLiteral("a"));
        QVERIFY(dock->model().isPlaced(view_id::kCommands));
        QCOMPARE(tree->currentSelectionId(), QStringLiteral("a"));

        // Recolhida numa faixa lateral: abre ao lado.
        dock->setZoneCollapsed(kLeftCell, true);
        QTest::qWait(30);
        emit processes->commandRequested(QStringLiteral("a"));
        QTest::qWait(30);
        QVERIFY(dock->isViewVisible(view_id::kCommands));

        // Atrás da Saída maximizada (modo foco).
        dock->setMaximized(view_id::kOutput);
        emit processes->commandRequested(QStringLiteral("a"));
        QTest::qWait(30);
        QVERIFY(dock->model().maximized().isEmpty());
        QVERIFY(dock->isViewVisible(view_id::kCommands));
    }

    // O divisor entre duas células empilhadas numa coluna (a Saída em cima, Variáveis + Histórico embaixo) também anda à
    // vontade, para os dois lados (Relato: o painel de baixo não encolhia).
    void theLowerCellOfAColumnResizesFreely()
    {
        seed({});
        MainWindow window;
        window.resize(1300, 850);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        // Lista à esquerda, Saída à direita (o padrão) e Variáveis + Histórico embaixo da Saída.
        dock->moveView(view_id::kVariables, kLowerRight);
        dock->moveView(view_id::kHistory, kLowerRight);
        QTest::qWait(150);
        QSplitter *vertical = dock->stackSplitter(1);
        QVERIFY(vertical && vertical->count() == 2);
        ViewZone *bottom = dock->zoneWidget(kLowerRight);
        QVERIFY(bottom);
        QWindow *handleWindow = window.windowHandle();
        int smallest = 100000;
        int largest = 0;
        for (int extreme : {-700, 700, -300, 300, -700, 700}) {
            QSplitterHandle *handle = vertical->handle(1);
            const QPoint center = handle->mapTo(&window, handle->rect().center());
            QTest::mousePress(handleWindow, Qt::LeftButton, Qt::NoModifier, center);
            QTest::mouseMove(handleWindow, center + QPoint(0, extreme / 2));
            QTest::mouseMove(handleWindow, center + QPoint(0, extreme));
            QTest::mouseRelease(handleWindow, Qt::LeftButton, Qt::NoModifier, center + QPoint(0, extreme));
            QTest::qWait(60);
            smallest = qMin(smallest, bottom->height());
            largest = qMax(largest, bottom->height());
        }
        QVERIFY2(smallest < 250, qPrintable(QStringLiteral("o fundo não passa de %1 px para baixo (maior %2)").arg(smallest).arg(largest)));
        QVERIFY2(largest > 400, qPrintable(QStringLiteral("o fundo não passa de %1 px para cima").arg(largest)));
    }

    void aLoneOutputCannotBeCollapsed()
    {
        seed({});
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        DockHost *dock = window.dockHost();
        dock->closeView(view_id::kCommands); // a Saída sozinha na janela: ninguém para receber o espaço
        QTest::qWait(60);
        drawer->setExpanded(false);
        QVERIFY(drawer->isExpanded());
    }

    void collapsingTheOutputGivesTheSpaceBackInEveryArrangement_data()
    {
        QTest::addColumn<QString>("arrangement");
        QTest::newRow("below") << QStringLiteral("below");
        QTest::newRow("left") << QStringLiteral("left");
        QTest::newRow("right") << QStringLiteral("right");
    }

    void collapsingTheOutputGivesTheSpaceBackInEveryArrangement()
    {
        QFETCH(QString, arrangement);
        seed({});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        DockHost *dock = window.dockHost();
        // A lista ao lado (ou em cima) da Saída: sempre há quem receba o espaço devolvido.
        if (arrangement == QLatin1String("below")) {
            dock->applyLayout(LayoutModel::preset(LayoutModel::Preset::Classic));
        } else if (arrangement == QLatin1String("left")) {
            dock->applyLayout(LayoutModel::fromLegacy(QStringLiteral("left"), {}));
        } else {
            dock->applyLayout(LayoutModel::defaultLayout());
        }
        QTest::qWait(80);
        drawer->setExpanded(true);
        QTest::qWait(80);
        QWidget *commands = window.viewRegistry()->existingWidget(view_id::kCommands);
        const bool vertical = arrangement == QLatin1String("below");
        const int expandedExtent = vertical ? drawer->height() : drawer->width();
        const int commandsBefore = vertical ? commands->height() : commands->width();

        drawer->setExpanded(false);
        QTest::qWait(120);
        const int collapsedExtent = vertical ? drawer->height() : drawer->width();
        QVERIFY2(collapsedExtent < expandedExtent / 2,
                 qPrintable(QStringLiteral("%1 -> %2").arg(expandedExtent).arg(collapsedExtent)));
        QVERIFY((vertical ? commands->height() : commands->width()) > commandsBefore);
        // O eixo que não colapsa continua inteiro (a coluna lateral não vira uma tirinha curta).
        if (!vertical) {
            QVERIFY(drawer->height() > 300);
        }

        drawer->setExpanded(true);
        QTest::qWait(120);
        QVERIFY2(qAbs((vertical ? drawer->height() : drawer->width()) - expandedExtent) <= 8,
                 qPrintable(QStringLiteral("%1 vs %2").arg(vertical ? drawer->height() : drawer->width()).arg(expandedExtent)));
    }

    void theFocusModeIsTheOutputMaximized()
    {
        seed({});
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        DockHost *dock = window.dockHost();
        QWidget *commands = window.viewRegistry()->existingWidget(view_id::kCommands);
        dock->toggleMaximize(view_id::kOutput); // o modo foco = a Saída maximizada
        QTest::qWait(60);
        QCOMPARE(dock->model().maximized(), view_id::kOutput);
        QVERIFY(!commands->isVisible());
        // Maximizar o painel da lista a partir do layout (duplo clique na aba) tira a Saída de cena do mesmo jeito.
        dock->toggleMaximize(view_id::kOutput); // o modo foco = a Saída maximizada
        QTest::qWait(60);
        QVERIFY(dock->model().maximized().isEmpty());
        QVERIFY(commands->isVisible());
        dock->toggleMaximize(view_id::kCommands);
        QTest::qWait(60);
        QVERIFY(!drawer->isVisible());
        QVERIFY(commands->isVisible());
        dock->toggleMaximize(view_id::kCommands);
        QVERIFY(drawer->isVisible());
    }

    void floatingPanelsFollowTheMainWindowVisibility()
    {
        seed({});
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        dock->floatView(view_id::kOutput);
        QTest::qWait(60);
        FloatingViewWindow *floating = dock->floatingWindow(view_id::kOutput);
        QVERIFY(floating != nullptr);
        QVERIFY(floating->isVisible());
        window.hide();
        QVERIFY(!floating->isVisible());
        window.showAndRaise();
        QVERIFY(floating->isVisible());
    }

    void aFloatingPanelStaysHiddenWhenTheKaiBootsHidden()
    {
        seed({});
        {
            MainWindow window; // nunca mostrada
            window.dockHost()->floatView(view_id::kOutput);
            QTest::qWait(60);
            FloatingViewWindow *floating = window.dockHost()->floatingWindow(view_id::kOutput);
            QVERIFY(floating != nullptr);
            QVERIFY(!floating->isVisible());
        }
        // Reabrir com o layout salvo: a flutuante continua sem aparecer sozinha.
        MainWindow again;
        QTest::qWait(60);
        FloatingViewWindow *floating = again.dockHost()->floatingWindow(view_id::kOutput);
        QVERIFY(floating != nullptr);
        QVERIFY(!floating->isVisible());
    }

    // Os antigos diálogos viram painéis: abrir pelos mesmos caminhos de antes mostra o painel no quadro, sem modal.
    void processesHistoryAndNotificationsOpenAsPanels()
    {
        seed({});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();

        emit window.findChild<StatusLine *>()->runningClicked();
        QTest::qWait(60);
        QVERIFY(dock->model().isPlaced(view_id::kProcesses));
        auto *processes = window.findChild<ProcessListDialog *>();
        QVERIFY(processes != nullptr);
        QVERIFY(processes->embedded());
        QVERIFY(processes->isVisible());
        QVERIFY(!processes->isWindow()); // dentro do quadro, não numa janela de diálogo

        emit window.findChild<TopUtilityBar *>()->runHistoryRequested();
        QTest::qWait(60);
        auto *history = window.findChild<RunHistoryDialog *>();
        QVERIFY(history != nullptr);
        QVERIFY(history->embedded() && !history->isWindow());
        QVERIFY(dock->model().isPlaced(view_id::kHistory));

        emit window.findChild<StatusLine *>()->notificationsRequested();
        QTest::qWait(60);
        auto *notifications = window.findChild<NotificationHistoryDialog *>();
        QVERIFY(notifications != nullptr);
        QVERIFY(notifications->embedded() && !notifications->isWindow());
        QVERIFY(dock->model().isPlaced(view_id::kNotifications));
        // Os três dividem a zona da Saída em abas com ela; o último aberto está na frente.
        QCOMPARE(dock->model().zone(kRightCell).active, view_id::kNotifications);
        QVERIFY(notifications->isVisible());
        QVERIFY(!history->isVisible());
    }

    void anEmbeddedPanelIsNotHiddenByItsOwnAcceptAndReject()
    {
        seed({});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        emit window.findChild<StatusLine *>()->notificationsRequested();
        QTest::qWait(60);
        auto *notifications = window.findChild<NotificationHistoryDialog *>();
        QVERIFY(notifications->isVisible());
        notifications->accept();
        notifications->reject();
        QTest::qWait(30);
        QVERIFY(notifications->isVisible());
    }

    void theVariablesPanelOpensFromTheViewMenu()
    {
        seed({});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QMenu *menu = window.findChild<TopUtilityBar *>()->viewMenu();
        emit menu->aboutToShow();
        QAction *variables = actionTitled(menu, utils::tr(QStringLiteral("layout.view.variables")));
        QVERIFY(variables != nullptr);
        QVERIFY(!variables->isChecked());
        variables->trigger();
        QTest::qWait(60);
        QCOMPARE(window.dockHost()->zoneOf(view_id::kVariables), std::optional<Zone>(kRightCell));
        auto *environments = window.findChild<EnvironmentManagerDialog *>();
        QVERIFY(environments != nullptr);
        QVERIFY(environments->embedded() && !environments->isWindow());
        QVERIFY(environments->isVisible());
    }

    // Pedir a gestão dos ambientes traz o painel Variáveis (a tela de ambientes embutida), e as edições nele valem
    // sozinhas: gravadas nas configurações, sem OK.
    void theManageEnvironmentsRequestOpensTheVariablesPanelThatSavesByItself()
    {
        seed({});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        QMetaObject::invokeMethod(&window, "handleManageEnvironmentsRequested");
        QTest::qWait(80);
        QVERIFY(dock->model().isPlaced(view_id::kVariables));
        auto *panel = window.findChild<EnvironmentManagerDialog *>();
        QVERIFY(panel != nullptr);
        QVERIFY(panel->embedded() && panel->isVisible() && !panel->isWindow());
        QVERIFY(panel->packageList()->count() >= 1);

        panel->nameField()->setText(QStringLiteral("Staging"));
        emit panel->nameField()->textEdited(QStringLiteral("Staging"));
        QTRY_VERIFY_WITH_TIMEOUT([]() {
            core::ConfigManager config;
            for (const core::Environment &e : config.loadSettings().environments) {
                if (e.name == QLatin1String("Staging")) {
                    return true;
                }
            }
            return false;
        }(), 4000);
    }

    void theAiChatPanelDoesNotExistWithoutAProvider()
    {
        seed({});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!window.viewRegistry()->isKnown(view_id::kAiChat));
        QMenu *menu = window.findChild<TopUtilityBar *>()->viewMenu();
        emit menu->aboutToShow();
        QVERIFY(actionTitled(menu, utils::tr(QStringLiteral("layout.view.ai_chat"))) == nullptr);
        QVERIFY(!window.findChild<AiChatWindow *>());
    }

    void withAProviderTheAiChatIsAPanelThatKeepsItsPlace()
    {
        seed({});
        {
            core::AiProfile profile;
            profile.id = QStringLiteral("p");
            profile.name = QStringLiteral("p");
            profile.model = QStringLiteral("m");
            profile.apiKey = QStringLiteral("k");
            profile.baseUrl = QStringLiteral("http://127.0.0.1:1");
            core::AiSettings settings;
            settings.profiles = {profile};
            settings.defaultProfileId = profile.id;
            QVERIFY(core::ConfigManager().saveAiSettings(settings));
        }
        {
            MainWindow window;
            window.resize(1300, 800);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QVERIFY(window.viewRegistry()->isKnown(view_id::kAiChat));
            QMetaObject::invokeMethod(&window, "showAiChat");
            QTest::qWait(80);
            auto *chat = window.findChild<AiChatWindow *>();
            QVERIFY(chat != nullptr);
            QVERIFY(chat->embedded());
            QVERIFY(chat->isVisible());
            QCOMPARE(window.dockHost()->zoneOf(view_id::kAiChat), std::optional<Zone>(kRightCell));
            window.dockHost()->moveView(view_id::kAiChat, kLowerLeft);
            QTest::qWait(60);
        }
        // O próximo Kai nasce com o chat onde o usuário o deixou.
        MainWindow again;
        QCOMPARE(again.dockHost()->zoneOf(view_id::kAiChat), std::optional<Zone>(kLowerLeft));
        // E removendo o provedor, o painel some do quadro.
        core::ConfigManager().saveAiSettings(core::AiSettings{});
        QVERIFY(QMetaObject::invokeMethod(&again, "refreshAiAvailability", Qt::DirectConnection));
        QVERIFY(!again.dockHost()->model().isPlaced(view_id::kAiChat));
    }

    // Atalhos dos painéis: Alt+N foca o enésimo painel visível, F6 percorre, Ctrl+Shift+M maximiza.
    void thePanelShortcutsDriveTheLayout()
    {
        seed({});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        const auto shortcut = [&window](const QString &sequence) -> QShortcut * {
            for (QShortcut *candidate : window.findChildren<QShortcut *>()) {
                if (candidate->key() == QKeySequence(sequence)) {
                    return candidate;
                }
            }
            return nullptr;
        };
        for (const QString &sequence : {QStringLiteral("Alt+1"), QStringLiteral("Alt+9"), QStringLiteral("F6"),
                                         QStringLiteral("Shift+F6"), QStringLiteral("Ctrl+Shift+M")}) {
            QVERIFY2(shortcut(sequence) != nullptr, qPrintable(sequence));
        }
        auto *drawer = window.findChild<TerminalDrawer *>();
        DockHost *dock = window.dockHost();
        QWidget *commands = window.viewRegistry()->existingWidget(view_id::kCommands);

        emit shortcut(QStringLiteral("Alt+2"))->activated(); // 2º painel visível: a Saída
        QTRY_VERIFY(drawer->isAncestorOf(QApplication::focusWidget()) || drawer == QApplication::focusWidget());
        emit shortcut(QStringLiteral("F6"))->activated(); // dá a volta: de volta à lista
        QTRY_VERIFY(commands->isAncestorOf(QApplication::focusWidget()) || commands == QApplication::focusWidget());

        emit shortcut(QStringLiteral("Ctrl+Shift+M"))->activated();
        QCOMPARE(dock->model().maximized(), view_id::kCommands);
        emit shortcut(QStringLiteral("Ctrl+Shift+M"))->activated();
        QVERIFY(dock->model().maximized().isEmpty());

        // Alt+N de um painel fechado não existe: só conta o que está visível.
        dock->closeView(view_id::kOutput);
        QTest::qWait(30);
        emit shortcut(QStringLiteral("Alt+2"))->activated();
        QVERIFY(!dock->model().isPlaced(view_id::kOutput)); // fechado continua fechado: Alt+N não reabre sozinho
    }

    void rearrangeAndRestoreAreRemappableActions()
    {
        bool hasRearrange = false;
        bool hasRestore = false;
        for (const auto &spec : kai::utils::actionShortcutSpecs()) {
            hasRearrange |= spec.id == QStringLiteral("action.rearrange_panels");
            hasRestore |= spec.id == QStringLiteral("action.restore_layout");
        }
        QVERIFY(hasRearrange && hasRestore);
        seed({});
        core::ConfigManager config;
        core::SettingsData settings = config.loadSettings();
        settings.shortcuts.insert(QStringLiteral("action.rearrange_panels"), {QStringLiteral("Ctrl+Alt+P")});
        settings.shortcuts.insert(QStringLiteral("action.restore_layout"), {QStringLiteral("Ctrl+Alt+R")});
        QVERIFY(config.saveSettings(settings));
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QShortcut *rearrange = nullptr;
        QShortcut *restore = nullptr;
        for (QShortcut *candidate : window.findChildren<QShortcut *>()) {
            if (candidate->key() == QKeySequence(QStringLiteral("Ctrl+Alt+P"))) {
                rearrange = candidate;
            }
            if (candidate->key() == QKeySequence(QStringLiteral("Ctrl+Alt+R"))) {
                restore = candidate;
            }
        }
        QVERIFY(rearrange != nullptr);
        QVERIFY(restore != nullptr);
        QVERIFY(window.dockHost()->editMode()); // o padrão novo mostra as abas
        emit rearrange->activated();
        QVERIFY(!window.dockHost()->editMode());
        window.dockHost()->moveView(view_id::kOutput, kLowerRight);
        emit restore->activated();
        QCOMPARE(window.dockHost()->model(), LayoutModel::defaultLayout());
        QVERIFY(window.dockHost()->editMode()); // restaurar devolve as abas
    }

    // A busca (Ctrl+K) leva aos painéis e ao layout, como o menu Exibir.
    void theCommandPaletteReachesThePanelsAndTheLayoutActions()
    {
        seed({});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QShortcut *paletteShortcut = nullptr;
        for (QShortcut *candidate : window.findChildren<QShortcut *>()) {
            if (candidate->key() == QKeySequence(QStringLiteral("Ctrl+K"))) {
                paletteShortcut = candidate;
            }
        }
        QVERIFY(paletteShortcut != nullptr);
        emit paletteShortcut->activated();
        auto *palette = window.findChild<QuickRunPopup *>(QStringLiteral("commandPalette"));
        QVERIFY(palette != nullptr);
        DockHost *dock = window.dockHost();
        const auto choose = [&](const QString &id) {
            emit palette->entryChosen(QuickRunPopup::Kind::Action, id);
            QTest::qWait(60);
        };
        choose(QStringLiteral("view_processes"));
        QVERIFY(dock->model().isPlaced(view_id::kProcesses));
        choose(QStringLiteral("view_variables"));
        QVERIFY(dock->model().isPlaced(view_id::kVariables));
        choose(QStringLiteral("view_document"));
        QVERIFY(dock->model().isPlaced(view_id::kDocument));
        choose(QStringLiteral("rearrange_panels"));
        QVERIFY(!dock->editMode()); // o padrão mostra as abas: a ação as desliga
        choose(QStringLiteral("restore_layout"));
        QCOMPARE(dock->model(), LayoutModel::defaultLayout());
        QVERIFY(dock->editMode());
        choose(QStringLiteral("view_ai_chat")); // sem provedor a ação nem existe na lista: nada acontece
        QVERIFY(!dock->model().isPlaced(view_id::kAiChat));
    }

    // ---- Fase 4: execuções lado a lado -------------------------------------------------------------------------------
    // Uma guia da Saída vira um painel `run:<id>`: o log dele fica ali, vivo, enquanto a Saída mostra outro comando.
    void aRunTabBecomesAPanelThatKeepsFollowingThatCommand()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-1; sleep 1; echo alpha-2")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out"))});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("alpha-1")), 4000);

        window.layoutController()->focusView(QStringLiteral("run:a"));
        QTest::qWait(80);
        DockHost *dock = window.dockHost();
        QCOMPARE(dock->zoneOf(QStringLiteral("run:a")), std::optional<Zone>(kRightCell));
        auto *mirror = window.findChild<RunMirrorPanel *>();
        QVERIFY(mirror != nullptr);
        QCOMPARE(mirror->commandId(), QStringLiteral("a"));
        QVERIFY(mirror->isVisible());
        QVERIFY(mirror->output()->plainOutput().contains(QStringLiteral("alpha-1"))); // o que já tinha saído
        // O título do painel é o nome do comando.
        QCOMPARE(window.viewRegistry()->title(QStringLiteral("run:a")), QStringLiteral("Alpha"));

        // Outro comando ocupa a Saída; o painel segue no Alpha.
        activate(window, QStringLiteral("b"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->plainOutput().contains(QStringLiteral("beta-out")), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(mirror->output()->plainOutput().contains(QStringLiteral("alpha-2")), 4000);
        QVERIFY(!mirror->output()->plainOutput().contains(QStringLiteral("beta-out")));
        QTRY_COMPARE_WITH_TIMEOUT(mirror->output()->status(), OutputStatus::Success, 4000);

        // Rodar o Alpha de novo recomeça o log do painel.
        activate(window, QStringLiteral("a"));
        QTRY_COMPARE_WITH_TIMEOUT(mirror->output()->status(), OutputStatus::Running, 4000);
        QTRY_VERIFY_WITH_TIMEOUT(mirror->output()->plainOutput().contains(QStringLiteral("alpha-2")), 4000);
        QCOMPARE(mirror->output()->plainOutput().count(QStringLiteral("alpha-1")), 1);
    }

    // O painel de um comando de saída formatada mostra os cards (antes mostrava o JSON cru) e tem, no cabeçalho, os botões de
    // execução: executar de novo e parar sem precisar da árvore.
    void aRunPanelKeepsTheFormattedOutputAndHasItsOwnActionButtons()
    {
        core::Command logs = shell(QStringLiteral("a"), QStringLiteral("Alpha"),
                                   QStringLiteral("echo '{\"level\":\"info\",\"msg\":\"hello\"}'; sleep 3"));
        logs.formattedOutput = true;
        seed({logs});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        activate(window, QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("hello")), 4000);

        window.layoutController()->focusView(QStringLiteral("run:a"));
        QTest::qWait(80);
        auto *mirror = window.findChild<RunMirrorPanel *>();
        QVERIFY(mirror != nullptr);
        QVERIFY(mirror->output()->formattedOutputEnabled());

        auto *strip = mirror->output()->findChild<ActionSidebar *>();
        QVERIFY(strip != nullptr);
        const auto buttons = strip->findChildren<QToolButton *>();
        QVERIFY(buttons.size() >= 4); // executar, parar, forçar, reiniciar (e o body no HTTP)
        QTRY_VERIFY_WITH_TIMEOUT(!buttons.at(0)->isEnabled() && buttons.at(1)->isEnabled(), 4000); // rodando: só parar
        buttons.at(1)->click();
        QTRY_VERIFY_WITH_TIMEOUT(buttons.at(0)->isEnabled() && !buttons.at(1)->isEnabled(), 6000); // parou: só executar
    }

    void droppingARunTabOnAZoneOpensItsPanel()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out"))});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("alpha-out")), 4000);
        DockHost *dock = window.dockHost();
        OutputTabsBar *bar = drawer->outputTabs();

        emit bar->tabDragStarted(QStringLiteral("a"));
        QVERIFY(dock->overlay()->isVisible()); // os destinos aparecem durante o arrasto
        DropTarget target;
        target.valid = true;
        target.zone = kLowerLeft;
        emit dock->overlay()->dropped(QStringLiteral("run:a"), target);
        emit bar->tabDragFinished(QStringLiteral("a"), Qt::CopyAction, QCursor::pos());
        QTRY_COMPARE(dock->zoneOf(QStringLiteral("run:a")), std::optional<Zone>(kLowerLeft));
        QVERIFY(!dock->overlay()->isVisible());
        QVERIFY(window.findChild<RunMirrorPanel *>() != nullptr);
        // O comando passa a viver só no painel: a guia sai da Saída.
        QTRY_VERIFY(!bar->hasTab(QStringLiteral("a")));
    }

    // "Parar tudo" da bandeja: pára o comando que roda pelo pipeline (não só os processos em segundo plano).
    void stopAllRunningStopsACommandRunningInThePipeline()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo started; sleep 30"))});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("a"));
        QTRY_COMPARE_WITH_TIMEOUT(drawer->embeddedPanel()->status(), OutputStatus::Running, 4000);
        QVERIFY(QMetaObject::invokeMethod(&window, "stopAllRunning", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->status() != OutputStatus::Running, 8000);
    }

    // Com painel próprio o comando aparece só nele: rodar de novo não cria guia na Saída, não a conecta nem a traz para a
    // frente; fechar o painel devolve a guia (sem tomar a frente).
    void aCommandWithItsOwnPanelRunsOnlyThere()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta-out"))});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        activate(window, QStringLiteral("b"));
        QTRY_VERIFY_WITH_TIMEOUT(drawer->embeddedPanel()->plainOutput().contains(QStringLiteral("beta-out")), 4000);
        activate(window, QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("alpha-out")), 4000);

        window.layoutController()->focusView(QStringLiteral("run:a"));
        QTest::qWait(80);
        auto *mirror = window.findChild<RunMirrorPanel *>();
        QVERIFY(mirror != nullptr);
        OutputTabsBar *bar = drawer->outputTabs();
        QTRY_VERIFY(!bar->hasTab(QStringLiteral("a")));
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b")); // a Saída voltou para a guia vizinha

        activate(window, QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(mirror->output()->plainOutput().count(QStringLiteral("alpha-out")) == 1
                                     && mirror->output()->status() == OutputStatus::Success, 4000);
        QVERIFY(!bar->hasTab(QStringLiteral("a")));
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b"));
        QVERIFY(!drawer->embeddedPanel()->plainOutput().contains(QStringLiteral("alpha-out")));

        window.dockHost()->closeView(QStringLiteral("run:a"));
        QTest::qWait(80);
        QVERIFY(bar->hasTab(QStringLiteral("a")));
        QCOMPARE(drawer->currentCommandId(), QStringLiteral("b"));
    }

    void aRunTabDroppedOutsideTheWindowBecomesAFloatingPanel()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out"))});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        activate(window, QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("alpha-out")), 4000);
        OutputTabsBar *bar = window.findChild<TerminalDrawer *>()->outputTabs();
        emit bar->tabDragStarted(QStringLiteral("a"));
        emit bar->tabDragFinished(QStringLiteral("a"), Qt::IgnoreAction, QPoint(-6000, -6000));
        QTRY_VERIFY(window.dockHost()->model().isFloating(QStringLiteral("run:a")));
        QVERIFY(window.dockHost()->floatingWindow(QStringLiteral("run:a")) != nullptr);
    }

    void closingARunPanelDiscardsItAndNothingRemembersIt()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out"))});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        activate(window, QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("alpha-out")), 4000);
        window.layoutController()->focusView(QStringLiteral("run:a"));
        QVERIFY(window.findChild<RunMirrorPanel *>() != nullptr);
        window.dockHost()->closeView(QStringLiteral("run:a"));
        QTest::qWait(60);
        QVERIFY(!window.dockHost()->model().isPlaced(QStringLiteral("run:a")));
        QVERIFY(!window.dockHost()->model().closedViews().contains(QStringLiteral("run:a")));
        QVERIFY(window.viewRegistry()->existingWidget(QStringLiteral("run:a")) == nullptr);
    }

    void runPanelsAreForgottenWhenTheKaiRestarts()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out"))});
        {
            MainWindow window;
            window.resize(1300, 800);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            activate(window, QStringLiteral("a"));
            QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("alpha-out")), 4000);
            window.layoutController()->focusView(QStringLiteral("run:a"));
            window.layoutController()->flush();
        }
        MainWindow again;
        QVERIFY(!again.dockHost()->model().isPlaced(QStringLiteral("run:a")));
    }

    void aPanelOfADeletedCommandGoesAway()
    {
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out"))});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        activate(window, QStringLiteral("a"));
        QTRY_VERIFY_WITH_TIMEOUT(window.storedOutputFor(QStringLiteral("a")).contains(QStringLiteral("alpha-out")), 4000);
        window.layoutController()->focusView(QStringLiteral("run:a"));
        QVERIFY(window.dockHost()->model().isPlaced(QStringLiteral("run:a")));
        // O comando é apagado (pelo caminho do agente de IA, que edita a lista viva e a grava) e a árvore recarrega.
        auto *host = window.findChild<MainWindowAgentHost *>();
        QVERIFY(host != nullptr);
        host->commandsData().commands.clear();
        host->commandsChanged();
        QTRY_VERIFY(!window.dockHost()->model().isPlaced(QStringLiteral("run:a")));
    }

    // Só roda com KAI_TEST_SCREENSHOT_DIR: um PNG de cada arranjo para olhar o visual (tokens, cantos, faixa de abas).
    void screenshotsForManualReview()
    {
        const QString dir = QString::fromLocal8Bit(qgetenv("KAI_TEST_SCREENSHOT_DIR"));
        if (dir.isEmpty()) {
            QSKIP("defina KAI_TEST_SCREENSHOT_DIR para gerar as imagens");
        }
        seed({shell(QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("echo alpha-out; echo more; sleep 1")),
              shell(QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("echo beta"))});
        MainWindow window;
        window.resize(1280, 780);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        activate(window, QStringLiteral("a"));
        QTest::qWait(900);
        DockHost *dock = window.dockHost();
        const auto shot = [&](const QString &name) {
            QTest::qWait(250);
            window.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
        };
        shot(QStringLiteral("layout-1-default"));
        dock->moveView(view_id::kOutput, kLowerRight);
        shot(QStringLiteral("layout-2-output-lower-right"));
        dock->moveView(view_id::kOutput, kLowerLeft);
        shot(QStringLiteral("layout-3-output-left"));
        dock->restoreDefault();
        dock->setEditMode(true);
        shot(QStringLiteral("layout-4-rearrange-mode"));
        dock->setEditMode(false);
        dock->moveView(view_id::kCommands, kRightCell, 0);
        shot(QStringLiteral("layout-5-two-tabs-bottom"));
        dock->restoreDefault();
        dock->setMaximized(view_id::kOutput);
        shot(QStringLiteral("layout-6-maximized-output"));
        dock->setMaximized(QString());
        dock->closeView(view_id::kOutput);
        dock->closeView(view_id::kCommands);
        shot(QStringLiteral("layout-7-empty"));
        dock->restoreDefault();
        dock->floatView(view_id::kOutput);
        QTest::qWait(300);
        if (FloatingViewWindow *floating = dock->floatingWindow(view_id::kOutput)) {
            floating->grab().save(dir + QStringLiteral("/layout-8-floating.png"));
        }
        shot(QStringLiteral("layout-8-main-with-floating"));
        dock->restoreDefault();
        QTest::qWait(100);
        dock->revealView(view_id::kProcesses);
        dock->revealView(view_id::kHistory);
        dock->revealView(view_id::kNotifications);
        shot(QStringLiteral("layout-9-bottom-panels-notifications"));
        dock->revealView(view_id::kHistory);
        shot(QStringLiteral("layout-10-history"));
        dock->revealView(view_id::kProcesses);
        shot(QStringLiteral("layout-11-processes"));
        dock->revealView(view_id::kVariables);
        dock->revealView(view_id::kDocument);
        shot(QStringLiteral("layout-12-right-document-variables"));
        dock->setZoneCollapsed(kRightCell, true);
        shot(QStringLiteral("layout-13-right-collapsed-rail"));
        dock->revealView(view_id::kDocument);
        shot(QStringLiteral("layout-14-rail-overlay-open"));
        dock->hideRevealed();
        dock->setGrid(2, 2, Expansion::Horizontal);
        dock->moveView(view_id::kHistory, kLowerRight);
        dock->setZoneCollapsed(kLowerRight, true);
        shot(QStringLiteral("layout-15-bottom-collapsed-rail"));
        // O menu Exibir aberto.
        dock->restoreDefault();
        dock->revealView(view_id::kHistory);
        dock->floatView(view_id::kVariables);
        if (QMenu *view = window.findChild<TopUtilityBar *>()->viewMenu()) {
            emit view->aboutToShow();
            view->resize(view->sizeHint());
            view->ensurePolished();
            view->grab().save(dir + QStringLiteral("/layout-16-view-menu.png"));
        }
        // Execução em painel próprio, ao lado da Saída.
        dock->restoreDefault();
        window.layoutController()->focusView(QStringLiteral("run:a"));
        shot(QStringLiteral("layout-17-run-panel"));
        // Uma grade 3×2, a célula dividida e as faixas das bordas.
        dock->restoreDefault();
        dock->setGrid(3, 2, Expansion::Vertical);
        dock->moveView(view_id::kHistory, Zone{0, 1});
        dock->moveView(view_id::kProcesses, Zone{0, 1});
        dock->moveView(view_id::kOutput, Zone{1, 2});
        dock->moveView(view_id::kDocument, Zone{1, 2});
        dock->moveView(view_id::kVariables, Zone{1, 2});
        dock->applyLayout(dock->model());
        shot(QStringLiteral("layout-18-grid-3x2"));
        dock->setZoneSplit(Zone{1, 2}, true);
        shot(QStringLiteral("layout-19-cell-split"));
        dock->setZoneSplit(Zone{1, 2}, false);
        dock->setZoneCollapsed(Zone{1, 2}, true);
        dock->setZoneCollapsed(Zone{0, 0}, true);
        dock->revealView(view_id::kOutput);
        shot(QStringLiteral("layout-20-rails-left-right"));
        // O modo horizontal e as células vazias absorvidas.
        dock->restoreDefault();
        dock->setGrid(3, 3, Expansion::Horizontal);
        dock->moveView(view_id::kHistory, Zone{2, 2});
        dock->moveView(view_id::kOutput, Zone{1, 1});
        dock->moveView(view_id::kCommands, Zone{0, 0});
        shot(QStringLiteral("layout-22-horizontal-diagonal"));
        // Janela pequena com três colunas: o quadro rola em vez de espremer os painéis.
        dock->restoreDefault();
        dock->setGrid(3, 2, Expansion::Vertical);
        dock->moveView(view_id::kHistory, Zone{0, 1});
        dock->moveView(view_id::kVariables, Zone{0, 2});
        window.resize(820, 560);
        QTest::qWait(150);
        shot(QStringLiteral("layout-23-small-window-scroll"));
        window.resize(1280, 780);
        // A aba Layout das configurações, com a grade e a miniatura.
        dock->restoreDefault();
        dock->setGrid(3, 3, Expansion::Vertical);
        dock->moveView(view_id::kHistory, Zone{2, 0});
        dock->moveView(view_id::kVariables, Zone{1, 1});
        {
            QTemporaryDir themes;
            core::CommandsData commands;
            QVector<core::Collection> collections;
            SettingsDialog dialog(core::SettingsData(), {QStringLiteral("dracula")}, themes.path(), commands, collections,
                                  []() {}, []() {});
            dialog.setLayoutModel(dock->model());
            dialog.layoutTab()->resize(620, 760);
            QTest::qWait(150);
            dialog.layoutTab()->grab().save(dir + QStringLiteral("/layout-24-settings-grid.png"));
        }
        // O painel Variáveis (a tela de ambientes) e a de histórico com dados.
        dock->restoreDefault();
        dock->moveView(view_id::kVariables, kRightCell);
        dock->revealView(view_id::kVariables);
        shot(QStringLiteral("layout-21-variables-panel"));
    }

    // Painéis de formulário: o conteúdo nunca é comprimido abaixo do que o layout dele aguenta (rola em vez disso), e a célula
    // do quadro reserva essa largura (o quadro rola uma vez só, em vez de cada painel ganhar a própria barra).
    void theFormPanelsKeepTheWidthTheirLayoutNeeds()
    {
        seed({});
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        for (const QString &id : {view_id::kVariables, view_id::kProcesses, view_id::kHistory, view_id::kNotifications}) {
            dock->revealView(id);
            QTest::qWait(30);
            QWidget *card = window.viewRegistry()->existingWidget(id);
            QVERIFY(card != nullptr);
            auto *scroll = card->findChild<QScrollArea *>(QStringLiteral("panelScroll"));
            QVERIFY2(scroll != nullptr, qPrintable(id));
            QWidget *content = scroll->widget();
            const int declared = window.viewRegistry()->spec(id)->minWidth;
            const int needed = qMax(declared, content->minimumSizeHint().width());
            QCOMPARE(content->minimumWidth(), needed);
            QCOMPARE(card->property("kaiMinContentWidth").toInt(), needed);
            // O quadro reserva a largura para a célula onde ele está à frente.
            const auto zone = dock->zoneOf(id);
            QVERIFY(zone.has_value());
            QVERIFY2(dock->board()->minimumWidth() >= needed, qPrintable(QStringLiteral("%1: %2 < %3").arg(id).arg(dock->board()->minimumWidth()).arg(needed)));
        }
        // Com o quadro estreito, o painel não é comprimido: ele rola.
        window.resize(720, 560);
        QTest::qWait(150);
        QWidget *variables = window.viewRegistry()->existingWidget(view_id::kVariables);
        dock->revealView(view_id::kVariables);
        QTest::qWait(60);
        auto *scroll = variables->findChild<QScrollArea *>(QStringLiteral("panelScroll"));
        QVERIFY(scroll->widget()->width() >= scroll->widget()->minimumWidth());
    }

    // Os painéis que eram diálogos (Processos, Histórico...) ficam no canto do cartão, não deslocados pela centralização do QDialog.
    void aFormPanelSitsAtTheTopLeftOfItsScrollArea()
    {
        seed({});
        MainWindow window;
        window.resize(1280, 780);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        // Painéis que nascem numa célula e depois ficam escondidos e voltam a aparecer (recolher e abrir outra célula refaz o
        // quadro): o QDialog (ver centerOnParent) não pode se centralizar no pai ao ser mostrado de novo.
        dock->setGrid(3, 2, Expansion::Vertical);
        dock->moveView(view_id::kHistory, Zone{0, 1});
        dock->moveView(view_id::kProcesses, Zone{0, 1});
        dock->moveView(view_id::kOutput, Zone{1, 2});
        dock->moveView(view_id::kDocument, Zone{1, 2});
        dock->moveView(view_id::kVariables, Zone{1, 2});
        QTest::qWait(100);
        dock->setZoneCollapsed(Zone{1, 2}, true);
        dock->setZoneCollapsed(Zone{0, 0}, true);
        dock->revealView(view_id::kOutput);
        QTest::qWait(250);
        for (const QString &id : {view_id::kProcesses, view_id::kHistory}) {
            QWidget *card = window.viewRegistry()->existingWidget(id);
            auto *scroll = card->findChild<QScrollArea *>(QStringLiteral("panelScroll"));
            QVERIFY2(scroll != nullptr, qPrintable(id));
            QCOMPARE(scroll->widget()->pos(), QPoint(0, 0));
            QVERIFY2(scroll->widget()->width() >= scroll->viewport()->width() - 1, qPrintable(id));
        }
    }

    void theWelcomeScreenHidesTheWholeDock()
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8()); // config vazia = boas-vindas
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!window.dockHost()->isVisible());
    }
};

QTEST_MAIN(TestMainWindowLayout)
#include "test_main_window_layout.moc"
