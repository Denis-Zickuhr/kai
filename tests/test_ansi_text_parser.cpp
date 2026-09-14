#include <QTest>

#include "ui/features/output/ansi-text-parser.h"

using namespace kai::ui;

// Testa a lógica pura de parsing ANSI, sem depender de display.
class TestAnsiTextParser : public QObject {
    Q_OBJECT

private slots:
    void plainTextWithoutEscapeCodesIsSingleSegment()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("hello world"));

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).text, QStringLiteral("hello world"));
    }

    // Bug reportado (bug-enconding.png): sequências de escape ANSI de
    // controle de cursor/tela — que surgem sob PTY (ex: read -p do bash) —
    // vazavam como lixo visual ("[?25l", "[2K", "[1A", "[J"). Devem ser
    // removidas, preservando o texto legível e as cores.
    void cursorControlSequencesAreStrippedFromOutput()
    {
        AnsiTextParser parser;
        // Sequências reais capturadas no print: hide cursor, clear line,
        // cursor up, clear screen — intercaladas com texto e uma cor.
        const QString raw = QStringLiteral(
            "\x1b[?25l\x1b[2K\x1b[31mVocê está em PRODUÇÃO\x1b[0m [y/N] \x1b[1A\x1b[J");
        const QVector<AnsiSegment> segments = parser.parse(raw);

        QString visibleText;
        for (const AnsiSegment &seg : segments) {
            visibleText += seg.text;
        }
        // O texto legível fica intacto...
        QCOMPARE(visibleText, QStringLiteral("Você está em PRODUÇÃO [y/N] "));
        // ...e nenhum resíduo de escape/control char sobra.
        QVERIFY(!visibleText.contains(QLatin1Char('\x1b')));
        QVERIFY(!visibleText.contains(QStringLiteral("[2K")));
        QVERIFY(!visibleText.contains(QStringLiteral("[?25l")));
        QVERIFY(!visibleText.contains(QStringLiteral("[1A")));
        // A cor vermelha do trecho "PRODUÇÃO" foi preservada.
        bool hasRed = false;
        for (const AnsiSegment &seg : segments) {
            if (seg.format.foreground().color() == QColor(205, 0, 0)) {
                hasRed = true;
            }
        }
        QVERIFY(hasRed);
    }

    // Bug real reportado com print, duas vezes: em certos caminhos do
    // Windows o byte ESC chega/é exibido como o glifo "←" em vez do
    // controle invisível de sempre ("←[?25l←[K←[29;120H" em vez de
    // sequências reconhecíveis) — o parser precisa tratar as duas grafias
    // do lead-in igual, senão a sequência inteira vaza como texto literal.
    void arrowGlyphLeadInIsTreatedLikeRealEscapeByte()
    {
        AnsiTextParser parser;
        const QString raw = QStringLiteral(
            "←[?25l←[K←[31mVocê está em PRODUÇÃO←[0m [y/N] ←[29;120H");
        const QVector<AnsiSegment> segments = parser.parse(raw);

        QString visibleText;
        for (const AnsiSegment &seg : segments) {
            visibleText += seg.text;
        }
        QCOMPARE(visibleText, QStringLiteral("Você está em PRODUÇÃO [y/N] "));
        QVERIFY(!visibleText.contains(QChar(0x2190)));
        QVERIFY(!visibleText.contains(QStringLiteral("[K")));
        QVERIFY(!visibleText.contains(QStringLiteral("[?25l")));
        QVERIFY(!visibleText.contains(QStringLiteral("[29;120H")));
    }

    // A sequência "←[..." pode chegar partida na fronteira de dois chunks
    // (mesma robustez já exigida do \x1b de verdade) — não deve vazar o
    // pedaço incompleto como texto nem quebrar o chunk seguinte.
    void arrowGlyphLeadInSlicedAcrossChunksIsStillStripped()
    {
        AnsiTextParser parser;
        auto a = parser.parse(QStringLiteral("antes ←[2"));
        auto b = parser.parse(QStringLiteral("5l depois"));

        QString visible;
        for (const AnsiSegment &seg : a) visible += seg.text;
        for (const AnsiSegment &seg : b) visible += seg.text;
        QCOMPARE(visible, QStringLiteral("antes  depois"));
    }

    void redForegroundCodeAppliesColorToFollowingText()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("\x1b[31mERROR\x1b[0m normal"));

        QCOMPARE(segments.size(), 2);
        QCOMPARE(segments.at(0).text, QStringLiteral("ERROR"));
        QCOMPARE(segments.at(0).format.foreground().color(), QColor(205, 0, 0));
        QCOMPARE(segments.at(1).text, QStringLiteral(" normal"));
    }

    void combinedBoldAndColorSgrIsApplied()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("\x1b[1;31mBOLD_RED\x1b[0m"));

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).text, QStringLiteral("BOLD_RED"));
        QCOMPARE(segments.at(0).format.fontWeight(), static_cast<int>(QFont::Bold));
        QCOMPARE(segments.at(0).format.foreground().color(), QColor(205, 0, 0));
    }

    // Formato persiste entre chamadas sucessivas (parsing incremental de
    // stream de processo, onde a sequência ANSI pode vir em um chunk e o
    // texto colorido no chunk seguinte).
    void formatPersistsAcrossIncrementalChunks()
    {
        AnsiTextParser parser;
        parser.parse(QStringLiteral("\x1b[32m"));
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("green text"));

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).format.foreground().color(), QColor(0, 205, 0));
    }

    void brightColorCodesAreDistinctFromNormal()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("\x1b[91mbright red\x1b[0m"));

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).format.foreground().color(), QColor(255, 0, 0));
    }

    void resetCodeRestoresDefaultFormat()
    {
        AnsiTextParser parser;
        parser.parse(QStringLiteral("\x1b[31m"));
        parser.resetFormat();
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("plain"));

        QCOMPARE(segments.at(0).format.foreground().color(), QColor(Qt::white));
    }

    void emptyInputProducesNoSegments()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QString());
        QCOMPARE(segments.size(), 0);
    }

    // spec 05: auto-highlight de links (URLs) no Terminal Drawer.
    void urlInPlainTextIsDetectedAsLink()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("Servidor em http://localhost:8080/api rodando"));

        QCOMPARE(segments.size(), 3);
        QCOMPARE(segments.at(0).text, QStringLiteral("Servidor em "));
        QVERIFY(!segments.at(0).isLink);

        QCOMPARE(segments.at(1).text, QStringLiteral("http://localhost:8080/api"));
        QVERIFY(segments.at(1).isLink);
        QVERIFY(segments.at(1).format.fontUnderline());

        QCOMPARE(segments.at(2).text, QStringLiteral(" rodando"));
        QVERIFY(!segments.at(2).isLink);
    }

    void httpsUrlAtStartOfLineIsDetected()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("https://api.exemplo.com/v1/users"));

        QCOMPARE(segments.size(), 1);
        QVERIFY(segments.at(0).isLink);
    }

    void urlInsideColoredTextIsStillDetected()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("\x1b[32mGET http://localhost/health\x1b[0m"));

        bool foundLink = false;
        for (const AnsiSegment &segment : segments) {
            if (segment.isLink) {
                foundLink = true;
                QCOMPARE(segment.text, QStringLiteral("http://localhost/health"));
            }
        }
        QVERIFY(foundLink);
    }

    void textWithoutUrlHasNoLinkSegments()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("apenas texto normal sem links"));

        for (const AnsiSegment &segment : segments) {
            QVERIFY(!segment.isLink);
        }
    }
};

QTEST_MAIN(TestAnsiTextParser)
#include "test_ansi_text_parser.moc"
