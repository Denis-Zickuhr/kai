#include <QTest>

#include <QHeaderView>
#include <QLabel>
#include <QSignalSpy>
#include <QTableWidget>
#include <QToolButton>

#include "ui/features/http/http-body-editor-widget.h"
#include "ui/features/http/http-entries-editor-widget.h"
#include "ui/features/http/quick-http-edit-dialog.h"
#include "ui/shared/collapsible-section-card.h"
#include "utils/translation-manager.h"

using namespace kai::ui;
using namespace kai::core;

namespace {

HttpEntry row(const QString &name, const QString &value, bool enabled = true, bool file = false)
{
    return HttpEntry{name, value, enabled, file};
}

} // namespace

// A lista de headers/query/campos segue o padrão das outras tabelas: só o nome, lápis e lixeira por linha.
class TestHttpEntriesEditor : public QObject {
    Q_OBJECT

private slots:
    void theTableListsOnlyTheNamesAndMarksDisabledRows()
    {
        HttpEntriesEditorWidget editor;
        editor.setEntries({row(QStringLiteral("Accept"), QStringLiteral("application/json")),
                           row(QStringLiteral("X-Off"), QStringLiteral("1"), false)});
        auto *table = editor.findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->columnCount(), 2); // nome + ações
        QVERIFY(!table->horizontalHeader()->isVisible());
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("Accept"));
        QVERIFY(table->item(1, 0)->text().startsWith(QStringLiteral("X-Off")));
        QVERIFY(table->item(1, 0)->text() != QStringLiteral("X-Off")); // marcada como desligada
        QVERIFY(!table->item(0, 0)->text().contains(QStringLiteral("application/json"))); // o valor não aparece
        QVERIFY(table->cellWidget(0, 1)); // lápis/lixeira
        QCOMPARE(editor.count(), 2);
    }

    void rowsKeepTheirValuesFlagsAndOrder()
    {
        HttpEntriesEditorWidget editor;
        const QVector<HttpEntry> entries = {row(QStringLiteral("B"), QStringLiteral("2")),
                                            row(QStringLiteral("A"), QStringLiteral("1"), false, true)};
        editor.setEntries(entries);
        QCOMPARE(editor.entries(), entries);
    }

    // Bug: o card ficava no estado vazio e o "Adicionar" parecia não fazer nada; a contagem agora vem das linhas reais.
    void aCardWithAnEditorShowsTheRowsAsSoonAsThereAreAny()
    {
        CollapsibleSectionCard card(QStringLiteral("Headers"));
        auto *editor = new HttpEntriesEditorWidget(&card);
        editor->setShowOwnAddButton(false);
        card.setBody(editor);
        QObject::connect(editor, &HttpEntriesEditorWidget::changed, &card, [&]() { card.setCount(editor->count()); });
        card.setExpanded(true, false);
        card.show();
        QVERIFY(!editor->isVisibleTo(&card));
        editor->setEntries({row(QStringLiteral("Accept"), QStringLiteral("*/*"))});
        QVERIFY(editor->isVisibleTo(&card));
    }

    void changedIsEmittedWhenTheContentChanges()
    {
        HttpEntriesEditorWidget editor;
        QSignalSpy spy(&editor, &HttpEntriesEditorWidget::changed);
        editor.setEntries({row(QStringLiteral("A"), QStringLiteral("1"))});
        QVERIFY(spy.count() >= 1);
    }

    void theBodyEditorSwitchesPagesAndRoundTripsEveryType()
    {
        HttpBodyEditorWidget editor;
        for (const HttpBodyType type : {HttpBodyType::None, HttpBodyType::Raw, HttpBodyType::Json, HttpBodyType::Xml,
                                        HttpBodyType::Form, HttpBodyType::Multipart, HttpBodyType::File, HttpBodyType::GraphQl}) {
            HttpConfig in;
            in.bodyType = type;
            in.body = QStringLiteral("text");
            in.graphqlVariables = QStringLiteral("{\"a\": 1}");
            in.formFields = {row(QStringLiteral("f"), QStringLiteral("v"))};
            in.bodyFile = QStringLiteral("/tmp/x");
            editor.setConfig(in);
            HttpConfig out;
            editor.applyTo(out);
            QCOMPARE(out.bodyType, type);
            QCOMPARE(out.body, in.body);
            QCOMPARE(out.formFields, in.formFields);
            QCOMPARE(out.bodyFile, in.bodyFile);
        }
    }

    void aJsonBodyWithVariablesIsValidAndFormatsKeepingThem()
    {
        HttpBodyEditorWidget editor;
        HttpConfig cfg;
        cfg.bodyType = HttpBodyType::Json;
        cfg.body = QStringLiteral("{\"products\": {{products}}, \"id\": \"u-{{ID}}\"}");
        editor.setConfig(cfg);
        const auto statusText = [&editor]() {
            const auto labels = editor.findChildren<QLabel *>();
            for (const QLabel *label : labels) {
                if (label->text().contains(QLatin1String("JSON")) || label->text().contains(QLatin1String("line"))) {
                    return label->text();
                }
            }
            return QString();
        };
        QCOMPARE(statusText(), kai::utils::tr(QStringLiteral("json.status.valid")));

        const auto buttons = editor.findChildren<QToolButton *>();
        QToolButton *format = nullptr;
        for (QToolButton *button : buttons) {
            if (button->toolTip() == kai::utils::tr(QStringLiteral("command.format_json"))) {
                format = button;
            }
        }
        QVERIFY(format);
        format->click();
        HttpConfig out;
        editor.applyTo(out);
        const QString formatted = out.body;
        QVERIFY(formatted.contains(QLatin1String("{{products}}")));
        QVERIFY(formatted.contains(QLatin1String("\"u-{{ID}}\"")));
        QVERIFY(formatted.contains(QLatin1Char('\n')));
    }

    void quickEditAppliesOnlyWhatItEdits()
    {
        HttpConfig cfg;
        cfg.setMethodName(QStringLiteral("POST"));
        cfg.url = QStringLiteral("https://x/a");
        cfg.queryParams = {row(QStringLiteral("p"), QStringLiteral("1"))};
        cfg.headers = {row(QStringLiteral("H"), QStringLiteral("v"))};
        cfg.bodyType = HttpBodyType::Json;
        cfg.body = QStringLiteral("{}");
        cfg.auth.type = HttpAuthType::Bearer;
        cfg.auth.token = QStringLiteral("{{T}}");
        cfg.options.timeoutSec = 7;
        cfg.responseFile = QStringLiteral("/tmp/out");
        ExecutionCondition assertion;
        assertion.left = QStringLiteral("{{RESPONSE_STATUS}}");
        cfg.assertions = {assertion};

        QuickHttpEditDialog dialog(QStringLiteral("Req"), cfg);
        const HttpConfig back = dialog.config();
        QCOMPARE(back.methodName(), QStringLiteral("POST"));
        QCOMPARE(back.url, cfg.url);
        QCOMPARE(back.queryParams, cfg.queryParams);
        QCOMPARE(back.headers, cfg.headers);
        QCOMPARE(back.bodyType, HttpBodyType::Json);
        QCOMPARE(back.body, cfg.body);
        QCOMPARE(back.auth.type, HttpAuthType::Bearer);
        QCOMPARE(back.auth.token, QStringLiteral("{{T}}"));
        // O que a edição rápida não mostra fica como estava.
        QCOMPARE(back.options.timeoutSec.value_or(0), 7);
        QCOMPARE(back.responseFile, cfg.responseFile);
        QCOMPARE(back.assertions.size(), 1);
    }
};

QTEST_MAIN(TestHttpEntriesEditor)
#include "test_http_entries_editor.moc"
