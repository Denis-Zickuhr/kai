#include <QtTest>

#include <QLineEdit>
#include <QTemporaryDir>

#include "non-fresh-config.h"
#include "ui/main-window.h"

using namespace kai::ui;

class TestMainWindowFocus : public QObject {
    Q_OBJECT

private slots:
    // Reexibir a janela (atalho global, bandeja) não pode arrancar o foco de onde o usuário estava para a árvore.
    void showingTheWindowAgainKeepsTheFocusWhereItWas()
    {
        QTemporaryDir config;
        useNonFreshConfig(config);
        MainWindow window;
        window.resize(1100, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(60); // o foco inicial na árvore é adiado (singleShot 0)

        auto *field = window.findChild<QLineEdit *>(QStringLiteral("outputInput"));
        QVERIFY(field);
        field->setEnabled(true);
        field->setFocus();
        QVERIFY(field->hasFocus());

        window.hide();
        window.show();
        QTest::qWait(60);
        // focusWidget() da janela é o filho que recebe o foco quando ela está ativa (vale também sem ativação, como aqui).
        QVERIFY2(window.focusWidget() == field,
                 qPrintable(QStringLiteral("foco foi para %1").arg(
                     window.focusWidget() ? QString::fromLatin1(window.focusWidget()->metaObject()->className()) : QStringLiteral("<nenhum>"))));
    }
};

QTEST_MAIN(TestMainWindowFocus)
#include "test_main_window_focus.moc"
