#include "ui/features/docs/doc-highlighter.h"

#include "utils/design-tokens.h"

#include <QFont>
#include <QTextDocument>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

// Estados de bloco (para o que atravessa linhas).
constexpr int kNone = 0;
constexpr int kXmlComment = 1;
constexpr int kXmlCdata = 2;
constexpr int kFence = 3;

QTextCharFormat colored(const QString &color, bool bold = false, bool italic = false)
{
    QTextCharFormat format;
    format.setForeground(QColor(color));
    if (bold) {
        format.setFontWeight(QFont::Bold);
    }
    format.setFontItalic(italic);
    return format;
}

} // namespace

DocHighlighter::DocHighlighter(QTextDocument *document, texttools::Language language)
    : QSyntaxHighlighter(document), m_language(language)
{
    buildFormats();
}

void DocHighlighter::buildFormats()
{
    m_key = colored(tk::accent());
    m_string = colored(tk::successFg());
    m_number = colored(tk::warningFg());
    m_literal = colored(tk::infoFg(), true);
    m_punct = colored(tk::mutedFg());
    m_comment = colored(tk::mutedFg(), false, true);
    m_tag = colored(tk::accent(), true);
    m_attribute = colored(tk::infoFg());
    m_heading = colored(tk::accent(), true);
    m_code = colored(tk::successFg());
    m_link = colored(tk::infoFg());
    m_emphasis = colored(tk::fg(), true);
    m_marker = colored(tk::warningFg(), true);
    m_anchor = colored(tk::warningFg());
}

void DocHighlighter::setLanguage(texttools::Language language)
{
    if (m_language == language) {
        return;
    }
    m_language = language;
    rehighlight();
}

void DocHighlighter::refresh()
{
    buildFormats();
    rehighlight();
}

void DocHighlighter::apply(const QRegularExpression &pattern, const QString &text, const QTextCharFormat &format, int group)
{
    QRegularExpressionMatchIterator it = pattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (match.capturedStart(group) >= 0) {
            setFormat(int(match.capturedStart(group)), int(match.capturedLength(group)), format);
        }
    }
}

void DocHighlighter::highlightBlock(const QString &text)
{
    setCurrentBlockState(kNone);
    switch (m_language) {
    case texttools::Language::Json: highlightJson(text); break;
    case texttools::Language::Yaml: highlightYaml(text); break;
    case texttools::Language::Xml: highlightXml(text); break;
    case texttools::Language::Markdown: highlightMarkdown(text); break;
    case texttools::Language::Text: break;
    }
}

void DocHighlighter::highlightJson(const QString &text)
{
    static const QRegularExpression number(QStringLiteral("(?<![\\w\"])-?\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?(?![\\w\"])"));
    static const QRegularExpression literal(QStringLiteral("\\b(?:true|false|null)\\b"));
    static const QRegularExpression punct(QStringLiteral("[{}\\[\\],:]"));
    static const QRegularExpression stringRe(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"?"));
    apply(punct, text, m_punct);
    apply(number, text, m_number);
    apply(literal, text, m_literal);
    // Strings por último: o que está dentro delas não vira número/literal. Uma string seguida de ':' é chave.
    QRegularExpressionMatchIterator it = stringRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int end = int(match.capturedEnd());
        int next = end;
        while (next < text.size() && text.at(next).isSpace()) {
            ++next;
        }
        const bool isKey = next < text.size() && text.at(next) == QLatin1Char(':');
        setFormat(int(match.capturedStart()), int(match.capturedLength()), isKey ? m_key : m_string);
    }
}

void DocHighlighter::highlightYaml(const QString &text)
{
    static const QRegularExpression key(QStringLiteral("^\\s*(?:-\\s+)*((?:\"[^\"]*\"|'[^']*'|[^\\s#:\\[\\]{},&*!|>'\"%@`][^:#]*?))\\s*:(?:\\s|$)"));
    static const QRegularExpression dash(QStringLiteral("^\\s*(-)\\s"));
    static const QRegularExpression stringRe(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^']|'')*'"));
    static const QRegularExpression number(QStringLiteral("(?<![\\w.])-?\\d+(?:\\.\\d+)?(?![\\w.])"));
    static const QRegularExpression literal(QStringLiteral("\\b(?:true|false|null|yes|no|on|off)\\b|(?<=\\s)~(?=\\s|$)"));
    static const QRegularExpression anchor(QStringLiteral("[&*][A-Za-z0-9_-]+"));
    static const QRegularExpression doc(QStringLiteral("^(?:---|\\.\\.\\.)\\s*$"));
    static const QRegularExpression comment(QStringLiteral("(?:^|\\s)(#.*)$"));
    apply(number, text, m_number);
    apply(literal, text, m_literal);
    apply(anchor, text, m_anchor);
    apply(stringRe, text, m_string);
    apply(dash, text, m_marker, 1);
    apply(key, text, m_key, 1);
    apply(doc, text, m_marker);
    // O comentário vale só fora de aspas: refaz por cima, ignorando o '#' dentro de uma string.
    const QRegularExpressionMatch match = comment.match(text);
    if (match.hasMatch()) {
        bool inside = false;
        QChar quote;
        const int at = int(match.capturedStart(1));
        for (int i = 0; i < at; ++i) {
            const QChar c = text.at(i);
            if (!inside && (c == QLatin1Char('"') || c == QLatin1Char('\''))) {
                inside = true;
                quote = c;
            } else if (inside && c == quote) {
                inside = false;
            }
        }
        if (!inside) {
            setFormat(at, int(match.capturedLength(1)), m_comment);
        }
    }
}

void DocHighlighter::highlightXml(const QString &text)
{
    static const QRegularExpression tag(QStringLiteral("</?([A-Za-z_][\\w:.-]*)"));
    static const QRegularExpression attribute(QStringLiteral("\\s([A-Za-z_][\\w:.-]*)\\s*="));
    static const QRegularExpression value(QStringLiteral("\"[^\"]*\"|'[^']*'"));
    static const QRegularExpression bracket(QStringLiteral("</?|/?>|\\?>|<\\?"));
    static const QRegularExpression entity(QStringLiteral("&#?\\w+;"));
    static const QRegularExpression declaration(QStringLiteral("<\\?.*?\\?>"));

    int start = 0;
    // Continuação de um comentário/CDATA que começou numa linha anterior.
    if (previousBlockState() == kXmlComment || previousBlockState() == kXmlCdata) {
        const bool comment = previousBlockState() == kXmlComment;
        const int end = text.indexOf(comment ? QStringLiteral("-->") : QStringLiteral("]]>"));
        if (end < 0) {
            setFormat(0, int(text.size()), comment ? m_comment : m_code);
            setCurrentBlockState(previousBlockState());
            return;
        }
        setFormat(0, end + 3, comment ? m_comment : m_code);
        start = end + 3;
    }
    const QString rest = text.mid(start);
    // Tudo fora de comentário/CDATA desta linha.
    int i = 0;
    while (i < rest.size()) {
        const int comment = rest.indexOf(QStringLiteral("<!--"), i);
        const int cdata = rest.indexOf(QStringLiteral("<![CDATA["), i);
        int special = -1;
        bool isComment = false;
        if (comment >= 0 && (cdata < 0 || comment < cdata)) {
            special = comment;
            isComment = true;
        } else if (cdata >= 0) {
            special = cdata;
        }
        const int segmentEnd = special < 0 ? int(rest.size()) : special;
        const QString segment = rest.mid(i, segmentEnd - i);
        const int base = start + i;
        auto shifted = [&](const QRegularExpression &re, const QTextCharFormat &format, int group = 0) {
            QRegularExpressionMatchIterator it = re.globalMatch(segment);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                setFormat(base + int(m.capturedStart(group)), int(m.capturedLength(group)), format);
            }
        };
        shifted(bracket, m_punct);
        shifted(entity, m_number);
        shifted(tag, m_tag, 1);
        shifted(attribute, m_attribute, 1);
        shifted(value, m_string);
        shifted(declaration, m_comment);
        if (special < 0) {
            break;
        }
        const QString closing = isComment ? QStringLiteral("-->") : QStringLiteral("]]>");
        const int end = rest.indexOf(closing, special + 3);
        if (end < 0) {
            setFormat(start + special, int(rest.size()) - special, isComment ? m_comment : m_code);
            setCurrentBlockState(isComment ? kXmlComment : kXmlCdata);
            return;
        }
        setFormat(start + special, end + 3 - special, isComment ? m_comment : m_code);
        i = end + 3;
    }
}

void DocHighlighter::highlightMarkdown(const QString &text)
{
    static const QRegularExpression fence(QStringLiteral("^\\s*(```|~~~)"));
    static const QRegularExpression heading(QStringLiteral("^#{1,6}\\s.*$"));
    static const QRegularExpression bold(QStringLiteral("(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)\\1"));
    static const QRegularExpression italic(QStringLiteral("(?<![*\\w])([*_])(?=\\S)(.+?)(?<=\\S)\\1(?![*\\w])"));
    static const QRegularExpression inlineCode(QStringLiteral("`[^`]+`"));
    static const QRegularExpression link(QStringLiteral("!?\\[[^\\]]*\\]\\([^)]*\\)"));
    static const QRegularExpression list(QStringLiteral("^\\s*(?:[-*+]|\\d+[.)])\\s"));
    static const QRegularExpression quote(QStringLiteral("^\\s*>+"));
    static const QRegularExpression rule(QStringLiteral("^\\s*(?:---+|\\*\\*\\*+|___+)\\s*$"));

    const bool inFence = previousBlockState() == kFence;
    if (fence.match(text).hasMatch()) {
        setFormat(0, int(text.size()), m_code);
        setCurrentBlockState(inFence ? kNone : kFence);
        return;
    }
    if (inFence) {
        setFormat(0, int(text.size()), m_code);
        setCurrentBlockState(kFence);
        return;
    }
    if (heading.match(text).hasMatch()) {
        setFormat(0, int(text.size()), m_heading);
        return;
    }
    apply(link, text, m_link);
    apply(bold, text, m_emphasis);
    apply(italic, text, m_emphasis);
    apply(inlineCode, text, m_code);
    apply(list, text, m_marker);
    apply(quote, text, m_marker);
    apply(rule, text, m_punct);
}

} // namespace kai::ui
