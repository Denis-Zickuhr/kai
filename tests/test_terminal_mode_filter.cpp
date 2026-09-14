#include <QTest>

#include "cli/terminal-mode-filter.h"

using namespace kai::cli;

namespace {
const QString kEsc = QString(QChar(0x1b));
QString esc(const QString &rest) { return kEsc + rest; }
}

class TestTerminalModeFilter : public QObject {
    Q_OBJECT

private slots:
    // Bug real: o ConPTY emite ESC[?9001h ESC[?1004h ao iniciar e isso
    // chegava cru no terminal de quem chamou `kai <cli_path>` via WSL.
    void stripsConPtyInputModeSequences()
    {
        TerminalModeFilter f;
        QCOMPARE(f.feed(esc("[?9001h") + esc("[?1004h") + QStringLiteral("OK\r\n")),
                 QStringLiteral("OK\r\n"));
        QCOMPARE(f.flush(), QString());
    }

    void stripsResetAndCursorVisibilityToo()
    {
        TerminalModeFilter f;
        QCOMPARE(f.feed(QStringLiteral("a") + esc("[?25l") + QStringLiteral("b") + esc("[?9001l")),
                 QStringLiteral("ab"));
    }

    void keepsColorsAndUnrelatedModes()
    {
        TerminalModeFilter f;
        const QString colored = esc("[1;32m") + QStringLiteral("verde") + esc("[0m") + esc("[?1049h") + esc("[2K");
        QCOMPARE(f.feed(colored), colored);
    }

    void rewritesCombinedParamsKeepingUnblockedOnes()
    {
        TerminalModeFilter f;
        QCOMPARE(f.feed(esc("[?9001;1049h")), esc("[?1049h"));
        QCOMPARE(f.feed(esc("[?9001;1004h")), QString());
    }

    void sequenceSplitAcrossChunksIsStillStripped()
    {
        TerminalModeFilter f;
        QString outText = f.feed(QStringLiteral("x") + kEsc);
        outText += f.feed(QStringLiteral("[?90"));
        outText += f.feed(QStringLiteral("01hy"));
        outText += f.flush();
        QCOMPARE(outText, QStringLiteral("xy"));
    }

    void incompleteTailIsReleasedOnFlush()
    {
        TerminalModeFilter f;
        QCOMPARE(f.feed(QStringLiteral("fim") + esc("[?12")), QStringLiteral("fim"));
        QCOMPARE(f.flush(), esc("[?12"));
    }

    // Bug real (print: a linha do `kai oi` sumia e o "ola" ia pro topo da
    // tela): bytes MEDIDOS de um ConPTY rodando `cmd /c echo` — ele abre com
    // limpar tela + cursor pro canto + título da janela antes da saída.
    void conPtyScreenTakeoverIsStripped()
    {
        TerminalModeFilter f;
        const QString conPtyPreamble = esc("[?9001h") + esc("[?1004h") + esc("[?25l") + esc("[?9001l")
            + esc("[?1004l") + esc("[2J") + esc("[m") + esc("[H")
            + esc("]0;C:\\WINDOWS\\SYSTEM32\\cmd.exe") + QChar(0x07) + esc("[?25h");
        QCOMPARE(f.feed(conPtyPreamble + QStringLiteral("ola\r\n")), esc("[m") + QStringLiteral("ola\r\n"));
    }

    void titleSplitAcrossChunksIsStripped()
    {
        TerminalModeFilter f;
        QString out = f.feed(QStringLiteral("a") + esc("]0;cmd"));
        out += f.feed(QStringLiteral(".exe") + QChar(0x07) + QStringLiteral("b"));
        QCOMPARE(out + f.flush(), QStringLiteral("ab"));
    }

    // Movimentos/limpezas LOCAIS (linha, cursor relativo, coluna) e OSC que
    // não é título (ex: hyperlink OSC 8) continuam passando.
    void localEditsAndOtherOscAreKept()
    {
        TerminalModeFilter f;
        const QString kept = esc("[2K") + esc("[1A") + esc("[J") + esc("[3G")
            + esc("]8;;https://kai") + QChar(0x07) + QStringLiteral("link");
        QCOMPARE(f.feed(kept), kept);
    }

    // Bug real (print: o progresso do `docker compose` aparecia no TOPO da
    // tela). Bytes MEDIDOS de um ConPTY rodando `cmd -> wsl.exe -> bash`: o
    // bash manda "sobe 2 linhas" (ESC[2A) e o ConPTY entrega "vá pra linha
    // 2" (ESC[2;1H). Tem que virar movimento RELATIVO ao cursor real.
    void conPtyAbsolutePositionsBecomeRelative()
    {
        TerminalModeFilter f;
        const QString conPty = esc("[?9001h") + esc("[?1004h") + esc("[?25l") + esc("[2J") + esc("[m")
            + esc("[H") + QStringLiteral("L1\r\nL2\r\nL3\r\n")
            + esc("]0;C:\\WINDOWS\\SYSTEM32\\cmd.exe") + QChar(0x07) + esc("[?25h") + esc("[?25l")
            + esc("[2;1H") + QStringLiteral("REDRAW") + esc("[K")
            + esc("[4;1H") + QStringLiteral("TTY\r\n");
        QCOMPARE(f.feed(conPty),
                 esc("[m") + QStringLiteral("L1\r\nL2\r\nL3\r\n")
                     + esc("[2A") + QStringLiteral("REDRAW") + esc("[K")
                     + esc("[2B") + QStringLiteral("\rTTY\r\n"));
    }

    // Descer pra uma linha que o terminal real ainda não tem: quebras de
    // linha (o terminal rola), não ESC[B (que para na borda de baixo).
    void movingBelowPrintedRowsUsesNewlines()
    {
        TerminalModeFilter f;
        QCOMPARE(f.feed(QStringLiteral("a\r\n") + esc("[5;3H") + QStringLiteral("x")),
                 QStringLiteral("a\r\n\r\n\r\n\r\n\r") + esc("[2C") + QStringLiteral("x"));
    }

    void loneEscapeIsPassedThrough()
    {
        TerminalModeFilter f;
        QCOMPARE(f.feed(kEsc + QStringLiteral("Mtexto")), kEsc + QStringLiteral("Mtexto"));
    }
};

QTEST_MAIN(TestTerminalModeFilter)
#include "test_terminal_mode_filter.moc"
