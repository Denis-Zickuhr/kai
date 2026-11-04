#include <QTest>
#include <QWidget>
#include <QSignalSpy>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QScrollBar>
#include <QGuiApplication>
#include <QClipboard>

#include "ui/features/output/pty-terminal-widget.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

// Terminal interativo (Command::interactiveTerminal): o que o diferencia de um parser de cores (AnsiTextParser) é
// SOBRESCREVER via posicionamento de cursor, e é isso que os testes verificam.
class TestPtyTerminalWidget : public QObject {
    Q_OBJECT

private slots:
    // Ctrl+K é do readline (apagar até o fim da linha): o atalho da paleta não pode sequestrá-lo com o terminal em foco.
    void ctrlKStaysWithTheTerminalInsteadOfTheWindowShortcut()
    {
        PtyTerminalWidget widget;
        QKeyEvent ctrlK(QEvent::ShortcutOverride, Qt::Key_K, Qt::ControlModifier);
        ctrlK.ignore();
        QCoreApplication::sendEvent(&widget, &ctrlK);
        QVERIFY(ctrlK.isAccepted());

        QKeyEvent ctrlN(QEvent::ShortcutOverride, Qt::Key_N, Qt::ControlModifier);
        ctrlN.ignore();
        QCoreApplication::sendEvent(&widget, &ctrlN);
        QVERIFY(!ctrlN.isAccepted());
    }

    void interactiveTerminalFlagSurvivesJsonRoundTrip()
    {
        Command c;
        c.id = QStringLiteral("c1");
        c.type = CommandType::Command;
        c.interactiveTerminal = true;
        const Command restored = Command::fromJson(c.toJson());
        QVERIFY(restored.interactiveTerminal);

        Command c2;
        c2.interactiveTerminal = false;
        QVERIFY(!Command::fromJson(c2.toJson()).interactiveTerminal);
    }

    void feedsPlainTextIntoScreen()
    {
        PtyTerminalWidget widget;
        widget.resize(400, 300);
        widget.feed(QStringLiteral("Hello, Kai!"));
        QVERIFY(widget.plainScreenText().contains(QStringLiteral("Hello, Kai!")));
    }

    // Regressão: o widget não tratava mouse (ctrl+clique em links). urlAt() é a base da detecção, usada pelo cursor
    // de mãozinha (hover) e pelo clique (mouseReleaseEvent); mais fácil de testar que eventos de mouse.
    void urlAtDetectsUrlUnderColumnButNotOutsideIt()
    {
        PtyTerminalWidget widget;
        widget.resize(800, 300);
        widget.feed(QStringLiteral("veja: http://example.com/x fim"));

        const int cw = widget.cellWidthPx();
        const int ch = widget.cellHeightPx();
        QVERIFY(cw > 0 && ch > 0);
        const int y = ch / 2; // meio da primeira linha

        QVERIFY(widget.urlAt(QPoint(cw / 2, y)).isEmpty());

        const QString hit = widget.urlAt(QPoint(cw * 10 + cw / 2, y));
        QCOMPARE(hit, QStringLiteral("http://example.com/x"));

        const int afterCol = 6 + QStringLiteral("http://example.com/x").length() + 1;
        QVERIFY(widget.urlAt(QPoint(cw * afterCol + cw / 2, y)).isEmpty());
    }

    // Apps que desenham via cursor (vim/htop/less) escrevem, voltam o cursor e sobrescrevem; um log de texto
    // concatenaria tudo numa linha ilegível.
    void cursorPositioningOverwritesInPlaceInsteadOfAppending()
    {
        PtyTerminalWidget widget;
        widget.resize(400, 300);
        widget.feed(QStringLiteral("AAAA"));
        widget.feed(QStringLiteral("\x1b[1;1H"));
        widget.feed(QStringLiteral("BB"));

        QVERIFY2(widget.plainScreenText().startsWith(QStringLiteral("BBAA")),
                  qPrintable(widget.plainScreenText().left(20)));
    }

    // Reconectar a um comando reseta e realimenta o vterm com o log bruto acumulado: tem de reconstruir a mesma tela,
    // sem acumular lixo de reconexões anteriores.
    void resetAndReplayReconstructsScreenDeterministically()
    {
        PtyTerminalWidget widget;
        widget.resize(400, 300);
        const QString raw = QStringLiteral("AAAA\x1b[1;1HBB");

        widget.resetAndReplay(raw);
        const QString first = widget.plainScreenText();
        QVERIFY(first.startsWith(QStringLiteral("BBAA")));

        widget.resetAndReplay(raw);
        QCOMPARE(widget.plainScreenText(), first);
    }

    void acceptingInputStartsFalseAndReflectsSetter()
    {
        PtyTerminalWidget widget;
        QVERIFY(!widget.acceptingInput());
        widget.setAcceptingInput(true);
        QVERIFY(widget.acceptingInput());
        widget.setAcceptingInput(false);
        QVERIFY(!widget.acceptingInput());
    }

    // Regressão: Ctrl+C não enviava SIGINT. No X11/Linux QKeyEvent::text() para Ctrl+C já vem como "\x03"; o código
    // só tratava Ctrl+<letra> com text() vazio, então "\x03" ia ao ramo imprimível junto com VTERM_MOD_CTRL e a
    // libvterm aplicava a máscara de controle (c & 0x1f) de novo. O QKeyEvent é construído com esse texto explícito
    // porque QTest::keyClick sob "offscreen" pode não replicar a peculiaridade do X11.
    void ctrlCSendsSigintByteEvenWhenPlatformPrefillsControlCharInText()
    {
        PtyTerminalWidget widget;
        widget.resize(400, 300);
        widget.setAcceptingInput(true);

        QSignalSpy spy(&widget, &PtyTerminalWidget::rawInputBytes);
        QKeyEvent event(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QStringLiteral("\x03"));
        QCoreApplication::sendEvent(&widget, &event);

        QVERIFY2(!spy.isEmpty(), "nenhum byte foi enviado pro PTY em resposta ao Ctrl+C");
        QCOMPARE(spy.constFirst().constFirst().toByteArray(), QByteArray(1, char(0x03)));
    }

    void resizingWidgetChangesCellDimensions()
    {
        // show() é necessário: um top-level não mapeado (plataforma "offscreen") não entrega resizeEvent síncrono só com
        // resize(); mostrar antes garante o recálculo de linhas/colunas.
        PtyTerminalWidget widget;
        widget.show();
        widget.resize(200, 150);
        const int smallRows = widget.rows();
        const int smallCols = widget.cols();

        widget.resize(1000, 700);
        QVERIFY(widget.rows() > smallRows);
        QVERIFY(widget.cols() > smallCols);
    }

    // Linhas que saem por cima da tela principal (não a alternate screen) viram scrollback real, refletido na
    // QScrollBar vertical herdada de QAbstractScrollArea.
    void scrollbackCapturesLinesPushedOffThePrimaryScreen()
    {
        PtyTerminalWidget widget;
        widget.show();
        widget.resize(300, 100);

        for (int i = 0; i < widget.rows() * 5; ++i) {
            widget.feed(QStringLiteral("line %1\r\n").arg(i));
        }

        QVERIFY2(widget.scrollbackLineCount() > 0, "nenhuma linha foi parar no scrollback");
        QCOMPARE(widget.verticalScrollBar()->maximum(), widget.scrollbackLineCount());
        // "Grudado no fundo" por padrão: sem o usuário rolar, a barra fica no máximo (tela viva).
        QCOMPARE(widget.verticalScrollBar()->value(), widget.verticalScrollBar()->maximum());
    }

    void scrollingUpStopsFollowingNewOutputUntilScrolledBackDown()
    {
        PtyTerminalWidget widget;
        widget.show();
        widget.resize(300, 100);
        for (int i = 0; i < widget.rows() * 5; ++i) {
            widget.feed(QStringLiteral("line %1\r\n").arg(i));
        }

        widget.verticalScrollBar()->setValue(0);
        const int maxBefore = widget.verticalScrollBar()->maximum();

        widget.feed(QStringLiteral("mais uma linha depois de rolar\r\n"));

        QVERIFY2(widget.verticalScrollBar()->maximum() > maxBefore,
                  "scrollback não cresceu com a saída nova enquanto rolado");
        // Não pode ter puxado a visão pro fundo: o usuário ainda olha o mesmo ponto do histórico.
        QCOMPARE(widget.verticalScrollBar()->value(), 0);
    }

    // Reconectar a OUTRO comando (widget compartilhado) não pode herdar o scrollback do anterior.
    void resetScreenClearsScrollback()
    {
        PtyTerminalWidget widget;
        widget.show();
        widget.resize(300, 100);
        for (int i = 0; i < widget.rows() * 5; ++i) {
            widget.feed(QStringLiteral("line %1\r\n").arg(i));
        }
        QVERIFY(widget.scrollbackLineCount() > 0);

        widget.resetScreen();

        QCOMPARE(widget.scrollbackLineCount(), 0);
        QCOMPARE(widget.verticalScrollBar()->maximum(), 0);
    }

    // Clique+arraste (sem Ctrl, reservado ao link) marca uma seleção e soltar o botão copia para a área de
    // transferência.
    void dragSelectingTextCopiesItToClipboard()
    {
        PtyTerminalWidget widget;
        widget.resize(800, 300);
        widget.feed(QStringLiteral("hello world"));

        const int cw = widget.cellWidthPx();
        const int ch = widget.cellHeightPx();
        const int y = ch / 2;

        QVERIFY(!widget.hasSelection());

        QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(cw / 2, y));
        QTest::mouseMove(&widget, QPoint(cw * 4 + cw / 2, y));
        // mouseMoveEvent só reage a botão pressionado via event->buttons(); QTest::mouseMove não o seta: dispara na mão.
        {
            QMouseEvent move(QEvent::MouseMove, QPointF(cw * 4 + cw / 2, y), QPointF(cw * 4 + cw / 2, y),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&widget, &move);
        }
        QVERIFY(widget.hasSelection());
        QCOMPARE(widget.selectedText(), QStringLiteral("hello"));

        QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(cw * 4 + cw / 2, y));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello"));
    }

    // Um clique simples (sem arrastar) limpa qualquer seleção anterior.
    void plainClickClearsExistingSelection()
    {
        PtyTerminalWidget widget;
        widget.resize(800, 300);
        widget.feed(QStringLiteral("hello world"));
        const int cw = widget.cellWidthPx();
        const int ch = widget.cellHeightPx();
        const int y = ch / 2;

        QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(cw / 2, y));
        {
            QMouseEvent move(QEvent::MouseMove, QPointF(cw * 4 + cw / 2, y), QPointF(cw * 4 + cw / 2, y),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&widget, &move);
        }
        QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(cw * 4 + cw / 2, y));
        QVERIFY(widget.hasSelection());

        QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(cw * 8 + cw / 2, y));
        QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(cw * 8 + cw / 2, y));
        QVERIFY(!widget.hasSelection());
    }

    // Regressão: colar texto com CRLF (clipboard do Windows/Notepad/navegador) mandava um "\r" cru junto do "\n", e o
    // "\r" (volta ao início da linha) embaralhava o texto colado. rawInputBytes() é o que sai para o PTY: cada
    // quebra de linha colada precisa virar um único "\r".
    void pastingClipboardTextNormalizesCrlfLineEndings()
    {
        PtyTerminalWidget widget;
        widget.resize(400, 300);
        widget.setAcceptingInput(true);
        QGuiApplication::clipboard()->setText(QStringLiteral("linha1\r\nlinha2"));

        QSignalSpy spy(&widget, &PtyTerminalWidget::rawInputBytes);
        QTest::keyClick(&widget, Qt::Key_V, Qt::ControlModifier);

        QByteArray sent;
        for (const QList<QVariant> &args : spy) {
            sent += args.at(0).toByteArray();
        }
        QVERIFY2(!sent.contains("\r\n"), "CRLF cru vazou pro PTY (deveria virar um só '\\r')");
        QVERIFY2(!sent.contains('\n'), "'\\n' cru vazou pro PTY (deveria ter virado '\\r')");
        QVERIFY(sent.contains('\r'));
    }
};

QTEST_MAIN(TestPtyTerminalWidget)
#include "test_pty_terminal_widget.moc"
