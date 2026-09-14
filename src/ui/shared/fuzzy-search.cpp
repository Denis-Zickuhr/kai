#include "ui/shared/fuzzy-search.h"
#include "utils/translation-manager.h"

#include <algorithm>

namespace kai::ui {

namespace {
bool isSeparator(QChar c)
{
    return c == QLatin1Char(' ') || c == QLatin1Char('-') ||
           c == QLatin1Char('_') || c == QLatin1Char('/');
}
}

int FuzzyMatcher::score(const QString &query, const QString &target)
{
    if (query.isEmpty()) {
        return 0;
    }
    if (target.isEmpty()) {
        return -1;
    }

    const QString lowerQuery = query.toLower();
    const QString lowerTarget = target.toLower();

    int queryIdx = 0;
    int totalScore = 0;
    int consecutiveRun = 0;
    bool previousMatched = false;

    for (int targetIdx = 0; targetIdx < lowerTarget.size() && queryIdx < lowerQuery.size(); ++targetIdx) {
        if (lowerTarget.at(targetIdx) != lowerQuery.at(queryIdx)) {
            previousMatched = false;
            consecutiveRun = 0;
            continue;
        }

        int charScore = 10;

        if (previousMatched) {
            consecutiveRun++;
            charScore += consecutiveRun * 5; // matches contíguos valem mais
        } else {
            consecutiveRun = 0;
        }

        if (targetIdx == 0 || isSeparator(lowerTarget.at(targetIdx - 1))) {
            charScore += 15; // bônus de prefixo / início de palavra
        }

        totalScore += charScore;
        previousMatched = true;
        ++queryIdx;
    }

    if (queryIdx < lowerQuery.size()) {
        // Nem todos os caracteres da query foram encontrados em ordem.
        return -1;
    }

    // Penaliza targets muito mais longos que a query (prioriza matches
    // "mais específicos" sobre strings enormes com o mesmo conjunto de
    // caracteres espalhado).
    const int lengthPenalty = lowerTarget.size() - lowerQuery.size();
    totalScore -= lengthPenalty;

    return totalScore;
}

QVector<FuzzyMatchResult> FuzzyMatcher::search(const QString &query, const QStringList &candidates)
{
    QVector<FuzzyMatchResult> results;

    if (query.isEmpty()) {
        results.reserve(candidates.size());
        for (int i = 0; i < candidates.size(); ++i) {
            results.append({candidates.at(i), 0, i});
        }
        return results;
    }

    for (int i = 0; i < candidates.size(); ++i) {
        const int s = score(query, candidates.at(i));
        if (s >= 0) {
            results.append({candidates.at(i), s, i});
        }
    }

    std::sort(results.begin(), results.end(), [](const FuzzyMatchResult &a, const FuzzyMatchResult &b) {
        return a.score > b.score;
    });

    return results;
}

FuzzySearchBar::FuzzySearchBar(QWidget *parent)
    : QLineEdit(parent)
{
    // Placeholder simples; sem ícone de lupa (feedback do
    // usuário: "remova a lupa do pesquisar"). O campo fica oculto por
    // padrão e é exibido/focado via atalho (Ctrl+F), controlado pela
    // MainWindow.
    setPlaceholderText(utils::tr(QStringLiteral("fuzzy_search.placeholder")));
    setClearButtonEnabled(true);
    connect(this, &QLineEdit::textChanged, this, &FuzzySearchBar::handleTextChanged);
}

void FuzzySearchBar::handleTextChanged(const QString &text)
{
    emit queryChanged(text);
}

void FuzzySearchBar::keyPressEvent(QKeyEvent *event)
{
    // Navegação por teclado: seta para baixo a partir da busca
    // transfere o foco para a árvore, replicando o comportamento familiar
    // de launchers como CopyQ/Spotlight (digitar e já navegar resultados
    // sem precisar clicar).
    if (event->key() == Qt::Key_Down) {
        emit navigateToListRequested();
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

} // namespace kai::ui
