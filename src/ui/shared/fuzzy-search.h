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
    // Retorna std::nullopt-like (score < 0) quando a query não é uma
    // subsequência de target. Score >= 0 sempre que houver match.
    static int score(const QString &query, const QString &target);

    // Filtra e ordena `candidates` por relevância decrescente. Candidatos
    // sem match são omitidos do resultado.
    static QVector<FuzzyMatchResult> search(const QString &query, const QStringList &candidates);
};

// Barra de busca fuzzy: QLineEdit que emite queryChanged a cada
// digitação, já debounced no nível de UI é responsabilidade do consumidor
// (CommandTreeWidget) para não acoplar lógica de filtro aqui.
class FuzzySearchBar : public QLineEdit {
    Q_OBJECT

public:
    explicit FuzzySearchBar(QWidget *parent = nullptr);

signals:
    void queryChanged(const QString &query);

    // Emitido quando o usuário pressiona a seta para baixo com o campo de
    // busca focado (navegação por teclado): o consumidor
    // (MainWindow) deve mover o foco para a árvore de comandos e
    // selecionar o primeiro item visível, replicando o comportamento
    // familiar de launchers como CopyQ/Spotlight.
    void navigateToListRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void handleTextChanged(const QString &text);
};

} // namespace kai::ui
