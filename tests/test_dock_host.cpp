#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRandomGenerator>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
#include "ui/layout/fit-board.h"
#include "ui/layout/floating-view-window.h"
#include "ui/layout/layout-controller.h"
#include "ui/layout/layout-store.h"
#include "ui/layout/view-registry.h"
#include "ui/layout/view-zone.h"
#include "ui/layout/zone-rail.h"
#include "utils/translation-manager.h"

using namespace kai::ui::layout;

namespace {

// As células do Rig: a lista em cima e a Saída embaixo na coluna 0; a coluna 1 (a lateral) começa vazia.
const Zone kTop{0, 0};
const Zone kBottom{1, 0};
const Zone kSide{0, 1};
const Zone kSideBottom{1, 1};

QRect rectIn(DockHost *dock, Zone zone)
{
    QWidget *w = dock->zoneWidget(zone);
    return w ? QRect(w->mapTo(dock, QPoint(0, 0)), w->size()) : QRect();
}

ViewSpec spec(const QString &id, Home home, bool closable = true)
{
    ViewSpec s;
    s.id = id;
    s.titleProvider = [id]() { return id.toUpper(); };
    s.icon = QStringLiteral("square");
    s.home = home;
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
        registry.registerView(spec(view_id::kCommands, Home::Primary), commands);
        registry.registerView(spec(view_id::kOutput, Home::Secondary), output);
        ViewSpec document = spec(view_id::kDocument, Home::Secondary);
        document.factory = [this]() {
            ++documentsBuilt;
            return new QLabel(QStringLiteral("doc"));
        };
        registry.registerView(document);
        ViewSpec history = spec(view_id::kHistory, Home::Secondary);
        history.factory = []() { return new QLabel(QStringLiteral("history")); };
        registry.registerView(history);
        registry.registerPrefix(view_id::kKipPrefix, [](const QString &id) -> std::optional<ViewSpec> {
            if (id != QStringLiteral("kip:alive")) {
                return std::nullopt;
            }
            ViewSpec s = spec(id, Home::Secondary);
            s.factory = []() { return new QLabel(QStringLiteral("kip")); };
            return s;
        });
        dock = new DockHost(&registry, &owner, &owner);
        dock->setGeometry(0, 0, 1000, 700);
        // A lista em cima e a Saída embaixo, os dois na primeira coluna de uma grade 2×2 (a segunda coluna está vazia e os
        // testes a usam para por painéis ao lado), sem as abas fixas: as células de um painel só não têm faixa (as de abas
        // mostram só com mais de um). O Documento (aba ao lado da Saída no padrão) fica fechado: os testes montam o que
        // querem em volta da Saída.
        LayoutModel model = LayoutModel::defaultLayout();
        model.placeView(view_id::kCommands, kTop);
        model.placeView(view_id::kOutput, kBottom);
        model.closeView(view_id::kDocument);
        model.setTabsAlways(false);
        dock->setModel(model);
    }

    QRect railIcon(Edge edge, const QString &id) const
    {
        ZoneRail *rail = dock->railWidget(edge);
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
        QVERIFY(rig.dock->zoneWidget(kTop)->isAncestorOf(rig.commands));
        QVERIFY(rig.dock->zoneWidget(kBottom)->isAncestorOf(rig.output));
        QVERIFY(!rig.dock->zoneWidget(kSideBottom));
        QVERIFY(!rig.dock->zoneWidget(kSide));
        QVERIFY(rig.commands->isVisible());
        QVERIFY(rig.output->isVisible());
        QVERIFY(rig.commands->y() < rig.output->mapTo(&rig.owner, QPoint()).y());
        QVERIFY(!rig.dock->emptyStateShown());
    }

    void aSinglePanelZoneHasNoTabStrip()
    {
        Rig rig;
        rig.show();
        QVERIFY(!rig.dock->zoneWidget(kTop)->strip()->isVisibleTo(rig.dock->zoneWidget(kTop)));
        QVERIFY(!rig.dock->zoneWidget(kBottom)->strip()->isVisibleTo(rig.dock->zoneWidget(kBottom)));
    }

    void rearrangeModeShowsEveryStrip()
    {
        Rig rig;
        rig.show();
        rig.dock->setEditMode(true);
        QVERIFY(rig.dock->zoneWidget(kTop)->strip()->isVisibleTo(rig.dock->zoneWidget(kTop)));
        QVERIFY(rig.dock->zoneWidget(kBottom)->strip()->isVisibleTo(rig.dock->zoneWidget(kBottom)));
        rig.dock->setEditMode(false);
        QVERIFY(!rig.dock->zoneWidget(kBottom)->strip()->isVisibleTo(rig.dock->zoneWidget(kBottom)));
    }

    void movingAPanelKeepsTheVeryWidgetAndItsState()
    {
        Rig rig;
        rig.show();
        QPointer<QLineEdit> output = rig.output;
        rig.dock->moveView(view_id::kOutput, kSide);
        QTest::qWait(30);
        QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(kSide));
        QVERIFY(output); // não foi recriado nem destruído
        QCOMPARE(output.data(), rig.output);
        QCOMPARE(rig.output->text(), QStringLiteral("typed in output"));
        QVERIFY(rig.dock->zoneWidget(kSide)->isAncestorOf(rig.output));
        QVERIFY(!rig.dock->zoneWidget(kBottom));
        QVERIFY(rig.output->isVisible());
        // E a lista ficou sozinha no miolo, ocupando a altura toda.
        QVERIFY(rig.commands->isVisible());
    }

    void tabbingTwoPanelsInOneZoneShowsTheStripAndOnlyTheFrontOne()
    {
        Rig rig;
        rig.show();
        rig.dock->moveView(view_id::kDocument, kBottom);
        QTest::qWait(30);
        ViewZone *bottom = rig.dock->zoneWidget(kBottom);
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
        rig.dock->moveView(view_id::kHistory, kSideBottom);
        rig.dock->closeView(view_id::kHistory);
        QVERIFY(!rig.dock->model().isPlaced(view_id::kHistory));
        QVERIFY(!rig.dock->zoneWidget(kSideBottom));
        QVERIFY(rig.dock->revealView(view_id::kHistory));
        QCOMPARE(rig.dock->zoneOf(view_id::kHistory), std::optional<Zone>(kSideBottom));
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
        rig.registry.registerView(spec(QStringLiteral("pinned"), Home::Primary, false), new QLabel(&rig.owner));
        LayoutModel model = rig.dock->model();
        model.placeView(QStringLiteral("pinned"), kSideBottom);
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
        QVERIFY(!rig.dock->zoneWidget(kTop));
        QCOMPARE(maximized.count(), 1);
        rig.dock->toggleMaximize(view_id::kOutput);
        QTest::qWait(30);
        QVERIFY(rig.commands->isVisible());
        QVERIFY(rig.output->isVisible());
        QCOMPARE(maximized.count(), 2);
    }

    // O ícone de maximizar fica na faixa de abas, ao lado do "mais": maximiza o painel da frente e, com ele maximizado,
    // vira "restaurar". A faixa aparece na zona maximizada mesmo fora do modo reorganizar.
    void theStripMaximizeIconTogglesTheMaximizedPanel()
    {
        Rig rig;
        rig.show();
        rig.dock->setEditMode(true);
        ZoneTabStrip *strip = rig.dock->zoneWidget(kBottom)->strip();
        QVERIFY(strip->maximizeRect().isValid());
        QVERIFY(strip->maximizeRect().right() < strip->menuRect().left() + 1);
        QTest::mouseClick(strip, Qt::LeftButton, Qt::NoModifier, strip->maximizeRect().center());
        QTest::qWait(30);
        QCOMPARE(rig.dock->model().maximized(), view_id::kOutput);

        rig.dock->setEditMode(false);
        QTest::qWait(30);
        ViewZone *zone = rig.dock->zoneWidget(kBottom);
        QVERIFY(zone);
        QVERIFY(zone->strip()->isVisibleTo(zone));
        QTest::mouseClick(zone->strip(), Qt::LeftButton, Qt::NoModifier, zone->strip()->maximizeRect().center());
        QTest::qWait(30);
        QVERIFY(rig.dock->model().maximized().isEmpty());
        QVERIFY(rig.commands->isVisible());
    }

    // "Maximizar" saiu do menu do painel: só o ícone da faixa (e o atalho / o menu Exibir) fazem isso.
    void thePanelMenuHasNoMaximizeEntry()
    {
        Rig rig;
        rig.show();
        QMenu menu;
        rig.dock->populatePanelMenu(&menu, view_id::kOutput);
        QVERIFY(!menu.actions().isEmpty());
        for (const QAction *action : menu.actions()) {
            const QString text = action->text();
            QVERIFY2(!text.contains(QStringLiteral("aximiz")) && !text.contains(QStringLiteral("estore size"))
                         && !text.contains(QStringLiteral("estaurar tamanho")),
                     qPrintable(text));
        }
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
        rig.dock->moveView(view_id::kOutput, kBottom);
        QCOMPARE(edited.count(), 1);
        rig.dock->moveView(view_id::kOutput, kBottom); // já está lá
        QCOMPARE(edited.count(), 1);
        rig.dock->closeView(view_id::kHistory); // nunca esteve aberto
        QCOMPARE(edited.count(), 1);
    }

    void draggingASplitterHandleIsRememberedAsAProportion()
    {
        Rig rig;
        rig.show();
        QSplitter *mid = rig.dock->stackSplitter(0);
        QVERIFY(mid != nullptr);
        QSignalSpy edited(rig.dock, &DockHost::modelEdited);
        const int total = mid->sizes().at(0) + mid->sizes().at(1);
        mid->setSizes({int(total * 0.8), total - int(total * 0.8)});
        emit mid->splitterMoved(int(total * 0.8), 1);
        QVERIFY(qAbs(rig.dock->model().zone(kBottom).size - 0.2) < 0.02);
        QVERIFY(edited.count() >= 1);
        // A janela cresce: a proporção continua.
        rig.owner.resize(1300, 900);
        rig.dock->setGeometry(0, 0, 1300, 900);
        QTest::qWait(60);
        const QList<int> sizes = mid->sizes();
        QVERIFY2(qAbs(double(sizes.at(1)) / (sizes.at(0) + sizes.at(1)) - 0.2) < 0.03,
                 qPrintable(QString::number(double(sizes.at(1)) / (sizes.at(0) + sizes.at(1)))));
    }

    // O painel aberto ao lado da faixa é gravado no modelo e volta aberto quando o layout é aplicado de novo (reinício).
    void theRevealedPanelOfARailComesBackOpenAfterARestart()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.show();
        rig.dock->setZoneCollapsed(kSide, true);
        QVERIFY(rig.dock->revealView(view_id::kHistory));
        QTest::qWait(30);
        QCOMPARE(rig.dock->revealedView(kSide), view_id::kHistory);
        QCOMPARE(rig.dock->model().zone(kSide).open, view_id::kHistory);

        // "Reinício": outro DockHost recebe o JSON gravado.
        const LayoutModel saved = LayoutModel::fromJson(rig.dock->model().toJson(), {});
        Rig again;
        again.dock->setModel(saved);
        again.show();
        QCOMPARE(again.dock->revealedView(kSide), view_id::kHistory);
        QVERIFY(again.dock->zoneWidget(kSide) != nullptr);

        // Fechar o painel aberto (clicar de novo no ícone) também é lembrado.
        ZoneRail *rail = again.dock->railWidget(Edge::Right);
        QVERIFY(rail != nullptr);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QTest::qWait(30);
        QVERIFY(again.dock->revealedView(kSide).isEmpty());
        QVERIFY(again.dock->model().zone(kSide).open.isEmpty());
    }

    // Soltar entre os ícones de uma faixa recolhida reordena os painéis dela (e entra na faixa se vinha de fora).
    void droppingOnACollapsedRailReordersItsIcons()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.dock->moveView(view_id::kDocument, kSide);
        rig.dock->moveView(view_id::kOutput, kSide);
        rig.show();
        rig.dock->setZoneCollapsed(kSide, true);
        QTest::qWait(30);
        ZoneRail *rail = rig.dock->railWidget(Edge::Right);
        QVERIFY(rail != nullptr);
        QCOMPARE(rig.dock->model().zone(kSide).views, (QStringList{view_id::kHistory, view_id::kDocument, view_id::kOutput}));
        // Soltar o primeiro depois do último ícone.
        const QRect last = rail->iconRect(view_id::kOutput);
        const QPoint pos = rail->mapTo(rig.dock, QPoint(last.center().x(), last.bottom() + 4));
        const DropTarget target = rig.dock->dropTargetAt(pos);
        QVERIFY(target.valid);
        QCOMPARE(target.zone, kSide);
        QCOMPARE(target.index, 3);
        rig.dock->moveView(view_id::kHistory, target.zone, target.index);
        QTest::qWait(30);
        QCOMPARE(rig.dock->model().zone(kSide).views, (QStringList{view_id::kDocument, view_id::kOutput, view_id::kHistory}));
        QVERIFY(rig.dock->model().zone(kSide).collapsed); // continua recolhida
    }

    // Soltar sobre a faixa de abas de uma zona continua inserindo naquela zona, na posição das abas.
    void droppingOnATabStripStillInsertsInThatZone()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        ViewZone *right = rig.dock->zoneWidget(kSide);
        QVERIFY(right && right->strip()->isVisibleTo(right));
        const QPoint onStrip = right->strip()->mapTo(rig.dock, QPoint(right->strip()->width() / 2, right->strip()->height() / 2));
        const DropTarget target = rig.dock->dropTargetAt(onStrip);
        QVERIFY(target.valid);
        QCOMPARE(target.zone, kSide);
        QVERIFY(target.index >= 0);
    }

    // Modo dividir: a zona mostra todos os painéis ao mesmo tempo, cada um numa caixa com cabeçalho; desligar volta às abas.
    void splitModeShowsEveryPanelOfTheZoneAtOnce()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kBottom);
        rig.dock->moveView(view_id::kDocument, kBottom);
        rig.show();
        ViewZone *bottom = rig.dock->zoneWidget(kBottom);
        QVERIFY(bottom && !bottom->splitActive());
        QVERIFY(!rig.output->isVisible() || !bottom->splitActive());
        const int tabsBefore = bottom->strip()->count();
        QCOMPARE(tabsBefore, 3);

        rig.dock->setZoneSplit(kBottom, true);
        QTest::qWait(60);
        bottom = rig.dock->zoneWidget(kBottom);
        QVERIFY(bottom->splitActive());
        QVERIFY(rig.dock->model().zone(kBottom).split);
        QCOMPARE(bottom->sectionHeaders().size(), 3);
        QVERIFY(!bottom->strip()->isVisibleTo(bottom)); // sem a faixa de abas: cada caixa tem o cabeçalho dela
        // Os três painéis à vista juntos, lado a lado (a zona de baixo é larga), cada um com largura de verdade.
        QVERIFY(rig.output->isVisible());
        QVERIFY(rig.registry.existingWidget(view_id::kHistory)->isVisible());
        QVERIFY(rig.registry.existingWidget(view_id::kDocument)->isVisible());
        QVERIFY(rig.output->width() > 100);
        QVERIFY(rig.registry.existingWidget(view_id::kHistory)->mapTo(rig.dock, QPoint(0, 0)).x()
                > rig.output->mapTo(rig.dock, QPoint(0, 0)).x());
        QVERIFY(rig.dock->isViewVisible(view_id::kHistory));
        QVERIFY(rig.dock->isViewVisible(view_id::kDocument));

        // Soltar sobre um cabeçalho insere antes/depois dele; moveView reordena as caixas.
        const QRect secondHeader = QRect(bottom->sectionHeaders().at(1)->mapTo(rig.dock, QPoint(0, 0)), bottom->sectionHeaders().at(1)->size());
        const DropTarget before = rig.dock->dropTargetAt(QPoint(secondHeader.left() + 3, secondHeader.center().y()));
        QCOMPARE(before.zone, kBottom);
        QCOMPARE(before.index, 1);
        const DropTarget after = rig.dock->dropTargetAt(QPoint(secondHeader.right() - 3, secondHeader.center().y()));
        QCOMPARE(after.index, 2);
        rig.dock->moveView(view_id::kOutput, after.zone, after.index);
        QTest::qWait(60);
        QCOMPARE(rig.dock->model().zone(kBottom).views, (QStringList{view_id::kHistory, view_id::kOutput, view_id::kDocument}));
        QVERIFY(rig.dock->zoneWidget(kBottom)->splitActive()); // continua dividida depois de reordenar

        // De volta às abas.
        rig.dock->setZoneSplit(kBottom, false);
        QTest::qWait(60);
        bottom = rig.dock->zoneWidget(kBottom);
        QVERIFY(!bottom->splitActive());
        QVERIFY(bottom->strip()->isVisibleTo(bottom));
        QCOMPARE(bottom->strip()->count(), 3);
        QVERIFY(!rig.dock->model().zone(kBottom).split);
        // Cada painel continua no mesmo widget (nada foi recriado) e só o da frente aparece.
        QVERIFY(rig.registry.existingWidget(view_id::kHistory) != nullptr);
        QCOMPARE(rig.documentsBuilt, 1);
    }

    // Nas laterais as caixas do modo dividir se empilham; o menu do painel oferece dividir/voltar às abas.
    void splitModeStacksTheBoxesInASideZoneAndTheMenuOffersIt()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.dock->moveView(view_id::kDocument, kSide);
        rig.show();
        QMenu menu;
        rig.dock->populatePanelMenu(&menu, view_id::kHistory);
        QStringList texts;
        for (QAction *action : menu.actions()) {
            texts << action->text();
        }
        QVERIFY2(texts.contains(kai::utils::tr(QStringLiteral("layout.menu.split_zone"))), qPrintable(texts.join('|')));
        rig.dock->setZoneSplit(kSide, true);
        QTest::qWait(60);
        ViewZone *right = rig.dock->zoneWidget(kSide);
        QVERIFY(right->splitActive());
        const auto headers = right->sectionHeaders();
        QCOMPARE(headers.size(), 2);
        QVERIFY(headers.at(1)->mapTo(rig.dock, QPoint(0, 0)).y() > headers.at(0)->mapTo(rig.dock, QPoint(0, 0)).y()); // empilhadas
        QMenu menu2;
        rig.dock->populatePanelMenu(&menu2, view_id::kHistory);
        QStringList texts2;
        for (QAction *action : menu2.actions()) {
            texts2 << action->text();
        }
        QVERIFY(texts2.contains(kai::utils::tr(QStringLiteral("layout.menu.unsplit_zone"))));
        // A ordem do foco (Alt+N) inclui as duas caixas.
        LayoutController controller(rig.dock, nullptr, {}, &rig.owner, &rig.owner);
        const QStringList order = controller.visibleViewOrder();
        QVERIFY(order.contains(view_id::kHistory) && order.contains(view_id::kDocument));
    }

    // Reordenar as abas arrastando uma para outra posição da MESMA faixa: cai onde foi solta, nos dois sentidos.
    void draggingATabWithinItsZoneReordersIt()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kBottom);
        rig.dock->moveView(view_id::kDocument, kBottom);
        rig.show();
        ViewZone *bottom = rig.dock->zoneWidget(kBottom);
        QVERIFY(bottom && bottom->strip()->isVisibleTo(bottom));
        QCOMPARE(rig.dock->model().zone(kBottom).views, (QStringList{view_id::kOutput, view_id::kHistory, view_id::kDocument}));
        ZoneTabStrip *strip = bottom->strip();
        const auto dropAt = [&](int x) {
            const QPoint pos = strip->mapTo(rig.dock, QPoint(x, strip->height() / 2));
            return rig.dock->dropTargetAt(pos);
        };
        // A primeira aba (Saída) solta depois da última: vai para o fim.
        DropTarget toEnd = dropAt(strip->width() - 4);
        QCOMPARE(toEnd.zone, kBottom);
        QCOMPARE(toEnd.index, 3);
        rig.dock->moveView(view_id::kOutput, toEnd.zone, toEnd.index);
        QTest::qWait(30);
        QCOMPARE(rig.dock->model().zone(kBottom).views, (QStringList{view_id::kHistory, view_id::kDocument, view_id::kOutput}));
        // A última (Saída) solta no começo.
        bottom = rig.dock->zoneWidget(kBottom);
        strip = bottom->strip();
        const QPoint start = strip->mapTo(rig.dock, QPoint(2, strip->height() / 2));
        const DropTarget toStart = rig.dock->dropTargetAt(start);
        QCOMPARE(toStart.index, 0);
        rig.dock->moveView(view_id::kOutput, toStart.zone, toStart.index);
        QTest::qWait(30);
        QCOMPARE(rig.dock->model().zone(kBottom).views, (QStringList{view_id::kOutput, view_id::kHistory, view_id::kDocument}));
    }

    void aDropAppliedAfterTheDragMovesThePanel()
    {
        Rig rig;
        rig.show();
        ViewZone *bottom = rig.dock->zoneWidget(kBottom);
        DropTarget target;
        target.valid = true;
        target.zone = kSide;
        emit rig.dock->overlay()->dropped(view_id::kOutput, target);
        emit bottom->dragFinished(view_id::kOutput, Qt::MoveAction, QCursor::pos());
        QTRY_COMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(kSide));
        QVERIFY(rig.output->isVisible());
    }

    void droppingOutsideTheWindowFloatsThePanel()
    {
        Rig rig;
        rig.show();
        ViewZone *bottom = rig.dock->zoneWidget(kBottom);
        emit bottom->dragFinished(view_id::kOutput, Qt::IgnoreAction, QPoint(-5000, -5000));
        QTRY_VERIFY(rig.dock->model().isFloating(view_id::kOutput));
        QVERIFY(rig.dock->floatingWindow(view_id::kOutput) != nullptr);
    }

    void aCancelledDropInsideTheWindowChangesNothing()
    {
        Rig rig;
        rig.show();
        ViewZone *bottom = rig.dock->zoneWidget(kBottom);
        const QPoint inside = rig.owner.mapToGlobal(QPoint(50, 50));
        emit bottom->dragFinished(view_id::kOutput, Qt::IgnoreAction, inside);
        QTest::qWait(30);
        QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(kBottom));
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
        QVERIFY(!rig.dock->zoneWidget(kBottom));

        window->dockButton()->click();
        QTest::qWait(60);
        QVERIFY(!rig.dock->floatingWindow(view_id::kOutput));
        QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(kBottom));
        QVERIFY(rig.dock->zoneWidget(kBottom)->isAncestorOf(rig.output));
    }

    // Relato: com um painel flutuante e fixo, clicar nele trazia o Kai inteiro para a frente (no Windows uma janela com dono
    // anda em grupo com ele). A flutuante é uma janela sem dono, e esconder/mostrar junto com o Kai segue pelo DockHost.
    void aFloatingWindowHasNoOwnerAndKeepsTheOwnersStyle()
    {
        Rig rig;
        rig.owner.setStyleSheet(QStringLiteral("QLabel { color: red; }"));
        rig.show();
        rig.dock->floatView(view_id::kOutput);
        QTest::qWait(30);
        FloatingViewWindow *window = rig.dock->floatingWindow(view_id::kOutput);
        QVERIFY(window != nullptr);
        window->pinButton()->click(); // fixo (sempre no topo)
        QTest::qWait(60);
        QVERIFY(window->isVisible());
        QVERIFY(window->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(window->parentWidget() == nullptr);
        QVERIFY(window->windowHandle() != nullptr);
        QVERIFY(window->windowHandle()->transientParent() == nullptr);
        QCOMPARE(window->styleSheet(), rig.owner.styleSheet());
        // O tema muda: a cópia acompanha.
        rig.owner.setStyleSheet(QStringLiteral("QLabel { color: blue; }"));
        rig.dock->refreshFloatingStyle();
        QCOMPARE(window->styleSheet(), rig.owner.styleSheet());
        // Esconder o dono esconde a flutuante (agora pelo DockHost), e ela não volta sozinha.
        rig.dock->setFloatingVisible(false);
        QVERIFY(!window->isVisible());
    }

    // O cursor de redimensionar da borda é do app inteiro (override): perder a ativação com ele ligado o deixava preso.
    void theBorderCursorIsClearedWhenTheWindowLosesActivation()
    {
        Rig rig;
        rig.show();
        rig.dock->floatView(view_id::kOutput);
        QTest::qWait(30);
        FloatingViewWindow *window = rig.dock->floatingWindow(view_id::kOutput);
        QVERIFY(window != nullptr);
        QApplication::setActiveWindow(window);
        QVERIFY(window->isActiveWindow());
        QWindow *handle = window->windowHandle();
        QTest::mouseMove(handle, QPoint(window->width() / 2, window->height() / 2));
        QVERIFY(!QGuiApplication::overrideCursor());
        QTest::mouseMove(handle, QPoint(2, window->height() / 2));
        QVERIFY2(QGuiApplication::overrideCursor() != nullptr, "o cursor de borda deveria ligar na borda da janela ativa");
        QApplication::setActiveWindow(&rig.owner); // o usuário clica em outra janela do Kai
        QVERIFY2(!QGuiApplication::overrideCursor(), "o cursor de borda ficou preso ao perder a ativação");
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
        rig.dock->moveView(view_id::kCommands, kSideBottom);
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
        QCOMPARE(rig.dock->zoneOf(QStringLiteral("kip:alive")), std::optional<Zone>(kSide));
        QVERIFY(rig.registry.existingWidget(QStringLiteral("kip:alive")) != nullptr);
    }

    // ---- O painel aberto a partir de uma faixa ocupa espaço e se redimensiona --------------------------------------------
    void aRevealedZonePushesTheContentInsteadOfCoveringIt()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        rig.dock->setZoneCollapsed(kSide, true);
        QTest::qWait(30);
        const int wide = rig.commands->width();
        QVERIFY(rig.dock->revealView(view_id::kOutput));
        QTest::qWait(60);
        ViewZone *zone = rig.dock->zoneWidget(kSide);
        QVERIFY(zone != nullptr);
        QVERIFY(rig.dock->railWidget(Edge::Right) != nullptr); // a faixa continua lá para fechar
        QCOMPARE(rig.dock->revealedView(kSide), view_id::kOutput);
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
        QVERIFY(rig.dock->model().zone(kSide).collapsed);
        // Clicar no ícone da faixa de novo fecha e devolve o espaço.
        ZoneRail *rail = rig.dock->railWidget(Edge::Right);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kOutput).center());
        QTest::qWait(60);
        QVERIFY(rig.dock->zoneWidget(kSide) == nullptr);
        QVERIFY(rig.dock->revealedView(kSide).isEmpty());
        QCOMPARE(rig.commands->width(), wide);
    }

    void aRevealedZoneHidesItsTabStripEvenWithSeveralPanels()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.dock->moveView(view_id::kDocument, kSide);
        rig.show();
        QVERIFY(rig.dock->zoneWidget(kSide)->strip()->isVisibleTo(rig.dock->zoneWidget(kSide)));
        rig.dock->setZoneCollapsed(kSide, true);
        QVERIFY(rig.dock->revealView(view_id::kHistory));
        QTest::qWait(60);
        ViewZone *zone = rig.dock->zoneWidget(kSide);
        QVERIFY(zone != nullptr);
        QVERIFY(!zone->strip()->isVisibleTo(zone));
        // Trocar de painel pelo ícone da faixa segue funcionando, ainda sem as abas.
        ZoneRail *rail = rig.dock->railWidget(Edge::Right);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kDocument).center());
        QTest::qWait(60);
        QCOMPARE(rig.dock->revealedView(kSide), view_id::kDocument);
        QVERIFY(!rig.dock->zoneWidget(kSide)->strip()->isVisibleTo(rig.dock->zoneWidget(kSide)));
        // Expandida de vez, as abas voltam.
        rig.dock->setZoneCollapsed(kSide, false);
        QTest::qWait(60);
        QVERIFY(rig.dock->zoneWidget(kSide)->strip()->isVisibleTo(rig.dock->zoneWidget(kSide)));
    }

    // O divisor entre o conteúdo e o painel aberto responde ao mouse de verdade (roteado pela janela) e o tamanho fica salvo.
    void aRevealedZoneIsResizedByDraggingTheSplitter()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        rig.dock->setZoneCollapsed(kSide, true);
        QVERIFY(rig.dock->revealView(view_id::kOutput));
        QTest::qWait(60);
        ViewZone *zone = rig.dock->zoneWidget(kSide);
        QVERIFY(zone != nullptr);
        QSplitter *root = rig.dock->rootSplitter();
        const int index = root->indexOf(zone);
        QVERIFY(index > 0);
        QSplitterHandle *handle = root->handle(index);
        QVERIFY(handle != nullptr);
        const QPoint center = handle->mapTo(&rig.owner, handle->rect().center());
        QCOMPARE(rig.owner.childAt(center), static_cast<QWidget *>(handle));
        const int before = zone->width();
        const double sizeBefore = 0.5; // sem proporção guardada, as duas colunas dividem a largura
        QSignalSpy edited(rig.dock, &DockHost::modelEdited);
        QWindow *window = rig.owner.windowHandle();
        QVERIFY(window != nullptr);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(window, center - QPoint(60, 0));
        QTest::mouseMove(window, center - QPoint(120, 0));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center - QPoint(120, 0));
        QVERIFY2(zone->width() > before + 60, qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(zone->width())));
        QVERIFY(edited.count() >= 1);
        QVERIFY(rig.dock->model().lineSize(1) > sizeBefore + 0.03);
        // Expandir de vez mantém o que o usuário escolheu.
        rig.dock->setZoneCollapsed(kSide, false);
        QTest::qWait(60);
        QVERIFY(rig.dock->zoneWidget(kSide));
        QVERIFY(rig.dock->model().lineSize(1) > sizeBefore + 0.03);
    }

    // ---- Recolher uma zona para a lateral ----------------------------------------------------------------------------
    void aCollapsedZoneBecomesARailOfIcons()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.dock->moveView(view_id::kDocument, kSide);
        rig.show();
        QVERIFY(rig.dock->zoneWidget(kSide));
        rig.dock->setZoneCollapsed(kSide, true);
        QTest::qWait(30);
        QVERIFY(!rig.dock->zoneWidget(kSide)); // a zona saiu do quadro
        ZoneRail *rail = rig.dock->railWidget(Edge::Right);
        QVERIFY(rail != nullptr);
        QVERIFY(rail->isVisible());
        QCOMPARE(rail->width(), ZoneRail::kThickness);
        QVERIFY(rail->iconRect(view_id::kHistory).isValid());
        QVERIFY(rail->iconRect(view_id::kDocument).isValid());
        QVERIFY(!rig.registry.existingWidget(view_id::kHistory)->isVisible());
        QVERIFY(rig.dock->model().zone(kSide).collapsed);
        // A lista e a Saída ganharam o espaço.
        QVERIFY(rig.commands->width() > 700);
    }

    void clickingARailIconOpensThePanelBesideTheContentAndClickingAgainClosesIt()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.show();
        rig.dock->setZoneCollapsed(kSide, true);
        QTest::qWait(30);
        ZoneRail *rail = rig.dock->railWidget(Edge::Right);
        QVERIFY(rail);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QTest::qWait(60);
        QCOMPARE(rig.dock->revealedView(kSide), view_id::kHistory);
        QVERIFY(rig.dock->zoneWidget(kSide));
        QVERIFY(rig.dock->isViewVisible(view_id::kHistory));
        QVERIFY(rig.registry.existingWidget(view_id::kHistory)->isVisible());
        QVERIFY(rig.dock->zoneWidget(kSide)->isAncestorOf(rig.registry.existingWidget(view_id::kHistory)));
        QVERIFY(rig.commands->isVisible());
        // Clicar de novo no ícone fecha.
        rail = rig.dock->railWidget(Edge::Right);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QTest::qWait(60);
        QVERIFY(rig.dock->revealedView(kSide).isEmpty());
        QVERIFY(!rig.dock->isViewVisible(view_id::kHistory));
        QVERIFY(!rig.registry.existingWidget(view_id::kHistory)->isVisible());
        // Clicar fora NÃO fecha: o painel faz parte do quadro, como qualquer outro.
        rail = rig.dock->railWidget(Edge::Right);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QTest::qWait(60);
        QTest::mouseClick(rig.commands, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QTest::qWait(60);
        QCOMPARE(rig.dock->revealedView(kSide), view_id::kHistory);
        QVERIFY(rig.registry.existingWidget(view_id::kHistory)->isVisible());
    }

    // Arrastar o ícone da faixa leva o painel a outra zona, como arrastar a aba; soltar sem ter apertado nele não clica.
    void draggingARailIconMovesThePanelAndAReleaseAloneIsNotAClick()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.show();
        rig.dock->setZoneCollapsed(kSide, true);
        QTest::qWait(30);
        ZoneRail *rail = rig.dock->railWidget(Edge::Right);
        QVERIFY(rail);
        QSignalSpy clicked(rail, &ZoneRail::viewClicked);
        // Um soltar solto (o botão foi apertado em outro lugar) não é clique.
        QTest::mouseRelease(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QCOMPARE(clicked.count(), 0);
        // O clique de verdade segue funcionando.
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QCOMPARE(clicked.count(), 1);
        rail = rig.dock->railWidget(Edge::Right);
        // O arrasto do ícone mostra os alvos de soltar e o painel vai para a zona escolhida.
        emit rail->dragStarted(view_id::kHistory);
        QVERIFY(rig.dock->overlay()->isVisible());
        DropTarget target;
        target.valid = true;
        target.zone = kSideBottom;
        emit rig.dock->overlay()->dropped(view_id::kHistory, target);
        emit rail->dragFinished(view_id::kHistory, Qt::MoveAction, QCursor::pos());
        QTRY_COMPARE(rig.dock->zoneOf(view_id::kHistory), std::optional<Zone>(kSideBottom));
        QVERIFY(!rig.dock->overlay()->isVisible());
    }

    void revealingAPanelOfACollapsedZoneOpensItBesideTheContent()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSideBottom);
        rig.show();
        rig.dock->setZoneCollapsed(kSideBottom, true);
        QVERIFY(rig.dock->revealView(view_id::kHistory));
        QCOMPARE(rig.dock->revealedView(kSideBottom), view_id::kHistory);
        QVERIFY(rig.dock->isViewVisible(view_id::kHistory));
        // Fechar o painel aberto apaga a faixa quando a zona fica vazia.
        rig.dock->closeView(view_id::kHistory);
        QVERIFY(rig.dock->revealedView(kSideBottom).isEmpty());
        QVERIFY(!rig.dock->railWidget(Edge::Right));
        QVERIFY(!rig.dock->model().zone(kSideBottom).collapsed);
    }

    // Soltar um painel numa zona recolhida NÃO desfaz a faixa: ele entra nela e abre ao lado, com o resto como estava.
    void movingAPanelIntoACollapsedZoneKeepsItCollapsedAndOpensIt()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.show();
        rig.dock->setZoneCollapsed(kSide, true);
        const double size = rig.dock->model().zone(kSide).size;
        rig.dock->moveView(view_id::kDocument, kSide);
        QTest::qWait(60);
        QVERIFY(rig.dock->model().zone(kSide).collapsed);
        QVERIFY(rig.dock->railWidget(Edge::Right) != nullptr);
        QVERIFY(rig.dock->model().zone(kSide).views.contains(view_id::kDocument));
        QCOMPARE(rig.dock->revealedView(kSide), view_id::kDocument);
        QVERIFY(rig.dock->isViewVisible(view_id::kDocument));
        QCOMPARE(rig.dock->model().zone(kSide).size, size);
        QVERIFY(rig.railIcon(Edge::Right, view_id::kHistory).isValid());
        QVERIFY(rig.railIcon(Edge::Right, view_id::kDocument).isValid());
    }

    void collapsingIsRememberedInTheModelJson()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.dock->setZoneCollapsed(kSide, true);
        const LayoutModel back = LayoutModel::fromJson(rig.dock->model().toJson(), rig.registry.validator());
        QVERIFY(back.zone(kSide).collapsed);
        Rig another;
        another.dock->setModel(back);
        QVERIFY(another.dock->railWidget(Edge::Right));
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
            rig.dock->moveView(view_id::kOutput, kSide);
            QVERIFY(controller.saveCurrentAs(QStringLiteral("  my layout ")));
            QCOMPARE(controller.savedPerspectiveNames(), QStringList{QStringLiteral("my layout")});
            QCOMPARE(changed.count(), 1);
            QVERIFY(!controller.saveCurrentAs(QStringLiteral("   ")));
            rig.dock->restoreDefault();
            QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(kSide));
            QVERIFY(controller.applySaved(QStringLiteral("my layout")));
            QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(kSide));
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
        QCOMPARE(again.dock->zoneOf(view_id::kOutput), std::optional<Zone>(kSide));
        QVERIFY(controller.deleteSaved(QStringLiteral("my layout")));
        QVERIFY(!controller.deleteSaved(QStringLiteral("my layout")));
    }

    void presetsRearrangeThePanels()
    {
        Rig rig;
        LayoutController controller(rig.dock, nullptr, {}, &rig.owner);
        controller.applyPreset(LayoutModel::Preset::Http);
        QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(kSide));
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
        model.placeView(QStringLiteral("kip:alive"), kSide);
        perspectives.insert(QStringLiteral("p"), model);
        LayoutController controller(rig.dock, nullptr, perspectives, &rig.owner);
        QVERIFY(controller.applySaved(QStringLiteral("p")));
        QCOMPARE(rig.dock->zoneOf(QStringLiteral("kip:alive")), std::optional<Zone>(kSide));
        // O comando KIP some do registro: a próxima aplicação descarta o painel em vez de quebrar.
        Rig other;
        other.registry.registerPrefix(QStringLiteral("zzz:"), [](const QString &) { return std::nullopt; });
        QMap<QString, LayoutModel> ghost;
        LayoutModel ghostModel = LayoutModel::defaultLayout();
        ghostModel.placeView(QStringLiteral("kip:gone"), kSide);
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

    void movingBetweenEveryCellNeverLosesAPanel()
    {
        Rig rig;
        rig.show();
        for (Zone zone : {kTop, kSide, kSideBottom, kBottom, kSide, kSideBottom}) {
            rig.dock->moveView(view_id::kOutput, zone);
            rig.dock->moveView(view_id::kCommands, zone.col == 0 ? kSide : kTop);
            QTest::qWait(10);
            QVERIFY(rig.output->isVisible());
            QVERIFY(rig.commands->isVisible());
            QVERIFY(rig.output->width() > 0 && rig.output->height() > 0);
        }
    }

    // ---- A grade: o que cada célula vazia faz, e onde soltar ------------------------------------------------------------

    // 1º desenho (expansão vertical): a coluna da esquerda cheia; no meio e à direita só a célula do meio, que sobe e desce
    // e ocupa a coluna toda.
    void inVerticalModeAnEmptyCellIsAbsorbedByTheOneInTheSameColumn()
    {
        Rig rig;
        LayoutModel model;
        model.setGrid(3, 3, Expansion::Vertical);
        model.placeView(view_id::kCommands, Zone{0, 0});
        model.placeView(view_id::kOutput, Zone{1, 0});
        model.placeView(view_id::kHistory, Zone{2, 0});
        model.placeView(view_id::kDocument, Zone{1, 1});
        model.placeView(QStringLiteral("kip:alive"), Zone{1, 2});
        rig.dock->setModel(model);
        rig.show();
        const int h = rig.dock->height();
        // A coluna cheia: três células de um terço da altura.
        for (int row = 0; row < 3; ++row) {
            QVERIFY2(qAbs(rectIn(rig.dock, Zone{row, 0}).height() - h / 3) < 12, qPrintable(QString::number(row)));
        }
        // As outras duas: uma célula só, na altura toda.
        QVERIFY2(rectIn(rig.dock, Zone{1, 1}).height() >= h - 2, qPrintable(QString::number(rectIn(rig.dock, Zone{1, 1}).height())));
        QVERIFY(rectIn(rig.dock, Zone{1, 2}).height() >= h - 2);
        // As três colunas dividem a largura.
        const int w = rig.dock->width();
        QVERIFY(qAbs(rectIn(rig.dock, Zone{0, 0}).width() - w / 3) < 12);
        QVERIFY(qAbs(rectIn(rig.dock, Zone{1, 2}).width() - w / 3) < 12);
        QVERIFY(!rig.dock->zoneWidget(Zone{0, 1})); // vazia: não existe na tela
    }

    // 2º desenho (expansão horizontal): uma célula por linha, na diagonal; cada uma ocupa a linha toda.
    void inHorizontalModeAnEmptyCellIsAbsorbedByTheOneInTheSameRow()
    {
        Rig rig;
        LayoutModel model;
        model.setGrid(3, 3, Expansion::Horizontal);
        model.placeView(view_id::kCommands, Zone{0, 0});
        model.placeView(view_id::kOutput, Zone{1, 1});
        model.placeView(view_id::kHistory, Zone{2, 2});
        rig.dock->setModel(model);
        rig.show();
        const int w = rig.dock->width();
        const int h = rig.dock->height();
        for (Zone zone : {Zone{0, 0}, Zone{1, 1}, Zone{2, 2}}) {
            const QRect rect = rectIn(rig.dock, zone);
            QVERIFY2(rect.width() >= w - 2, qPrintable(QStringLiteral("%1,%2: %3").arg(zone.row).arg(zone.col).arg(rect.width())));
            QVERIFY2(qAbs(rect.height() - h / 3) < 12, qPrintable(QString::number(rect.height())));
        }
        // Em cima, no meio e embaixo, nessa ordem.
        QVERIFY(rectIn(rig.dock, Zone{0, 0}).bottom() < rectIn(rig.dock, Zone{1, 1}).top() + 2);
        QVERIFY(rectIn(rig.dock, Zone{1, 1}).bottom() < rectIn(rig.dock, Zone{2, 2}).top() + 2);
    }

    // Durante o arrasto a grade inteira aparece (as células vazias também) e dá para soltar em qualquer uma.
    void everyCellOfTheGridIsADropTarget()
    {
        Rig rig;
        rig.show();
        const QRect area = rig.dock->boardArea();
        QCOMPARE(area.size(), rig.dock->size());
        QCOMPARE(rig.dock->overlay()->parentWidget(), static_cast<QWidget *>(rig.dock));
        // As quatro células nominais cobrem a janela sem sobrepor.
        QRegion covered;
        for (Zone zone : rig.dock->model().cells()) {
            const QRect nominal = rig.dock->nominalRect(zone);
            QVERIFY(!covered.intersects(nominal));
            covered += nominal;
        }
        QCOMPARE(covered.boundingRect(), area);
        // Célula vazia (a coluna da direita): é o destino, mesmo estando "debaixo" da vizinha esticada.
        for (Zone zone : {kSide, kSideBottom}) {
            const DropTarget target = rig.dock->dropTargetAt(rig.dock->nominalRect(zone).center());
            QVERIFY(target.valid && !target.floating);
            QCOMPARE(target.zone, zone);
            QCOMPARE(target.rect, rig.dock->nominalRect(zone));
        }
        // Célula cheia: vale a célula que está ali de verdade.
        const DropTarget onOutput = rig.dock->dropTargetAt(rectIn(rig.dock, kBottom).center());
        QCOMPARE(onOutput.zone, kBottom);
        QCOMPARE(onOutput.rect, rectIn(rig.dock, kBottom));
        // A dica inclui todas as células.
        rig.dock->beginViewDrag();
        QVERIFY(rig.dock->overlay()->isVisible());
        rig.dock->overlay()->hide();
    }

    void droppingOnAnEmptyCellFillsItAndTheNeighbourGivesUpTheSpace()
    {
        Rig rig;
        rig.show();
        const int before = rig.commands->width();
        const DropTarget target = rig.dock->dropTargetAt(rig.dock->nominalRect(kSideBottom).center());
        QVERIFY(target.valid);
        rig.dock->moveView(view_id::kOutput, target.zone, target.index);
        QTest::qWait(60);
        QCOMPARE(rig.dock->zoneOf(view_id::kOutput), std::optional<Zone>(kSideBottom));
        QVERIFY(rig.dock->zoneWidget(kSideBottom)->isAncestorOf(rig.output));
        QVERIFY(rig.commands->width() < before); // a lista passou a dividir a largura com a nova coluna
        QVERIFY(rig.commands->height() >= rig.dock->height() - 40); // e ocupa a coluna toda (a de baixo ficou vazia)
        // E soltar numa célula cheia entra como aba nela.
        const DropTarget onCommands = rig.dock->dropTargetAt(rectIn(rig.dock, kTop).center());
        rig.dock->moveView(view_id::kHistory, onCommands.zone, onCommands.index);
        QCOMPARE(rig.dock->zoneOf(view_id::kHistory), std::optional<Zone>(kTop));
        QCOMPARE(rig.dock->model().zone(kTop).views, (QStringList{view_id::kCommands, view_id::kHistory}));
    }

    // Uma coluna com três células: ficam em ordem, o divisor de cima anda e o tamanho vai para o modelo.
    void aColumnStacksItsCellsInOrderAndTheDividersResize()
    {
        Rig rig;
        LayoutModel model;
        model.setGrid(1, 3, Expansion::Vertical);
        model.placeView(view_id::kCommands, Zone{0, 0});
        model.placeView(view_id::kOutput, Zone{1, 0});
        model.placeView(view_id::kHistory, Zone{2, 0});
        rig.dock->setModel(model);
        rig.show();
        QVERIFY(rectIn(rig.dock, Zone{0, 0}).bottom() < rectIn(rig.dock, Zone{1, 0}).top() + 2);
        QVERIFY(rectIn(rig.dock, Zone{1, 0}).bottom() < rectIn(rig.dock, Zone{2, 0}).top() + 2);
        QSplitter *stack = rig.dock->stackSplitter(0);
        QVERIFY(stack && stack->count() == 3);
        const QList<int> sizes = stack->sizes();
        const int total = sizes.at(0) + sizes.at(1) + sizes.at(2);
        stack->setSizes({int(total * 0.1), int(total * 0.5), total - int(total * 0.1) - int(total * 0.5)});
        emit stack->splitterMoved(int(total * 0.1), 1);
        QVERIFY(qAbs(rig.dock->model().zone(Zone{0, 0}).size - 0.1) < 0.02);
        // Redimensionar a janela mantém as proporções e nenhuma célula some.
        rig.owner.resize(1000, 520);
        QTest::qWait(60);
        for (int row = 0; row < 3; ++row) {
            QVERIFY(rig.dock->zoneWidget(Zone{row, 0})->height() > 20);
        }
    }

    // Janela pequena: o quadro rola em vez de espremer as células.
    void aSmallWindowScrollsInsteadOfSqueezingTheCells()
    {
        Rig rig;
        LayoutModel model;
        model.setGrid(3, 1, Expansion::Vertical);
        model.placeView(view_id::kCommands, Zone{0, 0});
        model.placeView(view_id::kOutput, Zone{0, 1});
        model.placeView(view_id::kHistory, Zone{0, 2});
        rig.dock->setModel(model);
        rig.owner.resize(600, 400);
        rig.dock->setGeometry(0, 0, 600, 400);
        rig.show();
        QTest::qWait(60);
        QScrollArea *scroll = rig.dock->scrollArea();
        QVERIFY(scroll != nullptr);
        QVERIFY2(scroll->horizontalScrollBar()->maximum() > 0, "três colunas em 600 px deveriam rolar");
        QVERIFY(rig.dock->board()->width() >= 3 * 300);
        for (Zone zone : {Zone{0, 0}, Zone{0, 1}, Zone{0, 2}}) {
            QVERIFY2(rig.dock->zoneWidget(zone)->width() >= 290, qPrintable(QString::number(rig.dock->zoneWidget(zone)->width())));
        }
        QCOMPARE(scroll->verticalScrollBar()->maximum(), 0); // a altura cabe: só rola na horizontal
        // Com espaço de sobra a rolagem some e o quadro volta a acompanhar a janela.
        rig.owner.resize(1300, 700);
        rig.dock->setGeometry(0, 0, 1300, 700);
        QTest::qWait(60);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QCOMPARE(rig.dock->board()->width(), scroll->viewport()->width());
    }

    void gridChangesRebuildTheBoardAndKeepEveryPanel()
    {
        Rig rig;
        rig.show();
        QSignalSpy edited(rig.dock, &DockHost::modelEdited);
        QSignalSpy arranged(rig.dock, &DockHost::arrangementChanged);
        rig.dock->setGrid(3, 2, Expansion::Horizontal);
        QCOMPARE(edited.count(), 1);
        QVERIFY(arranged.count() >= 1);
        QCOMPARE(rig.dock->model().cols(), 3);
        QCOMPARE(rig.dock->model().expansion(), Expansion::Horizontal);
        QCOMPARE(rig.dock->plan().expansion, Expansion::Horizontal);
        QVERIFY(rig.commands->isVisible());
        QVERIFY(rig.output->isVisible());
        rig.dock->setGrid(3, 2, Expansion::Horizontal); // igual: nada
        QCOMPARE(edited.count(), 1);
        // Encolher para uma célula só junta os painéis como abas dela; nada se perde.
        rig.dock->setGrid(1, 1, Expansion::Vertical);
        QTest::qWait(30);
        QCOMPARE(rig.dock->model().zone(kTop).views, (QStringList{view_id::kCommands, view_id::kOutput}));
        QVERIFY(rig.dock->isViewVisible(rig.dock->model().zone(kTop).active));
    }

    // O Top continua sendo uma borda: em modo horizontal a linha de cima recolhe numa faixa no topo, alinhada aos cartões.
    void theTopRowCollapsesIntoARailAlignedWithTheCards()
    {
        Rig rig;
        LayoutModel model;
        model.setGrid(2, 2, Expansion::Horizontal);
        model.placeView(view_id::kHistory, Zone{0, 0});
        model.placeView(view_id::kDocument, Zone{0, 0});
        model.placeView(view_id::kCommands, Zone{1, 0});
        model.placeView(view_id::kOutput, Zone{1, 1});
        rig.dock->setModel(model);
        rig.show();
        rig.dock->setZoneCollapsed(Zone{0, 0}, true);
        QTest::qWait(30);
        QVERIFY(!rig.dock->zoneWidget(Zone{0, 0}));
        ZoneRail *rail = rig.dock->railWidget(Edge::Top);
        QVERIFY(rail != nullptr);
        QVERIFY(!rail->vertical());
        QVERIFY(rail->isVisible());
        QCOMPARE(rail->width(), rig.dock->width()); // a faixa vai de ponta a ponta, como os cartões
        QVERIFY(rail->mapTo(rig.dock, QPoint(0, 0)).y() <= 1); // e fica no topo
        QVERIFY(rail->iconRect(view_id::kDocument).isValid());
        // As células de baixo ocuparam a janela toda.
        QVERIFY(rectIn(rig.dock, Zone{1, 0}).top() <= ZoneRail::kThickness + 2);
        // Clicar no ícone abre o painel por baixo da faixa.
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QTest::qWait(60);
        QCOMPARE(rig.dock->revealedView(Zone{0, 0}), view_id::kHistory);
        QVERIFY(rig.dock->zoneWidget(Zone{0, 0})->mapTo(rig.dock, QPoint(0, 0)).y() >= rail->height() - 1);
        // Expandir de volta.
        rig.dock->setZoneCollapsed(Zone{0, 0}, false);
        QTest::qWait(30);
        QVERIFY(rig.dock->zoneWidget(Zone{0, 0}));
    }

    // A faixa de baixo (modo horizontal, linha de baixo): o botão de expandir devolve a célula.
    void theExpandButtonBringsTheCellBack()
    {
        Rig rig;
        LayoutModel model;
        model.setGrid(2, 2, Expansion::Horizontal);
        model.placeView(view_id::kCommands, Zone{0, 0});
        model.placeView(view_id::kOutput, Zone{0, 1});
        model.placeView(view_id::kHistory, Zone{1, 1});
        rig.dock->setModel(model);
        rig.show();
        rig.dock->setZoneCollapsed(Zone{1, 1}, true);
        QTest::qWait(30);
        ZoneRail *rail = rig.dock->railWidget(Edge::Bottom);
        QVERIFY(rail);
        QCOMPARE(rail->height(), ZoneRail::kThickness);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->expandRect().center());
        QTest::qWait(30);
        QVERIFY(!rig.dock->railWidget(Edge::Bottom));
        QVERIFY(rig.dock->zoneWidget(Zone{1, 1}));
        QVERIFY(!rig.dock->model().zone(Zone{1, 1}).collapsed);
    }

    // Uma borda com duas células recolhidas tem UMA faixa, com os ícones das duas; o botão expande as duas.
    void severalCollapsedCellsOfAnEdgeShareOneRail()
    {
        Rig rig;
        LayoutModel model;
        model.setGrid(2, 2, Expansion::Vertical);
        model.placeView(view_id::kCommands, Zone{0, 0});
        model.placeView(view_id::kOutput, Zone{0, 1});
        model.placeView(view_id::kHistory, Zone{1, 1});
        rig.dock->setModel(model);
        rig.show();
        rig.dock->setZoneCollapsed(Zone{0, 1}, true);
        rig.dock->setZoneCollapsed(Zone{1, 1}, true);
        QTest::qWait(30);
        ZoneRail *rail = rig.dock->railWidget(Edge::Right);
        QVERIFY(rail != nullptr);
        QCOMPARE(rail->viewIds(), (QStringList{view_id::kOutput, view_id::kHistory}));
        // Abrir o ícone da segunda célula revela essa célula e marca o ícone.
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->iconRect(view_id::kHistory).center());
        QTest::qWait(60);
        QCOMPARE(rig.dock->revealedView(Zone{1, 1}), view_id::kHistory);
        QVERIFY(rig.dock->revealedView(Zone{0, 1}).isEmpty());
        QCOMPARE(rig.dock->railWidget(Edge::Right)->openViews(), QStringList{view_id::kHistory});
        // Soltar um ícone entre os da faixa cai na célula certa, na posição certa.
        const QRect first = rig.dock->railWidget(Edge::Right)->iconRect(view_id::kOutput);
        const QPoint above = rig.dock->railWidget(Edge::Right)->mapTo(rig.dock, QPoint(first.center().x(), first.top() + 1));
        const DropTarget target = rig.dock->dropTargetAt(above);
        QVERIFY(target.valid);
        QCOMPARE(target.zone, (Zone{0, 1}));
        QCOMPARE(target.index, 0);
        // Expandir pelo botão devolve as duas.
        rail = rig.dock->railWidget(Edge::Right);
        QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, rail->expandRect().center());
        QTest::qWait(30);
        QVERIFY(!rig.dock->railWidget(Edge::Right));
        QVERIFY(!rig.dock->model().zone(Zone{0, 1}).collapsed);
        QVERIFY(!rig.dock->model().zone(Zone{1, 1}).collapsed);
    }

    // O alvo virtual "Flutuar" aparece no arrasto; soltar na pílula faz o painel flutuar.
    void theFloatTargetIsOnTheDragAndDroppingOnItFloatsThePanel()
    {
        Rig rig;
        rig.show();
        const int w = rig.dock->width();
        const QRect pill = rig.dock->floatPillRect();
        QVERIFY(pill.isValid() && pill.top() >= 0 && pill.right() <= w);
        const DropTarget floating = rig.dock->dropTargetAt(pill.center());
        QVERIFY(floating.valid && floating.floating);

        // Soltar nela (o laço do arrasto simulado: a queda chega do overlay e o arrasto termina) tira o painel do quadro.
        rig.dock->beginViewDrag();
        emit rig.dock->overlay()->dropped(view_id::kOutput, floating);
        rig.dock->endViewDrag(view_id::kOutput, Qt::MoveAction, QPoint(300, 300));
        QTest::qWait(60);
        QVERIFY(rig.dock->floatingWindow(view_id::kOutput) != nullptr);
    }

    // As células do meio de uma linha/coluna não recolhem, e as faixas somem enquanto um painel está maximizado.
    void aMiddleCellNeverCollapsesAndRailsHideWhileAPanelIsMaximized()
    {
        Rig rig;
        LayoutModel model;
        model.setGrid(3, 1, Expansion::Vertical);
        model.placeView(view_id::kCommands, Zone{0, 0});
        model.placeView(view_id::kHistory, Zone{0, 1});
        model.placeView(view_id::kOutput, Zone{0, 2});
        rig.dock->setModel(model);
        rig.dock->setZoneCollapsed(Zone{0, 1}, true);
        QVERIFY(!rig.dock->model().zone(Zone{0, 1}).collapsed);
        rig.dock->setZoneCollapsed(Zone{0, 2}, true);
        QVERIFY(rig.dock->railWidget(Edge::Right));
        rig.dock->setMaximized(view_id::kCommands);
        QVERIFY(!rig.dock->railWidget(Edge::Right));
        rig.dock->setMaximized(QString());
        QVERIFY(rig.dock->railWidget(Edge::Right));
    }

    void thePanelMenuListsEveryCellAndOffersCollapseOnlyOnTheEdges()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        QMenu menu;
        rig.dock->populatePanelMenu(&menu, view_id::kHistory);
        QStringList texts;
        for (QAction *action : menu.actions()) {
            texts << action->text();
        }
        QVERIFY(texts.size() >= 4);
        QVERIFY(menu.actions().first()->menu() != nullptr); // "Mover para" é um submenu
        QCOMPARE(menu.actions().first()->menu()->actions().size(), 4); // uma entrada por célula do 2×2
        QVERIFY(texts.contains(kai::utils::tr(QStringLiteral("layout.menu.collapse_zone"))));
        rig.dock->setZoneCollapsed(kSide, true);
        QMenu again;
        rig.dock->populatePanelMenu(&again, view_id::kHistory);
        texts.clear();
        for (QAction *action : again.actions()) {
            texts << action->text();
        }
        QVERIFY(texts.contains(kai::utils::tr(QStringLiteral("layout.menu.expand_zone"))));
        // Uma célula do meio não oferece recolher.
        Rig other;
        other.dock->setGrid(3, 1, Expansion::Vertical);
        other.dock->moveView(view_id::kHistory, Zone{0, 1});
        QMenu middle;
        other.dock->populatePanelMenu(&middle, view_id::kHistory);
        texts.clear();
        for (QAction *action : middle.actions()) {
            texts << action->text();
        }
        QVERIFY(!texts.contains(kai::utils::tr(QStringLiteral("layout.menu.collapse_zone"))));
    }

    void borderDragResizesASideCellWithinLimits()
    {
        Rig rig;
        rig.dock->moveView(view_id::kOutput, kSide);
        rig.show();
        const int before = rig.dock->zoneWidth(kSide);
        QVERIFY(before > 0);
        QVERIFY(rig.dock->setZoneWidth(kSide, before + 40));
        QVERIFY(rig.dock->zoneWidth(kSide) > before);
        QVERIFY(rig.dock->setZoneWidth(kSide, 1)); // piso do painel, nunca zero
        QVERIFY(rig.dock->zoneWidth(kSide) > 1);
        QVERIFY(!rig.dock->setZoneWidth(kSideBottom, 200)); // célula que não existe
    }

    // Embaralha as operações do usuário (mover, fechar, flutuar, encaixar, maximizar, recolher, modo reorganizar, abrir,
    // mudar a grade) e confere os invariantes depois de cada uma: nenhum painel some, duplica ou fica visível fora do lugar.
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
        QRandomGenerator random(seed);
        for (int step = 0; step < 400; ++step) {
            const QString id = ids.at(int(random.bounded(ids.size())));
            // Também endereços fora da grade: o host os traz para a célula mais próxima.
            const Zone zone{int(random.bounded(kMaxGridLines)), int(random.bounded(kMaxGridLines))};
            switch (random.bounded(11)) {
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
            case 9:
                rig.dock->setGrid(1 + int(random.bounded(4)), 1 + int(random.bounded(4)),
                                  random.bounded(2) == 0 ? Expansion::Vertical : Expansion::Horizontal);
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
                for (Zone candidate : model.cells()) {
                    if (ViewZone *container = rig.dock->zoneWidget(candidate); container && container->isAncestorOf(widget)) {
                        ++homes;
                    }
                }
                if (FloatingViewWindow *window = rig.dock->floatingWindow(view); window && window->isAncestorOf(widget)) {
                    ++homes;
                }
                QVERIFY2(homes <= 1, qPrintable(QStringLiteral("%1 em %2 lugares (passo %3)").arg(view).arg(homes).arg(step)));
                // Fora do modelo: escondido. Dentro de uma célula sem estar na frente: escondido.
                if (!model.isPlaced(view)) {
                    QVERIFY2(!widget->isVisible(), qPrintable(QStringLiteral("%1 visível sem lugar (passo %2)").arg(view).arg(step)));
                }
                if (widget->isVisible()) {
                    QVERIFY2(rig.dock->isViewVisible(view), qPrintable(QStringLiteral("%1 passo %2").arg(view).arg(step)));
                }
            }
            // Nada fora da grade, e as células recolhidas são só as da borda.
            for (Zone cell : model.cells()) {
                if (model.zone(cell).collapsed) {
                    QVERIFY2(model.collapsible(cell), qPrintable(QStringLiteral("passo %1").arg(step)));
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
    void theNewDefaultShowsTwoCellsWithTheirTabStrips()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        ViewZone *left = rig.dock->zoneWidget(kTop);
        ViewZone *right = rig.dock->zoneWidget(kSide);
        QVERIFY(left && right);
        QVERIFY(left->isAncestorOf(rig.commands));
        QVERIFY(right->isAncestorOf(rig.output));
        QVERIFY(left->strip()->isVisibleTo(left)); // abas à mostra mesmo com um painel só
        QVERIFY(right->strip()->isVisibleTo(right));
        QVERIFY(!rig.dock->zoneWidget(kBottom));
        // Metade da janela para cada uma, na altura toda (as células de baixo estão vazias).
        QVERIFY2(qAbs(left->width() - right->width()) < 30, qPrintable(QStringLiteral("%1 x %2").arg(left->width()).arg(right->width())));
        QVERIFY(left->height() >= rig.dock->height() - 2);
        // E as abas se desligam (e a escolha fica no modelo, para voltar como estava).
        QSignalSpy edited(rig.dock, &DockHost::modelEdited);
        rig.dock->setEditMode(false);
        QCOMPARE(edited.count(), 1);
        QVERIFY(!rig.dock->model().tabsAlways());
        QVERIFY(!rig.dock->zoneWidget(kTop)->strip()->isVisibleTo(rig.dock->zoneWidget(kTop)));
    }

    void draggingTheDividerBetweenTwoColumnsKeepsTheProportion()
    {
        Rig rig;
        rig.dock->setModel(LayoutModel::defaultLayout());
        rig.show();
        QSplitter *root = rig.dock->rootSplitter();
        QVERIFY(root && root->count() == 2);
        const int total = root->sizes().at(0) + root->sizes().at(1);
        root->setSizes({int(total * 0.3), total - int(total * 0.3)});
        emit root->splitterMoved(int(total * 0.3), 1);
        QVERIFY(qAbs(rig.dock->model().lineSize(0) - 0.3) < 0.02);
        rig.owner.resize(1300, 800);
        rig.dock->setGeometry(0, 0, 1300, 800);
        QTest::qWait(60);
        const QList<int> sizes = root->sizes();
        QVERIFY2(qAbs(double(sizes.at(0)) / (sizes.at(0) + sizes.at(1)) - 0.3) < 0.03,
                 qPrintable(QString::number(double(sizes.at(0)) / (sizes.at(0) + sizes.at(1)))));
    }

    // A ordem do foco (Alt+N) é a da tela: pilha a pilha, e as flutuantes por último.
    void visibleOrderFollowsTheScreenThenFloating()
    {
        Rig rig;
        rig.show();
        LayoutController controller(rig.dock, nullptr, {}, &rig.owner);
        QCOMPARE(controller.visibleViewOrder(), (QStringList{view_id::kCommands, view_id::kOutput}));
        rig.dock->moveView(view_id::kCommands, kSide);
        rig.dock->moveView(view_id::kOutput, kTop);
        rig.dock->floatView(view_id::kHistory);
        QTest::qWait(30);
        QCOMPARE(controller.visibleViewOrder(), (QStringList{view_id::kOutput, view_id::kCommands, view_id::kHistory}));
        // Uma aba escondida (não é a da frente) não conta.
        rig.dock->moveView(view_id::kDocument, kSide);
        rig.dock->revealView(view_id::kCommands);
        QCOMPARE(controller.visibleViewOrder().count(view_id::kDocument), 0);
        // No modo horizontal as linhas vêm de cima para baixo.
        rig.dock->setGrid(2, 2, Expansion::Horizontal);
        QTest::qWait(30);
        const QStringList order = controller.visibleViewOrder();
        QVERIFY(order.indexOf(view_id::kOutput) >= 0);
    }

    // A faixa põe um separador entre os painéis de células diferentes (variáveis numa, saída em outra); na mesma célula, não.
    void theRailSeparatesPanelsFromDifferentCells()
    {
        Rig rig;
        rig.dock->moveView(view_id::kHistory, kSide);
        rig.dock->moveView(view_id::kDocument, kSideBottom);
        rig.dock->setZoneCollapsed(kSide, true);
        rig.dock->setZoneCollapsed(kSideBottom, true);
        rig.show();
        ZoneRail *rail = rig.dock->railWidget(Edge::Right);
        QVERIFY(rail != nullptr);
        QCOMPARE(rail->separatorRects().size(), 1);
        const QRect sep = rail->separatorRects().first();
        QVERIFY(sep.height() > 0);
        // O separador cabe entre os dois ícones sem cobri-los.
        QVERIFY(!sep.intersects(rail->iconRect(view_id::kHistory)));
        QVERIFY(!sep.intersects(rail->iconRect(view_id::kDocument)));
        QVERIFY(rail->iconRect(view_id::kDocument).top() - rail->iconRect(view_id::kHistory).bottom() > 1);

        Rig same;
        same.dock->moveView(view_id::kHistory, kSide);
        same.dock->moveView(view_id::kDocument, kSide);
        same.dock->setZoneCollapsed(kSide, true);
        same.show();
        QCOMPARE(same.dock->railWidget(Edge::Right)->separatorRects().size(), 0);
    }

    // Modo Ajustar: as células têm o tamanho de casas da grade, a alça estica por mais casas e o modelo guarda.
    void fitGripsOnlyShowInReorderMode()
    {
        Rig rig;
        LayoutModel model = rig.dock->model();
        model.setGrid(2, 2, Expansion::Fit);
        rig.dock->setModel(model);
        rig.show();
        rig.dock->setEditMode(false);
        QTest::qWait(30);
        QVERIFY(rig.dock->fitBoard()->gripOf(kTop)->isHidden());
        rig.dock->setEditMode(true);
        QTest::qWait(30);
        QVERIFY(!rig.dock->fitBoard()->gripOf(kTop)->isHidden());
    }

    void fitModeSizesCellsBySpanAndTheGripStretchesThem()
    {
        Rig rig;
        LayoutModel model = rig.dock->model();
        model.setGrid(2, 2, Expansion::Fit);
        rig.dock->setModel(model);
        rig.show();
        QVERIFY(rig.dock->fitBoard() != nullptr);
        QVERIFY(rig.dock->rootSplitter() == nullptr);
        const int cellWidth = rectIn(rig.dock, kTop).width();
        QVERIFY(cellWidth > 0);
        // A coluna do lado está vazia e continua vazia: ninguém a absorve.
        QCOMPARE(rectIn(rig.dock, kBottom).width(), cellWidth);
        QVERIFY(rig.dock->zoneWidget(kSide) == nullptr);

        QVERIFY(rig.dock->setZoneSpan(kTop, 2, 1));
        QTest::qWait(30);
        QVERIFY(rectIn(rig.dock, kTop).width() > cellWidth * 3 / 2);
        QCOMPARE(rectIn(rig.dock, kBottom).width(), cellWidth);

        // Arrastar a alça da célula de baixo até a casa da direita: o pedido aparece e, ao soltar, vale.
        FitBoard *board = rig.dock->fitBoard();
        QVERIFY(board->gripOf(kBottom) != nullptr);
        board->dragMoved(kBottom, board->cellRect(1, 1, 1, 1).center());
        QCOMPARE(board->dragPreview(), QRect(0, 1, 2, 1));
        board->dragEnded(kBottom);
        QTest::qWait(30); // o host aplica depois que a alça sai da pilha de chamadas
        QCOMPARE(rig.dock->model().zone(kBottom).colSpan, 2);
        QVERIFY(rectIn(rig.dock, kBottom).width() > cellWidth * 3 / 2);

        // A casa da direita da primeira linha é da célula de cima (esticada): a de baixo não sobe para lá.
        board = rig.dock->fitBoard();
        board->dragMoved(kBottom, board->cellRect(0, 1, 1, 1).center());
        QCOMPARE(board->dragPreview().height(), 1);
        board->dragEnded(kBottom);
        QTest::qWait(30);
        QCOMPARE(rig.dock->model().zone(kBottom).rowSpan, 1);
        // Voltar ao modo de preencher desfaz o quadro Fit.
        rig.dock->setGrid(2, 2, Expansion::Vertical);
        QVERIFY(rig.dock->fitBoard() == nullptr);
        QVERIFY(rig.dock->rootSplitter() != nullptr);
    }
};

QTEST_MAIN(TestDockHost)
#include "test_dock_host.moc"
