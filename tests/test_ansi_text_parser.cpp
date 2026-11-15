#include <QTest>

#include "ui/features/output/ansi-text-parser.h"
#include "utils/design-tokens.h"

using namespace kai::ui;

namespace {
// O parser ajusta a cor ANSI por contraste com o fundo da Saída do tema ativo (3.5:1), então o esperado sai da mesma regra.
QColor readable(int code, bool bright = false)
{
    return kai::utils::tokens::ensureContrast(AnsiTextParser::ansiColorFor(code, bright),
                                              QColor(kai::utils::tokens::terminalBg()), 3.5);
}
} // namespace

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

    // Controles de cursor/tela sob PTY (ex: read -p do bash) não podem vazar como texto; texto legível e cores ficam.
    void cursorControlSequencesAreStrippedFromOutput()
    {
        AnsiTextParser parser;
        const QString raw = QStringLiteral(
            "\x1b[?25l\x1b[2K\x1b[31mVocê está em PRODUÇÃO\x1b[0m [y/N] \x1b[1A\x1b[J");
        const QVector<AnsiSegment> segments = parser.parse(raw);

        QString visibleText;
        for (const AnsiSegment &seg : segments) {
            visibleText += seg.text;
        }
        QCOMPARE(visibleText, QStringLiteral("Você está em PRODUÇÃO [y/N] "));
        QVERIFY(!visibleText.contains(QLatin1Char('\x1b')));
        QVERIFY(!visibleText.contains(QStringLiteral("[2K")));
        QVERIFY(!visibleText.contains(QStringLiteral("[?25l")));
        QVERIFY(!visibleText.contains(QStringLiteral("[1A")));
        bool hasRed = false;
        for (const AnsiSegment &seg : segments) {
            if (seg.format.foreground().color() == readable(1)) {
                hasRed = true;
            }
        }
        QVERIFY(hasRed);
    }

    // "Cursor n linhas acima" vira um segmento de controle (sem texto) para quem exibe apagar o quadro anterior.
    void cursorUpIsReportedAsAControlSegment()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("a\n\x1b[3Ab\x1b[Ac"));

        QString visibleText;
        QVector<int> moves;
        for (const AnsiSegment &seg : segments) {
            visibleText += seg.text;
            if (seg.moveUpLines > 0) {
                moves << seg.moveUpLines;
            }
        }
        QCOMPARE(visibleText, QStringLiteral("a\nbc"));
        QCOMPARE(moves, (QVector<int>{3, 1}));
    }

    // Em caminhos do Windows o ESC chega como o glifo "←": o parser trata as duas grafias igual.
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

    // Lead-in "←[..." partido entre dois chunks não vaza texto nem quebra o chunk seguinte.
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
        QCOMPARE(segments.at(0).format.foreground().color(), readable(1));
        QCOMPARE(segments.at(1).text, QStringLiteral(" normal"));
    }

    void combinedBoldAndColorSgrIsApplied()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("\x1b[1;31mBOLD_RED\x1b[0m"));

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).text, QStringLiteral("BOLD_RED"));
        QCOMPARE(segments.at(0).format.fontWeight(), static_cast<int>(QFont::Bold));
        QCOMPARE(segments.at(0).format.foreground().color(), readable(1));
    }

    // Formato persiste entre chunks (ANSI num chunk, texto colorido no seguinte).
    void formatPersistsAcrossIncrementalChunks()
    {
        AnsiTextParser parser;
        parser.parse(QStringLiteral("\x1b[32m"));
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("green text"));

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).format.foreground().color(), readable(2));
    }

    void brightColorCodesAreDistinctFromNormal()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("\x1b[91mbright red\x1b[0m"));

        QCOMPARE(segments.size(), 1);
        QCOMPARE(segments.at(0).format.foreground().color(), readable(1, true));
    }

    void resetCodeRestoresDefaultFormat()
    {
        AnsiTextParser parser;
        parser.parse(QStringLiteral("\x1b[31m"));
        parser.resetFormat();
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("plain"));

        QCOMPARE(segments.at(0).format.foreground().color(), QColor(kai::utils::tokens::terminalFg()));
    }

    void emptyInputProducesNoSegments()
    {
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QString());
        QCOMPARE(segments.size(), 0);
    }

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
    void ansiWhiteStaysReadableOnALightTheme()
    {
        const QMap<QString, QString> light{{QStringLiteral("bg"), QStringLiteral("#f5f5f7")},
                                           {QStringLiteral("fg"), QStringLiteral("#1c1c1e")}};
        kai::utils::tokens::publishTheme(light);
        AnsiTextParser parser;
        const QVector<AnsiSegment> segments = parser.parse(QStringLiteral("\x1b[37mwhite\x1b[0m"));
        kai::utils::tokens::publishTheme({});

        QCOMPARE(segments.size(), 1);
        const QColor shown = segments.at(0).format.foreground().color();
        // O "branco" cru (229) some num fundo claro; o ajuste escurece até o mínimo de contraste.
        QVERIFY2(shown.lightnessF() < 0.7, qPrintable(shown.name()));
    }

};

QTEST_MAIN(TestAnsiTextParser)
#include "test_ansi_text_parser.moc"
