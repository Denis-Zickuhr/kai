#pragma once

#include <QString>
#include <QVector>

namespace kai::utils {

// Um corpo JSON com variáveis ({"a": {{x}}}) não é JSON válido até as variáveis serem trocadas.
// Para validar/formatar, cada {{...}} vira um marcador (uma string, se estava fora de aspas) e volta depois.
struct MaskedTemplate {
    struct Entry {
        QString marker;
        QString original;
        bool quoted = false;
    };
    QString text;
    QVector<Entry> entries;
};

inline MaskedTemplate maskTemplateVars(const QString &raw)
{
    MaskedTemplate masked;
    bool inString = false;
    for (int i = 0; i < raw.size();) {
        const QChar c = raw.at(i);
        if (c == QLatin1Char('\\') && inString && i + 1 < raw.size()) {
            masked.text += raw.mid(i, 2);
            i += 2;
            continue;
        }
        if (c == QLatin1Char('"')) {
            inString = !inString;
        } else if (c == QLatin1Char('{') && raw.mid(i, 2) == QStringLiteral("{{")) {
            const int end = raw.indexOf(QStringLiteral("}}"), i + 2);
            const QString inner = end < 0 ? QString() : raw.mid(i + 2, end - i - 2);
            if (end >= 0 && !inner.contains(QLatin1Char('{')) && !inner.contains(QLatin1Char('\n'))) {
                MaskedTemplate::Entry entry;
                entry.marker = QStringLiteral("__kai_var_%1__").arg(masked.entries.size());
                entry.original = raw.mid(i, end + 2 - i);
                entry.quoted = !inString;
                masked.text += entry.quoted ? QLatin1Char('"') + entry.marker + QLatin1Char('"') : entry.marker;
                masked.entries.append(entry);
                i = end + 2;
                continue;
            }
        }
        masked.text += c;
        ++i;
    }
    return masked;
}

inline QString unmaskTemplateVars(QString text, const MaskedTemplate &masked)
{
    for (const MaskedTemplate::Entry &entry : masked.entries) {
        const QString target = entry.quoted ? QLatin1Char('"') + entry.marker + QLatin1Char('"') : entry.marker;
        text.replace(target, entry.original);
    }
    return text;
}

} // namespace kai::utils
