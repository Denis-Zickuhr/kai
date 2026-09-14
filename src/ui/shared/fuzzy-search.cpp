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
        return -1;
    }

    // Penaliza targets bem mais longos que a query.
    const int lengthPenalty = lowerTarget.size() - lowerQuery.size();
    totalScore -= lengthPenalty;

    // O contrato é "score >= 0 sempre que houver match": a penalidade só ordena, nunca descarta (uma letra num nome
    // longo dava negativo e sumia da busca).
    return std::max(0, totalScore);
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
    // Sem ícone de lupa; o campo fica oculto e é exibido/focado via Ctrl+F pela MainWindow.
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
    // Seta para baixo transfere o foco para a árvore (como CopyQ/Spotlight).
    if (event->key() == Qt::Key_Down) {
        emit navigateToListRequested();
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

} // namespace kai::ui
