#include <QTest>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/shared/dialog-frame.h"
#include "utils/design-tokens.h"

using namespace kai::ui;

// Todo diálogo usa a moldura própria do Kai (barra de título, borda e cantos
// desenhados por ele), nunca a decoração do sistema.
class TestDialogFrame : public QObject {
    Q_OBJECT

    static QDialog *makeDialog(const QString &title = QStringLiteral("Editar Comando"))
    {
        auto *dialog = new QDialog;
        dialog->setWindowTitle(title);
        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(20, 12, 20, 12);
        layout->addWidget(new QLabel(QStringLiteral("conteúdo"), dialog));
        return dialog;
    }
    static QWidget *bar(QDialog &dialog)
    {
        return dialog.findChild<QWidget *>(QStringLiteral("kaiDialogTitleBar"));
    }

private slots:
    void initTestCase() { installDialogFrames(); }

    void installingTwiceIsHarmless()
    {
        installDialogFrames();
        QScopedPointer<QDialog> dialog(makeDialog());
        dialog->show();
        QCOMPARE(dialog->findChildren<QWidget *>(QStringLiteral("kaiDialogTitleBar")).size(), 1);
    }

    void aNewDialogLosesTheSystemFrameAndGetsKaisTitleBar()
    {
        QScopedPointer<QDialog> dialog(makeDialog());
        dialog->show();
        QVERIFY(hasDialogFrame(dialog.data()));
        QVERIFY(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));

        QWidget *titleBar = bar(*dialog);
        QVERIFY(titleBar);
        QVERIFY(titleBar->isVisible());
        QCOMPARE(titleBar->pos(), QPoint(1, 1));
        auto *title = titleBar->findChild<QLabel *>(QStringLiteral("kaiDialogTitle"));
        QVERIFY(title);
        QCOMPARE(title->text(), QStringLiteral("Editar Comando"));

        // O título acompanha o setWindowTitle posterior.
        dialog->setWindowTitle(QStringLiteral("Outro"));
        QCOMPARE(title->text(), QStringLiteral("Outro"));
    }

    void layoutStartsBelowTheTitleBar()
    {
        QScopedPointer<QDialog> dialog(makeDialog());
        dialog->show();
        QWidget *titleBar = bar(*dialog);
        QVERIFY(titleBar);
        QVERIFY(dialog->layout()->contentsMargins().top() >= 12 + titleBar->height());
        auto *content = dialog->findChild<QLabel *>(QString());
        QVERIFY(content);
        QVERIFY2(content->geometry().top() >= titleBar->geometry().bottom(),
                 "o conteúdo não pode ficar por baixo da barra");
    }

    void closeButtonRejectsTheDialog()
    {
        QScopedPointer<QDialog> dialog(makeDialog());
        QSignalSpy rejected(dialog.data(), &QDialog::rejected);
        dialog->show();
        auto *close = bar(*dialog)->findChild<QToolButton *>(QStringLiteral("winClose"));
        QVERIFY(close);
        close->click();
        QCOMPARE(rejected.count(), 1);
    }

    void cornersFollowTheCornerPreference()
    {
        QScopedPointer<QDialog> dialog(makeDialog());
        dialog->show();
        QVERIFY(kai::utils::tokens::radiusMd() > 0);
        // Máscara de região (cantos recortados) — o canto de cima não é do diálogo.
        QVERIFY(!dialog->mask().isEmpty());
        QVERIFY(!dialog->mask().contains(QPoint(0, 0)));
        QVERIFY(dialog->mask().contains(QPoint(dialog->width() / 2, dialog->height() / 2)));
    }

    // Nada de janela translúcida (lixo gráfico no WSLg); a separação do fundo vem
    // de um contorno desenhado por cima, que não rouba cliques.
    void dialogIsOpaqueAndHasAnEdgeOverlayThatIgnoresTheMouse()
    {
        QScopedPointer<QDialog> dialog(makeDialog());
        dialog->show();
        QVERIFY(!dialog->testAttribute(Qt::WA_TranslucentBackground));
        auto *edge = dialog->findChild<QWidget *>(QStringLiteral("kaiDialogEdge"));
        QVERIFY(edge);
        QCOMPARE(edge->geometry(), dialog->rect());
        QVERIFY(edge->testAttribute(Qt::WA_TransparentForMouseEvents));

        // O contorno escurece as bordas por dentro (sombra interna) e deixa o miolo intacto.
        const QImage image = dialog->grab().toImage();
        const QColor center = image.pixelColor(image.width() / 2, image.height() / 2);
        const QColor nearEdge = image.pixelColor(image.width() / 2, 3);
        QVERIFY2(nearEdge != center, "a faixa da borda precisa se distinguir do miolo");
        QCOMPARE(dialog->childAt(dialog->width() / 2, dialog->height() / 2) == edge, false);
    }

    void showingAgainDoesNotStackAnotherBar()
    {
        QScopedPointer<QDialog> dialog(makeDialog());
        dialog->show();
        const int top = dialog->layout()->contentsMargins().top();
        dialog->hide();
        dialog->show();
        QCOMPARE(dialog->layout()->contentsMargins().top(), top);
        QCOMPARE(dialog->findChildren<QWidget *>(QStringLiteral("kaiDialogTitleBar")).size(), 1);
    }

    void explicitSizeKeepsTheContentAreaAndFixedSizeGrows()
    {
        QScopedPointer<QDialog> sized(makeDialog());
        sized->resize(500, 300);
        sized->show();
        QCOMPARE(sized->width(), 500);
        QVERIFY2(sized->height() > 300, "o tamanho pedido valia só para o conteúdo");

        QScopedPointer<QDialog> fixed(makeDialog());
        fixed->setFixedSize(400, 200);
        fixed->show();
        QCOMPARE(fixed->width(), 400);
        QVERIFY(fixed->height() > 200);
        QCOMPARE(fixed->minimumSize(), fixed->maximumSize());
    }

    void messageBoxesAreFramedToo()
    {
        QScopedPointer<QMessageBox> box(new QMessageBox(QMessageBox::Warning, QStringLiteral("Aviso"),
                                                         QStringLiteral("Tem certeza?"), QMessageBox::Ok | QMessageBox::Cancel));
        box->show();
        QVERIFY(hasDialogFrame(box.data()));
        QVERIFY(bar(*box));
        auto *content = box->findChild<QLabel *>(QStringLiteral("qt_msgbox_label"));
        QVERIFY(content);
        QVERIFY(content->mapTo(box.data(), QPoint(0, 0)).y() >= bar(*box)->height());
        // O botão fechar de um QMessageBox sem escape button ainda fecha a caixa.
        QSignalSpy finished(box.data(), &QDialog::finished);
        bar(*box)->findChild<QToolButton *>(QStringLiteral("winClose"))->click();
        QCOMPARE(finished.count(), 1);
    }

    // O Qt 6 cria a janela nativa antes do polish (e quem chama winId() a cria
    // ainda antes): a moldura vale mesmo assim.
    void dialogsWithAnAlreadyCreatedNativeWindowAreFramedToo()
    {
        QScopedPointer<QDialog> dialog(new QDialog);
        new QVBoxLayout(dialog.data());
        dialog->winId();
        dialog->show();
        QVERIFY(hasDialogFrame(dialog.data()));
        QVERIFY(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(bar(*dialog));
    }

    // Um diálogo que já se desenha sem moldura (frameless por conta própria) não
    // ganha uma segunda barra; e um já visível nunca é recriado aberto.
    void alreadyFramelessOrVisibleDialogsAreLeftAlone()
    {
        QScopedPointer<QDialog> custom(makeDialog());
        custom->setWindowFlag(Qt::FramelessWindowHint, true);
        custom->show();
        QVERIFY(!hasDialogFrame(custom.data()));
        QVERIFY(!bar(*custom));

        QScopedPointer<QDialog> visible(makeDialog());
        visible->setProperty("kaiFramed", true); // simula "já tratado" para o filtro do show
        visible->show();
        visible->setProperty("kaiFramed", false);
        applyDialogFrame(visible.data());
        QVERIFY(!visible->property("kaiFramed").toBool());
    }

    void nonDialogWindowsAreLeftAlone()
    {
        QWidget plain;
        new QVBoxLayout(&plain);
        plain.show();
        QVERIFY(!plain.windowFlags().testFlag(Qt::FramelessWindowHint));
    }
};

QTEST_MAIN(TestDialogFrame)
#include "test_dialog_frame.moc"
