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
        KeyTraceFilter filter(true);
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
        QVERIFY(keyLine.contains(QStringLiteral("sinceActivate=")));
        QVERIFY(keyLine.contains(QStringLiteral("modal=")));
    }

    // Sempre ligado no app: sem `traceKeys` as teclas (conteúdo digitado) não são registradas, só a ativação de janelas.
    void keysAreNotTracedUnlessEnabled()
    {
        KeyTraceFilter filter(false);
        qApp->installEventFilter(&filter);
        QSignalSpy traced(&filter, &KeyTraceFilter::traced);
        QLineEdit edit;
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        edit.setFocus();
        QTest::keyClick(&edit, Qt::Key_A);
        qApp->removeEventFilter(&filter);
        for (const QList<QVariant> &args : traced) {
            QVERIFY(!args.first().toString().startsWith(QStringLiteral("KeyPress")));
        }
    }

    // Sempre ligado: a troca de foco e as janelas que aparecem ficam no registro (sem texto digitado).
    void focusChangesAndShownWindowsAreTracedWithoutKeyContent()
    {
        KeyTraceFilter filter(false);
        qApp->installEventFilter(&filter);
        QSignalSpy traced(&filter, &KeyTraceFilter::traced);
        QWidget window;
        window.setWindowTitle(QStringLiteral("probe window"));
        auto *first = new QLineEdit(&window);
        first->setObjectName(QStringLiteral("first"));
        auto *second = new QLineEdit(&window);
        second->setObjectName(QStringLiteral("second"));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        first->setFocus();
        second->setFocus();
        QTest::keyClick(second, Qt::Key_Q); // texto comum: nunca entra no registro
        qApp->removeEventFilter(&filter);

        QStringList lines;
        for (const QList<QVariant> &args : traced) {
            lines << args.first().toString();
        }
        const QString all = lines.join(QLatin1Char('\n'));
        QVERIFY2(all.contains(QStringLiteral("WindowShow 'probe window'")), qPrintable(all));
        QVERIFY2(all.contains(QStringLiteral("FocusChange QLineEdit#first -> QLineEdit#second")), qPrintable(all));
        QVERIFY(!all.contains(QStringLiteral("KeyPress")));
        QVERIFY(!all.contains(QStringLiteral("'q'")));
    }
};

QTEST_MAIN(TestKeyTraceFilter)
#include "test_key_trace_filter.moc"
