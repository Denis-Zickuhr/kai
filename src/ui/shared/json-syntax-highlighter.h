#pragma once

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QVector>

namespace kai::ui {

// Realce de sintaxe JSON (chaves, strings, números, booleanos/null, pontuação) com paleta Dracula-like ajustada ao contraste do fundo de código do tema (legível em temas claros).
class JsonSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit JsonSyntaxHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    enum class Kind { Key, String, Number, Keyword, Punctuation };
    struct Rule {
        QRegularExpression pattern;
        Kind kind;
    };
    QVector<Rule> m_rules;
};

} // namespace kai::ui
