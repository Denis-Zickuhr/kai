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

// Terminal interativo (Command::interactiveTerminal, feedback do usuário:
// "eu preciso realmente de um terminal iterativo"). O que diferencia isto de
// um simples parser de cores (AnsiTextParser) é justamente SOBRESCREVER via
// posicionamento de cursor — é o que os testes abaixo verificam de fato,
// não só "não crashou".
class TestPtyTerminalWidget : public QObject {
    Q_OBJECT

private slots:
    void interactiveTerminalFlagSurvivesJsonRoundTrip()
    {
        Command c;
        c.id = QStringLiteral("c1");
        c.type = CommandType::Shell;
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

    // Bug relatado: "terminais tty não suportam ctrl+clique em links" — o
    // widget não tinha NENHUM tratamento de mouse antes. urlAt() é a base
    // de detecção usada tanto pelo cursor de mãozinha (hover) quanto pelo
    // clique de verdade (mouseReleaseEvent) — mais fácil de testar sem
    // simular eventos de mouse reais.
    void urlAtDetectsUrlUnderColumnButNotOutsideIt()
    {
        PtyTerminalWidget widget;
        widget.resize(800, 300);
        widget.feed(QStringLiteral("veja: http://example.com/x fim"));

        const int cw = widget.cellWidthPx();
        const int ch = widget.cellHeightPx();
        QVERIFY(cw > 0 && ch > 0);
        const int y = ch / 2; // meio da primeira linha

        // Coluna 0 ("veja: ") — fora da URL.
        QVERIFY(widget.urlAt(QPoint(cw / 2, y)).isEmpty());

        // Alguma coluna DENTRO de "http://example.com/x" (começa na coluna 6).
        const QString hit = widget.urlAt(QPoint(cw * 10 + cw / 2, y));
        QCOMPARE(hit, QStringLiteral("http://example.com/x"));

        // Depois da URL ("fim") — fora de novo.
        const int afterCol = 6 + QStringLiteral("http://example.com/x").length() + 1;
        QVERIFY(widget.urlAt(QPoint(cw * afterCol + cw / 2, y)).isEmpty());
    }

    // O bug real que o terminal interativo resolve: um app que desenha via
    // cursor (vim/htop/less/Claude Code aninhado) escreve, volta o cursor e
    // SOBRESCREVE — um log de texto simples só concatenaria tudo numa
    // linha só, ilegível.
    void cursorPositioningOverwritesInPlaceInsteadOfAppending()
    {
        PtyTerminalWidget widget;
        widget.resize(400, 300);
        widget.feed(QStringLiteral("AAAA"));
        widget.feed(QStringLiteral("\x1b[1;1H")); // CUP: volta pra linha 1, coluna 1
        widget.feed(QStringLiteral("BB"));

        QVERIFY2(widget.plainScreenText().startsWith(QStringLiteral("BBAA")),
                  qPrintable(widget.plainScreenText().left(20)));
    }

    // Reconectar a um comando (troca de seleção na árvore) reseta e
    // realimenta o vterm com o log bruto já acumulado — precisa reconstruir
    // a MESMA tela de forma determinística, sem duplicar/acumular lixo de
    // reconexões anteriores.
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

    // Bug relatado: "ctrl c ainda não funciona pra sigkill" (SIGINT, na
    // real). Causa: no X11/Linux, QKeyEvent::text() para Ctrl+C JÁ vem
    // pronto como o próprio byte de controle ("\x03") — não vazio. O
    // código antigo só tratava Ctrl+<letra> manualmente quando text()
    // estava VAZIO; com texto não-vazio, mandava "\x03" pro branch de
    // texto imprimível JUNTO com VTERM_MOD_CTRL, e a libvterm tentava
    // aplicar a máscara de controle (c & 0x1f) de novo sobre um byte que
    // JÁ é controle — resultado indefinido. Reproduz aqui construindo o
    // QKeyEvent com esse texto explícito (em vez de confiar no que
    // QTest::keyClick geraria sob a plataforma "offscreen" dos testes,
    // que pode não replicar a mesma peculiaridade do X11 real).
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
        // show() é necessário aqui: um top-level ainda não mapeado (sob a
        // plataforma "offscreen" dos testes) não entrega resizeEvent de
        // forma síncrona só com resize() — mostrar primeiro garante que o
        // resize seguinte realmente dispare o recálculo de linhas/colunas.
        PtyTerminalWidget widget;
        widget.show();
        widget.resize(200, 150);
        const int smallRows = widget.rows();
        const int smallCols = widget.cols();

        widget.resize(1000, 700);
        QVERIFY(widget.rows() > smallRows);
        QVERIFY(widget.cols() > smallCols);
    }

    // Feedback do usuário: "faltou uma barra de scroll". Linhas que saem
    // por cima da tela principal (não a alternate screen — vim/htop/less
    // não precisam disso) devem virar scrollback de verdade, refletido na
    // QScrollBar vertical herdada de QAbstractScrollArea.
    void scrollbackCapturesLinesPushedOffThePrimaryScreen()
    {
        PtyTerminalWidget widget;
        widget.show();
        widget.resize(300, 100); // poucas linhas cabem — força rolagem cedo

        for (int i = 0; i < widget.rows() * 5; ++i) {
            widget.feed(QStringLiteral("line %1\r\n").arg(i));
        }

        QVERIFY2(widget.scrollbackLineCount() > 0, "nenhuma linha foi parar no scrollback");
        QCOMPARE(widget.verticalScrollBar()->maximum(), widget.scrollbackLineCount());
        // "Grudado no fundo" por padrão (comportamento de terminal real):
        // sem o usuário rolar, o valor da barra fica no máximo (mostrando
        // a tela viva), nunca preso lá atrás no começo do histórico.
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

        widget.verticalScrollBar()->setValue(0); // usuário rola pro topo do histórico
        const int maxBefore = widget.verticalScrollBar()->maximum();

        widget.feed(QStringLiteral("mais uma linha depois de rolar\r\n"));

        QVERIFY2(widget.verticalScrollBar()->maximum() > maxBefore,
                  "scrollback não cresceu com a saída nova enquanto rolado");
        // Não deve ter "puxado" a visão pro fundo sozinho — o usuário
        // ainda está olhando o mesmo ponto da história de antes.
        QCOMPARE(widget.verticalScrollBar()->value(), 0);
    }

    // Bug óbvio de se esquecer: reconectar a OUTRO comando (mesmo widget
    // compartilhado — ver cabeçalho da classe) não pode herdar o
    // scrollback de quem estava conectado antes.
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

    // REGRESSÃO/feature (pedido do usuário: "terminal interativo não
    // permite selecionar texto") — clique+arraste (sem Ctrl, reservado pro
    // link) marca uma seleção e solta o botão já copia pra área de
    // transferência (convenção de terminais reais).
    void dragSelectingTextCopiesItToClipboard()
    {
        PtyTerminalWidget widget;
        widget.resize(800, 300);
        widget.feed(QStringLiteral("hello world"));

        const int cw = widget.cellWidthPx();
        const int ch = widget.cellHeightPx();
        const int y = ch / 2;

        QVERIFY(!widget.hasSelection());

        // Arrasta da coluna 0 até a coluna 4 (dentro de "hello").
        QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(cw / 2, y));
        QTest::mouseMove(&widget, QPoint(cw * 4 + cw / 2, y));
        // mouseMoveEvent só reage a botão pressionado via event->buttons();
        // QTest::mouseMove não seta isso sozinho — dispara o evento na mão.
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

    // Um clique simples (sem arrastar) limpa qualquer seleção anterior —
    // mesmo comportamento de qualquer editor/terminal real.
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
};

QTEST_MAIN(TestPtyTerminalWidget)
#include "test_pty_terminal_widget.moc"
