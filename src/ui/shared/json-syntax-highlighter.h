#pragma once

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QVector>

namespace kai::ui {

// Realce de sintaxe JSON (chaves, strings, números, booleanos/null, pontuação) com paleta Dracula-like, independente do QSS.
class JsonSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit JsonSyntaxHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<Rule> m_rules;
};

} // namespace kai::ui
