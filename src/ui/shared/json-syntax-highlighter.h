#pragma once

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QVector>

namespace kai::ui {

// Realce de sintaxe JSON simples para um QTextDocument (editor de body).
// Colore chaves, strings, números, booleanos/null e pontuação. Cores
// pensadas para tema escuro (paleta Dracula-like), independentes do QSS.
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
