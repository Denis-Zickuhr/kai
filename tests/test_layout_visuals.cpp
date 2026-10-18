#include <QApplication>
#include <QDir>
#include <QSplitter>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

#include "core/config-manager.h"
#include "core/models.h"
#include "ui/features/output/terminal-drawer.h"
#include "ui/layout/dock-host.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/view-registry.h"
#include "ui/layout/view-zone.h"
#include "ui/layout/zone-rail.h"
#include "ui/main-window.h"

using namespace kai;
using namespace kai::ui;
using namespace kai::ui::layout;

// O visual dos painéis: a faixa de abas colada no cartão (cantos pelo raio escolhido), o destaque só na zona com o foco, a
// moldura padrão nos painéis que eram diálogos e as faixas laterais. Com KAI_TEST_SCREENSHOT_DIR grava os PNGs para olhar
// (tema e cantos como no uso real: xmas-dark, arredondado).
class TestLayoutVisuals : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;

    void seed(int cornerStyle)
    {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        core::ConfigManager config;
        core::CommandsData data;
        core::Folder folder;
        folder.id = QStringLiteral("f1");
        folder.name = QStringLiteral("Demos");
        data.folders << folder;
        for (const char *name : {"Alpha", "Beta"}) {
            core::Command c;
            c.id = QString::fromLatin1(name).toLower();
            c.folderId = QStringLiteral("f1");
            c.name = QString::fromLatin1(name);
            c.type = core::CommandType::Command;
            c.command = QStringLiteral("echo %1").arg(c.name);
            data.commands << c;
        }
        QVERIFY(config.saveCommands(data));
        core::SettingsData settings = config.loadSettings();
        settings.activeTheme = QStringLiteral("xmas-dark");
        settings.uiCornerStyle = cornerStyle;
        settings.displayActionsPlacement = QStringLiteral("side"); // a coluna de ações ao lado da árvore também entra no quadro
        QVERIFY(config.saveSettings(settings));
    }

    static QString shotDir() { return QString::fromLocal8Bit(qgetenv("KAI_TEST_SCREENSHOT_DIR")); }

    static void shot(MainWindow &window, const QString &name)
    {
        const QString dir = shotDir();
        if (dir.isEmpty()) {
            return;
        }
        QTest::qWait(250);
        window.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    static bool tabbed(QWidget *w) { return w && w->property("kaiTabbed").toBool(); }

private slots:
    void init()
    {
        QDir(m_config.path()).removeRecursively();
        QDir().mkpath(m_config.path());
    }

    // O cartão logo abaixo de uma faixa de abas perde a borda de cima (a faixa é a borda); sem faixa, nada muda.
    void theCardGluesToItsTabStripOnlyWhileTheStripShows()
    {
        seed(2);
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        auto *drawer = window.findChild<TerminalDrawer *>();
        QWidget *document = window.viewRegistry()->existingWidget(view_id::kDocument);
        QVERIFY(tabbed(drawer)); // o padrão mostra a faixa de abas em toda zona: o cartão cola nela
        dock->setEditMode(false);
        QVERIFY(!tabbed(drawer)); // sem a faixa (zona de um painel só), o cartão é inteiro
        dock->setEditMode(true);
        QVERIFY(tabbed(drawer));
        dock->setEditMode(false);
        dock->moveView(view_id::kDocument, Zone::Right); // dois painéis na zona: a faixa aparece sozinha
        document = window.viewRegistry()->existingWidget(view_id::kDocument);
        QVERIFY(tabbed(drawer));
        QVERIFY(tabbed(document));
        // Sai da zona de abas: volta a ter a moldura inteira.
        dock->moveView(view_id::kDocument, Zone::Bottom);
        QVERIFY(!tabbed(drawer));
        QVERIFY(!tabbed(document));
        // A lista de comandos também cola (a árvore e as colunas de ações).
        dock->setEditMode(true);
        auto *commands = window.viewRegistry()->existingWidget(view_id::kCommands);
        bool anyCard = false;
        for (QWidget *child : commands->findChildren<QWidget *>(QStringLiteral("panelCard"))) {
            anyCard = true;
            QVERIFY(tabbed(child));
        }
        QVERIFY(anyCard);

        // Cartões encostados (árvore + coluna de ações) viram uma moldura só: costura reta, em cima e embaixo igual.
        auto *actions = commands->findChild<QSplitter *>(QStringLiteral("actionsSplitter"));
        QVERIFY(actions != nullptr);
        const auto cardOf = [](QWidget *host) -> QWidget * {
            const auto isCard = [](const QWidget *w) {
                return w->objectName() == QLatin1String("panelCard") || w->objectName() == QLatin1String("actionGroupContainer");
            };
            if (isCard(host)) {
                return host;
            }
            for (QWidget *inner : host->findChildren<QWidget *>()) {
                if (isCard(inner)) {
                    return inner;
                }
            }
            return nullptr;
        };
        QList<QWidget *> shown;
        for (int i = 0; i < actions->count(); ++i) {
            if (actions->widget(i)->isVisibleTo(actions)) {
                shown << cardOf(actions->widget(i));
            }
        }
        QVERIFY2(shown.size() >= 2, "a árvore e ao menos uma coluna de ações aparecem");
        for (int i = 0; i < shown.size(); ++i) {
            QCOMPARE(shown.at(i)->property("kaiJoinRight").toBool(), i + 1 < shown.size());
            QCOMPARE(shown.at(i)->property("kaiJoinLeft").toBool(), i > 0);
        }
        // Sem a faixa, voltam a ser cartões separados.
        dock->setEditMode(false);
        for (QWidget *card : std::as_const(shown)) {
            QVERIFY(!card->property("kaiJoinRight").toBool());
            QVERIFY(!card->property("kaiJoinLeft").toBool());
        }
    }

    // O sublinhado forte (cor de destaque) é só da zona com o foco; clicar numa aba mostra o sublinhado nela de imediato.
    void theUnderlineFollowsTheClickedTabAndTheFocusedZone()
    {
        seed(2);
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        DockHost *dock = window.dockHost();
        dock->moveView(view_id::kDocument, Zone::Right);
        QTest::qWait(60);
        ViewZone *right = dock->zoneWidget(Zone::Right);
        ZoneTabStrip *strip = right->strip();
        QCOMPARE(strip->activeId(), view_id::kDocument);
        QTest::mouseClick(strip, Qt::LeftButton, Qt::NoModifier, strip->tabRect(0).center());
        QTest::qWait(30);
        QCOMPARE(strip->activeId(), view_id::kOutput); // a faixa mostra a aba clicada...
        QCOMPARE(right->activeView(), view_id::kOutput);
        QVERIFY(window.findChild<TerminalDrawer *>()->isVisible()); // ...e o painel dela está na frente
        QVERIFY(!window.viewRegistry()->existingWidget(view_id::kDocument)->isVisible());

        // O foco do teclado decide qual zona leva o destaque forte.
        ViewZone *center = dock->zoneWidget(Zone::Left); // a zona da lista
        QVERIFY(center != nullptr);
        window.layoutController()->focusView(view_id::kCommands);
        QTRY_VERIFY(center->strip()->zoneFocused());
        QVERIFY(!right->strip()->zoneFocused());
        window.layoutController()->focusView(view_id::kOutput);
        QTRY_VERIFY(right->strip()->zoneFocused());
        QVERIFY(!center->strip()->zoneFocused());
    }

    void embeddedPanelsLiveInsideTheStandardCard()
    {
        seed(2);
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockHost *dock = window.dockHost();
        for (const QString &id : {view_id::kProcesses, view_id::kHistory, view_id::kNotifications, view_id::kVariables,
                                  view_id::kDocument}) {
            dock->revealView(id);
            QWidget *card = window.viewRegistry()->existingWidget(id);
            QVERIFY2(card != nullptr, qPrintable(id));
            QCOMPARE(card->objectName(), QStringLiteral("panelCard"));
            QVERIFY2(card->isVisible(), qPrintable(id));
            // O conteúdo do cartão está na tela (não só o cartão vazio).
            QVERIFY2(card->findChildren<QWidget *>().size() > 1, qPrintable(id));
            for (QWidget *child : card->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
                QVERIFY2(child->isVisible(), qPrintable(id + QLatin1Char(' ') + QString::fromLatin1(child->metaObject()->className())));
            }
        }
    }

    // O Processos não é vestido com a barra de título de diálogo do Kai (era o caso por ele ser criado antes de entrar no
    // quadro).
    void theProcessesPanelHasNoDialogTitleBar()
    {
        seed(2);
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.dockHost()->revealView(view_id::kProcesses);
        QTest::qWait(60);
        auto *card = window.viewRegistry()->existingWidget(view_id::kProcesses);
        QVERIFY(card != nullptr);
        QVERIFY(card->findChildren<QWidget *>(QStringLiteral("dialogTitleBar")).isEmpty());
        QVERIFY(!card->findChild<QDialog *>()->property("kaiFramed").toBool());
    }

    void screenshots_data()
    {
        QTest::addColumn<int>("corner");
        QTest::newRow("rounded") << 2;
        QTest::newRow("soft") << 1;
        QTest::newRow("straight") << 0;
    }

    void screenshots()
    {
        if (shotDir().isEmpty()) {
            QSKIP("defina KAI_TEST_SCREENSHOT_DIR para gerar as imagens");
        }
        QFETCH(int, corner);
        seed(corner);
        const QString tag = QStringLiteral("vis-%1-").arg(corner);
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        DockHost *dock = window.dockHost();

        shot(window, tag + QStringLiteral("1-default"));

        dock->moveView(view_id::kDocument, Zone::Right);
        shot(window, tag + QStringLiteral("2-output-document-tabs"));

        dock->restoreDefault();
        dock->revealView(view_id::kHistory);
        dock->revealView(view_id::kProcesses);
        shot(window, tag + QStringLiteral("3-processes"));
        dock->revealView(view_id::kHistory);
        shot(window, tag + QStringLiteral("4-history"));
        dock->revealView(view_id::kNotifications);
        shot(window, tag + QStringLiteral("5-notifications"));
        dock->revealView(view_id::kVariables);
        shot(window, tag + QStringLiteral("6-variables"));

        // Faixas laterais: a Saída recolhida à direita e aberta ao lado da lista.
        dock->restoreDefault();
        dock->setZoneCollapsed(Zone::Right, true);
        shot(window, tag + QStringLiteral("7-right-rail"));
        dock->revealView(view_id::kOutput);
        shot(window, tag + QStringLiteral("8-right-overlay"));
        dock->hideRevealed();

        // Faixa de baixo.
        dock->restoreDefault();
        dock->moveView(view_id::kHistory, Zone::Bottom);
        dock->setZoneCollapsed(Zone::Bottom, true);
        dock->revealView(view_id::kHistory);
        shot(window, tag + QStringLiteral("9-bottom-overlay"));
        dock->hideRevealed();

        // Os alvos de soltar durante um arrasto (esquerda e direita cheias, centro e fundo vazios).
        dock->restoreDefault();
        dock->beginViewDrag();
        shot(window, tag + QStringLiteral("10-drag-targets"));
        dock->overlay()->hide();

        // Lista numa faixa à esquerda, Saída e Documento em abas à direita.
        dock->restoreDefault();
        dock->moveView(view_id::kDocument, Zone::Right);
        dock->setZoneCollapsed(Zone::Left, true);
        shot(window, tag + QStringLiteral("11-left-rail"));
    }
};

QTEST_MAIN(TestLayoutVisuals)
#include "test_layout_visuals.moc"
