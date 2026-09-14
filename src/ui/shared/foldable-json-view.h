#pragma once

#include <QPlainTextEdit>
#include <QVector>

namespace kai::ui {

// Visualizador de JSON em texto livre com colapso inline (estilo JetBrains). O colapso alterna QTextBlock::
// setVisible(false) nas linhas estritamente entre abertura e fechamento de um par, sem reparsear o texto.
// Premissa: o texto é o pretty-print do QJsonDocument::Indented (abertura termina em '{'/'[', fechamento
// começa com '}'/']'), o que permite parear com uma pilha simples (ver setFoldableJsonText no .cpp).
class FoldableJsonView : public QPlainTextEdit {
public:
    explicit FoldableJsonView(QWidget *parent = nullptr);

    // Define o texto já formatado (pretty-print) e recalcula os pares de colapso. Pares com span (linhas
    // ocultáveis) acima de `autoCollapseLineThreshold` começam colapsados; os demais, expandidos. 0 desliga o auto-colapso.
    void setFoldableJsonText(const QString &prettyJson, int autoCollapseLineThreshold = 40);

    // Texto cru, sem cálculo de dobras (fallback de JSON inválido).
    void setRawText(const QString &text);

    // Expande/colapsa todos os pares.
    void expandAllFolds();
    void collapseAllFolds();

    // Alterna o par cujo bloco inicial é `startBlockNumber`; não faz nada se não houver par ali.
    void toggleFold(int startBlockNumber);

    // true se `blockNumber` inicia um par dobrável (o gutter decide se desenha o marcador).
    bool foldStartAtBlock(int blockNumber) const;
    // true se o par que começa em `startBlockNumber` está colapsado (escolhe o ícone do gutter).
    bool isFoldCollapsed(int startBlockNumber) const;

    void setGutterColors(const QColor &background, const QColor &markerColor);

    // Chamados pelo FoldGutter (anônimo no .cpp): públicos em vez de `friend`, que ficaria ambíguo com um
    // FoldGutter de outro escopo.
    void paintGutter(QPaintEvent *event);
    void handleGutterClick(const QPoint &gutterPos);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    struct FoldRange {
        int startBlock = -1;
        int endBlock = -1;   // linha do fechamento correspondente
        bool collapsed = false;
    };

    int gutterWidth() const;
    void updateGutterWidth();
    void updateGutterArea(const QRect &rect, int dy);
    void applyVisibility(const FoldRange &range);
    int rangeIndexStartingAt(int startBlockNumber) const;

    QWidget *m_gutter = nullptr;
    QVector<FoldRange> m_ranges;
    QColor m_gutterBg;
    QColor m_gutterMarkerColor;
};

} // namespace kai::ui
