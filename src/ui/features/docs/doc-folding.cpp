#include "ui/features/docs/doc-folding.h"

#include <QStringList>

#include <algorithm>
#include <functional>

namespace kai::ui::folding {

namespace {

constexpr qsizetype kMaxAnalyzedChars = 4 * 1024 * 1024;

bool isJsonCloser(const QString &trimmed)
{
    const QString line = trimmed.endsWith(QLatin1Char(',')) ? trimmed.chopped(1) : trimmed;
    return line == QLatin1String("}") || line == QLatin1String("]");
}

bool isXmlCloser(const QString &trimmed)
{
    if (trimmed == QLatin1String("-->") || trimmed == QLatin1String("]]>")) {
        return true;
    }
    // Um único fechamento "</nome>" sozinho na linha; "</a></b>" fecha duas regiões e não pode sumir de uma só.
    return trimmed.startsWith(QLatin1String("</")) && trimmed.endsWith(QLatin1Char('>')) && trimmed.count(QLatin1Char('<')) == 1;
}

// Região que vai de `start` até a linha `close` (a do fechamento). A linha de fechamento some junto quando só tem o
// fechamento; com mais coisa (`}, {`), ela fica à vista e só o miolo some.
void addRegion(QVector<Region> &out, const QStringList &lines, int start, int close, bool (*isCloser)(const QString &))
{
    if (close <= start) {
        return;
    }
    Region region;
    region.start = start;
    const QString closing = lines.value(close).trimmed();
    if (isCloser(closing)) {
        region.end = close;
        region.suffix = closing;
    } else {
        region.end = close - 1;
    }
    if (region.end > start) {
        out.append(region);
    }
}

QVector<Region> jsonRegions(const QString &text, const QStringList &lines)
{
    QVector<Region> out;
    QVector<int> open; // linha de cada { ou [ ainda sem par
    bool inString = false;
    bool escaped = false;
    int line = 0;
    for (const QChar c : text) {
        if (c == QLatin1Char('\n')) {
            ++line;
            inString = false; // string de JSON não atravessa linha: uma aspa esquecida não pode derrubar o resto
            escaped = false;
            continue;
        }
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == QLatin1Char('\\')) {
                escaped = true;
            } else if (c == QLatin1Char('"')) {
                inString = false;
            }
            continue;
        }
        if (c == QLatin1Char('"')) {
            inString = true;
        } else if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
            open.append(line);
        } else if ((c == QLatin1Char('}') || c == QLatin1Char(']')) && !open.isEmpty()) {
            addRegion(out, lines, open.takeLast(), line, isJsonCloser);
        }
    }
    return out;
}

QVector<Region> xmlRegions(const QString &text, const QStringList &lines)
{
    struct Open {
        QString name;
        int line;
    };
    QVector<Region> out;
    QVector<Open> open;
    const qsizetype size = text.size();
    int line = 0;
    qsizetype pos = 0;
    // Avança `pos` até `to` contando as quebras de linha que passou.
    const auto advance = [&](qsizetype to) {
        for (; pos < to && pos < size; ++pos) {
            if (text.at(pos) == QLatin1Char('\n')) {
                ++line;
            }
        }
    };
    while (pos < size) {
        const qsizetype lt = text.indexOf(QLatin1Char('<'), pos);
        if (lt < 0) {
            break;
        }
        advance(lt);
        const int startLine = line;
        // Comentário e CDATA podem ter várias linhas: viram uma região cada.
        const QStringView here = QStringView(text).mid(lt);
        if (here.startsWith(QLatin1String("<!--"))) {
            const qsizetype end = text.indexOf(QLatin1String("-->"), lt + 4);
            advance(end < 0 ? size : end + 3);
            addRegion(out, lines, startLine, line, isXmlCloser);
            continue;
        }
        if (here.startsWith(QLatin1String("<![CDATA["))) {
            const qsizetype end = text.indexOf(QLatin1String("]]>"), lt + 9);
            advance(end < 0 ? size : end + 3);
            addRegion(out, lines, startLine, line, isXmlCloser);
            continue;
        }
        if (here.startsWith(QLatin1String("<?"))) {
            const qsizetype end = text.indexOf(QLatin1String("?>"), lt + 2);
            advance(end < 0 ? size : end + 2);
            continue;
        }
        if (here.startsWith(QLatin1String("<!"))) {
            const qsizetype end = text.indexOf(QLatin1Char('>'), lt + 2);
            advance(end < 0 ? size : end + 1);
            continue;
        }
        const bool closing = lt + 1 < size && text.at(lt + 1) == QLatin1Char('/');
        qsizetype nameEnd = lt + (closing ? 2 : 1);
        while (nameEnd < size && !text.at(nameEnd).isSpace() && text.at(nameEnd) != QLatin1Char('>')
               && text.at(nameEnd) != QLatin1Char('/')) {
            ++nameEnd;
        }
        const QString name = text.mid(lt + (closing ? 2 : 1), nameEnd - lt - (closing ? 2 : 1));
        // Fim da marca, sem se enganar com um ">" dentro do valor de um atributo.
        qsizetype gt = nameEnd;
        QChar quote;
        while (gt < size) {
            const QChar c = text.at(gt);
            if (!quote.isNull()) {
                if (c == quote) {
                    quote = QChar();
                }
            } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                quote = c;
            } else if (c == QLatin1Char('>')) {
                break;
            }
            ++gt;
        }
        if (gt >= size) {
            break; // marca sem fim: nada mais a casar
        }
        const bool selfClosing = !closing && gt > lt && text.at(gt - 1) == QLatin1Char('/');
        if (closing) {
            for (int i = int(open.size()) - 1; i >= 0; --i) {
                if (open.at(i).name == name) {
                    addRegion(out, lines, open.at(i).line, startLine, isXmlCloser);
                    open.resize(i);
                    break;
                }
            }
        } else if (!selfClosing && !name.isEmpty()) {
            open.append({name, startLine});
        }
        advance(gt + 1);
    }
    return out;
}

int indentOf(const QString &line)
{
    int indent = 0;
    while (indent < line.size() && (line.at(indent) == QLatin1Char(' ') || line.at(indent) == QLatin1Char('\t'))) {
        ++indent;
    }
    return indent;
}

bool isSequenceItem(const QString &line, int indent)
{
    return indent < line.size() && line.at(indent) == QLatin1Char('-')
        && (indent + 1 == line.size() || line.at(indent + 1).isSpace());
}

// YAML dobra por recuo: um cabeçalho vale até a última linha mais recuada que ele. Uma lista no mesmo recuo da chave
// (`chave:` seguida de `- item`) também é filha dela.
QVector<Region> yamlRegions(const QStringList &lines)
{
    struct Open {
        int effective; // recuo contando o +1 dos itens de lista colados na chave
        int line;
        bool keyColon;
    };
    QVector<Region> out;
    QVector<Open> open;
    int lastContent = -1;
    const auto closeWhile = [&](const std::function<bool(const Open &)> &shouldClose) {
        while (!open.isEmpty() && shouldClose(open.last())) {
            const Open done = open.takeLast();
            if (lastContent > done.line) {
                out.append({done.line, lastContent, QString()});
            }
        }
    };
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const int indent = indentOf(line);
        if (indent >= line.size() || line.at(indent) == QLatin1Char('\r') || line.at(indent) == QLatin1Char('#')) {
            continue; // vazia ou comentário: não muda a estrutura
        }
        const bool item = isSequenceItem(line, indent);
        int effective = indent;
        if (item) {
            closeWhile([indent](const Open &top) { return top.effective > indent || (top.effective == indent && !top.keyColon); });
            if (!open.isEmpty() && open.last().effective == indent && open.last().keyColon) {
                effective = indent + 1;
            }
        } else {
            closeWhile([indent](const Open &top) { return top.effective >= indent; });
        }
        QString trimmed = line.trimmed();
        open.append({effective, i, !item && trimmed.endsWith(QLatin1Char(':'))});
        lastContent = i;
    }
    closeWhile([](const Open &) { return true; });
    return out;
}

} // namespace

bool supports(texttools::Language language)
{
    return language == texttools::Language::Json || language == texttools::Language::Yaml
        || language == texttools::Language::Xml;
}

QVector<Region> regions(const QString &text, texttools::Language language)
{
    if (!supports(language) || text.size() > kMaxAnalyzedChars) {
        return {};
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    QVector<Region> found;
    switch (language) {
    case texttools::Language::Json: found = jsonRegions(text, lines); break;
    case texttools::Language::Xml: found = xmlRegions(text, lines); break;
    case texttools::Language::Yaml: found = yamlRegions(lines); break;
    default: break;
    }
    // Por início, a mais externa primeiro; dois fechamentos que começam na mesma linha (`[{`) ficam com um só.
    std::sort(found.begin(), found.end(), [](const Region &a, const Region &b) {
        return a.start != b.start ? a.start < b.start : a.end > b.end;
    });
    QVector<Region> unique;
    for (const Region &region : std::as_const(found)) {
        if (unique.isEmpty() || unique.last().start != region.start) {
            unique.append(region);
        }
    }
    return unique;
}

} // namespace kai::ui::folding
