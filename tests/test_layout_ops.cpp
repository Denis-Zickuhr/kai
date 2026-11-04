// As operações de layout da IA (kai_layout_apply) sobre o LayoutModel: tudo ou nada, validação, descrição das mudanças.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "ui/layout/layout-model.h"
#include "ui/layout/layout-ops.h"

using namespace kai::ui::layout;

namespace {

LayoutOpsContext context()
{
    LayoutOpsContext c;
    c.isKnown = [](const QString &id) {
        return id == view_id::kCommands || id == view_id::kOutput || id == view_id::kDocument || id == view_id::kHistory;
    };
    c.closable = [](const QString &id) { return id != view_id::kCommands; };
    return c;
}

QJsonObject op(const QString &name, const QJsonObject &extra = {})
{
    QJsonObject o = extra;
    o[QStringLiteral("op")] = name;
    return o;
}

} // namespace

class TestLayoutOps : public QObject {
    Q_OBJECT

private slots:
    void movesAPanelToAnotherCellAndDescribesIt()
    {
        const LayoutModel start = LayoutModel::defaultLayout();
        const auto r = applyLayoutOps(start, {op(QStringLiteral("place"), {{QStringLiteral("view"), view_id::kOutput}, {QStringLiteral("zone"), QStringLiteral("1,0")}})},
                                      context());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.model.zoneOf(view_id::kOutput), std::optional<Zone>(Zone{1, 0}));
        QVERIFY(r.changes.join(QLatin1Char('\n')).contains(QStringLiteral("output: 0,1 → 1,0")));
    }

    void closeAndReopenAPanel()
    {
        const LayoutModel start = LayoutModel::defaultLayout();
        auto r = applyLayoutOps(start, {op(QStringLiteral("close"), {{QStringLiteral("view"), view_id::kOutput}})}, context());
        QVERIFY(r.ok);
        QVERIFY(!r.model.isPlaced(view_id::kOutput));
        r = applyLayoutOps(r.model, {op(QStringLiteral("open"), {{QStringLiteral("view"), view_id::kOutput}})}, context());
        QVERIFY(r.ok);
        QVERIFY(r.model.isPlaced(view_id::kOutput));
    }

    void resizesCellsAndColumns()
    {
        const LayoutModel start = LayoutModel::defaultLayout();
        const auto r = applyLayoutOps(start,
                                      {op(QStringLiteral("line_size"), {{QStringLiteral("line"), 0}, {QStringLiteral("size"), 0.3}}),
                                       op(QStringLiteral("zone_size"), {{QStringLiteral("zone"), QStringLiteral("0,1")}, {QStringLiteral("size"), 0.4}})},
                                      context());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(qAbs(r.model.lineSize(0) - 0.3) < 0.001);
        QVERIFY(qAbs(r.model.zone(Zone{0, 1}).size - 0.4) < 0.001);
        QVERIFY(!r.changes.isEmpty());
    }

    void changesTheGridAndKeepsPanelsWhenShrinking()
    {
        LayoutModel start = LayoutModel::defaultLayout();
        auto r = applyLayoutOps(start, {op(QStringLiteral("grid"), {{QStringLiteral("cols"), 3}, {QStringLiteral("rows"), 1}, {QStringLiteral("expansion"), QStringLiteral("horizontal")}}),
                                        op(QStringLiteral("place"), {{QStringLiteral("view"), view_id::kHistory}, {QStringLiteral("zone"), QStringLiteral("0,2")}})},
                                context());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.model.cols(), 3);
        QCOMPARE(r.model.rows(), 1);
        QCOMPARE(r.model.expansion(), Expansion::Horizontal);
        r = applyLayoutOps(r.model, {op(QStringLiteral("grid"), {{QStringLiteral("cols"), 1}, {QStringLiteral("rows"), 1}})}, context());
        QVERIFY(r.ok);
        QVERIFY(r.model.isPlaced(view_id::kHistory)); // nada se perde
    }

    void floatsMaximizesSplitsAndCollapses()
    {
        const LayoutModel start = LayoutModel::defaultLayout();
        auto r = applyLayoutOps(start, {op(QStringLiteral("float"), {{QStringLiteral("view"), view_id::kOutput}, {QStringLiteral("x"), 10}, {QStringLiteral("y"), 20},
                                                                       {QStringLiteral("w"), 600}, {QStringLiteral("h"), 400}, {QStringLiteral("always_on_top"), true}})},
                                context());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.model.isFloating(view_id::kOutput));
        QCOMPARE(r.model.floating().first().geometry, QRect(10, 20, 600, 400));
        r = applyLayoutOps(start, {op(QStringLiteral("maximize"), {{QStringLiteral("view"), view_id::kOutput}})}, context());
        QVERIFY(r.ok);
        QCOMPARE(r.model.maximized(), view_id::kOutput);
        r = applyLayoutOps(r.model, {op(QStringLiteral("maximize"), {{QStringLiteral("view"), QString()}})}, context());
        QVERIFY(r.model.maximized().isEmpty());
        r = applyLayoutOps(start, {op(QStringLiteral("collapse"), {{QStringLiteral("zone"), QStringLiteral("0,1")}, {QStringLiteral("collapsed"), true}}),
                                   op(QStringLiteral("split"), {{QStringLiteral("zone"), QStringLiteral("0,0")}, {QStringLiteral("split"), true}})},
                           context());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.model.zone(Zone{0, 1}).collapsed);
        QVERIFY(r.model.zone(Zone{0, 0}).split);
    }

    void anyInvalidOperationRejectsEverythingWithItsNumber()
    {
        const LayoutModel start = LayoutModel::defaultLayout();
        const QList<QJsonObject> bad = {
            op(QStringLiteral("place"), {{QStringLiteral("view"), QStringLiteral("ghost")}, {QStringLiteral("zone"), QStringLiteral("0,0")}}),
            op(QStringLiteral("place"), {{QStringLiteral("view"), view_id::kOutput}, {QStringLiteral("zone"), QStringLiteral("9,9")}}),
            op(QStringLiteral("place"), {{QStringLiteral("view"), view_id::kOutput}, {QStringLiteral("zone"), QStringLiteral("x")}}),
            op(QStringLiteral("close"), {{QStringLiteral("view"), view_id::kCommands}}),
            op(QStringLiteral("grid"), {{QStringLiteral("cols"), 9}, {QStringLiteral("rows"), 1}}),
            op(QStringLiteral("grid"), {{QStringLiteral("cols"), 2}, {QStringLiteral("rows"), 1}, {QStringLiteral("expansion"), QStringLiteral("diagonal")}}),
            op(QStringLiteral("zone_size"), {{QStringLiteral("zone"), QStringLiteral("0,0")}, {QStringLiteral("size"), 5}}),
            op(QStringLiteral("line_size"), {{QStringLiteral("line"), 7}, {QStringLiteral("size"), 0.5}}),
            op(QStringLiteral("float"), {{QStringLiteral("view"), view_id::kOutput}}),
            op(QStringLiteral("activate"), {{QStringLiteral("view"), view_id::kHistory}}),
            op(QStringLiteral("collapse"), {{QStringLiteral("zone"), QStringLiteral("0,0")}, {QStringLiteral("collapsed"), QStringLiteral("yes")}}),
            op(QStringLiteral("explode")),
        };
        for (const QJsonObject &b : bad) {
            const QJsonArray ops{op(QStringLiteral("tabs_always"), {{QStringLiteral("on"), false}}), b};
            const auto r = applyLayoutOps(start, ops, context());
            QVERIFY2(!r.ok, qPrintable(QString::fromUtf8(QJsonDocument(b).toJson(QJsonDocument::Compact))));
            QVERIFY(r.error.startsWith(QStringLiteral("Operation 2:")));
        }
    }

    void presetNamesRoundTrip()
    {
        const QStringList names = layoutPresetNames();
        QVERIFY(names.size() >= 5);
        for (const QString &n : names) QVERIFY2(LayoutModel::presetFromName(n).has_value(), qPrintable(n));
    }
};

QTEST_MAIN(TestLayoutOps)
#include "test_layout_ops.moc"
