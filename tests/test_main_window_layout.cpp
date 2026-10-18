#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QShortcut>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include "core/ai-profile.h"
#include "core/config-manager.h"
#include "core/models.h"
#include "non-fresh-config.h"
#include "ui/features/output/output-tabs-bar.h"
#include "ui/features/ai/ai-chat-window.h"
#include "ui/features/ai/main-window-agent-host.h"
#include "ui/features/environments/dynamic-vars-inspector-widget.h"
#include "ui/features/history/notification-history-dialog.h"
#include "ui/features/history/process-list-dialog.h"
#include "ui/features/history/run-history-dialog.h"
#include "ui/features/output/output-panel.h"
#include "ui/features/output/quick-run-popup.h"
#include "ui/features/output/run-mirror-panel.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/view-registry.h"
#include "ui/layout/view-zone.h"
#include "ui/main-window.h"
#include "ui/shared/status-line.h"
#include "ui/shared/top-utility-bar.h"
#include "utils/action-shortcuts.h"
#include "utils/translation-manager.h"

using namespace kai;
using namespace kai::ui;
using namespace kai::ui::layout;

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
        const QList<std::pair<Zone, bool>> moves = {{Zone::Right, false}, {Zone::Left, false}, {Zone::Bottom, false}};
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
        QCOMPARE(dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Bottom));

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
        dock->moveView(view_id::kOutput, Zone::Right);
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
        QCOMPARE(dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
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
            window.dockHost()->moveView(view_id::kOutput, Zone::Bottom);
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(layoutPath()), 3000);
        }
        QFile file(layoutPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
        const QJsonObject zones = root.value(QStringLiteral("zones")).toObject();
        QCOMPARE(zones.value(QStringLiteral("bottom")).toObject().value(QStringLiteral("views")).toArray().first().toString(),
                 view_id::kOutput);
        QVERIFY(root.value(QStringLiteral("maximized")).isNull()); // o modo foco não vai para o layout.json

        // E a próxima janela nasce assim.
        MainWindow again;
        QCOMPARE(again.dockHost()->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Bottom));
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
        QCOMPARE(window.dockHost()->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
        QCOMPARE(window.dockHost()->zoneOf(view_id::kCommands), std::optional<Zone>(Zone::Left));
        const QStringList backups = QDir(QFileInfo(layoutPath()).absolutePath()).entryList({QStringLiteral("layout.json.corrupt-*")});
        QCOMPARE(backups.size(), 1);
    }

    void aLayoutFileWithGhostPanelsIsRepaired()
    {
        seed({});
        {
            QFile file(layoutPath());
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(R"({"version":1,"zones":{"left":{"views":["ghost","commands"]},"center":{"views":["commands","output"]}}})");
        }
        MainWindow window;
        DockHost *dock = window.dockHost();
        QVERIFY(!dock->model().isPlaced(QStringLiteral("ghost")));
        QCOMPARE(dock->zoneOf(view_id::kCommands), std::optional<Zone>(Zone::Left));
        QCOMPARE(dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Center));
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
            file.write(R"({"version":1,"zones":{"center":{"views":["commands"]},"bottom":{"views":["output"]}}})");
        }
        MainWindow window;
        QCOMPARE(window.dockHost()->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Bottom));
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
            QCOMPARE(window.dockHost()->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Left));
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
        dock->moveView(view_id::kOutput, Zone::Left);
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

    void theOutputInTheMiddleCannotBeCollapsed()
    {
        seed({});
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        DockHost *dock = window.dockHost();
        dock->moveView(view_id::kCommands, Zone::Left);
        dock->moveView(view_id::kOutput, Zone::Center);
        QTest::qWait(60);
        drawer->setExpanded(false);
        QVERIFY(drawer->isExpanded());
        auto *toggle = drawer->findChild<QToolButton *>(QStringLiteral("terminalDrawerToggle"));
        QVERIFY(toggle != nullptr);
        QVERIFY(toggle->isHidden());
    }

    void collapsingTheOutputGivesTheSpaceBackInEveryZone_data()
    {
        QTest::addColumn<int>("zone");
        QTest::newRow("bottom") << int(Zone::Bottom);
        QTest::newRow("left") << int(Zone::Left);
        QTest::newRow("right") << int(Zone::Right);
    }

    void collapsingTheOutputGivesTheSpaceBackInEveryZone()
    {
        QFETCH(int, zone);
        seed({});
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *drawer = window.findChild<TerminalDrawer *>();
        DockHost *dock = window.dockHost();
        // A lista no miolo, para sempre haver quem receba o espaço devolvido em qualquer zona.
        dock->moveView(view_id::kCommands, Zone::Center);
        dock->moveView(view_id::kOutput, Zone(zone));
        QTest::qWait(80);
        drawer->setExpanded(true);
        QTest::qWait(80);
        QWidget *commands = window.viewRegistry()->existingWidget(view_id::kCommands);
        const bool vertical = Zone(zone) == Zone::Bottom;
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
        emit drawer->outputTabs()->commandsToggleRequested();
        QTest::qWait(60);
        QCOMPARE(dock->model().maximized(), view_id::kOutput);
        QVERIFY(!commands->isVisible());
        // Maximizar o painel da lista a partir do layout (duplo clique na aba) tira a Saída de cena do mesmo jeito.
        emit drawer->outputTabs()->commandsToggleRequested();
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
        QCOMPARE(dock->model().zone(Zone::Right).active, view_id::kNotifications);
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
        QCOMPARE(window.dockHost()->zoneOf(view_id::kVariables), std::optional<Zone>(Zone::Right));
        QVERIFY(window.findChild<DynamicVarsInspectorWidget *>() != nullptr);
        QVERIFY(window.findChild<DynamicVarsInspectorWidget *>()->isVisible());
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
        QVERIFY(!window.findChild<QToolButton *>(QStringLiteral("aiChatButton"))->isVisible());
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
            window.findChild<QToolButton *>(QStringLiteral("aiChatButton"))->click();
            QTest::qWait(80);
            auto *chat = window.findChild<AiChatWindow *>();
            QVERIFY(chat != nullptr);
            QVERIFY(chat->embedded());
            QVERIFY(chat->isVisible());
            QCOMPARE(window.dockHost()->zoneOf(view_id::kAiChat), std::optional<Zone>(Zone::Right));
            window.dockHost()->moveView(view_id::kAiChat, Zone::Left);
            QTest::qWait(60);
        }
        // O próximo Kai nasce com o chat onde o usuário o deixou.
        MainWindow again;
        QCOMPARE(again.dockHost()->zoneOf(view_id::kAiChat), std::optional<Zone>(Zone::Left));
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
        window.dockHost()->moveView(view_id::kOutput, Zone::Bottom);
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
        QCOMPARE(dock->zoneOf(QStringLiteral("run:a")), std::optional<Zone>(Zone::Right));
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
        target.zone = Zone::Left;
        emit dock->overlay()->dropped(QStringLiteral("run:a"), target);
        emit bar->tabDragFinished(QStringLiteral("a"), Qt::CopyAction, QCursor::pos());
        QTRY_COMPARE(dock->zoneOf(QStringLiteral("run:a")), std::optional<Zone>(Zone::Left));
        QVERIFY(!dock->overlay()->isVisible());
        QVERIFY(window.findChild<RunMirrorPanel *>() != nullptr);
        // A guia continua na Saída: o painel é um espelho, não uma mudança de lugar.
        QVERIFY(bar->hasTab(QStringLiteral("a")));
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
        dock->moveView(view_id::kOutput, Zone::Right);
        shot(QStringLiteral("layout-2-output-right"));
        dock->moveView(view_id::kOutput, Zone::Left);
        shot(QStringLiteral("layout-3-output-left"));
        dock->restoreDefault();
        dock->setEditMode(true);
        shot(QStringLiteral("layout-4-rearrange-mode"));
        dock->setEditMode(false);
        dock->moveView(view_id::kCommands, Zone::Bottom, 0);
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
        dock->setZoneCollapsed(Zone::Right, true);
        shot(QStringLiteral("layout-13-right-collapsed-rail"));
        dock->revealView(view_id::kDocument);
        shot(QStringLiteral("layout-14-rail-overlay-open"));
        dock->hideRevealed();
        dock->setZoneCollapsed(Zone::Bottom, true);
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
