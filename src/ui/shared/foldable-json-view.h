#pragma once

#include <QPlainTextEdit>
#include <QVector>

namespace kai::ui {

// ============================================================================
// Visualizador de JSON em TEXTO LIVRE com colapso inline (estilo JetBrains)
// ----------------------------------------------------------------------------
// Substitui a antiga árvore (QTreeWidget) do JsonViewerWidget — feedback do
// usuário: "a saída json queria algo mais texto livre, estilo retorno do
// insomnia ou postman [...] quero um editor de json mais bonito e fluido e
// com ações de colapsar inline, tipo o do jetbrains".
//
// Mecânica de colapso: usamos QTextBlock::setVisible(false) nos blocos
// (linhas) ESTRITAMENTE ENTRE o início e o fim de um par de chaves/colchetes
// — essa é a forma canônica do Qt de implementar folding num QPlainTextEdit
// (documentada nos exemplos oficiais "Code Editor"). Não reconstruímos nem
// reparseamos o texto ao colapsar/expandir: só alternamos a visibilidade dos
// blocos já existentes e pedimos um relayout do documento.
//
// O texto exibido é o JSON já PRETTY-PRINTED pelo QJsonDocument::Indented
// (4 espaços por nível, chave/colchete de abertura sozinho no fim da linha,
// fechamento sozinho no início da linha seguinte no mesmo nível) — esse
// formato É a premissa que torna o cálculo dos pares de colapso trivial: uma
// pilha simples entre linhas que TERMINAM em '{'/'[' e linhas que COMEÇAM
// com '}'/']' já basta (ver setFoldableJsonText no .cpp).
class FoldableJsonView : public QPlainTextEdit {
public:
    explicit FoldableJsonView(QWidget *parent = nullptr);

    // Define o texto já formatado (JSON pretty-print) e recalcula os pares
    // de colapso. `autoCollapseLineThreshold`: pares cujo span (nº de linhas
    // ocultáveis) ultrapasse esse limite começam COLAPSADOS — evita que uma
    // resposta gigante (ex: array com centenas de itens) fique ilegível de
    // cara; pares menores começam todos EXPANDIDOS (pedido explícito do
    // usuário: "texto livre", não a árvore pré-colapsada de antes). Passe 0
    // para desligar o auto-colapso (tudo expandido).
    void setFoldableJsonText(const QString &prettyJson, int autoCollapseLineThreshold = 40);

    // Define texto cru, SEM cálculo de dobras (fallback de JSON inválido —
    // mostra o texto tal como veio, sem colapso nem realce estrutural).
    void setRawText(const QString &text);

    // Expande/colapsa TODOS os pares de uma vez (botões "Expandir/Colapsar
    // tudo" da barra de ações).
    void expandAllFolds();
    void collapseAllFolds();

    // Alterna o par cujo bloco INICIAL é `startBlockNumber`. Não faz nada se
    // não houver um par começando ali (chamador deve ter validado via
    // foldStartAtBlock). Exposto para o gutter (clique) e para os testes.
    void toggleFold(int startBlockNumber);

    // true se o bloco `blockNumber` é o INÍCIO de um par dobrável — usado
    // pelo gutter para decidir se desenha o marcador naquela linha, e pelos
    // testes para simular o clique.
    bool foldStartAtBlock(int blockNumber) const;
    // true se o par que começa em `startBlockNumber` está colapsado no
    // momento (para o gutter escolher o ícone: chevron-right x chevron-down).
    bool isFoldCollapsed(int startBlockNumber) const;

    // Cores do gutter (tokens do tema — trocam com o tema/claro-escuro).
    void setGutterColors(const QColor &background, const QColor &markerColor);

    // Chamados pelo widget filho do gutter (FoldGutter, anônimo no .cpp) —
    // públicos em vez de `friend` porque a classe amiga vive num namespace
    // anônimo local ao .cpp, o que tornaria a declaração `friend` ambígua
    // com qualquer FoldGutter de outro escopo.
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
