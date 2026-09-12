#include "ui/ansi-text-parser.h"

#include <QRegularExpression>

namespace kai::ui {

namespace {
// Sequência SGR: ESC [ <params> m, onde <params> é uma lista de números
// separados por ';' (ex: "1;31" para bold + vermelho).
const QRegularExpression &sgrPattern()
{
    static const QRegularExpression pattern(QStringLiteral("\x1b\\[([0-9;]*)m"));
    return pattern;
}

// Sequências de escape ANSI que NÃO são SGR de cor e que um QPlainTextEdit
// não sabe interpretar (controle de cursor/tela). Sem removê-las, elas
// vazam como lixo visual na saída (bug reportado: "[?25l", "[2K", "[1A",
// "[J" aparecendo no texto) — passou a ocorrer com o PTY, que faz o bash
// emitir essas sequências de terminal real. Cobre:
//  - CSI genérico: ESC [ ... <letra final que não seja 'm'> (cursor up/
//    down, clear line/screen, posição, show/hide cursor com '?', etc.);
//  - OSC: ESC ] ... (BEL | ESC \) — ex. título de janela;
//  - escapes de 2 bytes: ESC seguido de um único caractere (ex: ESC(B).
// A ordem importa: aplicamos ISTO só ao texto JÁ separado do SGR, para não
// remover as cores; e o SGR terminado em 'm' é preservado pelo negative
// lookahead não ser necessário aqui (tratamos SGR antes, no parse()).
const QRegularExpression &nonSgrEscapePattern()
{
    static const QRegularExpression pattern(QStringLiteral(
        "\x1b\\[[0-9;?]*[A-Za-ln-z]"      // CSI que NÃO termina em 'm' (m excluído do range)
        "|\x1b\\][^\x07\x1b]*(?:\x07|\x1b\\\\)"  // OSC ... BEL ou ST
        "|\x1b[()][A-Za-z0-9]"            // designação de charset (ESC(B etc.)
        "|\x1b[=>]"                        // keypad mode
        "|[\x00-\x08\x0b\x0c\x0e-\x1f]"));// outros controles (mantém \t=09, \n=0a, \r=0d)
    return pattern;
}

// Remove do texto puro (sem SGR) as sequências de controle que não sabemos
// renderizar, evitando o "lixo" visual. \n, \r e \t são preservados.
QString stripNonSgrControl(const QString &text)
{
    QString cleaned = text;
    cleaned.remove(nonSgrEscapePattern());
    return cleaned;
}

// URLs http(s):// (auto-highlight de links no Terminal Drawer).
const QRegularExpression &urlPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(https?://[^\s]+)"));
    return pattern;
}

// Divide `text` em sub-segmentos de link/não-link, preservando o
// QTextCharFormat base (`baseFormat`) para as partes não-link. Partes que
// são URLs recebem `isLink=true` e um formato visual diferenciado
// (sublinhado + cor de destaque), sem perder a cor ANSI original de fundo.
void appendTextWithLinkDetection(QVector<AnsiSegment> &segments, const QString &text, const QTextCharFormat &baseFormat)
{
    int lastPos = 0;
    QRegularExpressionMatchIterator it = urlPattern().globalMatch(text);

    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();

        if (match.capturedStart() > lastPos) {
            segments.append({text.mid(lastPos, match.capturedStart() - lastPos), baseFormat, false});
        }

        QTextCharFormat linkFormat = baseFormat;
        linkFormat.setForeground(QColor(86, 156, 214)); // azul claro, destaque de link
        linkFormat.setFontUnderline(true);
        linkFormat.setAnchor(true);
        linkFormat.setAnchorHref(match.captured(0));
        segments.append({match.captured(0), linkFormat, true});

        lastPos = match.capturedEnd();
    }

    if (lastPos < text.size()) {
        segments.append({text.mid(lastPos), baseFormat, false});
    }
}
}

AnsiTextParser::AnsiTextParser()
{
    m_defaultFormat.setForeground(QColor(Qt::white));
    m_currentFormat = m_defaultFormat;
}

QColor AnsiTextParser::ansiColorFor(int code, bool bright)
{
    // Paleta padrão dos 8 cores ANSI (30-37 / 40-47), com variante "bright"
    // para os códigos 90-97 / 100-107.
    static const QColor normalColors[8] = {
        QColor(0, 0, 0),       // black
        QColor(205, 0, 0),     // red
        QColor(0, 205, 0),     // green
        QColor(205, 205, 0),   // yellow
        QColor(0, 0, 238),     // blue
        QColor(205, 0, 205),   // magenta
        QColor(0, 205, 205),   // cyan
        QColor(229, 229, 229), // white
    };
    static const QColor brightColors[8] = {
        QColor(127, 127, 127),
        QColor(255, 0, 0),
        QColor(0, 255, 0),
        QColor(255, 255, 0),
        QColor(92, 92, 255),
        QColor(255, 0, 255),
        QColor(0, 255, 255),
        QColor(255, 255, 255),
    };

    if (code < 0 || code > 7) {
        return bright ? brightColors[7] : normalColors[7];
    }
    return bright ? brightColors[code] : normalColors[code];
}

void AnsiTextParser::resetFormat()
{
    m_currentFormat = m_defaultFormat;
}

QVector<AnsiSegment> AnsiTextParser::parse(const QString &rawInput)
{
    QVector<AnsiSegment> segments;

    // Retoma a cauda pendente do chunk anterior (sequência de escape que
    // ficou partida na fronteira do buffer).
    QString input = m_pendingTail + rawInput;
    m_pendingTail.clear();
    // Se o fim do input é uma sequência de escape INCOMPLETA, retém para o
    // próximo chunk em vez de deixá-la ser tratada como texto/lixo.
    {
        const int esc = input.lastIndexOf(QChar(0x1b));
        if (esc >= 0) {
            const QString tail = input.mid(esc);
            // Completa? CSI termina em letra; OSC em BEL/ST; ESC+1 char.
            static const QRegularExpression completeSeq(QStringLiteral(
                "^\\x1b(?:\\[[0-9;?]*[A-Za-z]|\\][^\\x07\\x1b]*(?:\\x07|\\x1b\\\\)|[()][A-Za-z0-9]|[=>])"));
            if (!completeSeq.match(tail).hasMatch() && tail.length() < 32) {
                m_pendingTail = tail;
                input.chop(tail.length());
            }
        }
    }

    int lastPos = 0;
    const QRegularExpression &pattern = sgrPattern();
    QRegularExpressionMatchIterator it = pattern.globalMatch(input);

    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();

        if (match.capturedStart() > lastPos) {
            const QString text = stripNonSgrControl(input.mid(lastPos, match.capturedStart() - lastPos));
            if (!text.isEmpty()) {
                appendTextWithLinkDetection(segments, text, m_currentFormat);
            }
        }

        const QString paramsStr = match.captured(1);
        const QStringList params = paramsStr.isEmpty()
            ? QStringList{QStringLiteral("0")}
            : paramsStr.split(QLatin1Char(';'), Qt::SkipEmptyParts);

        for (const QString &paramStr : params) {
            bool ok = false;
            const int code = paramStr.toInt(&ok);
            if (!ok) {
                continue;
            }

            if (code == 0) {
                resetFormat();
            } else if (code == 1) {
                m_currentFormat.setFontWeight(QFont::Bold);
            } else if (code == 22) {
                m_currentFormat.setFontWeight(QFont::Normal);
            } else if (code >= 30 && code <= 37) {
                m_currentFormat.setForeground(ansiColorFor(code - 30, false));
            } else if (code >= 90 && code <= 97) {
                m_currentFormat.setForeground(ansiColorFor(code - 90, true));
            } else if (code >= 40 && code <= 47) {
                m_currentFormat.setBackground(ansiColorFor(code - 40, false));
            } else if (code >= 100 && code <= 107) {
                m_currentFormat.setBackground(ansiColorFor(code - 100, true));
            } else if (code == 39) {
                m_currentFormat.setForeground(m_defaultFormat.foreground());
            } else if (code == 49) {
                m_currentFormat.clearBackground();
            }
            // Demais códigos SGR (itálico, underline, etc.) são ignorados
            // por ora; podem ser adicionados incrementalmente sem quebrar
            // o parser existente.
        }

        lastPos = match.capturedEnd();
    }

    if (lastPos < input.size()) {
        const QString text = stripNonSgrControl(input.mid(lastPos));
        if (!text.isEmpty()) {
            appendTextWithLinkDetection(segments, text, m_currentFormat);
        }
    }

    return segments;
}

} // namespace kai::ui
