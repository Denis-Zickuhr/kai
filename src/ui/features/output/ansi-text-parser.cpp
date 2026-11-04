#include "ui/features/output/ansi-text-parser.h"

#include <QRegularExpression>

namespace kai::ui {

namespace {
// Lead-in do escape: o byte ESC (\x1b) OU o glifo "←" (U+2190). Em alguns caminhos do Windows (cmd.exe/ConPTY) o ESC
// chega como esse glifo e a sequência inteira vazaria como texto literal.
const QString &escLeadIn()
{
    static const QString pattern = QStringLiteral("(?:\x1b|\u2190)");
    return pattern;
}

// Sequência SGR: ESC [ <params> m, params = números separados por ';' (ex: "1;31").
const QRegularExpression &sgrPattern()
{
    static const QRegularExpression pattern(escLeadIn() + QStringLiteral("\\[([0-9;]*)m"));
    return pattern;
}

// Escapes ANSI que não são SGR de cor (cursor/tela, OSC como título de janela, escapes de 2 bytes como ESC(B)) e que
// um QPlainTextEdit não interpreta; sem removê-los vazam como lixo (o PTY faz o bash emiti-los). Aplicado só ao
// texto já separado do SGR, para não remover as cores.
const QRegularExpression &nonSgrEscapePattern()
{
    const QString esc = escLeadIn();
    static const QRegularExpression pattern(
        esc + QStringLiteral("\\[[0-9;?]*[A-Za-ln-z]")      // CSI que NÃO termina em 'm' (m excluído do range)
        + QStringLiteral("|") + esc + QStringLiteral("\\][^\x07\x1b\u2190]*(?:\x07|") + esc + QStringLiteral("\\\\)")  // OSC ... BEL ou ST
        + QStringLiteral("|") + esc + QStringLiteral("[()][A-Za-z0-9]")            // designação de charset (ESC(B etc.)
        + QStringLiteral("|") + esc + QStringLiteral("[=>]")                        // keypad mode
        + QStringLiteral("|[\x00-\x08\x0b\x0c\x0e-\x1f]"));// outros controles (mantém \t=09, \n=0a, \r=0d)
    return pattern;
}

// Remove do texto puro (sem SGR) as sequências de controle que não sabemos renderizar; \n, \r e \t ficam.
QString stripNonSgrControl(const QString &text)
{
    QString cleaned = text;
    cleaned.remove(nonSgrEscapePattern());
    return cleaned;
}

const QRegularExpression &urlPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(https?://[^\s]+)"));
    return pattern;
}

// Divide `text` em segmentos de link/não-link, preservando `baseFormat` nas partes não-link. URLs recebem
// `isLink=true` e formato de destaque (sublinhado + cor), mantendo o fundo ANSI original.
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
    // Paleta dos 8 cores ANSI (30-37 / 40-47), com variante "bright" para 90-97 / 100-107.
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

    // Retoma a cauda pendente do chunk anterior (escape partido na fronteira do buffer).
    QString input = m_pendingTail + rawInput;
    m_pendingTail.clear();
    // Se o fim do input é um escape INCOMPLETO, retém para o próximo chunk em vez de tratá-lo como texto.
    {
        // Aceita as duas grafias do lead-in (ver escLeadIn()); senão "←[..." partido na fronteira vazava como texto.
        const int esc = qMax(input.lastIndexOf(QChar(0x1b)), input.lastIndexOf(QChar(0x2190)));
        if (esc >= 0) {
            const QString tail = input.mid(esc);
            // Completa? CSI termina em letra; OSC em BEL/ST; ESC+1 char.
            static const QRegularExpression completeSeq(
                QStringLiteral("^(?:\\x1b|←)(?:\\[[0-9;?]*[A-Za-z]|\\][^\\x07\\x1b←]*(?:\\x07|(?:\\x1b|←)\\\\)|[()][A-Za-z0-9]|[=>])"));
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
            // Demais códigos SGR (itálico, underline, etc.) são ignorados.
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
