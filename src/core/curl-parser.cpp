#include "core/curl-parser.h"
#include "utils/translation-manager.h"

#include <QStringList>

namespace kai::core {

namespace {

// Tokeniza uma string estilo shell, respeitando aspas simples/duplas e
// escapes com '\'. Continuações de linha (\ seguido de newline) já devem
// ter sido normalizadas antes.
QStringList shellTokenize(const QString &input)
{
    QStringList tokens;
    QString current;
    bool inSingle = false;
    bool inDouble = false;
    bool hasToken = false;

    for (int i = 0; i < input.size(); ++i) {
        const QChar c = input.at(i);
        if (inSingle) {
            if (c == QLatin1Char('\'')) {
                inSingle = false;
            } else {
                current.append(c);
            }
            continue;
        }
        if (inDouble) {
            if (c == QLatin1Char('\\') && i + 1 < input.size()) {
                const QChar next = input.at(i + 1);
                // Dentro de aspas duplas, '\' só escapa alguns caracteres.
                if (next == QLatin1Char('"') || next == QLatin1Char('\\')
                    || next == QLatin1Char('$') || next == QLatin1Char('`')) {
                    current.append(next);
                    ++i;
                } else {
                    current.append(c);
                }
            } else if (c == QLatin1Char('"')) {
                inDouble = false;
            } else {
                current.append(c);
            }
            continue;
        }
        // Fora de aspas.
        if (c == QLatin1Char('\'')) {
            inSingle = true;
            hasToken = true;
        } else if (c == QLatin1Char('"')) {
            inDouble = true;
            hasToken = true;
        } else if (c == QLatin1Char('\\') && i + 1 < input.size()) {
            current.append(input.at(i + 1));
            ++i;
            hasToken = true;
        } else if (c.isSpace()) {
            if (hasToken) {
                tokens.append(current);
                current.clear();
                hasToken = false;
            }
        } else {
            current.append(c);
            hasToken = true;
        }
    }
    if (hasToken) {
        tokens.append(current);
    }
    return tokens;
}

} // namespace

CurlParseResult parseCurl(const QString &curlText)
{
    CurlParseResult result;

    // Normaliza continuações de linha ("\" no fim da linha) e newlines.
    QString normalized = curlText;
    normalized.replace(QStringLiteral("\\\r\n"), QStringLiteral(" "));
    normalized.replace(QStringLiteral("\\\n"), QStringLiteral(" "));
    normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
    normalized.replace(QLatin1Char('\r'), QLatin1Char(' '));

    const QStringList tokens = shellTokenize(normalized.trimmed());
    if (tokens.isEmpty()) {
        result.errorMessage = utils::tr(QStringLiteral("curl_parser.error.empty_command"));
        return result;
    }

    // O primeiro token deve ser "curl" (toleramos caminho completo).
    int start = 0;
    if (tokens.first() == QStringLiteral("curl") || tokens.first().endsWith(QStringLiteral("/curl"))) {
        start = 1;
    }

    HttpConfig cfg;
    bool methodExplicit = false;
    QString url;
    QStringList dataParts;

    auto valueOf = [&](int &i, const QString &inlineAfterEq) -> QString {
        // Suporta tanto "--flag valor" quanto "--flag=valor".
        if (!inlineAfterEq.isNull()) {
            return inlineAfterEq;
        }
        if (i + 1 < tokens.size()) {
            return tokens.at(++i);
        }
        return QString();
    };

    for (int i = start; i < tokens.size(); ++i) {
        QString tok = tokens.at(i);
        QString inlineVal; // valor após '=' em --flag=valor

        // Separa --flag=valor.
        if (tok.startsWith(QStringLiteral("--")) && tok.contains(QLatin1Char('='))) {
            const int eq = tok.indexOf(QLatin1Char('='));
            inlineVal = tok.mid(eq + 1);
            tok = tok.left(eq);
        }

        if (tok == QStringLiteral("-X") || tok == QStringLiteral("--request")) {
            const QString m = valueOf(i, inlineVal);
            cfg.method = httpMethodFromString(m);
            methodExplicit = true;
        } else if (tok == QStringLiteral("-H") || tok == QStringLiteral("--header")) {
            const QString h = valueOf(i, inlineVal);
            const int colon = h.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                const QString name = h.left(colon).trimmed();
                const QString val = h.mid(colon + 1).trimmed();
                cfg.headers.insert(name, val);
            }
        } else if (tok == QStringLiteral("-d") || tok == QStringLiteral("--data")
                   || tok == QStringLiteral("--data-raw") || tok == QStringLiteral("--data-binary")
                   || tok == QStringLiteral("--data-ascii") || tok == QStringLiteral("--data-urlencode")) {
            dataParts.append(valueOf(i, inlineVal));
        } else if (tok == QStringLiteral("--url")) {
            url = valueOf(i, inlineVal);
        } else if (tok == QStringLiteral("-u") || tok == QStringLiteral("--user")) {
            const QString creds = valueOf(i, inlineVal);
            const QByteArray b64 = creds.toUtf8().toBase64();
            cfg.headers.insert(QStringLiteral("Authorization"),
                               QStringLiteral("Basic ") + QString::fromLatin1(b64));
        } else if (tok == QStringLiteral("-A") || tok == QStringLiteral("--user-agent")) {
            cfg.headers.insert(QStringLiteral("User-Agent"), valueOf(i, inlineVal));
        } else if (tok == QStringLiteral("-e") || tok == QStringLiteral("--referer")) {
            cfg.headers.insert(QStringLiteral("Referer"), valueOf(i, inlineVal));
        } else if (tok.startsWith(QLatin1Char('-'))) {
            // Flags sem valor que ignoramos (ex: -s, -k, -L, --compressed).
            // Não consomem próximo token.
            continue;
        } else {
            // Token posicional = URL (o primeiro que aparecer).
            if (url.isEmpty()) {
                url = tok;
            }
        }
    }

    if (!dataParts.isEmpty()) {
        cfg.body = dataParts.join(QLatin1Char('&'));
        if (!methodExplicit) {
            cfg.method = HttpMethod::Post; // curl infere POST quando há -d
        }
    }

    cfg.url = url;
    if (url.isEmpty()) {
        result.errorMessage = utils::tr(QStringLiteral("curl_parser.error.no_url"));
        return result;
    }

    result.ok = true;
    result.config = cfg;
    return result;
}

} // namespace kai::core
