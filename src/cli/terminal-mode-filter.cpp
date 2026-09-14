#include "cli/terminal-mode-filter.h"

#include <QSet>
#include <QStringList>

namespace kai::cli {

namespace {

constexpr QChar kEsc(0x1b);
// Tamanho máximo de uma sequência retida à espera do próximo chunk — além
// disso não é uma sequência de modo de verdade, repassa como veio.
constexpr int kMaxPendingLength = 64;
// Títulos (OSC) podem ser bem maiores que uma sequência de modo.
constexpr int kMaxPendingOscLength = 1024;

bool isBlockedMode(int mode)
{
    static const QSet<int> blocked = {
        1,                                  // DECCKM (cursor keys de aplicação)
        25,                                 // visibilidade do cursor
        1000, 1002, 1003, 1005, 1006, 1015, // mouse tracking
        1004,                               // focus events
        2004,                               // bracketed paste
        9001,                               // win32-input-mode (ConPTY)
    };
    return blocked.contains(mode);
}

// Reescreve os parâmetros de ESC[?a;b;c<final> sem os modos bloqueados.
// Vazio = a sequência inteira some.
QString rewriteModeSequence(const QString &params, QChar final)
{
    QStringList kept;
    for (const QString &p : params.split(QLatin1Char(';'))) {
        bool ok = false;
        const int mode = p.toInt(&ok);
        if (!ok || !isBlockedMode(mode)) {
            kept << p;
        }
    }
    if (kept.isEmpty()) {
        return QString();
    }
    return QString(kEsc) + QStringLiteral("[?") + kept.join(QLatin1Char(';')) + final;
}

// Altura da tela virtual do ConPTY/forkpty (ver ProcessRunner: 500x30).
constexpr int kScreenRows = 30;

// Limpar a tela inteira (ESC[2J / ESC[3J) — o ConPTY abre TODA execução
// assim (medido: "ESC[2J ESC[m ESC[H" antes da 1ª linha). Repassado ao
// terminal de quem chamou, apagava a linha do próprio `kai oi` (bug
// relatado). O ESC[H que vem junto é tratado como posição (ver moveTo): na
// linha 1, coluna 1 da tela virtual, que é onde a saída começou, não anda.
bool isScreenClear(const QString &params, QChar final)
{
    return final == QLatin1Char('J') && (params == QStringLiteral("2") || params == QStringLiteral("3"));
}

// N-ésimo parâmetro numérico de uma CSI ("5;10" -> 5, 10); vazio/0 = padrão.
int csiParam(const QString &params, int index, int fallback)
{
    const QStringList parts = params.split(QLatin1Char(';'));
    if (index >= parts.size()) {
        return fallback;
    }
    bool ok = false;
    const int value = parts.at(index).toInt(&ok);
    return (ok && value > 0) ? value : fallback;
}

// OSC 0/1/2 = título da janela/aba. O ConPTY manda o do cmd.exe
// ("C:\WINDOWS\SYSTEM32\cmd.exe") e renomeava a aba de quem chamou.
bool isWindowTitle(QStringView oscBody)
{
    return oscBody.startsWith(u"0;") || oscBody.startsWith(u"1;") || oscBody.startsWith(u"2;");
}

} // namespace

void TerminalModeFilter::trackText(QStringView text)
{
    for (const QChar c : text) {
        if (c == QLatin1Char('\n')) {
            // Na última linha da tela virtual o ConPTY rola: a linha fica.
            m_row = qMin(m_row + 1, kScreenRows);
            m_maxRow = qMax(m_maxRow, m_row);
        } else if (c == QLatin1Char('\r')) {
            m_atLineStart = true;
        } else if (c.unicode() >= 0x20) {
            m_atLineStart = false;
        }
    }
}

QString TerminalModeFilter::moveToRow(int row)
{
    row = qBound(1, row, kScreenRows);
    QString out;
    if (row < m_row) {
        out += QString(kEsc) + QStringLiteral("[%1A").arg(m_row - row);
    } else if (row > m_row) {
        // Até a linha mais baixa já impressa, desce; abaixo dela o terminal
        // real ainda não tem linha — quebra de linha (rola se precisar), em
        // vez de ESC[B, que para na borda de baixo.
        const int within = qMin(row, m_maxRow) - m_row;
        if (within > 0) {
            out += QString(kEsc) + QStringLiteral("[%1B").arg(within);
        }
        const int beyond = row - qMax(m_row, m_maxRow);
        for (int i = 0; i < beyond; ++i) {
            out += QStringLiteral("\r\n");
        }
        if (beyond > 0) {
            m_atLineStart = true;
        }
        m_maxRow = qMax(m_maxRow, row);
    }
    m_row = row;
    return out;
}

QString TerminalModeFilter::moveTo(int row, int column)
{
    QString out = moveToRow(row);
    if (column <= 1) {
        if (!m_atLineStart) {
            out += QLatin1Char('\r');
        }
        m_atLineStart = true;
    } else {
        out += QLatin1Char('\r') + QString(kEsc) + QStringLiteral("[%1C").arg(column - 1);
        m_atLineStart = false;
    }
    return out;
}

QString TerminalModeFilter::feed(const QString &chunk)
{
    const QString text = m_pending + chunk;
    m_pending.clear();

    QString result;
    result.reserve(text.size());
    int i = 0;
    while (i < text.size()) {
        const int esc = text.indexOf(kEsc, i);
        if (esc < 0) {
            trackText(QStringView(text).mid(i));
            result += QStringView(text).mid(i);
            break;
        }
        trackText(QStringView(text).mid(i, esc - i));
        result += QStringView(text).mid(i, esc - i);

        // Sequência que ainda não terminou neste chunk: retém até o próximo
        // (limite pra não segurar lixo que nunca vai fechar).
        auto holdIncomplete = [&](int limit) {
            if (text.size() - esc <= limit) {
                m_pending = text.mid(esc);
            } else {
                result += QStringView(text).mid(esc);
            }
        };

        if (esc + 1 >= text.size()) {
            holdIncomplete(kMaxPendingLength);
            break;
        }
        const QChar introducer = text.at(esc + 1);

        if (introducer == QLatin1Char('[')) {
            // CSI: parâmetros (0x30-0x3F), intermediários (0x20-0x2F), final (0x40-0x7E).
            int j = esc + 2;
            while (j < text.size() && text.at(j).unicode() >= 0x30 && text.at(j).unicode() <= 0x3f) {
                ++j;
            }
            const QString params = text.mid(esc + 2, j - esc - 2);
            while (j < text.size() && text.at(j).unicode() >= 0x20 && text.at(j).unicode() <= 0x2f) {
                ++j;
            }
            if (j >= text.size()) {
                holdIncomplete(kMaxPendingLength);
                break;
            }
            const QChar final = text.at(j);
            const bool isPrivate = params.startsWith(QLatin1Char('?'));
            if (isPrivate && (final == QLatin1Char('h') || final == QLatin1Char('l'))) {
                result += rewriteModeSequence(params.mid(1), final);
            } else if (isPrivate) {
                result += QStringView(text).mid(esc, j - esc + 1);
            } else if (isScreenClear(params, final)) {
                // descarta
            } else if (final == QLatin1Char('H') || final == QLatin1Char('f')) {
                result += moveTo(csiParam(params, 0, 1), csiParam(params, 1, 1));
            } else if (final == QLatin1Char('d')) {
                result += moveToRow(csiParam(params, 0, 1));
            } else {
                const int n = csiParam(params, 0, 1);
                if (final == QLatin1Char('A') || final == QLatin1Char('F')) {
                    m_row = qMax(1, m_row - n);
                } else if (final == QLatin1Char('B') || final == QLatin1Char('E')) {
                    m_row = qMin(kScreenRows, m_row + n);
                    m_maxRow = qMax(m_maxRow, m_row);
                }
                if (final == QLatin1Char('E') || final == QLatin1Char('F')) {
                    m_atLineStart = true;
                }
                result += QStringView(text).mid(esc, j - esc + 1);
            }
            i = j + 1;
            continue;
        }

        if (introducer == QLatin1Char(']')) {
            // OSC: termina em BEL ou ST (ESC \).
            int j = esc + 2;
            int terminatorEnd = -1;
            while (j < text.size()) {
                if (text.at(j) == QChar(0x07)) {
                    terminatorEnd = j + 1;
                    break;
                }
                if (text.at(j) == kEsc && j + 1 < text.size() && text.at(j + 1) == QLatin1Char('\\')) {
                    terminatorEnd = j + 2;
                    break;
                }
                ++j;
            }
            if (terminatorEnd < 0) {
                holdIncomplete(kMaxPendingOscLength);
                break;
            }
            const QStringView body = QStringView(text).mid(esc + 2);
            if (!isWindowTitle(body)) {
                result += QStringView(text).mid(esc, terminatorEnd - esc);
            }
            i = terminatorEnd;
            continue;
        }

        result += kEsc;
        i = esc + 1;
    }
    return result;
}

QString TerminalModeFilter::flush()
{
    QString rest;
    rest.swap(m_pending);
    return rest;
}

} // namespace kai::cli
