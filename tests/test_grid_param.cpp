#include <QTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "core/cli-param-binder.h"
#include "core/grid-param.h"
#include "core/models.h"

using namespace kai::core;

namespace {

GridColumn column(const QString &name, ParameterType type, const QString &def = QString(), const QStringList &options = {})
{
    GridColumn c;
    c.name = name;
    c.type = type;
    c.defaultValue = def;
    c.options = options;
    return c;
}

Parameter products()
{
    Parameter p;
    p.name = QStringLiteral("products");
    p.type = ParameterType::Grid;
    p.gridColumns = {column(QStringLiteral("sku"), ParameterType::Text),
                     column(QStringLiteral("price"), ParameterType::Number, QStringLiteral("0")),
                     column(QStringLiteral("active"), ParameterType::Bool, QStringLiteral("true"))};
    return p;
}

QJsonArray rows(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).array();
}

} // namespace

class TestGridParam : public QObject {
    Q_OBJECT

private slots:
    void cellsAreTypedByTheirColumn()
    {
        const Parameter p = products();
        QCOMPARE(gridCellValue(p.gridColumns[1], QStringLiteral("19")), QJsonValue(19));
        QCOMPARE(gridCellValue(p.gridColumns[1], QStringLiteral("19,5")), QJsonValue(19.5));
        QVERIFY(gridCellValue(p.gridColumns[1], QString()).isNull());
        QVERIFY(gridCellValue(p.gridColumns[1], QStringLiteral("abc")).isNull());
        QCOMPARE(gridCellValue(p.gridColumns[2], QStringLiteral("yes")), QJsonValue(true));
        QCOMPARE(gridCellValue(p.gridColumns[2], QStringLiteral("no")), QJsonValue(false));
        QCOMPARE(gridCellValue(p.gridColumns[0], QStringLiteral("007")), QJsonValue(QStringLiteral("007")));
    }

    void cellTextRoundTrips()
    {
        const Parameter p = products();
        QCOMPARE(gridCellText(p.gridColumns[1], QJsonValue(19)), QStringLiteral("19"));
        QCOMPARE(gridCellText(p.gridColumns[1], QJsonValue(19.5)), QStringLiteral("19.5"));
        QCOMPARE(gridCellText(p.gridColumns[2], QJsonValue(true)), QStringLiteral("true"));
        QCOMPARE(gridCellText(p.gridColumns[1], QJsonValue(QJsonValue::Null)), QString());
    }

    void aSelectColumnStartsOnItsFirstOptionAndKeepsTheValueNotTheLabel()
    {
        const GridColumn c = column(QStringLiteral("status"), ParameterType::Select, QString(), {QStringLiteral("Active:A"), QStringLiteral("Off:O")});
        QCOMPARE(gridDefaultRow({c}).value(QStringLiteral("status")).toString(), QStringLiteral("A"));
    }

    void withoutATemplateTheValueIsTheJsonArrayInColumnOrder()
    {
        const Parameter p = products();
        QJsonArray r;
        r.append(QJsonObject{{"active", true}, {"price", 10}, {"sku", "B-1"}});
        r.append(QJsonObject{{"active", false}, {"price", 9.5}, {"sku", "B-2"}});
        QCOMPARE(renderGrid(p, r), QStringLiteral("[{\"sku\":\"B-1\",\"price\":10,\"active\":true},{\"sku\":\"B-2\",\"price\":9.5,\"active\":false}]"));
    }

    void aTemplateWritesEachRowAndJoinsThemWithTheSeparator()
    {
        Parameter p = products();
        p.gridRowTemplate = QStringLiteral("{\"sku\":{{sku|json}},\"price\":{{price|json}}}");
        QJsonArray r;
        r.append(QJsonObject{{"sku", "A \"x\""}, {"price", 5}});
        r.append(QJsonObject{{"sku", "B"}, {"price", QJsonValue::Null}});
        QCOMPARE(renderGrid(p, r), QStringLiteral("{\"sku\":\"A \\\"x\\\"\",\"price\":5},{\"sku\":\"B\",\"price\":null}"));

        p.gridRowTemplate = QStringLiteral("{{sku}}={{price}} {{ENV_VAR}}");
        p.gridRowSeparator = QStringLiteral("\\n");
        QCOMPARE(renderGrid(p, r), QStringLiteral("A \"x\"=5 {{ENV_VAR}}\nB= {{ENV_VAR}}"));
    }

    void theParameterInjectsTheValueTheJsonAndTheCount()
    {
        Parameter p = products();
        p.gridRowTemplate = QStringLiteral("{{sku}}");
        QJsonArray r;
        r.append(QJsonObject{{"sku", "A"}});
        r.append(QJsonObject{{"sku", "B"}});
        const QMap<QString, QString> v = gridValues(p, r);
        QCOMPARE(v.value(QStringLiteral("products")), QStringLiteral("A,B"));
        QCOMPARE(v.value(QStringLiteral("products__count")), QStringLiteral("2"));
        QCOMPARE(rows(v.value(QStringLiteral("products__json"))).size(), 2);
    }

    void rowsSurviveTheJsonRoundTrip()
    {
        const Parameter p = products();
        QJsonArray r;
        r.append(QJsonObject{{"sku", "A"}, {"price", 3}, {"active", true}});
        QCOMPARE(gridRowsFromJson(gridRowsToJson(p.gridColumns, r)), r);
        QVERIFY(gridRowsFromJson(QStringLiteral("not json")).isEmpty());
        QVERIFY(gridRowsFromJson(QStringLiteral("{\"a\":1}")).isEmpty());
    }

    void pastedTextBecomesRowsByColumnOrder()
    {
        const Parameter p = products();
        const QJsonArray pasted = gridRowsFromDelimitedText(p.gridColumns, QStringLiteral("A\t10\ttrue\r\nB;20\n\nC,30,false"));
        QCOMPARE(pasted.size(), 3);
        QCOMPARE(pasted.at(0).toObject().value("price").toInt(), 10);
        QCOMPARE(pasted.at(1).toObject().value("price").toInt(), 20);
        QCOMPARE(pasted.at(1).toObject().value("active").toBool(), true); // coluna ausente: o padrão
        QCOMPARE(pasted.at(2).toObject().value("active").toBool(), false);
    }

    void aGridParameterSurvivesToJsonFromJson()
    {
        Parameter p = products();
        p.gridRowTemplate = QStringLiteral("{{sku}}");
        p.gridRowSeparator = QStringLiteral(";");
        p.gridColumns[0].label = QStringLiteral("SKU");
        const Parameter back = Parameter::fromJson(p.toJson());
        QCOMPARE(back.type, ParameterType::Grid);
        QCOMPARE(back.gridColumns.size(), 3);
        QCOMPARE(back.gridColumns[0].label, QStringLiteral("SKU"));
        QCOMPARE(back.gridColumns[1].type, ParameterType::Number);
        QCOMPARE(back.gridRowTemplate, p.gridRowTemplate);
        QCOMPARE(back.gridRowSeparator, QStringLiteral(";"));
    }

    void anotherTypeNeverWritesGridKeys()
    {
        Parameter p;
        p.name = QStringLiteral("x");
        QVERIFY(!p.toJson().contains(QStringLiteral("columns")));
    }

    void theCliTakesAJsonArrayAndInjectsTheExtras()
    {
        const Parameter p = products();
        const CliParamBindingResult r = bindCliParams({p}, {QStringLiteral("[{\"sku\":\"A\",\"price\":2}]")});
        QVERIFY(r.issues.isEmpty());
        QCOMPARE(rows(r.values.value(QStringLiteral("products"))).at(0).toObject().value("active").toBool(), true);
        QCOMPARE(r.values.value(QStringLiteral("products__count")), QStringLiteral("1"));
        QVERIFY(r.values.contains(QStringLiteral("products__json")));
    }

    void theCliRejectsAValueThatIsNotAJsonArray()
    {
        const CliParamBindingResult r = bindCliParams({products()}, {QStringLiteral("nope")});
        QVERIFY(!r.issues.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestGridParam)
#include "test_grid_param.moc"
