#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

#include "ui/layout/layout-model.h"

using namespace kai::ui::layout;

namespace {

LayoutModel::Validator knownViews()
{
    return [](const QString &id) {
        static const QStringList fixed = {view_id::kCommands, view_id::kOutput, view_id::kDocument, view_id::kAiChat,
                                          view_id::kProcesses, view_id::kHistory, view_id::kNotifications,
                                          view_id::kVariables};
        return fixed.contains(id) || id == QStringLiteral("kip:alive");
    };
}

// O arranjo de antes do layout flexível (lista no centro, Saída embaixo): vários testes de operações precisam de uma zona
// de baixo e de um miolo para mexer.
LayoutModel classic()
{
    return LayoutModel::preset(LayoutModel::Preset::Classic);
}

QJsonObject parse(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

} // namespace

class TestLayoutModel : public QObject {
    Q_OBJECT

private slots:
    void defaultIsCommandsLeftOutputRightWithTabsShown()
    {
        const LayoutModel model = LayoutModel::defaultLayout();
        QCOMPARE(model.zone(Zone::Left).views, QStringList{view_id::kCommands});
        QCOMPARE(model.zone(Zone::Right).views, QStringList{view_id::kOutput});
        QVERIFY(!model.zoneOccupied(Zone::Center));
        QVERIFY(!model.zoneOccupied(Zone::Bottom));
        QCOMPARE(model.effectiveSize(Zone::Left), 0.5);
        QVERIFY(model.tabsAlways());
        QCOMPARE(LayoutModel::preset(LayoutModel::Preset::Runner), model);
    }

    void classicIsTheOldCommandsCenterOutputBottomArrangement()
    {
        const LayoutModel model = classic();
        QCOMPARE(model.zone(Zone::Center).views, QStringList{view_id::kCommands});
        QCOMPARE(model.zone(Zone::Bottom).views, QStringList{view_id::kOutput});
        QVERIFY(!model.zoneOccupied(Zone::Left));
        QVERIFY(!model.zoneOccupied(Zone::Right));
        QCOMPARE(model.effectiveSize(Zone::Bottom), 0.5);
    }

    void tabStripsAreShownByDefaultAndTheChoiceIsRemembered()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        QVERIFY(model.tabsAlways());
        QVERIFY(model.setTabsAlways(false));
        QVERIFY(!model.setTabsAlways(false));
        const LayoutModel back = LayoutModel::fromJson(model.toJson(), knownViews());
        QVERIFY(!back.tabsAlways());
        // Um layout.json de antes do campo existir mostra as abas.
        QVERIFY(LayoutModel::fromJson(parse(R"({"zones":{"left":{"views":["commands"]}}})"), knownViews()).tabsAlways());
    }

    void twoSidesWithoutAMiddleMayShareTheWholeWindow()
    {
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"left":{"views":["commands"],"size":0.7},"right":{"views":["output"]}}})"), knownViews());
        QCOMPARE(model.effectiveSize(Zone::Left), 0.7); // sem miolo para proteger: a conta de "laterais comem a janela" não vale
    }

    void roundTripThroughJsonKeepsEverything()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kOutput, Zone::Right);
        model.setZoneSize(Zone::Right, 0.4);
        model.placeView(view_id::kDocument, Zone::Bottom);
        model.placeView(view_id::kHistory, Zone::Bottom);
        model.floatView(QStringLiteral("kip:alive"), QRect(10, 20, 400, 500), true);
        model.closeView(view_id::kHistory);
        model.setMaximized(view_id::kCommands);

        const LayoutModel back = LayoutModel::fromJson(model.toJson(), knownViews());
        QCOMPARE(back, model);
        QCOMPARE(back.zone(Zone::Right).views, QStringList{view_id::kOutput});
        QVERIFY(back.isFloating(QStringLiteral("kip:alive")));
        QCOMPARE(back.floating().first().geometry, QRect(10, 20, 400, 500));
        QVERIFY(back.floating().first().alwaysOnTop);
        QCOMPARE(back.closedViews(), QStringList{view_id::kHistory});
        QCOMPARE(back.maximized(), view_id::kCommands);
    }

    void unknownViewsAreDroppedAndLogged()
    {
        QStringList problems;
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"center":{"views":["commands","ghost"]},"bottom":{"views":["output"]}}})"),
            knownViews(), &problems);
        QCOMPARE(model.zone(Zone::Center).views, QStringList{view_id::kCommands});
        QVERIFY(!problems.isEmpty());
        QVERIFY(problems.join(QLatin1Char('\n')).contains(QStringLiteral("ghost")));
    }

    void aKipPanelWhoseCommandIsGoneIsDiscarded()
    {
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"right":{"views":["kip:alive","kip:deleted"]},"center":{"views":["commands"]},"bottom":{"views":["output"]}}})"),
            knownViews());
        QCOMPARE(model.zone(Zone::Right).views, QStringList{QStringLiteral("kip:alive")});
    }

    void duplicatedViewKeepsOnlyTheFirstPlacement()
    {
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"left":{"views":["commands"]},"center":{"views":["commands","output"]}}})"),
            knownViews());
        QCOMPARE(model.zone(Zone::Left).views, QStringList{view_id::kCommands});
        QCOMPARE(model.zone(Zone::Center).views, QStringList{view_id::kOutput});
    }

    void emptyOrNonsenseJsonFallsBackToTheDefault()
    {
        // Os painéis de sempre voltam ao lugar padrão (lista à esquerda, Saída à direita).
        for (const char *json : {"{}", R"({"zones":42,"floating":"x","closed":7})", R"({"zones":{"center":{"views":["a","b"]}}})"}) {
            const LayoutModel model = LayoutModel::fromJson(parse(json), knownViews());
            QCOMPARE(model.zone(Zone::Left).views, QStringList{view_id::kCommands});
            QCOMPARE(model.zone(Zone::Right).views, QStringList{view_id::kOutput});
        }
    }

    void aLayoutWithNoPanelAtAllGetsTheDefaultBack()
    {
        // Tudo marcado como fechado e nada posto: a janela não pode ficar vazia.
        QStringList problems;
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{},"closed":["commands","output"]})"), knownViews(), &problems);
        QVERIFY(model.placedCount() > 0);
        QVERIFY(model.isPlaced(view_id::kCommands));
    }

    void coreViewsClosedByTheUserStayClosed()
    {
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"center":{"views":["commands"]}},"closed":["output"]})"), knownViews());
        QVERIFY(!model.isPlaced(view_id::kOutput));
        QCOMPARE(model.closedViews(), QStringList{view_id::kOutput});
    }

    void coreViewsMissingFromTheFileComeBack()
    {
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"center":{"views":["document"]}}})"), knownViews());
        QVERIFY(model.isPlaced(view_id::kCommands));
        QVERIFY(model.isPlaced(view_id::kOutput));
    }

    void activeViewAndSizesAreSanitised()
    {
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"center":{"views":["commands"],"active":"output"},"left":{"views":["document"],"size":9.5},"bottom":{"views":["output"],"size":-3}}})"),
            knownViews());
        QCOMPARE(model.zone(Zone::Center).active, view_id::kCommands);
        QVERIFY(model.zone(Zone::Left).size <= LayoutModel::kMaxSideSize); // fora do limite: ajustado (ou zerado se comia a janela)
        QCOMPARE(model.effectiveSize(Zone::Bottom), LayoutModel::kDefaultBottomSize);
    }

    void sidesCannotEatTheWholeWindow()
    {
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"left":{"views":["document"],"size":0.6},"right":{"views":["history"],"size":0.6},"center":{"views":["commands"]},"bottom":{"views":["output"]}}})"),
            knownViews());
        QVERIFY(model.effectiveSize(Zone::Left) + model.effectiveSize(Zone::Right) <= 0.8);
    }

    void floatingGeometryTooSmallGetsTheDefault()
    {
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"center":{"views":["commands"]}},"floating":[{"view":"history","geometry":[0,0,3,3]}]})"),
            knownViews());
        QVERIFY(model.isFloating(view_id::kHistory));
        QVERIFY(model.floating().first().geometry.width() >= 160);
    }

    void maximizedMustBeDocked()
    {
        const LayoutModel model = LayoutModel::fromJson(
            parse(R"({"zones":{"center":{"views":["commands"]}},"maximized":"history"})"), knownViews());
        QVERIFY(model.maximized().isEmpty());
    }

    void legacyBottomFallsOnTheNewDefault()
    {
        // O "embaixo" antigo é o valor que toda instalação tem gravado: cai no padrão novo.
        QCOMPARE(LayoutModel::fromLegacy(QStringLiteral("bottom"), {3000, 7000}), LayoutModel::defaultLayout());
    }

    void legacyLeftKeepsTheOutputOnTheLeftWithItsSavedProportion()
    {
        // Os dois números seguem a ordem do divisor: Saída primeiro à esquerda.
        const LayoutModel left = LayoutModel::fromLegacy(QStringLiteral("left"), {4000, 8000});
        QCOMPARE(left.zone(Zone::Left).views, QStringList{view_id::kOutput});
        QCOMPARE(left.zone(Zone::Center).views, QStringList{view_id::kCommands});
        QVERIFY(qAbs(left.effectiveSize(Zone::Left) - 1.0 / 3.0) < 1e-9);
    }

    void legacyRightIsTheNewDefaultWithTheListProportion()
    {
        const LayoutModel right = LayoutModel::fromLegacy(QStringLiteral("right"), {8000, 4000});
        QCOMPARE(right.zone(Zone::Left).views, QStringList{view_id::kCommands});
        QCOMPARE(right.zone(Zone::Right).views, QStringList{view_id::kOutput});
        QVERIFY(qAbs(right.effectiveSize(Zone::Left) - 2.0 / 3.0) < 1e-9);
    }

    void legacyWithoutSizesOrWithGarbageUsesTheDefaults()
    {
        QCOMPARE(LayoutModel::fromLegacy(QString(), {}), LayoutModel::defaultLayout());
        QCOMPARE(LayoutModel::fromLegacy(QStringLiteral("sideways"), {1, 2, 3}), LayoutModel::defaultLayout());
        QCOMPARE(LayoutModel::fromLegacy(QStringLiteral("right"), {0, 0}), LayoutModel::defaultLayout());
    }

    void movingAViewRemovesItFromTheOldZone()
    {
        LayoutModel model = classic();
        QVERIFY(model.placeView(view_id::kOutput, Zone::Right));
        QVERIFY(!model.zoneOccupied(Zone::Bottom));
        QCOMPARE(model.zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
        QVERIFY(!model.placeView(view_id::kOutput, Zone::Right)); // nada mudou
    }

    void tabsGroupInTheSameZoneAndTheDroppedOneBecomesActive()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kDocument, Zone::Bottom, 0);
        QCOMPARE(model.zone(Zone::Bottom).views, (QStringList{view_id::kDocument, view_id::kOutput}));
        QCOMPARE(model.zone(Zone::Bottom).active, view_id::kDocument);
        QVERIFY(model.moveWithinZone(view_id::kDocument, 1));
        QCOMPARE(model.zone(Zone::Bottom).views, (QStringList{view_id::kOutput, view_id::kDocument}));
    }

    void closingTheActiveTabActivatesANeighbour()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kDocument, Zone::Bottom);
        QVERIFY(model.closeView(view_id::kDocument));
        QCOMPARE(model.zone(Zone::Bottom).active, view_id::kOutput);
        QVERIFY(!model.closeView(view_id::kDocument)); // já fechado
    }

    void reopeningGoesBackToTheLastPlace()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kHistory, Zone::Right);
        model.closeView(view_id::kHistory);
        QVERIFY(!model.isPlaced(view_id::kHistory));
        QVERIFY(model.openView(view_id::kHistory, Zone::Bottom));
        QCOMPARE(model.zoneOf(view_id::kHistory), std::optional<Zone>(Zone::Right));
        QVERIFY(model.closedViews().isEmpty());
    }

    void lastPlaceSurvivesAJsonRoundTrip()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kHistory, Zone::Left);
        model.closeView(view_id::kHistory);
        LayoutModel back = LayoutModel::fromJson(model.toJson(), knownViews());
        back.openView(view_id::kHistory, Zone::Bottom);
        QCOMPARE(back.zoneOf(view_id::kHistory), std::optional<Zone>(Zone::Left));
    }

    void neverPlacedViewOpensInTheFallbackZone()
    {
        LayoutModel model = classic();
        model.openView(view_id::kAiChat, Zone::Right);
        QCOMPARE(model.zoneOf(view_id::kAiChat), std::optional<Zone>(Zone::Right));
    }

    void openingAnAlreadyVisibleViewJustActivatesIt()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kDocument, Zone::Bottom);
        model.setActive(view_id::kOutput);
        QVERIFY(model.openView(view_id::kDocument, Zone::Right));
        QCOMPARE(model.zoneOf(view_id::kDocument), std::optional<Zone>(Zone::Bottom));
        QCOMPARE(model.zone(Zone::Bottom).active, view_id::kDocument);
    }

    void floatingAViewTakesItOutOfTheZoneAndDockingBringsItBack()
    {
        LayoutModel model = classic();
        QVERIFY(model.floatView(view_id::kOutput, QRect(0, 0, 500, 300)));
        QVERIFY(model.isFloating(view_id::kOutput));
        QVERIFY(!model.zoneOccupied(Zone::Bottom));
        QVERIFY(model.isPlaced(view_id::kOutput));
        QVERIFY(model.placeView(view_id::kOutput, Zone::Bottom));
        QVERIFY(!model.isFloating(view_id::kOutput));
    }

    void closingAFloatingViewForgetsTheWindow()
    {
        LayoutModel model = classic();
        model.floatView(view_id::kHistory, QRect(5, 5, 300, 300));
        QVERIFY(model.closeView(view_id::kHistory));
        QVERIFY(!model.isFloating(view_id::kHistory));
    }

    void maximizeClearsWhenThePanelCloses()
    {
        LayoutModel model = classic();
        QVERIFY(model.setMaximized(view_id::kOutput));
        QCOMPARE(model.maximized(), view_id::kOutput);
        model.closeView(view_id::kOutput);
        QVERIFY(model.maximized().isEmpty());
        QVERIFY(!model.setMaximized(QStringLiteral("nope")));
    }

    void sizesAreClampedAndEmptyZonesHaveNoCollapse()
    {
        LayoutModel model = classic();
        QVERIFY(model.setZoneSize(Zone::Bottom, 5.0));
        QCOMPARE(model.zone(Zone::Bottom).size, LayoutModel::kMaxBottomSize);
        QVERIFY(!model.setZoneSize(Zone::Center, 0.5));
        QVERIFY(!model.setZoneCollapsed(Zone::Left, true));
        QVERIFY(model.setZoneCollapsed(Zone::Bottom, true));
        model.closeView(view_id::kOutput);
        QVERIFY(!model.zone(Zone::Bottom).collapsed);
    }

    void closeOthersKeepsOnlyTheChosenTab()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kDocument, Zone::Bottom);
        model.placeView(view_id::kHistory, Zone::Bottom);
        QVERIFY(model.closeOthersInZone(view_id::kDocument));
        QCOMPARE(model.zone(Zone::Bottom).views, QStringList{view_id::kDocument});
        QVERIFY(model.closedViews().contains(view_id::kOutput));
    }

    void presetsAreValidAndRestoringTheDefaultIsExact()
    {
        for (auto preset : {LayoutModel::Preset::Compact, LayoutModel::Preset::Runner, LayoutModel::Preset::Http,
                            LayoutModel::Preset::Reading}) {
            const LayoutModel model = LayoutModel::preset(preset);
            QVERIFY(model.placedCount() > 0);
            QCOMPARE(LayoutModel::fromJson(model.toJson(), knownViews()), model);
            QCOMPARE(LayoutModel::presetFromName(LayoutModel::presetName(preset)), std::optional(preset));
        }
        QCOMPARE(LayoutModel::preset(LayoutModel::Preset::Runner), LayoutModel::defaultLayout());
        QVERIFY(!LayoutModel::presetFromName(QStringLiteral("nope")).has_value());
    }

    void forgettingAViewLeavesNoTraceOfIt()
    {
        LayoutModel model = classic();
        model.placeView(QStringLiteral("run:a"), Zone::Right);
        model.placeView(QStringLiteral("run:b"), Zone::Right);
        model.closeView(QStringLiteral("run:b"));
        QVERIFY(model.forgetView(QStringLiteral("run:a")));
        QVERIFY(model.forgetView(QStringLiteral("run:b")));
        QVERIFY(!model.forgetView(QStringLiteral("run:a")));
        QVERIFY(!model.isPlaced(QStringLiteral("run:a")));
        QVERIFY(model.closedViews().isEmpty());
        QCOMPARE(model.lastZoneOf(QStringLiteral("run:a"), Zone::Left), Zone::Left); // nem a última zona sobra
        QVERIFY(!model.zoneOccupied(Zone::Right));
        // O painel maximizado esquecido solta o maximizar.
        model.placeView(QStringLiteral("run:c"), Zone::Bottom);
        model.setMaximized(QStringLiteral("run:c"));
        model.forgetView(QStringLiteral("run:c"));
        QVERIFY(model.maximized().isEmpty());
    }

    void zoneNamesRoundTrip()
    {
        for (Zone zone : kAllZones) {
            QCOMPARE(zoneFromName(zoneName(zone)), std::optional(zone));
        }
        QVERIFY(!zoneFromName(QStringLiteral("top")).has_value());
    }
};

QTEST_APPLESS_MAIN(TestLayoutModel)
#include "test_layout_model.moc"
