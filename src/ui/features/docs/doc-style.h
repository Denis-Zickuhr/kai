#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QString>

class QTextDocument;

namespace kai::ui {

// Aparência do markdown no leitor (DocViewer). O importador de markdown do Qt entrega um QTextDocument "cru" (fonte do
// sistema, sem espaçamento, código sem fundo); styleMarkdownDocument() percorre os blocos e dá o visual do Kai:
// títulos grandes, parágrafos arejados, código com fundo, citações com faixa, tabelas com cabeçalho e listras, links na
// cor de destaque. Não toca no texto, só nos formatos.
struct DocTheme {
    QFont bodyFont;
    QFont codeFont;
    QColor text{QStringLiteral("#e6e8ef")};
    QColor muted{QStringLiteral("#9aa0b4")};
    QColor accent{QStringLiteral("#8b9bff")};
    QColor heading{QStringLiteral("#ffffff")};
    QColor codeBackground{QStringLiteral("#1b1e29")};
    QColor codeText{QStringLiteral("#d6d9e6")};
    QColor inlineCodeBackground{QStringLiteral("#242839")};
    QColor quoteBackground{QStringLiteral("#1d2030")};
    QColor border{QStringLiteral("#2f3347")};
    QColor tableHeaderBackground{QStringLiteral("#222638")};
    QColor tableStripeBackground{QStringLiteral("#191c28")};
    QColor link{QStringLiteral("#8b9bff")};
    int margin = 28;
};

// Aplica o visual ao documento recém-importado (setMarkdown). Devolve, por âncora estilo GitHub ("minha-secao", com -1/-2
// nos repetidos), a posição do título no documento — o leitor usa para os links `#secao`.
QHash<QString, int> styleMarkdownDocument(QTextDocument *document, const DocTheme &theme);

// A âncora de um título como o GitHub: minúsculas, sem pontuação, espaços viram hífen.
QString headingSlug(const QString &heading);

} // namespace kai::ui
