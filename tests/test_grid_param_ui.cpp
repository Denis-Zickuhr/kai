#include <QTest>
#include <QFrame>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>

#include "core/grid-param.h"
#include "core/http-types.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "ui/features/command-editor/parameter-form-dialog.h"
#include "ui/features/command-editor/parameter-row-dialog.h"
#include "ui/features/http/http-options-editor-widget.h"
#include "ui/features/output/http-wait-view.h"
#include "ui/features/output/output-metrics-header.h"
#include "ui/shared/grid-param-field.h"
#include "utils/translation-manager.h"

using namespace kai::core;
using namespace kai::ui;

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
                     column(QStringLiteral("active"), ParameterType::Bool, QStringLiteral("true")),
                     column(QStringLiteral("kind"), ParameterType::Select, QString(), {QStringLiteral("Book:B"), QStringLiteral("Toy:T")})};
    return p;
}

QLineEdit *cellEdit(GridParamField &field, int row, int col)
{
    auto *table = field.findChild<QTableWidget *>();
    return table->cellWidget(row, col)->findChild<QLineEdit *>();
}

} // namespace

class TestGridParamUi : public QObject {
    Q_OBJECT

private slots:
    void theFieldEditsRowsWithTypedCells()
    {
        GridParamField field(products().gridColumns);
        field.show();
        field.addRow();
        field.addRow();
        QCOMPARE(field.rowCount(), 2);
        cellEdit(field, 0, 0)->setText(QStringLiteral("A-1"));
        cellEdit(field, 0, 1)->setText(QStringLiteral("19,9"));
        cellEdit(field, 1, 0)->setText(QStringLiteral("A-2"));
        auto *table = field.findChild<QTableWidget *>();
        table->cellWidget(1, 3)->findChild<QComboBox *>()->setCurrentIndex(1);
        table->cellWidget(1, 2)->findChild<QCheckBox *>()->setChecked(false);

        const QJsonArray rows = field.rows();
        QCOMPARE(rows.at(0).toObject().value("sku").toString(), QStringLiteral("A-1"));
        QCOMPARE(rows.at(0).toObject().value("price").toDouble(), 19.9);
        QCOMPARE(rows.at(0).toObject().value("kind").toString(), QStringLiteral("B")); // o valor, não o rótulo
        QCOMPARE(rows.at(1).toObject().value("kind").toString(), QStringLiteral("T"));
        QCOMPARE(rows.at(1).toObject().value("active").toBool(), false);
    }

    void aRowCanBeRemovedAndRowsAreRestored()
    {
        GridParamField field(products().gridColumns);
        QJsonArray rows;
        rows.append(QJsonObject{{"sku", "A"}, {"price", 1}, {"active", true}, {"kind", "B"}});
        rows.append(QJsonObject{{"sku", "B"}, {"price", 2}, {"active", true}, {"kind", "B"}});
        field.setRows(rows);
        QCOMPARE(field.rowCount(), 2);
        QCOMPARE(field.rows(), rows);
        auto *table = field.findChild<QTableWidget *>();
        table->cellWidget(0, 4)->findChild<QToolButton *>()->click();
        QCOMPARE(field.rowCount(), 1);
        QCOMPARE(field.rows().at(0).toObject().value("sku").toString(), QStringLiteral("B"));
    }

    void pastedTextAddsRows()
    {
        GridParamField field(products().gridColumns);
        field.appendRowsFromText(QStringLiteral("A\t10\nB\t20"));
        QCOMPARE(field.rowCount(), 2);
        QVERIFY(field.hasContent());
    }

    void aWideGridKeepsReadableColumnsAndScrolls()
    {
        QVector<GridColumn> columns;
        for (int i = 1; i <= 10; ++i) {
            GridColumn c = column(QStringLiteral("c%1").arg(i), ParameterType::Text);
            c.label = QStringLiteral("A fairly long column label %1").arg(i);
            columns.append(c);
        }
        GridParamField field(columns);
        field.resize(500, 300);
        field.addRow();
        field.show();
        QTest::qWait(50);
        auto *table = field.findChild<QTableWidget *>();
        QVERIFY(table);
        QVERIFY(table->horizontalScrollBar()->maximum() > 0);
        for (int c = 0; c < columns.size(); ++c) {
            QVERIFY2(table->columnWidth(c) >= 128, "a column must not shrink below a usable width");
            QCOMPARE(table->horizontalHeaderItem(c)->toolTip(), columns.at(c).label);
        }
        QVERIFY(table->horizontalHeaderItem(0)->text().endsWith(QChar(0x2026)));
        field.resize(2600, 300);
        QTest::qWait(50);
        QCOMPARE(table->horizontalScrollBar()->maximum(), 0);
        QCOMPARE(table->horizontalHeaderItem(0)->text(), columns.at(0).label);
    }

    void theGridLivesInsideOneCard()
    {
        GridParamField field(products().gridColumns);
        auto *card = field.findChild<QFrame *>(QStringLiteral("gridParamCard"));
        QVERIFY(card);
        QVERIFY(card->findChild<QTableWidget *>());
        QVERIFY(card->findChild<QPushButton *>());
        QVERIFY(card->styleSheet().contains(QStringLiteral("border-radius")));
    }

    void theAddButtonSitsAboveTheTableSoItNeverMoves()
    {
        GridParamField field(products().gridColumns);
        field.resize(700, 400);
        field.show();
        QTest::qWait(30);
        auto *table = field.findChild<QTableWidget *>();
        const auto buttons = field.findChildren<QPushButton *>();
        QVERIFY(!buttons.isEmpty());
        const int before = buttons.first()->mapTo(&field, QPoint(0, 0)).y();
        QVERIFY(before + buttons.first()->height() <= table->mapTo(&field, QPoint(0, 0)).y());
        for (int i = 0; i < 6; ++i) {
            field.addRow();
        }
        QTest::qWait(30);
        QCOMPARE(buttons.first()->mapTo(&field, QPoint(0, 0)).y(), before);
    }

    void theParameterEditorKeepsTheStep()
    {
        Parameter p = products();
        p.step = QStringLiteral("Two");
        ParameterRowDialog dialog(p, {}, {}, {});
        dialog.setStepSuggestions({QStringLiteral("One"), QStringLiteral("Two")});
        QCOMPARE(dialog.result().step, QStringLiteral("Two"));
        auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("paramStepCombo"));
        QVERIFY(combo);
        QVERIFY(combo->findText(QStringLiteral("One")) >= 0);
        combo->setCurrentText(QStringLiteral(" Three "));
        QCOMPARE(dialog.result().step, QStringLiteral("Three"));
    }

    void aFreshBlankGridHasNoContent()
    {
        GridParamField field(products().gridColumns);
        field.addRow();
        QVERIFY(!field.hasContent());
    }

    void theRunFormInjectsTheRowsAndRemembersThem()
    {
        QMap<QString, QString> last;
        QString saved;
        {
            ParameterFormDialog dialog({products()}, nullptr, last, {});
            auto *grid = dialog.findChild<GridParamField *>();
            QVERIFY(grid);
            QCOMPARE(grid->rowCount(), 1);
            cellEdit(*grid, 0, 0)->setText(QStringLiteral("A-1"));
            const QMap<QString, QString> values = dialog.values();
            QVERIFY(values.value(QStringLiteral("products")).contains(QStringLiteral(R"("sku":"A-1")")));
            QCOMPARE(values.value(QStringLiteral("products__count")), QStringLiteral("1"));
            saved = values.value(QStringLiteral("products__json"));
            last = values;
        }
        ParameterFormDialog again({products()}, nullptr, last, {});
        auto *grid = again.findChild<GridParamField *>();
        QCOMPARE(grid->rowCount(), 1);
        QCOMPARE(cellEdit(*grid, 0, 0)->text(), QStringLiteral("A-1"));
    }

    void aRequiredGridBlocksOkUntilARowExists()
    {
        Parameter p = products();
        p.required = true;
        ParameterFormDialog dialog({p}, nullptr, {}, {});
        auto *ok = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        auto *grid = dialog.findChild<GridParamField *>();
        grid->setRows(QJsonArray());
        QVERIFY(!ok->isEnabled());
        grid->addRow();
        QVERIFY(ok->isEnabled());
    }

    void theParameterEditorShowsTheColumnsOnlyForGrid()
    {
        Parameter p = products();
        ParameterRowDialog dialog(p, {}, {}, {});
        dialog.show();
        auto *columns = dialog.findChild<QTableWidget *>(QStringLiteral("paramGridColumnsTable"));
        QVERIFY(columns);
        QVERIFY(columns->isVisibleTo(&dialog));
        QCOMPARE(columns->rowCount(), 4);
        QCOMPARE(dialog.result().gridColumns.size(), 4);
        dialog.findChild<QComboBox *>(QStringLiteral("paramTypeCombo"))->setCurrentText(QStringLiteral("text"));
        QVERIFY(!columns->isVisibleTo(&dialog));
        QVERIFY(dialog.result().gridColumns.isEmpty());
    }

    void aGridWithoutColumnsCannotBeSaved()
    {
        Parameter p;
        p.name = QStringLiteral("p");
        p.type = ParameterType::Grid;
        ParameterRowDialog dialog(p, {}, {}, {});
        QVERIFY(!dialog.isValid());
        auto *template_ = dialog.findChild<QLineEdit *>(QStringLiteral("paramGridTemplateField"));
        QVERIFY(template_);
    }

    // Preenche o nome, aperta OK e responde o aviso de "a/b/c" com o botão de texto `answer` (vazio = Cancelar).
    static bool saveFolderNamed(FolderEditorDialog &dialog, const QString &name, const QString &answer)
    {
        dialog.findChild<QLineEdit *>(QStringLiteral("folderNameField"))->setText(name);
        // O aviso é modal (exec): um timer no laço de eventos dele acha a caixa e aperta o botão pedido.
        auto *timer = new QTimer(&dialog);
        timer->setInterval(10);
        connect(timer, &QTimer::timeout, timer, [timer, answer]() {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                auto *box = qobject_cast<QMessageBox *>(widget);
                if (!box || !box->isVisible()) {
                    continue;
                }
                for (QAbstractButton *button : box->buttons()) {
                    if (answer.isEmpty() ? box->buttonRole(button) == QMessageBox::RejectRole : button->text() == answer) {
                        timer->stop();
                        button->click();
                        return;
                    }
                }
            }
        });
        timer->start();
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        return dialog.result() == QDialog::Accepted;
    }

    void aSlashInANewFolderNameAsksAndCanNest()
    {
        FolderEditorDialog dialog({});
        QVERIFY(saveFolderNamed(dialog, QStringLiteral("a/b/c"), kai::utils::tr(QStringLiteral("folder.nested.create"))));
        QVERIFY(dialog.nestedFoldersRequested());
    }

    void aSlashCanBeKeptAsOneName()
    {
        FolderEditorDialog dialog({});
        QVERIFY(saveFolderNamed(dialog, QStringLiteral("a/b"), kai::utils::tr(QStringLiteral("folder.nested.keep"))));
        QVERIFY(!dialog.nestedFoldersRequested());
        QCOMPARE(dialog.buildFolder().name, QStringLiteral("a/b"));
    }

    void cancellingTheNestedQuestionKeepsTheDialogOpen()
    {
        FolderEditorDialog dialog({});
        QVERIFY(!saveFolderNamed(dialog, QStringLiteral("a/b"), QString()));
    }

    void aPlainNameNeverAsks()
    {
        FolderEditorDialog dialog({});
        QVERIFY(saveFolderNamed(dialog, QStringLiteral("plain"), QStringLiteral("unused")));
        QVERIFY(!dialog.nestedFoldersRequested());
    }

    void theTimeoutIsOffUntilEnabled()
    {
        HttpOptionsEditorWidget editor;
        QVERIFY(!editor.options().timeoutSec.has_value());
        HttpOptions options;
        options.timeoutSec = 45;
        editor.setOptions(options);
        QCOMPARE(editor.options().timeoutSec.value_or(0), 45);
        editor.setOptions(HttpOptions{});
        QVERIFY(!editor.options().timeoutSec.has_value());
    }

    void theWaitViewWarnsWhenThereIsNoTimeout()
    {
        HttpWaitView banner;
        banner.start(QDateTime::currentDateTime().addSecs(-3), 0);
        QVERIFY(banner.isWaiting());
        QVERIFY(!banner.isHidden());
        bool sawNoTimeout = false;
        for (QLabel *label : banner.findChildren<QLabel *>()) {
            sawNoTimeout |= label->text() == kai::utils::tr(QStringLiteral("output.http.no_timeout"));
        }
        QVERIFY(sawNoTimeout);
        banner.stop();
        QVERIFY(!banner.isWaiting());
        QVERIFY(banner.isHidden());
    }

    void noResponseGetsItsOwnLabel()
    {
        OutputMetricsHeader header;
        header.setNoResponse(QStringLiteral("Timed out"), 1200, true);
        bool found = false;
        for (QLabel *label : header.findChildren<QLabel *>()) {
            found |= label->text() == QStringLiteral("Timed out");
        }
        QVERIFY(found);
    }
};

QTEST_MAIN(TestGridParamUi)
#include "test_grid_param_ui.moc"
