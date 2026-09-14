#include "ui/shared/json-syntax-highlighter.h"

namespace kai::ui {

JsonSyntaxHighlighter::JsonSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // Chaves de objeto: "chave" seguida de ':'
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\"(?=\\s*:)"));
        r.format.setForeground(QColor(0x8b, 0xe9, 0xfd)); // ciano
        r.format.setFontWeight(QFont::DemiBold);
        m_rules.append(r);
    }
    // Strings (valores): qualquer "..." (aplicado depois das chaves; a
    // ordem no highlightBlock resolve a sobreposição — chave vence por vir
    // antes, mas como usamos formatos por range, aplicamos strings gerais
    // primeiro e chaves depois para as chaves prevalecerem).
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\""));
        r.format.setForeground(QColor(0xf1, 0xfa, 0x8c)); // amarelo
        m_rules.append(r);
    }
    // Números.
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("-?\\b\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b"));
        r.format.setForeground(QColor(0xbd, 0x93, 0xf9)); // roxo
        m_rules.append(r);
    }
    // Booleanos e null.
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("\\b(true|false|null)\\b"));
        r.format.setForeground(QColor(0xff, 0x79, 0xc6)); // rosa
        r.format.setFontWeight(QFont::DemiBold);
        m_rules.append(r);
    }
    // Pontuação estrutural.
    {
        Rule r;
        r.pattern = QRegularExpression(QStringLiteral("[\\{\\}\\[\\]:,]"));
        r.format.setForeground(QColor(0x62, 0x72, 0xa4)); // cinza-azulado
        m_rules.append(r);
    }
}

void JsonSyntaxHighlighter::highlightBlock(const QString &text)
{
    // Aplica na ordem: strings-gerais, depois chaves (para chave prevalecer),
    // e por fim números/bool/pontuação nos trechos não-string. Para manter
    // simples e robusto, aplicamos números/bool/pontuação primeiro, depois
    // strings e por último chaves — assim texto dentro de aspas não é
    // recolorido por regras de número/pontuação.
    // Ordem reversa da lista de rules (pontuação -> bool -> número -> string -> chave):
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
