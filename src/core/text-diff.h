#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace kai::core {

// Diff de texto puro (sem widget): o modelo que a DiffView desenha, o diff linha a linha de dois textos (Myers) e o
// destaque das palavras que mudaram dentro de uma linha. O parser de `git diff` vive em unified-diff.h.

// Trecho [start, start + length) de uma linha que mudou (destaque por palavra).
struct DiffSpan {
    int start = 0;
    int length = 0;
    bool operator==(const DiffSpan &o) const { return start == o.start && length == o.length; }
};

struct DiffLine {
    enum class Kind { Context, Added, Removed, Fold };
    Kind kind = Kind::Context;
    int oldNo = 0; // 0 = a linha não existe daquele lado
    int newNo = 0;
    QString text;
    QVector<DiffSpan> spans; // Added/Removed: as partes que mudaram em relação ao par do outro lado
    // Fold: linhas iguais escondidas (a view abre a dobra mostrando-as).
    QVector<DiffLine> folded;
};

struct DiffHunk {
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
    QString section; // o texto depois do "@@ ... @@" (nome da função, em geral)
    QVector<DiffLine> lines;
    // "@@ -18,8 +18,11 @@"
    QString header() const;
};

enum class DiffFileStatus { Modified, Added, Deleted, Renamed };

struct DiffFile {
    QString oldPath;
    QString newPath;
    DiffFileStatus status = DiffFileStatus::Modified;
    bool binary = false;
    // Diff do arquivo inteiro (dois textos): um trecho só, com as linhas iguais longas em dobras e sem cabeçalho "@@".
    bool wholeFile = false;
    QVector<DiffHunk> hunks;
    int additions = 0;
    int deletions = 0;

    // Caminho que identifica o arquivo: o novo (ou o antigo, num arquivo apagado).
    QString path() const;
    // Id de um trecho na resposta do campo `diff`: "<caminho>@<índice>".
    QString hunkId(int index) const;
    void recount();
};

QString diffFileStatusLetter(DiffFileStatus status); // M A D R

// Quebra em linhas: "\r\n" e "\n"; a quebra final não gera uma linha vazia a mais.
QStringList diffSplitLines(const QString &text);

// Edição mínima (Myers) entre duas sequências já reduzidas a números (linhas ou palavras).
enum class DiffOp { Equal, Delete, Insert };
struct DiffEdit {
    DiffOp op = DiffOp::Equal;
    int oldIndex = -1; // Equal/Delete
    int newIndex = -1; // Equal/Insert
};
// Acima de `maxCost` passos de edição o algoritmo desiste e devolve "apaga o resto, insere o resto" no trecho do meio
// (o diff continua correto, só deixa de ser mínimo) — o custo de memória é quadrático no número de edições.
QVector<DiffEdit> diffSequences(const QVector<int> &a, const QVector<int> &b, int maxCost = 4000);

// Trechos que mudaram entre uma linha removida e a que entrou no lugar dela. Vazio quando as linhas são diferentes
// demais (destacar quase tudo só faz barulho).
void diffWordSpans(const QString &oldLine, const QString &newLine, QVector<DiffSpan> *oldSpans,
                   QVector<DiffSpan> *newSpans);
// Emparelha cada bloco de linhas removidas com as adicionadas logo depois (i-ésima com i-ésima) e preenche os spans.
void annotateWordChanges(DiffHunk &hunk);

// Diff de dois textos inteiros: as mudanças com `context` linhas em volta e o restante das linhas iguais em dobras
// (DiffLine::Fold), para a view mostrar o arquivo todo. `ignoreWhitespace` compara as linhas sem espaços.
DiffFile diffTexts(const QString &left, const QString &right, int context = 3, bool ignoreWhitespace = false,
                   const QString &leftName = QString(), const QString &rightName = QString());

} // namespace kai::core
