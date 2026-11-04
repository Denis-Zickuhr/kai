#include "core/http-export.h"

#include <QFileInfo>
#include <QStringList>

namespace kai::core {

namespace {

QString shellQuote(const QString &text)
{
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

// Literal de texto de Python / JavaScript (aspas duplas, escapes comuns).
QString codeQuote(const QString &text)
{
    QString out = QStringLiteral("\"");
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '\\': out += QStringLiteral("\\\\"); break;
        case '"': out += QStringLiteral("\\\""); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        case '\t': out += QStringLiteral("\\t"); break;
        default: out += c; break;
        }
    }
    return out + QLatin1Char('"');
}

QString curlCommand(const MaterializedHttpRequest &r, const HttpEffectiveOptions &o)
{
    QStringList parts;
    parts << QStringLiteral("curl");
    if (r.method == QStringLiteral("HEAD")) {
        parts << QStringLiteral("-I");
    } else if (r.method != QStringLiteral("GET") || r.bodyType != HttpBodyType::None) {
        // POST com corpo é o padrão do curl; ainda assim o verbo explícito deixa o comando legível.
        parts << QStringLiteral("-X ") + r.method;
    }
    parts << shellQuote(r.url.toString());
    if (o.followRedirects) parts << QStringLiteral("-L");
    if (!o.verifyTls) parts << QStringLiteral("-k");
    if (o.timeoutSec > 0) parts << QStringLiteral("-m %1").arg(o.timeoutSec);
    const QString proxy = o.proxy.trimmed();
    if (proxy == QStringLiteral("none")) {
        parts << QStringLiteral("--noproxy ") + shellQuote(QStringLiteral("*"));
    } else if (!proxy.isEmpty() && proxy != QStringLiteral("system")) {
        parts << QStringLiteral("-x ") + shellQuote(proxy);
    }
    for (const auto &h : r.headers) {
        parts << QStringLiteral("-H ") + shellQuote(h.first + QStringLiteral(": ") + h.second);
    }
    switch (r.bodyType) {
    case HttpBodyType::Multipart:
        for (const HttpEntry &f : r.fields) {
            parts << QStringLiteral("-F ")
                         + shellQuote(f.name + QLatin1Char('=') + (f.isFile ? QStringLiteral("@") : QString()) + f.value);
        }
        break;
    case HttpBodyType::File:
        parts << QStringLiteral("--data-binary ") + shellQuote(QStringLiteral("@") + r.bodyFile);
        break;
    case HttpBodyType::Raw:
        parts << QStringLiteral("--data-raw ") + shellQuote(QString::fromUtf8(r.payload));
        break;
    default:
        break;
    }
    return parts.join(QStringLiteral(" \\\n  "));
}

QString pythonSnippet(const MaterializedHttpRequest &r, const HttpEffectiveOptions &o)
{
    QStringList lines;
    lines << QStringLiteral("import requests") << QString();
    QStringList args;
    args << QStringLiteral("    %1").arg(codeQuote(r.method)) << QStringLiteral("    %1").arg(codeQuote(r.url.toString()));
    if (!r.headers.isEmpty()) {
        QStringList items;
        for (const auto &h : r.headers) {
            // Multipart: o requests define o Content-Type com a fronteira.
            if (r.bodyType == HttpBodyType::Multipart && h.first.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0) {
                continue;
            }
            items << QStringLiteral("        %1: %2,").arg(codeQuote(h.first), codeQuote(h.second));
        }
        if (!items.isEmpty()) {
            args << QStringLiteral("    headers={\n%1\n    }").arg(items.join(QLatin1Char('\n')));
        }
    }
    switch (r.bodyType) {
    case HttpBodyType::Multipart: {
        QStringList items;
        for (const HttpEntry &f : r.fields) {
            items << (f.isFile
                ? QStringLiteral("        %1: open(%2, \"rb\"),").arg(codeQuote(f.name), codeQuote(f.value))
                : QStringLiteral("        %1: (None, %2),").arg(codeQuote(f.name), codeQuote(f.value)));
        }
        args << QStringLiteral("    files={\n%1\n    }").arg(items.join(QLatin1Char('\n')));
        break;
    }
    case HttpBodyType::File:
        args << QStringLiteral("    data=open(%1, \"rb\")").arg(codeQuote(r.bodyFile));
        break;
    case HttpBodyType::Raw:
        args << QStringLiteral("    data=%1").arg(codeQuote(QString::fromUtf8(r.payload)));
        break;
    default:
        break;
    }
    if (o.timeoutSec > 0) args << QStringLiteral("    timeout=%1").arg(o.timeoutSec);
    if (!o.verifyTls) args << QStringLiteral("    verify=False");
    if (!o.followRedirects) args << QStringLiteral("    allow_redirects=False");
    if (o.proxy.trimmed() == QStringLiteral("none")) {
        args << QStringLiteral("    proxies={\"http\": None, \"https\": None}");
    } else if (!o.proxy.trimmed().isEmpty() && o.proxy.trimmed() != QStringLiteral("system")) {
        args << QStringLiteral("    proxies={\"http\": %1, \"https\": %1}").arg(codeQuote(o.proxy.trimmed()));
    }
    lines << QStringLiteral("response = requests.request(\n%1\n)").arg(args.join(QStringLiteral(",\n")));
    lines << QStringLiteral("print(response.status_code)") << QStringLiteral("print(response.text)");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString javascriptSnippet(const MaterializedHttpRequest &r, const HttpEffectiveOptions &o)
{
    QStringList lines;
    if (r.bodyType == HttpBodyType::Multipart) {
        lines << QStringLiteral("const form = new FormData();");
        for (const HttpEntry &f : r.fields) {
            lines << (f.isFile
                ? QStringLiteral("form.append(%1, /* file: %2 */ new Blob([]), %3);")
                      .arg(codeQuote(f.name), f.value, codeQuote(QFileInfo(f.value).fileName()))
                : QStringLiteral("form.append(%1, %2);").arg(codeQuote(f.name), codeQuote(f.value)));
        }
        lines << QString();
    }
    QStringList options;
    options << QStringLiteral("  method: %1").arg(codeQuote(r.method));
    QStringList headerItems;
    for (const auto &h : r.headers) {
        if (r.bodyType == HttpBodyType::Multipart && h.first.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        headerItems << QStringLiteral("    %1: %2").arg(codeQuote(h.first), codeQuote(h.second));
    }
    if (!headerItems.isEmpty()) {
        options << QStringLiteral("  headers: {\n%1\n  }").arg(headerItems.join(QStringLiteral(",\n")));
    }
    switch (r.bodyType) {
    case HttpBodyType::Multipart:
        options << QStringLiteral("  body: form");
        break;
    case HttpBodyType::File:
        options << QStringLiteral("  body: /* contents of %1 */ \"\"").arg(r.bodyFile);
        break;
    case HttpBodyType::Raw:
        options << QStringLiteral("  body: %1").arg(codeQuote(QString::fromUtf8(r.payload)));
        break;
    default:
        break;
    }
    if (!o.followRedirects) options << QStringLiteral("  redirect: \"manual\"");
    lines << QStringLiteral("const response = await fetch(%1, {\n%2\n});").arg(codeQuote(r.url.toString()),
                                                                                options.join(QStringLiteral(",\n")));
    lines << QStringLiteral("console.log(response.status);") << QStringLiteral("console.log(await response.text());");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace

QString exportHttpRequest(const MaterializedHttpRequest &request, const HttpEffectiveOptions &options,
                          HttpExportFormat format)
{
    switch (format) {
    case HttpExportFormat::Curl: return curlCommand(request, options);
    case HttpExportFormat::PythonRequests: return pythonSnippet(request, options);
    case HttpExportFormat::JavaScriptFetch: return javascriptSnippet(request, options);
    }
    return QString();
}

} // namespace kai::core
