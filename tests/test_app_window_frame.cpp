#include <QTest>

#include <QApplication>
#include <QLabel>
#include <QToolButton>

#include "ui/features/output/terminal-drawer.h"
#include "ui/shared/app-window-frame.h"
#include "utils/design-tokens.h"

using namespace kai::ui;

// A moldura das janelas próprias do app (a Saída destacada): visual da janela principal.
class TestAppWindowFrame : public QObject {
    Q_OBJECT

private slots:
    void isFramelessWithTheAppTitleBarAndControls()
    {
        AppWindowFrame frame;
        frame.setUseSystemFrame(false); // o padrão no Windows é a moldura do sistema
        frame.setWindowTitle(QStringLiteral("Kai — Saída"));
        frame.resize(700, 500);
        frame.show();
        QVERIFY(frame.windowFlags() & Qt::FramelessWindowHint);
        QCOMPARE(frame.objectName(), QStringLiteral("rootContainer")); // o QSS do tema pinta o fundo
        QVERIFY(frame.minimizeButton() && frame.maximizeButton() && frame.closeButton());
        QVERIFY(frame.minimizeButton()->isVisible() && frame.maximizeButton()->isVisible()
                && frame.closeButton()->isVisible());
        QVERIFY(frame.contentWidget());
        QVERIFY(frame.titleBar()->isVisible());
    }

    void titleBarFollowsTheWindowTitle()
    {
        AppWindowFrame frame;
        frame.setWindowTitle(QStringLiteral("Kai — Saída — parado"));
        QCOMPARE(frame.titleText(), QStringLiteral("Kai — Saída — parado"));
        frame.setWindowTitle(QStringLiteral("Kai — Saída — rodando"));
        QCOMPARE(frame.titleText(), QStringLiteral("Kai — Saída — rodando"));
    }

    void maximizeButtonTogglesAndMaximizedWindowsLoseTheirRoundedCorners()
    {
        AppWindowFrame frame;
        frame.setUseSystemFrame(false);
        frame.resize(700, 500);
        frame.show();
        QVERIFY(QTest::qWaitForWindowExposed(&frame));
        if (kai::utils::tokens::radiusMd() > 0) {
            QVERIFY(!frame.mask().isEmpty() || frame.property("kaiMaximized").toBool()); // recortada nos cantos
        }
        frame.maximizeButton()->click();
        QTRY_VERIFY(frame.isMaximized());
        QTRY_VERIFY(frame.property("kaiMaximized").toBool()); // reta: ocupa a tela
        QVERIFY(frame.mask().isEmpty());
        frame.maximizeButton()->click();
        QTRY_VERIFY(!frame.isMaximized());
    }

    void closeButtonClosesTheWindow()
    {
        auto *frame = new AppWindowFrame;
        frame->setUseSystemFrame(false);
        frame->setAttribute(Qt::WA_DeleteOnClose);
        frame->show();
        QPointer<AppWindowFrame> guard(frame);
        frame->closeButton()->click();
        QTRY_VERIFY(guard.isNull());
    }

    void detachedOutputUsesTheAppWindowFrame()
    {
        TerminalDrawer drawer;
        drawer.resize(800, 400);
        drawer.show();
        drawer.showDetachedOutput();
        QVERIFY(drawer.hasDetachedWindow());
        AppWindowFrame *frame = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (auto *f = qobject_cast<AppWindowFrame *>(w)) frame = f;
        }
        QVERIFY2(frame, "a Saída destacada tem de usar a moldura do app");
        QCOMPARE(bool(frame->windowFlags() & Qt::FramelessWindowHint), !AppWindowFrame::systemFrameByDefault());
        QVERIFY(!frame->titleText().isEmpty());
        frame->close();
        QTRY_VERIFY(!drawer.hasDetachedWindow());
    }

    // Moldura do sistema (o padrão no Windows): a barra de título é a nativa, então a própria some e o sistema cuida de
    // bordas, redimensionamento e encaixe nos quadrantes da tela. Só a barra e as bordas mudam: o conteúdo é o mesmo.
    void systemFrameDropsTheAppTitleBarAndTheFramelessHint()
    {
        AppWindowFrame frame;
        frame.setUseSystemFrame(true);
        frame.setWindowTitle(QStringLiteral("Kai — Saída"));
        frame.resize(700, 500);
        frame.show();
        QVERIFY(QTest::qWaitForWindowExposed(&frame));
        QVERIFY(frame.usesSystemFrame());
        QVERIFY(!(frame.windowFlags() & Qt::FramelessWindowHint));
        QVERIFY(frame.windowFlags() & Qt::WindowTitleHint);
        QVERIFY(frame.windowFlags() & Qt::WindowMaximizeButtonHint);
        QVERIFY(!frame.titleBar()->isVisible());
        QVERIFY(frame.contentWidget()->isVisible());
        // O contêiner pinta reto e sem a máscara de cantos: quem arredonda (ou não) é o sistema.
        QVERIFY(frame.property("kaiMaximized").toBool());
        QVERIFY(frame.mask().isEmpty());
        QCOMPARE(frame.windowTitle(), QStringLiteral("Kai — Saída"));

        // E volta à moldura própria sem perder a janela.
        frame.setUseSystemFrame(false);
        QVERIFY(frame.windowFlags() & Qt::FramelessWindowHint);
        QTRY_VERIFY(frame.isVisible());
        QVERIFY(frame.titleBar()->isVisible());
    }

    void switchingTheFrameKeepsTheAlwaysOnTopHint()
    {
        AppWindowFrame frame;
        frame.setWindowFlag(Qt::WindowStaysOnTopHint, true);
        frame.setUseSystemFrame(!AppWindowFrame::systemFrameByDefault());
        QVERIFY(frame.windowFlags() & Qt::WindowStaysOnTopHint);
    }
};

QTEST_MAIN(TestAppWindowFrame)
#include "test_app_window_frame.moc"
