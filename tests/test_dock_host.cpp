#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRandomGenerator>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QSplitterHandle>
#include <QWindow>
#include <QGraphicsEffect>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include "ui/layout/dock-host.h"
#include "ui/layout/floating-view-window.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/layout-store.h"
#include "ui/layout/view-registry.h"
#include "ui/layout/view-zone.h"
#include "ui/layout/zone-rail.h"
#include "utils/translation-manager.h"

using namespace kai::ui::layout;

namespace {

ViewSpec spec(const QString &id, Zone zone, bool closable = true)
{
    ViewSpec s;
    s.id = id;
    s.titleProvider = [id]() { return id.toUpper(); };
    s.icon = QStringLiteral("square");
    s.defaultZone = zone;
    s.closable = closable;
    return s;
}

// Registro com painéis de brinquedo: Comandos e Saída já construídos (como no MainWindow), os demais criados sob demanda.
struct Rig {
    ViewRegistry registry;
    QWidget owner;
    QLineEdit *commands = new QLineEdit(&owner);
    QLineEdit *output = new QLineEdit(&owner);
    int documentsBuilt = 0;
    DockHost *dock = nullptr;

    Rig()
    {
        owner.resize(1000, 700);
        commands->setText(QStringLiteral("typed in commands"));
        output->setText(QStringLiteral("typed in output"));
        registry.registerView(spec(view_id::kCommands, Zone::Center), commands);
        registry.registerView(spec(view_id::kOutput, Zone::Bottom), output);
        ViewSpec document = spec(view_id::kDocument, Zone::Right);
        document.factory = [this]() {
            ++documentsBuilt;
            return new QLabel(QStringLiteral("doc"));
        };
        registry.registerView(document);
        ViewSpec history = spec(view_id::kHistory, Zone::Right);
        history.factory = []() { return new QLabel(QStringLiteral("history")); };
        registry.registerView(history);
        registry.registerPrefix(view_id::kKipPrefix, [](const QString &id) -> std::optional<ViewSpec> {
            if (id != QStringLiteral("kip:alive")) {
                return std::nullopt;
            }
            ViewSpec s = spec(id, Zone::Right);
            s.factory = []() { return new QLabel(QStringLiteral("kip")); };
            return s;
        });
        dock = new DockHost(&registry, &owner, &owner);
        dock->setGeometry(0, 0, 1000, 700);
        // O arranjo clássico sem as abas fixas: as zonas de um painel só não têm faixa (as de abas mostram só com mais de um).
        LayoutModel model = LayoutModel::preset(LayoutModel::Preset::Classic);
        model.setTabsAlways(false);
        dock->setModel(model);
    }

    QRect railIcon(Zone zone, const QString &id) const
    {
        ZoneRail *rail = dock->railWidget(zone);
        return rail ? rail->iconRect(id) : QRect();
    }

    void show()
    {
        owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        QTest::qWait(30);
    }
};

} // namespace

class TestDockHost : public QObject {
    Q_OBJECT

private slots:
    void defaultLayoutPutsCommandsAboveTheOutput()
    {
        Rig rig;
        rig.show();
        QVERIFY(rig.dock->zoneWidget(Zone::Center)->isAncestorOf(rig.commands));
        QVERIFY(rig.dock->zoneWidget(Zone::Bottom)->isAncestorOf(rig.output));
        QVERIFY(!rig.dock->zoneWidget(Zone::Left));
        QVERIFY(!rig.dock->zoneWidget(Zone::Right));
        QVERIFY(rig.commands->isVisible());
        QVERIFY(rig.output->isVisible());
        QVERIFY(rig.commands->y() < rig.output->mapTo(&rig.owner, QPoint()).y());
        QVERIFY(!rig.dock->emptyStateShown());
    }

    void aSinglePanelZoneHasNoTabStrip()
    {
        Rig rig;
        rig.show();
        QVERIFY(!rig.dock->zoneWidget(Zone::Center)->strip()->isVisibleTo(rig.dock->zoneWidget(Zone::Center)));
        QVERIFY(!rig.dock->zoneWidget(Zone::Bottom)->strip()->isVisibleTo(rig.dock->zoneWidget(Zone::Bottom)));
    }

    void rearrangeModeShowsEveryStrip()
    {
        Rig rig;
        rig.show();
        rig.dock->setEditMode(true);
        QVERIFY(rig.dock->zoneWidget(Zone::Center)->strip()->isVisibleTo(rig.dock->zoneWidget(Zone::Center)));
        QVERIFY(rig.dock->zoneWidget(Zone::Bottom)->strip()->isVisibleTo(rig.dock->zoneWidget(Zone::Bottom)));
        rig.dock->setEditMode(false);
        QVERIFY(!rig.dock->zoneWidget(Zone::Bottom)->strip()->isVisibleTo(rig.dock->zoneWidget(Zone::Bottom)));
    }

    void movingAPanelKeepsTheVeryWidgetAndItsState()
    {
        Rig rig;
        rig.show();
        QPointer<QLineEdit> output = rig.output;
        rig.dock->moveView(view_id::kOutput, Zone::Right);
        QTest::qWait(30);
        QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
        QVERIFY(output); // não foi recriado nem destruído
        QCOMPARE(output.data(), rig.output);
        QCOMPARE(rig.output->text(), QStringLiteral("typed in output"));
        QVERIFY(rig.dock->zoneWidget(Zone::Right)->isAncestorOf(rig.output));
        QVERIFY(!rig.dock->zoneWidget(Zone::Bottom));
        QVERIFY(rig.output->isVisible());
        // E a lista ficou sozinha no miolo, ocupando a altura toda.
        QVERIFY(rig.commands->isVisible());
    }

    void movingBetweenEveryZoneNeverLosesAPanel()
    {
        Rig rig;
        rig.show();
        for (Zone zone : {Zone::Left, Zone::Right, Zone::Bottom, Zone::Center, Zone::Left, Zone::Bottom}) {
            rig.dock->moveView(view_id::kOutput, zone);
            rig.dock->moveView(view_id::kCommands, zone == Zone::Left ? Zone::Right : Zone::Left);
            QTest::qWait(10);
            QVERIFY(rig.output->isVisible());
            QVERIFY(rig.commands->isVisible());
            QVERIFY(rig.output->width() > 0 && rig.output->height() > 0);
        }
    }

    void tabbingTwoPanelsInOneZoneShowsTheStripAndOnlyTheFrontOne()
    {
        Rig rig;
        rig.show();
        rig.dock->moveView(view_id::kDocument, Zone::Bottom);
        QTest::qWait(30);
        ViewZone *bottom = rig.dock->zoneWidget(Zone::Bottom);
        QVERIFY(bottom->strip()->isVisibleTo(bottom));
        QCOMPARE(bottom->viewIds(), (QStringList{view_id::kOutput, view_id::kDocument}));
        QCOMPARE(bottom->activeView(), view_id::kDocument);
        QVERIFY(!rig.output->isVisible());
        QCOMPARE(rig.documentsBuilt, 1);
        rig.dock->revealView(view_id::kOutput);
        QVERIFY(rig.output->isVisible());
        QCOMPARE(bottom->activeView(), view_id::kOutput);
        QCOMPARE(rig.documentsBuilt, 1); // sob demanda, uma vez só
    }

    void closingAndRevealingReturnsToTheLastZone()
    {
        Rig rig;
        rig.show();
        rig.dock->moveView(view_id::kHistory, Zone::Left);
        rig.dock->closeView(view_id::kHistory);
        QVERIFY(!rig.dock->model().isPlaced(view_id::kHistory));
        QVERIFY(!rig.dock->zoneWidget(Zone::Left));
        QVERIFY(rig.dock->revealView(view_id::kHistory));
        QCOMPARE(rig.dock->zoneOf(view_id::kHistory), std::optional<Zone>(Zone::Left));
    }

    void revealingAnUnknownPanelDoesNothing()
    {
        Rig rig;
        QVERIFY(!rig.dock->revealView(QStringLiteral("nope")));
        QVERIFY(!rig.dock->model().isPlaced(QStringLiteral("nope")));
    }

    void aClosedPanelWaitsHiddenAndDoesNotCoverTheWindow()
    {
        Rig rig;
        rig.show();
        rig.dock->closeView(view_id::kOutput);
        QTest::qWait(30);
        QVERIFY(!rig.output->isVisible());
        QVERIFY(rig.commands->isVisible());
    }

    void closingEverythingShowsAnEmptyStateWithAWayBack()
    {
        Rig rig;
        rig.show();
        rig.dock->closeView(view_id::kOutput);
        rig.dock->closeView(view_id::kCommands);
        QTest::qWait(30);
        QVERIFY(rig.dock->emptyStateShown());
        auto *button = rig.dock->findChild<QPushButton *>(QStringLiteral("dockEmptyRestore"));
        QVERIFY(button != nullptr);
        QSignalSpy restore(rig.dock, &DockHost::restoreRequested);
        button->click();
        QCOMPARE(restore.count(), 1);
        rig.dock->restoreDefault();
        QTest::qWait(30);
        QVERIFY(!rig.dock->emptyStateShown());
        QVERIFY(rig.commands->isVisible());
        QVERIFY(rig.output->isVisible());
    }

    void aPanelThatCannotCloseIsNeverClosed()
    {
        Rig rig;
        rig.registry.registerView(spec(QStringLiteral("pinned"), Zone::Left, false), new QLabel(&rig.owner));
        LayoutModel model = rig.dock->model();
        model.placeView(QStringLiteral("pinned"), Zone::Left);
        rig.dock->setModel(model);
        rig.dock->closeView(QStringLiteral("pinned"));
        QVERIFY(rig.dock->model().isPlaced(QStringLiteral("pinned")));
    }

    void maximizingShowsOnlyThatZoneAndRestoringBringsBackTheRest()
    {
        Rig rig;
        rig.show();
        QSignalSpy maximized(rig.dock, &DockHost::maximizedChanged);
        rig.dock->setMaximized(view_id::kOutput);
        QTest::qWait(30);
        QVERIFY(rig.output->isVisible());
        QVERIFY(!rig.commands->isVisible());
        QVERIFY(!rig.dock->zoneWidget(Zone::Center));
        QCOMPARE(maximized.count(), 1);
        rig.dock->toggleMaximize(view_id::kOutput);
        QTest::qWait(30);
        QVERIFY(rig.commands->isVisible());
        QVERIFY(rig.output->isVisible());
        QCOMPARE(maximized.count(), 2);
    }

    void revealingAPanelHiddenByTheMaximizedOneRestoresTheLayout()
    {
        Rig rig;
        rig.show();
        rig.dock->setMaximized(view_id::kOutput);
        QVERIFY(rig.dock->revealView(view_id::kCommands));
        QTest::qWait(30);
        QVERIFY(rig.dock->model().maximized().isEmpty());
        QVERIFY(rig.commands->isVisible());
    }

    void userChangesEmitModelEditedButSetModelDoesNot()
    {
        Rig rig;
        QSignalSpy edited(rig.dock, &DockHost::modelEdited);
        rig.dock->setModel(LayoutModel::preset(LayoutModel::Preset::Http));
        QCOMPARE(edited.count(), 0);
        rig.dock->moveView(view_id::kOutput, Zone::Bottom);
        QCOMPARE(edited.count(), 1);
        rig.dock->moveView(view_id::kOutput, Zone::Bottom); // já está lá
        QCOMPARE(edited.count(), 1);
        rig.dock->closeView(view_id::kHistory); // nunca esteve aberto
        QCOMPARE(edited.count(), 1);
    }

    void draggingASplitterHandleIsRememberedAsAProportion()
    {
        Rig rig;
        rig.show();
        QSplitter *mid = rig.dock->midSplitter();
        QVERIFY(mid != nullptr);
        QSignalSpy edited(rig.dock, &DockHost::modelEdited);
        const int total = mid->sizes().at(0) + mid->sizes().at(1);
        mid->setSizes({int(total * 0.8), total - int(total * 0.8)});
        emit mid->splitterMoved(int(total * 0.8), 1);
        QVERIFY(qAbs(rig.dock->model().effectiveSize(Zone::Bottom) - 0.2) < 0.02);
        QVERIFY(edited.count() >= 1);
        // A janela cresce: a proporção continua.
        rig.owner.resize(1300, 900);
        rig.dock->setGeometry(0, 0, 1300, 900);
        QTest::qWait(60);
        const QList<int> sizes = mid->sizes();
        QVERIFY2(qAbs(double(sizes.at(1)) / (sizes.at(0) + sizes.at(1)) - 0.2) < 0.03,
                 qPrintable(QString::number(double(sizes.at(1)) / (sizes.at(0) + sizes.at(1)))));
    }

    // As quatro regiões da dica são sempre as mesmas, qualquer que seja o layout: esquerda e direita de ponta a ponta, fundo
    // na largura toda e centro no resto.
    void dropTargetsAreTheSameFourRegionsWhateverTheLayout()
    {
        for (const bool classic : {true, false}) {
            Rig rig;
            if (!classic) {
                rig.dock->setModel(LayoutModel::defaultLayout());
            }
            rig.show();
            const int w = rig.dock->width();
            const int h = rig.dock->height();
            const DropTarget left = rig.dock->dropTargetAt(QPoint(10, h / 2));
            QVERIFY(left.valid);
            QCOMPARE(left.zone, Zone::Left);
            QCOMPARE(left.rect, QRect(0, 0, left.rect.width(), h));
            QCOMPARE(rig.dock->dropTargetAt(QPoint(w - 10, h / 2)).zone, Zone::Right);
            QCOMPARE(rig.dock->dropTargetAt(QPoint(w / 2, h - 10)).zone, Zone::Bottom);
            const DropTarget center = rig.dock->dropTargetAt(QPoint(w / 2, h / 3));
            QCOMPARE(center.zone, Zone::Center);
            QVERIFY(center.rect.width() > w / 2);
            // O fundo cobre a largura toda; as pontas (esquerda/direita) vencem onde se cruzam.
            QCOMPARE(rig.dock->dropTargetAt(QPoint(w / 3, h - 10)).zone, Zone::Bottom);
            QCOMPARE(rig.dock->dropTargetAt(QPoint(10, h - 10)).zone, Zone::Left);
        }
    }

    // Soltar sobre a faixa de abas de uma zona continua inserindo naquela zona, na posição das abas.
    void droppingOnATabStripStillInsertsInThatZone()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        ViewZone *right = rig.dock->zoneWidget(Zone::Right);
        QVERIFY(right && right->strip()->isVisibleTo(right));
        const QPoint onStrip = right->strip()->mapTo(rig.dock, QPoint(right->strip()->width() / 2, right->strip()->height() / 2));
        const DropTarget target = rig.dock->dropTargetAt(onStrip);
        QVERIFY(target.valid);
        QCOMPARE(target.zone, Zone::Right);
        QVERIFY(target.index >= 0);
    }

    // Sem Centro, o Fundo fica EMBAIXO das laterais, na largura toda (não numa coluna entre elas).
    void theBottomSitsBelowTheSidesWhenThereIsNoCenter()
    {
        Rig rig;
        rig.dock->moveView(view_id::kCommands, Zone::Left);
        rig.dock->moveView(view_id::kOutput, Zone::Left);
        rig.dock->moveView(view_id::kHistory, Zone::Right);
        rig.dock->moveView(view_id::kDocument, Zone::Bottom);
        rig.show();
        QVERIFY(!rig.dock->zoneWidget(Zone::Center));
        const auto rectOf = [&](Zone zone) {
            QWidget *w = rig.dock->zoneWidget(zone);
            return QRect(w->mapTo(rig.dock, QPoint(0, 0)), w->size());
        };
        const QRect left = rectOf(Zone::Left);
        const QRect right = rectOf(Zone::Right);
        const QRect bottom = rectOf(Zone::Bottom);
        QVERIFY2(bottom.top() > left.bottom() - 2 && bottom.top() > right.bottom() - 2, "o fundo fica sob as laterais");
        QVERIFY2(bottom.width() > left.width() + right.width(), "e na largura toda");
        QVERIFY(rig.dock->isViewVisible(view_id::kDocument));
        // O tamanho do fundo se arrasta e fica salvo, como no arranjo com Centro.
        QSplitter *vertical = rig.dock->verticalSplitter();
        QVERIFY(vertical != nullptr && vertical->count() == 2);
        const double before = rig.dock->model().effectiveSize(Zone::Bottom);
        const int total = vertical->sizes().at(0) + vertical->sizes().at(1);
        vertical->setSizes({int(total * 0.3), total - int(total * 0.3)});
        emit vertical->splitterMoved(int(total * 0.3), 1);
        QVERIFY(rig.dock->model().effectiveSize(Zone::Bottom) > before + 0.05);
        // Com o Centro de volta, o Fundo volta a ser só do miolo.
        rig.dock->moveView(view_id::kHistory, Zone::Center);
        QTest::qWait(30);
        QVERIFY(rig.dock->midSplitter() != nullptr);
        QCOMPARE(rig.dock->verticalSplitter(), rig.dock->midSplitter());
        QVERIFY(rectOf(Zone::Bottom).width() < rig.dock->width());
    }

    void aDropAppliedAfterTheDragMovesThePanel()
    {
        Rig rig;
        rig.show();
        ViewZone *bottom = rig.dock->zoneWidget(Zone::Bottom);
        DropTarget target;
        target.valid = true;
        target.zone = Zone::Right;
        emit rig.dock->overlay()->dropped(view_id::kOutput, target);
        emit bottom->dragFinished(view_id::kOutput, Qt::MoveAction, QCursor::pos());
        QTRY_COMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
        QVERIFY(rig.output->isVisible());
    }

    void droppingOutsideTheWindowFloatsThePanel()
    {
        Rig rig;
        rig.show();
        ViewZone *bottom = rig.dock->zoneWidget(Zone::Bottom);
        emit bottom->dragFinished(view_id::kOutput, Qt::IgnoreAction, QPoint(-5000, -5000));
        QTRY_VERIFY(rig.dock->model().isFloating(view_id::kOutput));
        QVERIFY(rig.dock->floatingWindow(view_id::kOutput) != nullptr);
    }

    void aCancelledDropInsideTheWindowChangesNothing()
    {
        Rig rig;
        rig.show();
        ViewZone *bottom = rig.dock->zoneWidget(Zone::Bottom);
        const QPoint inside = rig.owner.mapToGlobal(QPoint(50, 50));
        emit bottom->dragFinished(view_id::kOutput, Qt::IgnoreAction, inside);
        QTest::qWait(30);
        QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Bottom));
    }

    void floatingMovesThePanelIntoItsOwnWindowAndDockingBringsItBack()
    {
        Rig rig;
        rig.show();
        rig.dock->floatView(view_id::kOutput);
        QTest::qWait(30);
        FloatingViewWindow *window = rig.dock->floatingWindow(view_id::kOutput);
        QVERIFY(window != nullptr);
        QVERIFY(window->isAncestorOf(rig.output));
        QVERIFY(window->isVisible());
        QCOMPARE(rig.output->text(), QStringLiteral("typed in output"));
        QVERIFY(!rig.dock->zoneWidget(Zone::Bottom));

        window->dockButton()->click();
        QTest::qWait(60);
        QVERIFY(!rig.dock->floatingWindow(view_id::kOutput));
        QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Bottom));
        QVERIFY(rig.dock->zoneWidget(Zone::Bottom)->isAncestorOf(rig.output));
    }

    void hidingTheOwnerWindowHidesTheFloatingOnesAndTheyDoNotComeBackAlone()
    {
        Rig rig;
        rig.show();
        rig.dock->floatView(view_id::kOutput);
        QTest::qWait(30);
        FloatingViewWindow *window = rig.dock->floatingWindow(view_id::kOutput);
        QVERIFY(window->isVisible());
        rig.dock->setFloatingVisible(false);
        QVERIFY(!window->isVisible());
        // Mexer no layout enquanto a janela está escondida não pode reabrir nada.
        rig.dock->moveView(view_id::kCommands, Zone::Left);
        QTest::qWait(30);
        QVERIFY(!rig.dock->floatingWindow(view_id::kOutput)->isVisible());
        rig.dock->setFloatingVisible(true);
        QVERIFY(rig.dock->floatingWindow(view_id::kOutput)->isVisible());
    }

    void floatingWindowsNeverAppearWhileTheOwnerIsHidden()
    {
        Rig rig; // o dono nunca foi mostrado
        LayoutModel model = LayoutModel::defaultLayout();
        model.floatView(view_id::kHistory, QRect(10, 10, 400, 300));
        rig.dock->setModel(model);
        FloatingViewWindow *window = rig.dock->floatingWindow(view_id::kHistory);
        QVERIFY(window != nullptr);
        QTest::qWait(30);
        QVERIFY(!window->isVisible());
    }

    void closingAFloatingWindowClosesThePanel()
    {
        Rig rig;
        rig.show();
        rig.dock->floatView(view_id::kHistory);
        QTest::qWait(30);
        FloatingViewWindow *window = rig.dock->floatingWindow(view_id::kHistory);
        QVERIFY(window != nullptr);
        window->closeButton()->click();
        QTest::qWait(30);
        QVERIFY(!rig.dock->model().isPlaced(view_id::kHistory));
        QVERIFY(!rig.dock->floatingWindow(view_id::kHistory));
    }

    void pinningAFloatingWindowIsRemembered()
    {
        Rig rig;
        rig.show();
        rig.dock->floatView(view_id::kHistory);
        FloatingViewWindow *window = rig.dock->floatingWindow(view_id::kHistory);
        QVERIFY(!window->alwaysOnTop());
        window->pinButton()->click();
        QVERIFY(window->alwaysOnTop());
        QVERIFY(rig.dock->model().floating().first().alwaysOnTop);
    }

    void aKipPanelIsCreatedFromItsPrefixOnDemand()
    {
        Rig rig;
        QVERIFY(rig.registry.isKnown(QStringLiteral("kip:alive")));
        QVERIFY(!rig.registry.isKnown(QStringLiteral("kip:gone")));
        QVERIFY(!rig.registry.isKnown(QStringLiteral("kip:")));
        QVERIFY(rig.dock->revealView(QStringLiteral("kip:alive")));
        QCOMPARE(rig.dock->zoneOf(QStringLiteral("kip:alive")), std::optional<Zone>(Zone::Right));
        QVERIFY(rig.registry.existingWidget(QStringLiteral("kip:alive")) != nullptr);
    }

    void panelMenuOffersMoveFloatMaximizeAndClose()
    {
        Rig rig;
        QMenu menu;
        rig.dock->populatePanelMenu(&menu, view_id::kOutput);
        QStringList texts;
        for (QAction *action : menu.actions()) {
            texts << action->text();
        }
        QVERIFY(texts.size() >= 4);
        QVERIFY(menu.actions().first()->menu() != nullptr); // "Mover para" é um submenu
        QCOMPARE(menu.actions().first()->menu()->actions().size(), 4);
    }

    void borderDragResizesASideZoneWithinLimits()
    {
        Rig rig;
        rig.dock->moveView(view_id::kOutput, Zone::Right);
        rig.show();
        const int before = rig.dock->zoneWidth(Zone::Right);
        QVERIFY(before > 0);
        QVERIFY(rig.dock->setZoneWidth(Zone::Right, before + 40));
        QVERIFY(rig.dock->zoneWidth(Zone::Right) > before);
        QVERIFY(rig.dock->setZoneWidth(Zone::Right, 1)); // piso do painel, nunca zero
        QVERIFY(rig.dock->zoneWidth(Zone::Right) > 1);
        QVERIFY(!rig.dock->setZoneWidth(Zone::Left, 200)); // zona que não existe
    }

    // Embaralha as operações do usuário (mover, fechar, flutuar, encaixar, maximizar, recolher, modo reorganizar, abrir)
    // e confere os invariantes depois de cada uma: nenhum painel some, duplica ou fica visível fora do lugar dele.
    void randomOperationsNeverLoseOrDuplicateAPanel_data()
    {
        QTest::addColumn<quint32>("seed");
        for (quint32 seed : {20261017u, 7u, 42u, 99991u, 123456u}) {
            QTest::newRow(qPrintable(QString::number(seed))) << seed;
        }
    }

    void randomOperationsNeverLoseOrDuplicateAPanel()
    {
        QFETCH(quint32, seed);
        Rig rig;
        rig.show();
        const QStringList ids = {view_id::kCommands, view_id::kOutput, view_id::kDocument, view_id::kHistory,
                                 QStringLiteral("kip:alive")};
        const Zone zones[] = {Zone::Left, Zone::Center, Zone::Right, Zone::Bottom};
        QRandomGenerator random(seed);
        for (int step = 0; step < 400; ++step) {
            const QString id = ids.at(int(random.bounded(ids.size())));
            const Zone zone = zones[random.bounded(4)];
            switch (random.bounded(10)) {
            case 0:
            case 1:
            case 2:
                rig.dock->moveView(id, zone, random.bounded(3) == 0 ? 0 : -1);
                break;
            case 3:
                rig.dock->closeView(id);
                break;
            case 4:
                rig.dock->floatView(id);
                break;
            case 5:
                rig.dock->dockView(id);
                break;
            case 6:
                rig.dock->toggleMaximize(id);
                break;
            case 7:
                rig.dock->setZoneCollapsed(zone, random.bounded(2) == 0);
                break;
            case 8:
                rig.dock->setEditMode(random.bounded(2) == 0);
                break;
            default:
                rig.dock->revealView(id);
                break;
            }
            QCoreApplication::processEvents();

            const LayoutModel &model = rig.dock->model();
            // Sem painel duplicado no modelo.
            const QStringList placed = model.placedViews();
            QCOMPARE(QSet<QString>(placed.begin(), placed.end()).size(), int(placed.size()));
            for (const QString &view : ids) {
                QWidget *widget = rig.registry.existingWidget(view);
                if (!widget) {
                    QVERIFY2(!model.isPlaced(view), qPrintable(view)); // colocado mas nunca construído
                    continue;
                }
                int homes = 0;
                for (Zone candidate : zones) {
                    if (ViewZone *container = rig.dock->zoneWidget(candidate); container && container->isAncestorOf(widget)) {
                        ++homes;
                    }
                }
                if (FloatingViewWindow *window = rig.dock->floatingWindow(view); window && window->isAncestorOf(widget)) {
                    ++homes;
                }
                QVERIFY2(homes <= 1, qPrintable(QStringLiteral("%1 em %2 lugares (passo %3)").arg(view).arg(homes).arg(step)));
                // Fora do modelo: escondido. Dentro de uma zona sem estar na frente: escondido.
                if (!model.isPlaced(view)) {
                    QVERIFY2(!widget->isVisible(), qPrintable(QStringLiteral("%1 visível sem lugar (passo %2)").arg(view).arg(step)));
                }
                if (widget->isVisible()) {
                    QVERIFY2(rig.dock->isViewVisible(view), qPrintable(QStringLiteral("%1 passo %2").arg(view).arg(step)));
                }
            }
            // As flutuantes do modelo e as janelas existentes coincidem.
            for (const FloatingView &floating : model.floating()) {
                QVERIFY2(rig.dock->floatingWindow(floating.view) != nullptr, qPrintable(floating.view));
            }
            // O quadro nunca fica sem caminho de volta: ou há algo na tela ou o estado vazio está no ar.
            if (model.placedCount() == 0) {
                QVERIFY(rig.dock->emptyStateShown());
            }
        }
        // Qualquer bagunça se desfaz com um "Restaurar layout".
        rig.dock->hideRevealed();
        rig.dock->restoreDefault();
        QCOMPARE(rig.dock->model(), LayoutModel::defaultLayout());
        QTest::qWait(30);
        QVERIFY(rig.commands->isVisible());
        QVERIFY(rig.output->isVisible());
    }

    // ---- O padrão novo: lista à esquerda, Saída à direita, abas à mostra -------------------------------------------------
    void theNewDefaultShowsTwoZonesWithTheirTabStrips()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        ViewZone *left = rig.dock->zoneWidget(Zone::Left);
        ViewZone *right = rig.dock->zoneWidget(Zone::Right);
        QVERIFY(left && right);
        QVERIFY(left->isAncestorOf(rig.commands));
        QVERIFY(right->isAncestorOf(rig.output));
        QVERIFY(left->strip()->isVisibleTo(left)); // abas à mostra mesmo com um painel só
        QVERIFY(right->strip()->isVisibleTo(right));
        QVERIFY(!rig.dock->zoneWidget(Zone::Center));
        // Metade da janela para cada uma.
        QVERIFY2(qAbs(left->width() - right->width()) < 30, qPrintable(QStringLiteral("%1 x %2").arg(left->width()).arg(right->width())));
        // E as abas se desligam (e a escolha fica no modelo, para voltar como estava).
        QSignalSpy edited(rig.dock, &DockHost::modelEdited);
        rig.dock->setEditMode(false);
        QCOMPARE(edited.count(), 1);
        QVERIFY(!rig.dock->model().tabsAlways());
        QVERIFY(!rig.dock->zoneWidget(Zone::Left)->strip()->isVisibleTo(rig.dock->zoneWidget(Zone::Left)));
    }

    void draggingTheMiddleSplitterBetweenTwoSidesKeepsTheProportion()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        QSplitter *root = rig.dock->rootSplitter();
        QVERIFY(root && root->count() == 2);
        const int total = root->sizes().at(0) + root->sizes().at(1);
        root->setSizes({int(total * 0.3), total - int(total * 0.3)});
        emit root->splitterMoved(int(total * 0.3), 1);
        QVERIFY(qAbs(rig.dock->model().effectiveSize(Zone::Left) - 0.3) < 0.02);
        rig.owner.resize(1300, 800);
        rig.dock->setGeometry(0, 0, 1300, 800);
        QTest::qWait(60);
        const QList<int> sizes = root->sizes();
        QVERIFY2(qAbs(double(sizes.at(0)) / (sizes.at(0) + sizes.at(1)) - 0.3) < 0.03,
                 qPrintable(QString::number(double(sizes.at(0)) / (sizes.at(0) + sizes.at(1)))));
    }

    // ---- O painel aberto a partir de uma faixa ocupa espaço e se redimensiona --------------------------------------------
    void aRevealedZonePushesTheContentInsteadOfCoveringIt()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        rig.dock->setZoneCollapsed(Zone::Right, true);
        QTest::qWait(30);
        const int wide = rig.commands->width();
        QVERIFY(rig.dock->revealView(view_id::kOutput));
        QTest::qWait(60);
        ViewZone *zone = rig.dock->zoneWidget(Zone::Right);
        QVERIFY(zone != nullptr);
        QVERIFY(rig.dock->railWidget(Zone::Right) != nullptr); // a faixa continua lá para fechar
        QCOMPARE(rig.dock->revealedView(Zone::Right), view_id::kOutput);
        QVERIFY(rig.dock->isViewVisible(view_id::kOutput));
        QVERIFY(rig.output->isVisible());
        // A faixa de ícones já troca de painel: a faixa de abas de cima não aparece na zona aberta.
        QVERIFY(!zone->strip()->isVisibleTo(zone));
        // Empurra: a lista encolheu e as duas não se sobrepõem.
        QVERIFY(rig.commands->width() < wide);
        const QRect list(rig.commands->mapTo(rig.dock, QPoint(0, 0)), rig.commands->size());
        const QRect opened(zone->mapTo(rig.dock, QPoint(0, 0)), zone->size());
        QVERIFY2(!list.intersects(opened), "o painel aberto não pode cobrir a lista");
        // Continua marcada como recolhida no modelo (é só uma abertura da sessão).
        QVERIFY(rig.dock->model().zone(Zone::Right).collapsed);
        // Clicar no ícone da faixa de novo fecha e devolve o espaço.
        ZoneRail *rail = rig.dock->railWidget(Zone::Right);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kOutput).center());
        QTest::qWait(60);
        QVERIFY(rig.dock->zoneWidget(Zone::Right) == nullptr);
        QVERIFY(rig.dock->revealedView(Zone::Right).isEmpty());
        QCOMPARE(rig.commands->width(), wide);
    }

    void aRevealedZoneHidesItsTabStripEvenWithSeveralPanels()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, Zone::Right);
        rig.dock->moveView(view_id::kDocument, Zone::Right);
        rig.show();
        QVERIFY(rig.dock->zoneWidget(Zone::Right)->strip()->isVisibleTo(rig.dock->zoneWidget(Zone::Right)));
        rig.dock->setZoneCollapsed(Zone::Right, true);
        QVERIFY(rig.dock->revealView(view_id::kHistory));
        QTest::qWait(60);
        ViewZone *zone = rig.dock->zoneWidget(Zone::Right);
        QVERIFY(zone != nullptr);
        QVERIFY(!zone->strip()->isVisibleTo(zone));
        // Trocar de painel pelo ícone da faixa segue funcionando, ainda sem as abas.
        ZoneRail *rail = rig.dock->railWidget(Zone::Right);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kDocument).center());
        QTest::qWait(60);
        QCOMPARE(rig.dock->revealedView(Zone::Right), view_id::kDocument);
        QVERIFY(!rig.dock->zoneWidget(Zone::Right)->strip()->isVisibleTo(rig.dock->zoneWidget(Zone::Right)));
        // Expandida de vez, as abas voltam.
        rig.dock->setZoneCollapsed(Zone::Right, false);
        QTest::qWait(60);
        QVERIFY(rig.dock->zoneWidget(Zone::Right)->strip()->isVisibleTo(rig.dock->zoneWidget(Zone::Right)));
    }

    // O divisor entre o conteúdo e o painel aberto responde ao mouse de verdade (roteado pela janela) e o tamanho fica salvo.
    void aRevealedZoneIsResizedByDraggingTheSplitter()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        rig.dock->setZoneCollapsed(Zone::Right, true);
        QVERIFY(rig.dock->revealView(view_id::kOutput));
        QTest::qWait(60);
        ViewZone *zone = rig.dock->zoneWidget(Zone::Right);
        QVERIFY(zone != nullptr);
        QSplitter *root = rig.dock->rootSplitter();
        const int index = root->indexOf(zone);
        QVERIFY(index > 0);
        QSplitterHandle *handle = root->handle(index);
        QVERIFY(handle != nullptr);
        const QPoint center = handle->mapTo(&rig.owner, handle->rect().center());
        QCOMPARE(rig.owner.childAt(center), static_cast<QWidget *>(handle));
        const int before = zone->width();
        const double sizeBefore = rig.dock->model().effectiveSize(Zone::Right);
        QSignalSpy edited(rig.dock, &DockHost::modelEdited);
        QWindow *window = rig.owner.windowHandle();
        QVERIFY(window != nullptr);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(window, center - QPoint(60, 0));
        QTest::mouseMove(window, center - QPoint(120, 0));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center - QPoint(120, 0));
        QVERIFY2(zone->width() > before + 60, qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(zone->width())));
        QVERIFY(edited.count() >= 1);
        QVERIFY(rig.dock->model().effectiveSize(Zone::Right) > sizeBefore + 0.03);
        // Expandir de vez mantém o que o usuário escolheu.
        rig.dock->setZoneCollapsed(Zone::Right, false);
        QTest::qWait(60);
        QVERIFY(rig.dock->zoneWidget(Zone::Right));
        QVERIFY(rig.dock->model().effectiveSize(Zone::Right) > sizeBefore + 0.03);
    }

    // ---- Recolher uma zona para a lateral ----------------------------------------------------------------------------
    void aCollapsedZoneBecomesARailOfIcons()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, Zone::Right);
        rig.dock->moveView(view_id::kDocument, Zone::Right);
        rig.show();
        QVERIFY(rig.dock->zoneWidget(Zone::Right));
        rig.dock->setZoneCollapsed(Zone::Right, true);
        QTest::qWait(30);
        QVERIFY(!rig.dock->zoneWidget(Zone::Right)); // a zona saiu do quadro
        ZoneRail *rail = rig.dock->railWidget(Zone::Right);
        QVERIFY(rail != nullptr);
        QVERIFY(rail->isVisible());
        QCOMPARE(rail->width(), ZoneRail::kThickness);
        QVERIFY(rail->iconRect(view_id::kHistory).isValid());
        QVERIFY(rail->iconRect(view_id::kDocument).isValid());
        QVERIFY(!rig.registry.existingWidget(view_id::kHistory)->isVisible());
        QVERIFY(rig.dock->model().zone(Zone::Right).collapsed);
        // A lista e a Saída ganharam o espaço.
        QVERIFY(rig.commands->width() > 700);
    }

    void clickingARailIconOpensThePanelBesideTheContentAndClickingAgainClosesIt()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, Zone::Right);
        rig.show();
        rig.dock->setZoneCollapsed(Zone::Right, true);
        QTest::qWait(30);
        ZoneRail *rail = rig.dock->railWidget(Zone::Right);
        QVERIFY(rail);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QTest::qWait(60);
        QCOMPARE(rig.dock->revealedView(Zone::Right), view_id::kHistory);
        QVERIFY(rig.dock->zoneWidget(Zone::Right));
        QVERIFY(rig.dock->isViewVisible(view_id::kHistory));
        QVERIFY(rig.registry.existingWidget(view_id::kHistory)->isVisible());
        QVERIFY(rig.dock->zoneWidget(Zone::Right)->isAncestorOf(rig.registry.existingWidget(view_id::kHistory)));
        QVERIFY(rig.commands->isVisible());
        // Clicar de novo no ícone fecha.
        rail = rig.dock->railWidget(Zone::Right);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QTest::qWait(60);
        QVERIFY(rig.dock->revealedView(Zone::Right).isEmpty());
        QVERIFY(!rig.dock->isViewVisible(view_id::kHistory));
        QVERIFY(!rig.registry.existingWidget(view_id::kHistory)->isVisible());
        // Clicar fora NÃO fecha: o painel faz parte do quadro, como qualquer outro.
        rail = rig.dock->railWidget(Zone::Right);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QTest::qWait(60);
        QTest::mouseClick(rig.commands, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QTest::qWait(60);
        QCOMPARE(rig.dock->revealedView(Zone::Right), view_id::kHistory);
        QVERIFY(rig.registry.existingWidget(view_id::kHistory)->isVisible());
    }

    // Arrastar o ícone da faixa leva o painel a outra zona, como arrastar a aba; soltar sem ter apertado nele não clica.
    void draggingARailIconMovesThePanelAndAReleaseAloneIsNotAClick()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, Zone::Right);
        rig.show();
        rig.dock->setZoneCollapsed(Zone::Right, true);
        QTest::qWait(30);
        ZoneRail *rail = rig.dock->railWidget(Zone::Right);
        QVERIFY(rail);
        QSignalSpy clicked(rail, &ZoneRail::viewClicked);
        // Um soltar solto (o botão foi apertado em outro lugar) não é clique.
        QTest::mouseRelease(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QCOMPARE(clicked.count(), 0);
        // O clique de verdade segue funcionando.
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QCOMPARE(clicked.count(), 1);
        rail = rig.dock->railWidget(Zone::Right);
        // O arrasto do ícone mostra os alvos de soltar e o painel vai para a zona escolhida.
        emit rail->dragStarted(view_id::kHistory);
        QVERIFY(rig.dock->overlay()->isVisible());
        DropTarget target;
        target.valid = true;
        target.zone = Zone::Left;
        emit rig.dock->overlay()->dropped(view_id::kHistory, target);
        emit rail->dragFinished(view_id::kHistory, Qt::MoveAction, QCursor::pos());
        QTRY_COMPARE(rig.dock->zoneOf(view_id::kHistory), std::optional<Zone>(Zone::Left));
        QVERIFY(!rig.dock->overlay()->isVisible());
    }

    void theExpandButtonBringsTheZoneBack()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, Zone::Bottom);
        rig.show();
        rig.dock->setZoneCollapsed(Zone::Bottom, true);
        QTest::qWait(30);
        ZoneRail *rail = rig.dock->railWidget(Zone::Bottom);
        QVERIFY(rail);
        QCOMPARE(rail->height(), ZoneRail::kThickness);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->expandRect().center());
        QTest::qWait(30);
        QVERIFY(!rig.dock->railWidget(Zone::Bottom));
        QVERIFY(rig.dock->zoneWidget(Zone::Bottom));
        QVERIFY(!rig.dock->model().zone(Zone::Bottom).collapsed);
        QVERIFY(rig.output->isVisible() || rig.registry.existingWidget(view_id::kHistory)->isVisible());
    }

    void revealingAPanelOfACollapsedZoneOpensItBesideTheContent()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, Zone::Left);
        rig.show();
        rig.dock->setZoneCollapsed(Zone::Left, true);
        QVERIFY(rig.dock->revealView(view_id::kHistory));
        QCOMPARE(rig.dock->revealedView(Zone::Left), view_id::kHistory);
        QVERIFY(rig.dock->isViewVisible(view_id::kHistory));
        // Fechar o painel aberto apaga a faixa quando a zona fica vazia.
        rig.dock->closeView(view_id::kHistory);
        QVERIFY(rig.dock->revealedView(Zone::Left).isEmpty());
        QVERIFY(!rig.dock->railWidget(Zone::Left));
        QVERIFY(!rig.dock->model().zone(Zone::Left).collapsed);
    }

    // Soltar um painel numa zona recolhida NÃO desfaz a faixa: ele entra nela e abre ao lado, com o resto como estava.
    void movingAPanelIntoACollapsedZoneKeepsItCollapsedAndOpensIt()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, Zone::Right);
        rig.show();
        rig.dock->setZoneCollapsed(Zone::Right, true);
        const double size = rig.dock->model().effectiveSize(Zone::Right);
        rig.dock->moveView(view_id::kDocument, Zone::Right);
        QTest::qWait(60);
        QVERIFY(rig.dock->model().zone(Zone::Right).collapsed);
        QVERIFY(rig.dock->railWidget(Zone::Right) != nullptr);
        QVERIFY(rig.dock->model().zone(Zone::Right).views.contains(view_id::kDocument));
        QCOMPARE(rig.dock->revealedView(Zone::Right), view_id::kDocument);
        QVERIFY(rig.dock->isViewVisible(view_id::kDocument));
        QCOMPARE(rig.dock->model().effectiveSize(Zone::Right), size);
        QVERIFY(rig.railIcon(Zone::Right, view_id::kHistory).isValid());
        QVERIFY(rig.railIcon(Zone::Right, view_id::kDocument).isValid());
    }

    void theCenterNeverCollapsesAndRailsHideWhileAPanelIsMaximized()
    {
        Rig rig;
        rig.dock->setZoneCollapsed(Zone::Center, true);
        QVERIFY(!rig.dock->model().zone(Zone::Center).collapsed);
        rig.dock->moveView(view_id::kHistory, Zone::Right);
        rig.dock->setZoneCollapsed(Zone::Right, true);
        rig.dock->setMaximized(view_id::kOutput);
        QVERIFY(!rig.dock->railWidget(Zone::Right));
        rig.dock->setMaximized(QString());
        QVERIFY(rig.dock->railWidget(Zone::Right));
    }

    void collapsingIsRememberedInTheModelJson()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, Zone::Right);
        rig.dock->setZoneCollapsed(Zone::Right, true);
        const LayoutModel back = LayoutModel::fromJson(rig.dock->model().toJson(), rig.registry.validator());
        QVERIFY(back.zone(Zone::Right).collapsed);
        Rig another;
        another.dock->setModel(back);
        QVERIFY(another.dock->railWidget(Zone::Right));
    }

    void thePanelMenuOffersCollapseAndExpand()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, Zone::Right);
        QMenu menu;
        rig.dock->populatePanelMenu(&menu, view_id::kHistory);
        QStringList texts;
        for (QAction *action : menu.actions()) {
            texts << action->text();
        }
        QVERIFY(texts.contains(kai::utils::tr(QStringLiteral("layout.menu.collapse_zone"))));
        rig.dock->setZoneCollapsed(Zone::Right, true);
        QMenu again;
        rig.dock->populatePanelMenu(&again, view_id::kHistory);
        texts.clear();
        for (QAction *action : again.actions()) {
            texts << action->text();
        }
        QVERIFY(texts.contains(kai::utils::tr(QStringLiteral("layout.menu.expand_zone"))));
        // O centro não oferece recolher.
        QMenu center;
        rig.dock->populatePanelMenu(&center, view_id::kCommands);
        texts.clear();
        for (QAction *action : center.actions()) {
            texts << action->text();
        }
        QVERIFY(!texts.contains(kai::utils::tr(QStringLiteral("layout.menu.collapse_zone"))));
    }

    // ---- LayoutController: foco por teclado, maximizar, perspectivas -----------------------------------------------
    void visibleOrderIsLeftCenterRightBottomThenFloating()
    {
        Rig rig;
        rig.show();
        LayoutController controller(rig.dock, nullptr, {}, &rig.owner);
        QCOMPARE(controller.visibleViewOrder(), (QStringList{view_id::kCommands, view_id::kOutput}));
        rig.dock->moveView(view_id::kCommands, Zone::Right);
        rig.dock->moveView(view_id::kOutput, Zone::Left);
        rig.dock->floatView(view_id::kHistory);
        QTest::qWait(30);
        QCOMPARE(controller.visibleViewOrder(), (QStringList{view_id::kOutput, view_id::kCommands, view_id::kHistory}));
        // Uma aba escondida (não é a da frente) não conta.
        rig.dock->moveView(view_id::kDocument, Zone::Right);
        rig.dock->revealView(view_id::kCommands);
        QCOMPARE(controller.visibleViewOrder().count(view_id::kDocument), 0);
    }

    void panelNumbersFocusThePanelAndWrapCycling()
    {
        Rig rig;
        rig.show();
        rig.owner.activateWindow();
        LayoutController controller(rig.dock, nullptr, {}, &rig.owner);
        controller.focusPanelNumber(2);
        QTRY_VERIFY(rig.output->hasFocus());
        QCOMPARE(controller.focusedViewId(), view_id::kOutput);
        controller.cycleFocus(true); // dá a volta: Saída -> Comandos
        QTRY_VERIFY(rig.commands->hasFocus());
        controller.cycleFocus(false);
        QTRY_VERIFY(rig.output->hasFocus());
        controller.focusPanelNumber(9); // número que não existe: nada acontece
        QVERIFY(rig.output->hasFocus());
    }

    void focusingAClosedPanelBringsItBack()
    {
        Rig rig;
        rig.show();
        LayoutController controller(rig.dock, nullptr, {}, &rig.owner);
        rig.dock->closeView(view_id::kOutput);
        controller.focusView(view_id::kOutput);
        QVERIFY(rig.dock->model().isPlaced(view_id::kOutput));
        QTRY_VERIFY(rig.output->isVisible());
    }

    void maximizeTogglesThePanelThatHasTheFocus()
    {
        Rig rig;
        rig.show();
        rig.owner.activateWindow();
        LayoutController controller(rig.dock, nullptr, {}, &rig.owner);
        rig.output->setFocus();
        QTRY_VERIFY(rig.output->hasFocus());
        controller.toggleMaximizeFocused();
        QCOMPARE(rig.dock->model().maximized(), view_id::kOutput);
        controller.toggleMaximizeFocused();
        QVERIFY(rig.dock->model().maximized().isEmpty());
    }

    void savedPerspectivesRestoreAndSurviveARestart()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        {
            Rig rig;
            LayoutStore store(path);
            LayoutController controller(rig.dock, &store, {}, &rig.owner);
            QSignalSpy changed(&controller, &LayoutController::perspectivesChanged);
            rig.dock->moveView(view_id::kOutput, Zone::Right);
            QVERIFY(controller.saveCurrentAs(QStringLiteral("  my layout ")));
            QCOMPARE(controller.savedPerspectiveNames(), QStringList{QStringLiteral("my layout")});
            QCOMPARE(changed.count(), 1);
            QVERIFY(!controller.saveCurrentAs(QStringLiteral("   ")));
            rig.dock->restoreDefault();
            QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
            QVERIFY(controller.applySaved(QStringLiteral("my layout")));
            QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
            QVERIFY(!controller.applySaved(QStringLiteral("nope")));
            controller.flush();
        }
        Rig again;
        LayoutStore store(path);
        const LayoutStore::Loaded loaded = store.load(again.registry.validator());
        QVERIFY(loaded.fileExisted);
        QVERIFY(loaded.perspectives.contains(QStringLiteral("my layout")));
        LayoutController controller(again.dock, &store, loaded.perspectives, &again.owner);
        QVERIFY(controller.applySaved(QStringLiteral("my layout")));
        QCOMPARE(again.dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
        QVERIFY(controller.deleteSaved(QStringLiteral("my layout")));
        QVERIFY(!controller.deleteSaved(QStringLiteral("my layout")));
    }

    void presetsRearrangeThePanels()
    {
        Rig rig;
        LayoutController controller(rig.dock, nullptr, {}, &rig.owner);
        controller.applyPreset(LayoutModel::Preset::Http);
        QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
        controller.applyPreset(LayoutModel::Preset::Compact);
        QVERIFY(!rig.dock->model().isPlaced(view_id::kOutput));
        QVERIFY(rig.dock->model().isPlaced(view_id::kCommands));
        controller.applyPreset(LayoutModel::Preset::Runner);
        QCOMPARE(rig.dock->model(), LayoutModel::defaultLayout());
    }

    void aPerspectiveWithAPanelThatNoLongerExistsLoadsWithoutIt()
    {
        Rig rig;
        QMap<QString, LayoutModel> perspectives;
        LayoutModel model = LayoutModel::defaultLayout();
        model.placeView(QStringLiteral("kip:alive"), Zone::Right);
        perspectives.insert(QStringLiteral("p"), model);
        LayoutController controller(rig.dock, nullptr, perspectives, &rig.owner);
        QVERIFY(controller.applySaved(QStringLiteral("p")));
        QCOMPARE(rig.dock->zoneOf(QStringLiteral("kip:alive")), std::optional<Zone>(Zone::Right));
        // O comando KIP some do registro: a próxima aplicação descarta o painel em vez de quebrar.
        Rig other;
        other.registry.registerPrefix(QStringLiteral("zzz:"), [](const QString &) { return std::nullopt; });
        QMap<QString, LayoutModel> ghost;
        LayoutModel ghostModel = LayoutModel::defaultLayout();
        ghostModel.placeView(QStringLiteral("kip:gone"), Zone::Right);
        ghost.insert(QStringLiteral("g"), ghostModel);
        LayoutController ghostController(other.dock, nullptr, ghost, &other.owner);
        QVERIFY(ghostController.applySaved(QStringLiteral("g")));
        QVERIFY(!other.dock->model().isPlaced(QStringLiteral("kip:gone")));
    }

    void theViewMenuHasPanelsPositionMaximizeRearrangeAndPerspectives()
    {
        Rig rig;
        LayoutController controller(rig.dock, nullptr, {}, &rig.owner);
        QMenu menu;
        controller.populateViewMenu(&menu);
        int checkable = 0;
        int submenus = 0;
        for (QAction *action : menu.actions()) {
            checkable += action->isCheckable() ? 1 : 0;
            submenus += action->menu() ? 1 : 0;
        }
        // Painéis (4 fixos), Maximizar e Reorganizar são marcáveis; posição dos painéis e perspectivas são submenus.
        QCOMPARE(checkable, 6);
        QCOMPARE(submenus, 2);
        QVERIFY(menu.actions().last()->text() == kai::utils::tr(QStringLiteral("menu.view.restore_layout")));
    }
};

QTEST_MAIN(TestDockHost)
#include "test_dock_host.moc"
