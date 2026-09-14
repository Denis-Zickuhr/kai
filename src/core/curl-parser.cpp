#include "core/curl-parser.h"
#include "utils/translation-manager.h"

#include <QStringList>

namespace kai::core {

namespace {

// Troca (sem diferenciar maiúsculas) ou acrescenta um header.
void setHeader(HttpConfig &cfg, const QString &name, const QString &value)
{
    for (HttpEntry &h : cfg.headers) {
        if (h.name.compare(name, Qt::CaseInsensitive) == 0) {
            h.value = value;
            return;
        }
    }
    HttpEntry e;
    e.name = name;
    e.value = value;
    cfg.headers.append(e);
}

// Flags do curl que recebem um valor que não nos interessa: o valor não pode virar a URL.
bool takesIgnoredValue(const QString &flag)
{
    static const QStringList flags = {
        QStringLiteral("-o"), QStringLiteral("--output"), QStringLiteral("-w"), QStringLiteral("--write-out"),
        QStringLiteral("--connect-timeout"), QStringLiteral("--cacert"), QStringLiteral("--capath"),
        QStringLiteral("--cert"), QStringLiteral("-E"), QStringLiteral("--key"), QStringLiteral("-T"),
        QStringLiteral("--upload-file"), QStringLiteral("--retry"), QStringLiteral("--retry-delay"),
        QStringLiteral("-c"), QStringLiteral("--cookie-jar"), QStringLiteral("--proxy-user"),
        QStringLiteral("-U"), QStringLiteral("--resolve"), QStringLiteral("--interface"),
        QStringLiteral("--limit-rate"), QStringLiteral("-K"), QStringLiteral("--config"),
        QStringLiteral("--max-redirs"), QStringLiteral("-y"), QStringLiteral("-Y"),
    };
    return flags.contains(flag);
}

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
            cfg.setMethodName(valueOf(i, inlineVal));
            methodExplicit = true;
        } else if (tok == QStringLiteral("-H") || tok == QStringLiteral("--header")) {
            const QString h = valueOf(i, inlineVal);
            const int colon = h.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                const QString name = h.left(colon).trimmed();
                const QString val = h.mid(colon + 1).trimmed();
                setHeader(cfg, name, val);
            }
        } else if (tok == QStringLiteral("-d") || tok == QStringLiteral("--data")
                   || tok == QStringLiteral("--data-raw") || tok == QStringLiteral("--data-binary")
                   || tok == QStringLiteral("--data-ascii") || tok == QStringLiteral("--data-urlencode")) {
            dataParts.append(valueOf(i, inlineVal));
        } else if (tok == QStringLiteral("--url")) {
            url = valueOf(i, inlineVal);
        } else if (tok == QStringLiteral("-u") || tok == QStringLiteral("--user")) {
            const QString creds = valueOf(i, inlineVal);
            const int colon = creds.indexOf(QLatin1Char(':'));
            cfg.auth.type = HttpAuthType::Basic;
            cfg.auth.username = colon >= 0 ? creds.left(colon) : creds;
            cfg.auth.password = colon >= 0 ? creds.mid(colon + 1) : QString();
        } else if (tok == QStringLiteral("-A") || tok == QStringLiteral("--user-agent")) {
            setHeader(cfg, QStringLiteral("User-Agent"), valueOf(i, inlineVal));
        } else if (tok == QStringLiteral("-e") || tok == QStringLiteral("--referer")) {
            setHeader(cfg, QStringLiteral("Referer"), valueOf(i, inlineVal));
        } else if (tok == QStringLiteral("-b") || tok == QStringLiteral("--cookie")) {
            const QString cookie = valueOf(i, inlineVal);
            if (cookie.contains(QLatin1Char('='))) { // sem '=' o curl lê um arquivo de cookies
                setHeader(cfg, QStringLiteral("Cookie"), cookie);
            }
        } else if (tok == QStringLiteral("-F") || tok == QStringLiteral("--form")
                   || tok == QStringLiteral("--form-string")) {
            const QString field = valueOf(i, inlineVal);
            const int eq = field.indexOf(QLatin1Char('='));
            if (eq > 0) {
                HttpEntry e;
                e.name = field.left(eq);
                e.value = field.mid(eq + 1);
                if (tok != QStringLiteral("--form-string") && e.value.startsWith(QLatin1Char('@'))) {
                    e.isFile = true;
                    e.value = e.value.mid(1).section(QLatin1Char(';'), 0, 0);
                }
                cfg.formFields.append(e);
            }
        } else if (tok == QStringLiteral("-I") || tok == QStringLiteral("--head")) {
            cfg.method = HttpMethod::Head;
            methodExplicit = true;
        } else if (tok == QStringLiteral("-k") || tok == QStringLiteral("--insecure")) {
            cfg.options.verifyTls = false;
        } else if (tok == QStringLiteral("-x") || tok == QStringLiteral("--proxy")) {
            cfg.options.proxy = valueOf(i, inlineVal);
        } else if (tok == QStringLiteral("-m") || tok == QStringLiteral("--max-time")) {
            bool ok = false;
            const int seconds = static_cast<int>(valueOf(i, inlineVal).toDouble(&ok));
            if (ok && seconds > 0) {
                cfg.options.timeoutSec = seconds;
            }
        } else if (tok == QStringLiteral("-L") || tok == QStringLiteral("--location")) {
            cfg.options.followRedirects = true;
        } else if (takesIgnoredValue(tok)) {
            valueOf(i, inlineVal);
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

    if (!cfg.formFields.isEmpty()) {
        cfg.bodyType = HttpBodyType::Multipart;
        if (!methodExplicit) {
            cfg.method = HttpMethod::Post; // curl infere POST quando há -F
        }
    } else if (!dataParts.isEmpty()) {
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
