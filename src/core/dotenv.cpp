#include "core/dotenv.h"

#include <QRegularExpression>
#include <QStringList>

namespace kai::core {

QMap<QString, QString> parseDotenv(const QString &text)
{
    static const QRegularExpression assignment(
        QStringLiteral("^\\s*(?:export\\s+)?([A-Za-z_][A-Za-z0-9_.]*)\\s*=\\s*(.*)$"));
    QMap<QString, QString> out;
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        QString line = rawLine;
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (line.trimmed().startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QRegularExpressionMatch match = assignment.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        const QString key = match.captured(1);
        QString rest = match.captured(2).trimmed();
        QString value;
        if (rest.startsWith(QLatin1Char('"'))) {
            int i = 1;
            for (; i < rest.size() && rest.at(i) != QLatin1Char('"'); ++i) {
                if (rest.at(i) == QLatin1Char('\\') && i + 1 < rest.size()) {
                    const QChar next = rest.at(++i);
                    value += next == QLatin1Char('n') ? QLatin1Char('\n')
                           : next == QLatin1Char('t') ? QLatin1Char('\t')
                           : next;
                } else {
                    value += rest.at(i);
                }
            }
        } else if (rest.startsWith(QLatin1Char('\''))) {
            const int end = rest.indexOf(QLatin1Char('\''), 1);
            value = end < 0 ? rest.mid(1) : rest.mid(1, end - 1);
        } else {
            // Sem aspas: um " #" começa um comentário.
            const int hash = rest.indexOf(QStringLiteral(" #"));
            value = (hash >= 0 ? rest.left(hash) : rest).trimmed();
        }
        out.insert(key, value);
    }
    return out;
}

} // namespace kai::core
