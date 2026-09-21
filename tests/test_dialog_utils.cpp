#include <QTest>
#include <QDialog>
#include <QWidget>
#include <QScreen>
#include <QGuiApplication>
#include <QComboBox>
#include <QCompleter>
#include <QAbstractItemView>

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

    // Bug relatado: "o select de coleções deve ter o mesmo estilo do
    // select normal, ainda não tem" — o popup do QCompleter de um combo
    // pesquisável (Pasta em Coleções/Pastas/Comandos, Coleção num
    // parâmetro, etc.) tinha seu próprio QSS mantido À PARTE do resto do
    // app, e tinha ficado pra trás: faltava o estado ":hover" que a regra
    // "QComboBox QAbstractItemView" de buildModernStylesheet() (o select
    // comum) já tinha, deixando os dois com aparência diferente.
    void makeSearchableComboPopupHasHoverStateLikeRegularComboPopup()
    {
        QComboBox combo;
        combo.addItem(QStringLiteral("a"));
        combo.addItem(QStringLiteral("b"));
        makeSearchableCombo(&combo);

        QCompleter *completer = combo.completer();
        QVERIFY(completer != nullptr);
        QAbstractItemView *popup = completer->popup();
        QVERIFY(popup != nullptr);
        QVERIFY2(popup->styleSheet().contains(QStringLiteral("item:hover")),
                 "popup do combo pesquisável não tem regra de hover, igual ao select comum");
    }
};

QTEST_MAIN(TestDialogUtils)
#include "test_dialog_utils.moc"
