#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QString>

class QTextBlock;
class QTextDocument;

namespace kai::ui {

// Aparência do markdown no leitor (DocViewer). O importador de markdown do Qt entrega um QTextDocument "cru" (fonte do
// sistema, sem espaçamento, código sem fundo); styleMarkdownDocument() percorre os blocos e dá o visual do Kai:
// títulos grandes, parágrafos arejados, código, citações, tabelas com cabeçalho e listras, links na cor de destaque. Não
// toca no texto, só nos formatos. Fundos e contornos (que precisam de canto arredondado) NÃO ficam nos formatos: são
// desenhados por doc-decorations.
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
    QColor buttonHover{QStringLiteral("#2a2f44")};   // fundo do botão de copiar sob o mouse
    QColor success{QStringLiteral("#4ade80")};       // o "✓" depois de copiar
    QColor info{QStringLiteral("#60a5fa")};          // aviso `> [!NOTE]`
    QColor warning{QStringLiteral("#fbbf24")};       // `> [!WARNING]`
    QColor error{QStringLiteral("#f87171")};         // `> [!CAUTION]`
    QColor page{QStringLiteral("#1f2230")};          // a "folha" onde o texto fica (coluna de leitura)
    QColor outer{QStringLiteral("#171923")};         // o fundo ao redor da folha
    int margin = 28;
};

// Respiro interno (px) das caixas desenhadas por doc-decorations: o texto do bloco de código / da citação fica a esta
// distância do contorno de cima e de baixo.
constexpr int kDocCodePadding = 12;
constexpr int kDocQuotePadding = 8;

// Avisos no estilo do GitHub: um bloco de citação cuja primeira linha é `[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]` ou
// `[!CAUTION]`. styleMarkdownDocument() troca o marcador por um rótulo colorido e anota o tipo no formato do texto (esta
// propriedade); doc-decorations pinta a caixa na cor do tipo.
constexpr int kDocCalloutProperty = 0x100000 + 7; // QTextFormat::UserProperty + 7

// Prepara o markdown para o importador do Qt: separa blocos cercados que estão um logo depois do outro.
QString separateAdjacentFences(const QString &markdown);

// O bloco faz parte de um bloco de código (cercado ou recuado)?
bool isDocCodeBlock(const QTextBlock &block);

// Aplica o visual ao documento recém-importado (setMarkdown). Devolve, por âncora estilo GitHub ("minha-secao", com -1/-2
// nos repetidos), a posição do título no documento — o leitor usa para os links `#secao`.
QHash<QString, int> styleMarkdownDocument(QTextDocument *document, const DocTheme &theme);

// A âncora de um título como o GitHub: minúsculas, sem pontuação, espaços viram hífen.
QString headingSlug(const QString &heading);

} // namespace kai::ui
