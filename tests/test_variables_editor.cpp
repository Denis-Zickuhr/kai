#include <QSignalSpy>
#include <QTest>

#include "ui/features/environments/variables-editor-widget.h"

using namespace kai::ui;

class TestVariablesEditor : public QObject {
    Q_OBJECT

private slots:
    void roundTripsValuesAndSecrets()
    {
        VariablesEditorWidget editor;
        editor.setValuesWithSecrets({{"API_URL", "https://x"}, {"TOKEN", "abc"}}, {"TOKEN"});
        QCOMPARE(editor.values().size(), 2);
        QCOMPARE(editor.values().value("TOKEN"), QStringLiteral("abc"));
        QCOMPARE(editor.secretKeys(), QSet<QString>{"TOKEN"});
    }

    void mergeReplacesExistingAndKeepsSecretMark()
    {
        VariablesEditorWidget editor;
        editor.setValuesWithSecrets({{"TOKEN", "old"}}, {"TOKEN"});
        QSignalSpy spy(&editor, &VariablesEditorWidget::changed);
        QCOMPARE(editor.mergeValues({{"TOKEN", "new"}, {"A", "1"}}), 2);
        QVERIFY(spy.count() >= 1);
        QCOMPARE(editor.values().value("TOKEN"), QStringLiteral("new"));
        QCOMPARE(editor.values().size(), 2);
        QCOMPARE(editor.secretKeys(), QSet<QString>{"TOKEN"});
    }
};

QTEST_MAIN(TestVariablesEditor)
#include "test_variables_editor.moc"
