#pragma once

#include "ui/features/docs/doc-style.h"

#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;
class QTextDocument;

namespace kai::ui {

// O QTextDocument só sabe pintar fundos retos. Tudo que precisa de canto arredondado (blocos de código, citações,
// tabelas, código em linha) é desenhado POR TRÁS do texto pelo leitor, com os raios dos tokens (preferência de cantos do
// usuário). styleMarkdownDocument() deixa esses fundos transparentes; collectDocDecorations() anota ONDE eles vão e
// paintDocDecorations() desenha, só na parte visível.
struct DocDecoration {
    enum class Kind { Code, Quote, Table, Heading };
    Kind kind = Kind::Code;
    int first = 0;        // posição do primeiro bloco (Table: a posição da tabela)
    int last = 0;         // posição do último bloco do grupo
    int level = 0;        // Quote: nível de aninhamento; Heading: 1 ou 2
    QString text;         // Code: o texto inteiro (o que o botão de copiar leva)
    QString language;     // Code: a linguagem da cerca, se houver
    QString callout;      // Quote: note, tip, important, warning ou caution (aviso estilo GitHub); vazio = citação comum
};

// Estado de interação do leitor (qual bloco de código está sob o mouse, em qual botão, qual acabou de ser copiado).
struct DocDecorationState {
    int hoveredCode = -1;     // índice em `decorations`
    bool overButton = false;
    int copiedCode = -1;
};

QVector<DocDecoration> collectDocDecorations(QTextDocument *document);

// O retângulo (coordenadas do documento) da caixa de um bloco de código e o do botão de copiar dentro dela.
QRectF docDecorationRect(QTextDocument *document, const DocDecoration &decoration);
QRectF docCopyButtonRect(const QRectF &codeBox);

// `visible` em coordenadas do documento. O painter já está com a rolagem aplicada.
void paintDocDecorations(QPainter *painter, QTextDocument *document, const QVector<DocDecoration> &decorations,
                         const DocTheme &theme, const QRectF &visible, const DocDecorationState &state);

} // namespace kai::ui
