#include "ui/shared/json-syntax-highlighter.h"

namespace kai::ui {

JsonSyntaxHighlighter::JsonSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\"(?=\\s*:)"));
        r.format.setForeground(QColor(0x8b, 0xe9, 0xfd)); // ciano
        r.format.setFontWeight(QFont::DemiBold);
        m_rules.append(r);
    }
    // Strings (valores): qualquer "...".
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\""));
        r.format.setForeground(QColor(0xf1, 0xfa, 0x8c)); // amarelo
        m_rules.append(r);
    }
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("-?\\b\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b"));
        r.format.setForeground(QColor(0xbd, 0x93, 0xf9)); // roxo
        m_rules.append(r);
    }
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("\\b(true|false|null)\\b"));
        r.format.setForeground(QColor(0xff, 0x79, 0xc6)); // rosa
        r.format.setFontWeight(QFont::DemiBold);
        m_rules.append(r);
    }
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("[\\{\\}\\[\\]:,]"));
        r.format.setForeground(QColor(0x62, 0x72, 0xa4)); // cinza-azulado
        m_rules.append(r);
    }
}

void JsonSyntaxHighlighter::highlightBlock(const QString &text)
{
    // Aplica as rules em ordem reversa (pontuação -> bool -> número -> string -> chave): texto dentro de aspas
    // não é recolorido por número/pontuação e as chaves prevalecem.
    for (int i = m_rules.size() - 1; i >= 0; --i) {
        const Rule &rule = m_rules.at(i);
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }
}

} // namespace kai::ui
