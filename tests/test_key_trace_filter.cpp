#include <QTest>

#include <QLineEdit>
#include <QSignalSpy>

#include "ui/shared/key-trace-filter.h"

using namespace kai::ui;

class TestKeyTraceFilter : public QObject {
    Q_OBJECT

private slots:
    // Cada tecla recebida vira uma linha com o widget de destino e o de foco.
    void aKeyPressIsTracedWithTheFocusedWidget()
    {
        KeyTraceFilter filter;
        qApp->installEventFilter(&filter);
        QSignalSpy traced(&filter, &KeyTraceFilter::traced);
        QLineEdit edit;
        edit.setObjectName(QStringLiteral("probe"));
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        edit.setFocus();
        QTest::keyClick(&edit, Qt::Key_A);
        qApp->removeEventFilter(&filter);

        QString keyLine;
        for (const QList<QVariant> &args : traced) {
            if (args.first().toString().startsWith(QStringLiteral("KeyPress"))) {
                keyLine = args.first().toString();
            }
        }
        QVERIFY2(!keyLine.isEmpty(), "nenhuma linha de KeyPress registrada");
        QVERIFY(keyLine.contains(QStringLiteral("QLineEdit#probe")));
        QVERIFY(keyLine.contains(QStringLiteral("text='a'")));
    }
};

QTEST_MAIN(TestKeyTraceFilter)
#include "test_key_trace_filter.moc"
