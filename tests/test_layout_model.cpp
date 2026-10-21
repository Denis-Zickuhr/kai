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

// O arranjo de antes do layout flexível (lista em cima, Saída embaixo: uma coluna, duas linhas): vários testes de operações
// precisam de duas células para mexer. Sem o painel Documento (aba ao lado da Saída), para a de baixo ter só a Saída.
LayoutModel classic()
{
    LayoutModel model = LayoutModel::preset(LayoutModel::Preset::Classic);
    model.closeView(view_id::kDocument);
    return model;
}

const QStringList kOutputAndDocument = {view_id::kOutput, view_id::kDocument};

// As células do padrão 2×2 (modo vertical): a lista em A, a Saída em B; C e D estão vazias.
const Zone kA{0, 0};
const Zone kB{0, 1};
const Zone kC{1, 0};
const Zone kD{1, 1};

QJsonObject parse(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

// Um layout.json da grade (versão 3), 2×2 vertical, com as células e o resto (closed, floating...) dados à mão.
QJsonObject v3(const QByteArray &cells, const QByteArray &extra = QByteArray(),
               const QByteArray &grid = QByteArrayLiteral(R"({"cols":2,"rows":2,"expansion":"vertical"})"))
{
    QByteArray json = QByteArrayLiteral(R"({"version":3,"grid":)") + grid + QByteArrayLiteral(R"(,"cells":)") + cells;
    if (!extra.isEmpty()) {
        json += "," + extra;
    }
    json += "}";
    return QJsonDocument::fromJson(json).object();
}

} // namespace

class TestLayoutModel : public QObject {
    Q_OBJECT

private slots:
    void defaultIsCommandsLeftOutputRightWithTabsShown()
    {
        const LayoutModel model = LayoutModel::defaultLayout();
        QCOMPARE(model.cols(), 2);
        QCOMPARE(model.rows(), 2);
        QCOMPARE(model.expansion(), Expansion::Vertical);
        QCOMPARE(model.zone(kA).views, QStringList{view_id::kCommands});
        QCOMPARE(model.zone(kB).views, kOutputAndDocument); // o Documento é aba ao lado da Saída
        QCOMPARE(model.zone(kB).active, view_id::kOutput);
        QVERIFY(!model.zoneOccupied(kC));
        QVERIFY(!model.zoneOccupied(kD));
        QVERIFY(model.tabsAlways());
        QCOMPARE(LayoutModel::preset(LayoutModel::Preset::Runner), model);
    }

    void classicIsTheOldCommandsOnTopOutputBelowArrangement()
    {
        const LayoutModel model = LayoutModel::preset(LayoutModel::Preset::Classic);
        QCOMPARE(model.cols(), 1);
        QCOMPARE(model.rows(), 2);
        QCOMPARE(model.zone(kA).views, QStringList{view_id::kCommands});
        QCOMPARE(model.zone(kC).views, kOutputAndDocument);
        QCOMPARE(model.zone(kC).active, view_id::kOutput);
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
        QVERIFY(LayoutModel::fromJson(v3(R"({"0,0":{"views":["commands"]}})"), knownViews()).tabsAlways());
    }

    void roundTripThroughJsonKeepsEverything()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        model.setGrid(3, 2, Expansion::Horizontal);
        model.placeView(view_id::kOutput, Zone{1, 2});
        model.setZoneSize(Zone{1, 2}, 0.4);
        model.placeView(view_id::kDocument, Zone{1, 1});
        model.placeView(view_id::kHistory, Zone{1, 1});
        model.setLineSize(0, 0.3);
        model.floatView(QStringLiteral("kip:alive"), QRect(10, 20, 400, 500), true);
        model.closeView(view_id::kHistory);
        model.setMaximized(view_id::kCommands);

        const LayoutModel back = LayoutModel::fromJson(model.toJson(), knownViews());
        QCOMPARE(back, model);
        QCOMPARE(back.zone(Zone{1, 2}).views, QStringList{view_id::kOutput});
        QCOMPARE(back.cols(), 3);
        QCOMPARE(back.expansion(), Expansion::Horizontal);
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
            v3(R"({"0,0":{"views":["commands","ghost"]},"1,1":{"views":["output"]}})"), knownViews(), &problems);
        QCOMPARE(model.zone(kA).views, QStringList{view_id::kCommands});
        QVERIFY(!problems.isEmpty());
        QVERIFY(problems.join(QLatin1Char('\n')).contains(QStringLiteral("ghost")));
    }

    void aKipPanelWhoseCommandIsGoneIsDiscarded()
    {
        const LayoutModel model = LayoutModel::fromJson(
            v3(R"({"0,1":{"views":["kip:alive","kip:deleted"]},"0,0":{"views":["commands"]},"1,1":{"views":["output"]}})"),
            knownViews());
        QCOMPARE(model.zone(kB).views, QStringList{QStringLiteral("kip:alive")});
    }

    void duplicatedViewKeepsOnlyTheFirstPlacement()
    {
        const LayoutModel model = LayoutModel::fromJson(
            v3(R"({"0,0":{"views":["commands"]},"1,0":{"views":["commands","output"]}})"), knownViews());
        QCOMPARE(model.zone(kA).views, QStringList{view_id::kCommands});
        QCOMPARE(model.zone(kC).views, kOutputAndDocument);
    }

    void emptyOrNonsenseJsonFallsBackToTheDefault()
    {
        // Os painéis de sempre voltam ao lugar padrão (lista à esquerda, Saída à direita).
        for (const char *json : {R"({"version":3,"grid":{},"cells":42,"floating":"x","closed":7})",
                                 R"({"version":3,"grid":{"cols":"x"},"cells":{"1,1":{"views":["a","b"]}}})",
                                 R"({"version":3,"grid":{"cols":99,"rows":-4},"cells":{"7,7":{"views":["commands"]}}})"}) {
            const LayoutModel model = LayoutModel::fromJson(parse(json), knownViews());
            QCOMPARE(model.zone(kA).views.first(), view_id::kCommands);
            QVERIFY(model.isPlaced(view_id::kOutput));
            QVERIFY(model.cols() >= 1 && model.cols() <= kMaxGridLines);
            QVERIFY(model.rows() >= 1 && model.rows() <= kMaxGridLines);
        }
        const LayoutModel plain = LayoutModel::fromJson(parse(R"({"version":3,"grid":{},"cells":42})"), knownViews());
        QCOMPARE(plain.zone(kA).views, QStringList{view_id::kCommands});
        QCOMPARE(plain.zone(kB).views, kOutputAndDocument);
    }

    void aLayoutWithNoPanelAtAllGetsTheDefaultBack()
    {
        // Tudo marcado como fechado e nada posto: a janela não pode ficar vazia.
        QStringList problems;
        const LayoutModel model = LayoutModel::fromJson(v3("{}", R"("closed":["commands","output"])"), knownViews(), &problems);
        QVERIFY(model.placedCount() > 0);
        QVERIFY(model.isPlaced(view_id::kCommands));
    }

    void coreViewsClosedByTheUserStayClosed()
    {
        const LayoutModel model = LayoutModel::fromJson(
            v3(R"({"0,0":{"views":["commands"]}})", R"("closed":["output"])"), knownViews());
        QVERIFY(!model.isPlaced(view_id::kOutput));
        // O Documento acompanha a Saída: com ela fechada, o arquivo que nem o menciona o deixa fechado também.
        QCOMPARE(model.closedViews(), kOutputAndDocument);
    }

    void aFileFromBeforeTheDocumentPanelGivesItATabBesideTheOutput()
    {
        const LayoutModel model = LayoutModel::fromJson(
            v3(R"({"0,0":{"views":["commands"]},"1,1":{"views":["output","history"],"active":"history"}})"), knownViews());
        QCOMPARE(model.zone(kD).views, (QStringList{view_id::kOutput, view_id::kHistory, view_id::kDocument}));
        QCOMPARE(model.zone(kD).active, view_id::kHistory); // a aba da frente não muda
        // Fechado de propósito (consta em "closed"), continua fechado.
        const LayoutModel closed = LayoutModel::fromJson(
            v3(R"({"0,0":{"views":["commands"]},"0,1":{"views":["output"]}})", R"("closed":["document"])"), knownViews());
        QVERIFY(!closed.isPlaced(view_id::kDocument));
        QCOMPARE(closed.closedViews(), QStringList{view_id::kDocument});
        // Já posto em outra célula, fica onde o usuário pôs.
        const LayoutModel moved = LayoutModel::fromJson(
            v3(R"({"0,0":{"views":["commands","document"]},"0,1":{"views":["output"]}})"), knownViews());
        QCOMPARE(moved.zoneOf(view_id::kDocument), std::optional<Zone>(kA));
    }

    void coreViewsMissingFromTheFileComeBack()
    {
        const LayoutModel model = LayoutModel::fromJson(v3(R"({"0,0":{"views":["document"]}})"), knownViews());
        QVERIFY(model.isPlaced(view_id::kCommands));
        QVERIFY(model.isPlaced(view_id::kOutput));
    }

    // Um layout.json de antes da grade (cinco zonas fixas) não é convertido: quem lê assume o padrão.
    void aFileFromBeforeTheGridIsDetectedAndFallsBackToTheDefault()
    {
        const QJsonObject old = parse(
            R"({"version":2,"zones":{"left":{"views":["output"]},"center":{"views":["commands"]}},"closed":["document"]})");
        QVERIFY(LayoutModel::isPreGrid(old));
        QStringList problems;
        QCOMPARE(LayoutModel::fromJson(old, knownViews(), &problems), LayoutModel::defaultLayout());
        QVERIFY(!problems.isEmpty());
        QVERIFY(!LayoutModel::isPreGrid(LayoutModel::defaultLayout().toJson()));
        QVERIFY(LayoutModel::isPreGrid(parse("{}")));
    }

    void activeViewAndSizesAreSanitised()
    {
        const LayoutModel model = LayoutModel::fromJson(
            v3(R"({"0,0":{"views":["commands"],"active":"output"},"0,1":{"views":["document"],"size":9.5},"1,1":{"views":["output"],"size":-3}})",
               QByteArray(), R"({"cols":2,"rows":2,"expansion":"vertical","lines":[7,-1]})"),
            knownViews());
        QCOMPARE(model.zone(kA).active, view_id::kCommands);
        QCOMPARE(model.zone(kB).size, 1.0); // fora do limite: ajustado
        QCOMPARE(model.zone(kD).size, 0.0); // negativo: sem tamanho (parte igual)
        QCOMPARE(model.lineSize(0), 1.0);
        QCOMPARE(model.lineSize(1), 0.0);
    }

    void floatingGeometryTooSmallGetsTheDefault()
    {
        const LayoutModel model = LayoutModel::fromJson(
            v3(R"({"0,0":{"views":["commands"]}})", R"("floating":[{"view":"history","geometry":[0,0,3,3]}])"),
            knownViews());
        QVERIFY(model.isFloating(view_id::kHistory));
        QVERIFY(model.floating().first().geometry.width() >= 160);
    }

    void maximizedMustBeDocked()
    {
        const LayoutModel model = LayoutModel::fromJson(
            v3(R"({"0,0":{"views":["commands"]}})", R"("maximized":"history")"), knownViews());
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
        QCOMPARE(left.zone(kA).views, kOutputAndDocument);
        QCOMPARE(left.zone(kB).views, QStringList{view_id::kCommands});
        QVERIFY(qAbs(left.plan().lineWeights.at(0) - 1.0 / 3.0) < 1e-9);
    }

    void legacyRightIsTheNewDefaultWithTheListProportion()
    {
        const LayoutModel right = LayoutModel::fromLegacy(QStringLiteral("right"), {8000, 4000});
        QCOMPARE(right.zone(kA).views, QStringList{view_id::kCommands});
        QCOMPARE(right.zone(kB).views, kOutputAndDocument);
        QVERIFY(qAbs(right.plan().lineWeights.at(0) - 2.0 / 3.0) < 1e-9);
    }

    void legacyWithoutSizesOrWithGarbageUsesTheDefaults()
    {
        QCOMPARE(LayoutModel::fromLegacy(QString(), {}), LayoutModel::defaultLayout());
        QCOMPARE(LayoutModel::fromLegacy(QStringLiteral("sideways"), {1, 2, 3}), LayoutModel::defaultLayout());
        QCOMPARE(LayoutModel::fromLegacy(QStringLiteral("right"), {0, 0}), LayoutModel::defaultLayout());
    }

    void movingAViewRemovesItFromTheOldCell()
    {
        LayoutModel model = classic();
        QVERIFY(model.placeView(view_id::kOutput, kA));
        QVERIFY(!model.zoneOccupied(kC));
        QCOMPARE(model.zoneOf(view_id::kOutput), std::optional<Zone>(kA));
        QVERIFY(!model.placeView(view_id::kOutput, kA)); // nada mudou
    }

    void aCellOutsideTheGridLandsInTheNearestOne()
    {
        LayoutModel model = classic(); // 1 coluna × 2 linhas
        QVERIFY(model.placeView(view_id::kHistory, Zone{4, 4}));
        QCOMPARE(model.zoneOf(view_id::kHistory), std::optional<Zone>(kC));
    }

    void tabsGroupInTheSameCellAndTheDroppedOneBecomesActive()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kDocument, kC, 0);
        QCOMPARE(model.zone(kC).views, (QStringList{view_id::kDocument, view_id::kOutput}));
        QCOMPARE(model.zone(kC).active, view_id::kDocument);
        QVERIFY(model.moveWithinZone(view_id::kDocument, 1));
        QCOMPARE(model.zone(kC).views, (QStringList{view_id::kOutput, view_id::kDocument}));
    }

    void closingTheActiveTabActivatesANeighbour()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kDocument, kC);
        QVERIFY(model.closeView(view_id::kDocument));
        QCOMPARE(model.zone(kC).active, view_id::kOutput);
        QVERIFY(!model.closeView(view_id::kDocument)); // já fechado
    }

    void reopeningGoesBackToTheLastPlace()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kHistory, kA);
        model.closeView(view_id::kHistory);
        QVERIFY(!model.isPlaced(view_id::kHistory));
        QVERIFY(model.openView(view_id::kHistory, kC));
        QCOMPARE(model.zoneOf(view_id::kHistory), std::optional<Zone>(kA));
        QCOMPARE(model.closedViews(), QStringList{view_id::kDocument}); // só o que classic() fechou
    }

    void lastPlaceSurvivesAJsonRoundTrip()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kHistory, kA);
        model.closeView(view_id::kHistory);
        LayoutModel back = LayoutModel::fromJson(model.toJson(), knownViews());
        back.openView(view_id::kHistory, kC);
        QCOMPARE(back.zoneOf(view_id::kHistory), std::optional<Zone>(kA));
    }

    void neverPlacedViewOpensInTheFallbackCell()
    {
        LayoutModel model = classic();
        model.openView(view_id::kAiChat, kA);
        QCOMPARE(model.zoneOf(view_id::kAiChat), std::optional<Zone>(kA));
    }

    void openingAnAlreadyVisibleViewJustActivatesIt()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kDocument, kC);
        model.setActive(view_id::kOutput);
        QVERIFY(model.openView(view_id::kDocument, kA));
        QCOMPARE(model.zoneOf(view_id::kDocument), std::optional<Zone>(kC));
        QCOMPARE(model.zone(kC).active, view_id::kDocument);
    }

    void floatingAViewTakesItOutOfTheCellAndDockingBringsItBack()
    {
        LayoutModel model = classic();
        QVERIFY(model.floatView(view_id::kOutput, QRect(0, 0, 500, 300)));
        QVERIFY(model.isFloating(view_id::kOutput));
        QVERIFY(!model.zoneOccupied(kC));
        QVERIFY(model.isPlaced(view_id::kOutput));
        QVERIFY(model.placeView(view_id::kOutput, kC));
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

    void sizesAreClampedAndEmptyCellsHaveNoCollapse()
    {
        LayoutModel model = classic();
        QVERIFY(model.setZoneSize(kC, 5.0));
        QCOMPARE(model.zone(kC).size, 1.0);
        QVERIFY(model.setZoneSize(kC, 0.0001));
        QCOMPARE(model.zone(kC).size, LayoutModel::kMinSize);
        QVERIFY(!model.setZoneSize(Zone{3, 3}, 0.5)); // fora da grade
        QVERIFY(model.setZoneCollapsed(kC, true));
        model.closeView(view_id::kOutput);
        QVERIFY(!model.zone(kC).collapsed); // célula vazia não fica recolhida
        QVERIFY(!LayoutModel::defaultLayout().zoneOccupied(kD));
        LayoutModel fresh = LayoutModel::defaultLayout();
        QVERIFY(!fresh.setZoneCollapsed(kD, true)); // célula vazia
    }

    void closeOthersKeepsOnlyTheChosenTab()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kDocument, kC);
        model.placeView(view_id::kHistory, kC);
        QVERIFY(model.closeOthersInZone(view_id::kDocument));
        QCOMPARE(model.zone(kC).views, QStringList{view_id::kDocument});
        QVERIFY(model.closedViews().contains(view_id::kOutput));
    }

    void presetsAreValidAndRestoringTheDefaultIsExact()
    {
        for (auto preset : {LayoutModel::Preset::Compact, LayoutModel::Preset::Runner, LayoutModel::Preset::Http,
                            LayoutModel::Preset::Reading, LayoutModel::Preset::Classic, LayoutModel::Preset::Triple,
                            LayoutModel::Preset::Quad}) {
            const LayoutModel model = LayoutModel::preset(preset);
            QVERIFY(model.placedCount() > 0);
            QCOMPARE(LayoutModel::fromJson(model.toJson(), knownViews()), model);
            QCOMPARE(LayoutModel::presetFromName(LayoutModel::presetName(preset)), std::optional(preset));
        }
        QCOMPARE(LayoutModel::preset(LayoutModel::Preset::Runner), LayoutModel::defaultLayout());
        QVERIFY(!LayoutModel::presetFromName(QStringLiteral("nope")).has_value());
    }

    // O Documento é aba ao lado da Saída em todos os arranjos, menos onde o arranjo o põe de propósito noutro lugar.
    void presetsKeepTheDocumentBesideTheOutput()
    {
        for (auto preset : {LayoutModel::Preset::Runner, LayoutModel::Preset::Http, LayoutModel::Preset::Classic}) {
            const LayoutModel model = LayoutModel::preset(preset);
            QVERIFY(model.zoneOf(view_id::kDocument).has_value());
            QCOMPARE(model.zoneOf(view_id::kDocument), model.zoneOf(view_id::kOutput));
            QCOMPARE(model.zone(*model.zoneOf(view_id::kOutput)).active, view_id::kOutput);
        }
        // Leitura: a lista sozinha na coluna da esquerda, o Documento em cima e a Saída embaixo.
        const LayoutModel reading = LayoutModel::preset(LayoutModel::Preset::Reading);
        QCOMPARE(reading.zoneOf(view_id::kCommands), std::optional<Zone>(kA));
        QCOMPARE(reading.zoneOf(view_id::kDocument), std::optional<Zone>(kB));
        QCOMPARE(reading.zoneOf(view_id::kOutput), std::optional<Zone>(kD));
        // Triplo: lista | Saída | Documento, três colunas e uma linha.
        const LayoutModel triple = LayoutModel::preset(LayoutModel::Preset::Triple);
        QCOMPARE(triple.cols(), 3);
        QCOMPARE(triple.rows(), 1);
        QCOMPARE(triple.zoneOf(view_id::kOutput), std::optional<Zone>(Zone{0, 1}));
        QCOMPARE(triple.zoneOf(view_id::kDocument), std::optional<Zone>(Zone{0, 2}));
        // Quádruplo: quatro painéis, um por célula do 2×2.
        const LayoutModel quad = LayoutModel::preset(LayoutModel::Preset::Quad);
        QCOMPARE(quad.plan().stacks.size(), 2);
        QCOMPARE(quad.plan().stacks.first().cells.size() + quad.plan().stacks.last().cells.size(), 4);
        const LayoutModel compact = LayoutModel::preset(LayoutModel::Preset::Compact);
        QVERIFY(!compact.isPlaced(view_id::kOutput));
        QVERIFY(!compact.isPlaced(view_id::kDocument)); // só a lista de comandos
    }

    void forgettingAViewLeavesNoTraceOfIt()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        model.closeView(view_id::kDocument);
        model.placeView(QStringLiteral("run:a"), kD);
        model.placeView(QStringLiteral("run:b"), kD);
        model.closeView(QStringLiteral("run:b"));
        QVERIFY(model.forgetView(QStringLiteral("run:a")));
        QVERIFY(model.forgetView(QStringLiteral("run:b")));
        QVERIFY(!model.forgetView(QStringLiteral("run:a")));
        QVERIFY(!model.isPlaced(QStringLiteral("run:a")));
        QCOMPARE(model.closedViews(), QStringList{view_id::kDocument});
        QCOMPARE(model.lastZoneOf(QStringLiteral("run:a"), kA), kA); // nem a última célula sobra
        QVERIFY(!model.zoneOccupied(kD));
        // O painel maximizado esquecido solta o maximizar.
        model.placeView(QStringLiteral("run:c"), kC);
        model.setMaximized(QStringLiteral("run:c"));
        model.forgetView(QStringLiteral("run:c"));
        QVERIFY(model.maximized().isEmpty());
    }

    // Reordenar dentro da célula: o índice pedido é a posição ANTES de tirar o painel (a que a faixa de abas calcula).
    void movingAViewForwardInsideItsCellLandsWhereTheDropWas()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kHistory, kC);
        model.placeView(view_id::kVariables, kC);
        QCOMPARE(model.zone(kC).views, (QStringList{view_id::kOutput, view_id::kHistory, view_id::kVariables}));
        // Soltar o primeiro entre o segundo e o terceiro.
        QVERIFY(model.placeView(view_id::kOutput, kC, 2));
        QCOMPARE(model.zone(kC).views, (QStringList{view_id::kHistory, view_id::kOutput, view_id::kVariables}));
        // Soltar o último no começo.
        QVERIFY(model.placeView(view_id::kVariables, kC, 0));
        QCOMPARE(model.zone(kC).views, (QStringList{view_id::kVariables, view_id::kHistory, view_id::kOutput}));
        // Soltar no fim (índice = tamanho da lista).
        QVERIFY(model.placeView(view_id::kVariables, kC, 3));
        QCOMPARE(model.zone(kC).views, (QStringList{view_id::kHistory, view_id::kOutput, view_id::kVariables}));
    }

    // O painel aberto ao lado de uma célula recolhida volta aberto ao reiniciar (e só vale numa célula recolhida que o tem).
    void theOpenPanelOfACollapsedCellIsRemembered()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        QVERIFY(model.setZoneCollapsed(kB, true));
        QVERIFY(model.setZoneOpen(kB, view_id::kDocument));
        QVERIFY(!model.setZoneOpen(kB, view_id::kDocument));
        const LayoutModel back = LayoutModel::fromJson(model.toJson(), knownViews());
        QCOMPARE(back.zone(kB).open, view_id::kDocument);
        QCOMPARE(back, model);
        // Expandida a célula, ou sem o painel, não sobra nada aberto.
        LayoutModel expanded = back;
        QVERIFY(expanded.setZoneCollapsed(kB, false));
        QVERIFY(expanded.zone(kB).open.isEmpty());
        QVERIFY(!model.setZoneOpen(kA, view_id::kDocument)); // não é da célula
        const LayoutModel stale = LayoutModel::fromJson(
            v3(R"({"0,0":{"views":["commands"]},"0,1":{"views":["output"],"open":"history"}})"), knownViews());
        QVERIFY(stale.zone(kB).open.isEmpty()); // célula não recolhida / painel que não está nela
    }

    // O modo dividir da célula vai para o layout.json e some quando ela esvazia.
    void theSplitModeOfACellRoundTrips()
    {
        LayoutModel model = classic();
        model.placeView(view_id::kHistory, kC);
        QVERIFY(model.setZoneSplit(kC, true));
        QVERIFY(!model.setZoneSplit(kC, true));
        LayoutModel fresh = LayoutModel::defaultLayout();
        QVERIFY(!fresh.setZoneSplit(kD, true)); // célula vazia
        const LayoutModel back = LayoutModel::fromJson(model.toJson(), knownViews());
        QVERIFY(back.zone(kC).split);
        QCOMPARE(back, model);
        model.closeView(view_id::kOutput);
        model.closeView(view_id::kHistory);
        QVERIFY(!model.zone(kC).split); // sem painéis, sem divisão
    }

    // ------------------------------------------------------------------------------------------------- a grade

    void theGridDefaultsTo2x2AndIsClampedBetweenOneAndFive()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        QVERIFY(model.setGrid(9, 0, Expansion::Vertical));
        QCOMPARE(model.cols(), kMaxGridLines);
        QCOMPARE(model.rows(), 1);
        QVERIFY(!model.setGrid(5, 1, Expansion::Vertical)); // nada mudou
        QVERIFY(model.setGrid(1, 1, Expansion::Horizontal));
        QCOMPARE(model.cols(), 1);
        QCOMPARE(model.expansion(), Expansion::Horizontal);
        QCOMPARE(model.cells().size(), 1);
        QCOMPARE(LayoutModel::defaultLayout().cells().size(), 4);
    }

    void shrinkingTheGridMovesThePanelsToTheNearestCell()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        model.placeView(view_id::kHistory, kD);
        model.placeView(view_id::kVariables, kC);
        QVERIFY(model.setGrid(1, 1, Expansion::Vertical));
        // Nada se perde: tudo vira aba da única célula que sobrou.
        QCOMPARE(model.placedCount(), 5);
        for (const QString &id : {view_id::kCommands, view_id::kOutput, view_id::kDocument, view_id::kHistory,
                                  view_id::kVariables}) {
            QCOMPARE(model.zoneOf(id), std::optional<Zone>(kA));
        }
        // Reduzir só as linhas leva a de baixo para a de cima, na mesma coluna.
        LayoutModel rows = LayoutModel::defaultLayout();
        rows.placeView(view_id::kHistory, kD);
        QVERIFY(rows.setGrid(2, 1, Expansion::Vertical));
        QCOMPARE(rows.zoneOf(view_id::kHistory), std::optional<Zone>(kB));
        // A última célula conhecida acompanha.
        QCOMPARE(rows.lastZoneOf(view_id::kHistory, kA), kB);
    }

    void changingTheGridShapeOrModeStartsFromEqualParts()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        model.setLineSize(0, 0.3);
        model.setZoneSize(kA, 0.6);
        QVERIFY(model.setGrid(3, 2, Expansion::Vertical));
        QCOMPARE(model.lineSize(0), 0.0);
        QCOMPARE(model.zone(kA).size, 0.0);
    }

    void secondaryCellIsTheLastColumnOfTheFirstRowOrTheLastRowOfASingleColumn()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        QCOMPARE(model.secondaryZone(), kB);
        model.setGrid(4, 3, Expansion::Vertical);
        QCOMPARE(model.secondaryZone(), (Zone{0, 3}));
        model.setGrid(1, 3, Expansion::Vertical);
        QCOMPARE(model.secondaryZone(), (Zone{2, 0}));
        model.setGrid(1, 1, Expansion::Vertical);
        QCOMPARE(model.secondaryZone(), kA);
        QCOMPARE(model.primaryZone(), kA);
    }

    // --- o plano: os dois desenhos do pedido

    void emptyCellsDisappearAndTheNeighboursFillTheColumn()
    {
        // Modo vertical (1º desenho): a coluna 0 cheia, a do meio e a da direita só com a célula do meio.
        LayoutModel model;
        model.setGrid(3, 3, Expansion::Vertical);
        model.placeView(QStringLiteral("a"), Zone{0, 0});
        model.placeView(QStringLiteral("b"), Zone{1, 0});
        model.placeView(QStringLiteral("c"), Zone{2, 0});
        model.placeView(QStringLiteral("d"), Zone{1, 1});
        model.placeView(QStringLiteral("e"), Zone{1, 2});
        const GridPlan plan = model.plan();
        QCOMPARE(plan.expansion, Expansion::Vertical);
        QCOMPARE(plan.stacks.size(), 3);
        QCOMPARE(plan.stacks.at(0).cells, (QList<Zone>{{0, 0}, {1, 0}, {2, 0}}));
        QCOMPARE(plan.stacks.at(1).cells, (QList<Zone>{{1, 1}})); // sobe e desce: ocupa a coluna toda
        QCOMPARE(plan.stacks.at(2).cells, (QList<Zone>{{1, 2}}));
        QCOMPARE(plan.stacks.at(1).weights, (QList<double>{1.0}));
        for (double weight : plan.stacks.at(0).weights) {
            QVERIFY(qAbs(weight - 1.0 / 3.0) < 1e-9);
        }
    }

    void inHorizontalModeTheRowsAreTheStacks()
    {
        // Modo horizontal (2º desenho): uma célula por linha, na diagonal; cada uma ocupa a linha toda.
        LayoutModel model;
        model.setGrid(3, 3, Expansion::Horizontal);
        model.placeView(QStringLiteral("a"), Zone{0, 0});
        model.placeView(QStringLiteral("b"), Zone{1, 1});
        model.placeView(QStringLiteral("c"), Zone{2, 2});
        const GridPlan plan = model.plan();
        QCOMPARE(plan.expansion, Expansion::Horizontal);
        QCOMPARE(plan.stacks.size(), 3);
        QCOMPARE(plan.stacks.at(0).cells, (QList<Zone>{{0, 0}}));
        QCOMPARE(plan.stacks.at(1).cells, (QList<Zone>{{1, 1}}));
        QCOMPARE(plan.stacks.at(2).cells, (QList<Zone>{{2, 2}}));
        for (double weight : plan.lineWeights) {
            QVERIFY(qAbs(weight - 1.0 / 3.0) < 1e-9);
        }
        // A mesma ocupação no modo vertical dá três colunas de uma célula só.
        model.setGrid(3, 3, Expansion::Vertical);
        QCOMPARE(model.plan().stacks.size(), 3);
    }

    void anEmptyColumnVanishesAndTheOthersShareTheWidth()
    {
        LayoutModel model;
        model.setGrid(3, 1, Expansion::Vertical);
        model.placeView(QStringLiteral("a"), Zone{0, 0});
        model.placeView(QStringLiteral("c"), Zone{0, 2});
        const GridPlan plan = model.plan();
        QCOMPARE(plan.stacks.size(), 2);
        QCOMPARE(plan.stacks.at(0).line, 0);
        QCOMPARE(plan.stacks.at(1).line, 2);
        QCOMPARE(plan.lineWeights, (QList<double>{0.5, 0.5}));
    }

    void theWeightsFollowTheStoredSizesAndUnsetOnesGetTheAverage()
    {
        LayoutModel model;
        model.setGrid(2, 2, Expansion::Vertical);
        model.placeView(QStringLiteral("a"), Zone{0, 0});
        model.placeView(QStringLiteral("b"), Zone{1, 0});
        model.placeView(QStringLiteral("c"), Zone{0, 1});
        model.setLineSize(0, 0.75);
        model.setZoneSize(Zone{0, 0}, 0.25);
        // Só uma coluna (e uma célula) tem tamanho: a que não tem leva a média das que têm, então ficam iguais.
        GridPlan plan = model.plan();
        QCOMPARE(plan.lineWeights, (QList<double>{0.5, 0.5}));
        QCOMPARE(plan.stacks.at(0).weights, (QList<double>{0.5, 0.5}));
        // Duas definidas e diferentes: valem, normalizadas.
        model.setZoneSize(Zone{1, 0}, 0.75);
        plan = model.plan();
        QVERIFY(qAbs(plan.stacks.at(0).weights.at(0) - 0.25) < 1e-9);
        QVERIFY(qAbs(plan.stacks.at(0).weights.at(1) - 0.75) < 1e-9);
        // Tudo definido: as proporções são as dadas, normalizadas.
        model.setLineSize(1, 0.25);
        plan = model.plan();
        QVERIFY(qAbs(plan.lineWeights.at(0) - 0.75) < 1e-9);
        QVERIFY(qAbs(plan.lineWeights.at(1) - 0.25) < 1e-9);
    }

    void collapsedCellsLeaveThePlanUnlessTheCallerSaysOtherwise()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        QVERIFY(model.setZoneCollapsed(kB, true));
        QCOMPARE(model.plan().stacks.size(), 1); // só a lista; a Saída virou faixa
        const GridPlan open = model.plan([&model](Zone zone) { return model.zoneOccupied(zone); });
        QCOMPARE(open.stacks.size(), 2); // aberta ao lado: volta ao plano
        const GridPlan onlyOutput = model.plan([](Zone zone) { return zone == Zone{0, 1}; });
        QCOMPARE(onlyOutput.stacks.size(), 1);
        QCOMPARE(onlyOutput.stacks.first().cells, (QList<Zone>{kB}));
        QVERIFY(LayoutModel().plan([](Zone) { return false; }).empty());
    }

    // --- recolher: só as células da borda

    void onlyEdgeCellsCanCollapseAndTheEdgeFollowsTheMode()
    {
        LayoutModel model;
        model.setGrid(3, 3, Expansion::Vertical);
        QCOMPARE(model.railEdge(Zone{1, 0}), std::optional<Edge>(Edge::Left));
        QCOMPARE(model.railEdge(Zone{0, 2}), std::optional<Edge>(Edge::Right));
        QVERIFY(!model.railEdge(Zone{1, 1}).has_value()); // coluna do meio
        QVERIFY(!model.railEdge(Zone{5, 5}).has_value()); // fora da grade
        model.setGrid(3, 3, Expansion::Horizontal);
        QCOMPARE(model.railEdge(Zone{0, 1}), std::optional<Edge>(Edge::Top));
        QCOMPARE(model.railEdge(Zone{2, 1}), std::optional<Edge>(Edge::Bottom));
        QVERIFY(!model.railEdge(Zone{1, 0}).has_value()); // linha do meio
        model.setGrid(1, 1, Expansion::Vertical);
        QCOMPARE(model.railEdge(kA), std::optional<Edge>(Edge::Left)); // sozinha: a esquerda
        QVERIFY(model.collapsible(kA));
    }

    void aMiddleCellRefusesToCollapse()
    {
        LayoutModel model;
        model.setGrid(3, 1, Expansion::Vertical);
        model.placeView(view_id::kCommands, Zone{0, 1});
        QVERIFY(!model.setZoneCollapsed(Zone{0, 1}, true));
        QVERIFY(!model.zone(Zone{0, 1}).collapsed);
        // Um arquivo editado à mão com a célula do meio recolhida é consertado.
        const LayoutModel fixed = LayoutModel::fromJson(
            v3(R"({"0,1":{"views":["commands"],"collapsed":true,"open":"commands"},"0,2":{"views":["output"]}})",
               QByteArray(), R"({"cols":3,"rows":1,"expansion":"vertical"})"),
            knownViews());
        QVERIFY(!fixed.zone(Zone{0, 1}).collapsed);
        QVERIFY(fixed.zone(Zone{0, 1}).open.isEmpty());
    }

    void changingTheGridUncollapsesWhatNoLongerSitsOnAnEdge()
    {
        LayoutModel model;
        model.setGrid(2, 1, Expansion::Vertical);
        model.placeView(view_id::kCommands, Zone{0, 0});
        model.placeView(view_id::kOutput, Zone{0, 1});
        QVERIFY(model.setZoneCollapsed(Zone{0, 1}, true));
        model.setGrid(3, 1, Expansion::Vertical); // a célula 0,1 é agora a do meio
        QVERIFY(!model.zone(Zone{0, 1}).collapsed);
    }

    void zoneNamesRoundTrip()
    {
        for (int row = 0; row < kMaxGridLines; ++row) {
            for (int col = 0; col < kMaxGridLines; ++col) {
                const Zone zone{row, col};
                QCOMPARE(zoneFromName(zoneName(zone)), std::optional(zone));
            }
        }
        QVERIFY(!zoneFromName(QStringLiteral("sideways")).has_value());
        QVERIFY(!zoneFromName(QStringLiteral("5,0")).has_value());
        QVERIFY(!zoneFromName(QStringLiteral("1,x")).has_value());
        QVERIFY(!zoneFromName(QStringLiteral("-1,0")).has_value());
        QCOMPARE(expansionFromName(expansionName(Expansion::Horizontal)), Expansion::Horizontal);
        QCOMPARE(expansionFromName(QStringLiteral("whatever")), Expansion::Vertical);
    }

    // ------------------------------------------------------------------------------------- modo Fit ("Ajustar")

    void fitModeKeepsEmptyCellsEmptyAndSpansAreClampedToTheGrid()
    {
        LayoutModel model = LayoutModel::defaultLayout(); // lista em 0,0; Saída (+Documento) em 0,1
        QVERIFY(model.setGrid(3, 2, Expansion::Fit));
        QCOMPARE(model.expansion(), Expansion::Fit);
        // Só o modo Fit estica.
        LayoutModel vertical = LayoutModel::defaultLayout();
        QVERIFY(!vertical.setZoneSpan(Zone{0, 0}, 1, 2));
        // A lista pode descer para a linha de baixo (vazia), mas não passar da grade nem invadir a Saída.
        QVERIFY(model.setZoneSpan(Zone{0, 0}, 1, 9));
        QCOMPARE(model.zone(Zone{0, 0}).rowSpan, 2);
        QVERIFY(!model.setZoneSpan(Zone{0, 0}, 2, 1)); // bate na Saída em 0,1
        QVERIFY(!model.setZoneSpan(Zone{1, 1}, 2, 1)); // célula vazia: não tem o que esticar
        QCOMPARE(model.clampSpan(Zone{0, 0}, 3, 2), QSize(1, 2));
        // O plano traz o retângulo de cada célula que aparece.
        const GridPlan plan = model.plan();
        QCOMPARE(plan.fit.size(), 2);
        QCOMPARE(plan.fit.at(0).zone, (Zone{0, 0}));
        QCOMPARE(plan.fit.at(0).rowSpan, 2);
        QCOMPARE(plan.fit.at(1).zone, (Zone{0, 1}));
        QCOMPARE(plan.fit.at(1).colSpan, 1);
    }

    void aSpanThatLaterCollidesShrinksOnReadAndComesBackWhenTheNeighbourLeaves()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        model.setGrid(2, 2, Expansion::Fit);
        model.closeView(view_id::kDocument);
        model.closeView(view_id::kOutput);
        QVERIFY(model.setZoneSpan(Zone{0, 0}, 2, 2));
        QCOMPARE(model.fitCells().first().colSpan, 2);
        model.placeView(view_id::kOutput, Zone{0, 1});
        QCOMPARE(model.fitCells().first().colSpan, 1); // a Saída tomou a casa: a lista encolhe na leitura
        QCOMPARE(model.zone(Zone{0, 0}).colSpan, 2);   // o pedido guardado não mudou
        model.closeView(view_id::kOutput);
        const QList<FitCell> alone = model.fitCells();
        QCOMPARE(alone.size(), 1);
        QCOMPARE(alone.first().colSpan, 2);
        QCOMPARE(alone.first().rowSpan, 2);
        // Um painel solto numa casa coberta toma o lugar e a célula grande recua.
        model.placeView(view_id::kOutput, Zone{1, 1});
        const QList<FitCell> cells = model.fitCells();
        QCOMPARE(cells.size(), 2);
        QCOMPARE(cells.first().colSpan * cells.first().rowSpan, 2);
    }

    void fitSpansRoundTripAndOldFilesLoadWithSingleCellSpans()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        model.setGrid(3, 2, Expansion::Fit);
        model.setZoneSpan(Zone{0, 0}, 1, 2);
        const QJsonObject json = model.toJson();
        QCOMPARE(json.value(QStringLiteral("grid")).toObject().value(QStringLiteral("expansion")).toString(), QStringLiteral("fit"));
        const QJsonObject cell = json.value(QStringLiteral("cells")).toObject().value(QStringLiteral("0,0")).toObject();
        QCOMPARE(cell.value(QStringLiteral("row_span")).toInt(), 2);
        QVERIFY(!cell.contains(QStringLiteral("col_span"))); // o que é 1 não vai para o arquivo
        const LayoutModel back = LayoutModel::fromJson(json, knownViews());
        QCOMPARE(back, model);
        QCOMPARE(back.zone(Zone{0, 0}).rowSpan, 2);
        // Um arquivo de antes do modo Fit (sem os campos) lê tudo como uma casa.
        const LayoutModel old = LayoutModel::fromJson(LayoutModel::defaultLayout().toJson(), knownViews());
        QCOMPARE(old.zone(Zone{0, 0}).colSpan, 1);
        QCOMPARE(old.zone(Zone{0, 0}).rowSpan, 1);
        QVERIFY(!LayoutModel::defaultLayout().toJson().value(QStringLiteral("cells")).toObject()
                     .value(QStringLiteral("0,0")).toObject().contains(QStringLiteral("row_span")));
        // Valores absurdos são limitados, não derrubam.
        QJsonObject broken = json;
        QJsonObject cells = broken.value(QStringLiteral("cells")).toObject();
        QJsonObject first = cells.value(QStringLiteral("0,0")).toObject();
        first.insert(QStringLiteral("row_span"), 99);
        first.insert(QStringLiteral("col_span"), -4);
        cells.insert(QStringLiteral("0,0"), first);
        broken.insert(QStringLiteral("cells"), cells);
        const LayoutModel fixed = LayoutModel::fromJson(broken, knownViews());
        QCOMPARE(fixed.zone(Zone{0, 0}).colSpan, 1);
        QCOMPARE(fixed.fitCells().first().rowSpan, 2);
        QCOMPARE(expansionFromName(expansionName(Expansion::Fit)), Expansion::Fit);
    }

    void anEmptiedCellForgetsItsSpan()
    {
        LayoutModel model = LayoutModel::defaultLayout();
        model.setGrid(2, 2, Expansion::Fit);
        model.setZoneSpan(Zone{0, 0}, 1, 2);
        model.closeView(view_id::kCommands);
        QCOMPARE(model.zone(Zone{0, 0}).rowSpan, 1);
        model.placeView(view_id::kCommands, Zone{0, 0});
        QCOMPARE(model.fitCells().first().rowSpan, 1);
    }
};

QTEST_APPLESS_MAIN(TestLayoutModel)
#include "test_layout_model.moc"
