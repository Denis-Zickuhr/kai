#include <QTest>
#include <QDialog>
#include <QWidget>
#include <QScreen>
#include <QGuiApplication>

#include "ui/shared/dialog-utils.h"

using namespace kai::ui;

// Cobre centerOnParent (feedback do usuário: "quero que TODOS os dialogs
// sejam CENTRALIZADOS em quem abriu, se não houver espaço, ajustar").
class TestDialogUtils : public QObject {
    Q_OBJECT

private slots:
    // Caso normal: diálogo pequeno, janela pai com espaço de sobra na tela —
    // deve ficar centralizado exatamente sobre a geometria do pai.
    void centersOnParentWhenThereIsRoom()
    {
        QWidget parent;
        parent.setGeometry(100, 100, 400, 300);
        parent.show();

        QDialog dialog(&parent);
        dialog.resize(200, 100);
        centerOnParent(&dialog);

        const QRect parentGeom = parent.frameGeometry();
        QCOMPARE(dialog.pos(), QPoint(parentGeom.center().x() - 100, parentGeom.center().y() - 50));
    }

    // Diálogo maior que a janela pai, mas ainda cabendo na tela — continua
    // centralizado (só clampado pela TELA, não pela janela pai).
    void staysWithinScreenWhenParentIsNearScreenEdge()
    {
        QWidget parent;
        // Pai encostado no canto: centralizar ingenuamente jogaria o
        // diálogo pra fora da tela (x negativo) se não houver ajuste.
        parent.setGeometry(0, 0, 100, 100);
        parent.show();

        QDialog dialog(&parent);
        dialog.resize(300, 300);
        centerOnParent(&dialog);

        QScreen *screen = parent.screen();
        QVERIFY(screen != nullptr);
        const QRect avail = screen->availableGeometry();
        const QRect finalGeom(dialog.pos(), dialog.size());
        QVERIFY(finalGeom.left() >= avail.left());
        QVERIFY(finalGeom.top() >= avail.top());
        QVERIFY(finalGeom.right() <= avail.right() + 1);
        QVERIFY(finalGeom.bottom() <= avail.bottom() + 1);
    }

    // Sem pai: cai no fallback de centralizar na própria tela do widget, em
    // vez de não fazer nada (comportamento documentado no header).
    void centersOnOwnScreenWhenThereIsNoParent()
    {
        QDialog dialog;
        dialog.resize(200, 150);
        centerOnParent(&dialog);

        QScreen *screen = dialog.screen();
        QVERIFY(screen != nullptr);
        const QRect avail = screen->availableGeometry();
        const QRect finalGeom(dialog.pos(), dialog.size());
        QVERIFY(finalGeom.left() >= avail.left());
        QVERIFY(finalGeom.top() >= avail.top());
    }

    // nullptr não deve crashar.
    void nullDialogIsNoOp()
    {
        centerOnParent(nullptr);
    }
};

QTEST_MAIN(TestDialogUtils)
#include "test_dialog_utils.moc"
