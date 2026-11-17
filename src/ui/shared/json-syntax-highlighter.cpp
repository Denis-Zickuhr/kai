#include "ui/shared/json-syntax-highlighter.h"

#include "utils/design-tokens.h"

namespace kai::ui {

JsonSyntaxHighlighter::JsonSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    m_rules.append({QRegularExpression(QStringLiteral("\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\"(?=\\s*:)")), Kind::Key});
    m_rules.append({QRegularExpression(QStringLiteral("\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\"")), Kind::String});
    m_rules.append({QRegularExpression(QStringLiteral("-?\\b\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b")), Kind::Number});
    m_rules.append({QRegularExpression(QStringLiteral("\\b(true|false|null)\\b")), Kind::Keyword});
    m_rules.append({QRegularExpression(QStringLiteral("[\\{\\}\\[\\]:,]")), Kind::Punctuation});
}

void JsonSyntaxHighlighter::highlightBlock(const QString &text)
{
    // Aplica as rules em ordem reversa (pontuação -> bool -> número -> string -> chave): texto dentro de aspas
    // não é recolorido por número/pontuação e as chaves prevalecem.
    const QColor background(utils::tokens::codeBg());
    const auto format = [&background](QColor base, bool bold) {
        QTextCharFormat f;
        f.setForeground(utils::tokens::ensureContrast(base, background));
        if (bold) {
            f.setFontWeight(QFont::DemiBold);
        }
        return f;
    };
    const QTextCharFormat key = format(QColor(0x8b, 0xe9, 0xfd), true);
    const QTextCharFormat string = format(QColor(0xf1, 0xfa, 0x8c), false);
    const QTextCharFormat number = format(QColor(0xbd, 0x93, 0xf9), false);
    const QTextCharFormat keyword = format(QColor(0xff, 0x79, 0xc6), true);
    const QTextCharFormat punctuation = format(QColor(0x62, 0x72, 0xa4), false);
    for (int i = m_rules.size() - 1; i >= 0; --i) {
        const Rule &rule = m_rules.at(i);
        const QTextCharFormat &fmt = rule.kind == Kind::Key ? key : rule.kind == Kind::String ? string
            : rule.kind == Kind::Number ? number : rule.kind == Kind::Keyword ? keyword : punctuation;
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), fmt);
        }
    }
}

} // namespace kai::ui
