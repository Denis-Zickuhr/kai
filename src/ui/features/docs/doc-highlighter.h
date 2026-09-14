#pragma once

#include "ui/features/docs/doc-text-tools.h"

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

namespace kai::ui {

// Realce de sintaxe do editor de documentos: JSON, YAML, XML e Markdown. As cores vêm dos tokens do tema (nada fixo), e
// `refresh()` as reaplica quando o tema muda. Texto simples não ganha realce.
class DocHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit DocHighlighter(QTextDocument *document, texttools::Language language = texttools::Language::Text);

    void setLanguage(texttools::Language language);
    texttools::Language language() const { return m_language; }
    // Recalcula as cores a partir do tema e realça de novo.
    void refresh();

protected:
    void highlightBlock(const QString &text) override;

private:
    void buildFormats();
    void highlightJson(const QString &text);
    void highlightYaml(const QString &text);
    void highlightXml(const QString &text);
    void highlightMarkdown(const QString &text);
    void apply(const QRegularExpression &pattern, const QString &text, const QTextCharFormat &format, int group = 0);

    texttools::Language m_language;
    QTextCharFormat m_key, m_string, m_number, m_literal, m_punct, m_comment, m_tag, m_attribute, m_heading, m_code,
        m_link, m_emphasis, m_marker, m_anchor;
};

} // namespace kai::ui
