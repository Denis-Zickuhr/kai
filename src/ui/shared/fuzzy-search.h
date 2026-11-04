#pragma once

#include <QLineEdit>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QKeyEvent>

namespace kai::ui {

// Resultado de um fuzzy match: item original + score (maior = melhor match).
struct FuzzyMatchResult {
    QString text;
    int score = 0;
    int originalIndex = -1;
};

// Algoritmo de fuzzy match por subsequência (estilo VSCode/fzf), puro e sem
// dependência de widgets — testável isoladamente.
//
// Regras de score:
// - Cada caractere da query que aparece em ordem no target soma pontos.
// - Matches consecutivos (substring contígua) valem mais que espalhados.
// - Match no início da string ou logo após separador (' ', '-', '_', '/')
//   ganha bônus, priorizando prefixos.
// - Case-insensitive.
class FuzzyMatcher {
public:
    // Score < 0 quando a query não é subsequência de target; >= 0 sempre que houver match.
    static int score(const QString &query, const QString &target);

    // Filtra e ordena por relevância decrescente; candidatos sem match são omitidos.
    static QVector<FuzzyMatchResult> search(const QString &query, const QStringList &candidates);
};

// Barra de busca fuzzy; o debounce do queryChanged é responsabilidade do consumidor (CommandTreeWidget).
class FuzzySearchBar : public QLineEdit {
    Q_OBJECT

public:
    explicit FuzzySearchBar(QWidget *parent = nullptr);

signals:
    void queryChanged(const QString &query);

    // Seta para baixo com o campo focado: o consumidor (MainWindow) move o foco para a árvore e seleciona o primeiro item visível.
    void navigateToListRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void handleTextChanged(const QString &text);
};

} // namespace kai::ui
