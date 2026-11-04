#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>

#include "ui/features/environments/variables-editor-widget.h"
#include "ui/shared/token-style.h"
#include "utils/design-tokens.h"

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

    // Campo de altura fixa espremido pelo layout: o raio acompanha a altura REAL (senão o Qt desenha canto reto).
    void fieldRadiusFollowsTheRealHeight()
    {
        auto effects = kai::utils::tokens::effects();
        const auto saved = effects;
        effects.cornerStyle = 2;
        kai::utils::tokens::setEffects(effects);
        QLineEdit field;
        int last = -1;
        bindFieldStyle(&field, [&last](int radius) {
            last = radius;
            return QStringLiteral("QLineEdit { border-radius: %1px; }").arg(radius);
        });
        field.setFixedHeight(23);
        field.show();
        QTRY_COMPARE(last, kai::utils::tokens::radiusMdForHeight(23));
        QVERIFY(last < kai::utils::tokens::radiusMd());
        field.setFixedHeight(60);
        QTRY_COMPARE(last, kai::utils::tokens::radiusMdForHeight(60));
        kai::utils::tokens::setEffects(saved);
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
