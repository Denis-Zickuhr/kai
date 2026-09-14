#pragma once

#include <QWidget>

#include <functional>

class QMouseEvent;
class QScrollBar;
class QTabBar;

namespace kai::ui {

// Barra de rolagem PRÓPRIA para faixas que não cabem (abas de pastas raiz,
// cabeçalho da Saída): uma trilha fina na cor de destaque do tema (levemente
// translúcida) com uma alça que mostra quanto está visível e onde. Só existe
// enquanto há overflow (escondida senão) e é ARRASTÁVEL: arrastar a alça ou
// clicar na trilha rola o conteúdo. É uma camada sobre o widget hospedeiro, sem
// ocupar espaço no layout.
class OverflowIndicator : public QWidget {
    // Q_OBJECT: ver PanelFrame (evita o fundo global de QWidget por cima).
    Q_OBJECT

public:
    // Devolve false quando não há o que medir. offset = quanto já rolou,
    // viewport = largura visível, content = largura total do conteúdo.
    using Metrics = std::function<bool(int &offset, int &viewport, int &content)>;
    // Rola o conteúdo até o deslocamento pedido (em pixels do conteúdo).
    using ScrollTo = std::function<void(int offset)>;

    OverflowIndicator(QWidget *host, Metrics metrics, ScrollTo scrollTo = {});

    // Medições e rolagem prontas: abas com botões de rolagem do Qt / barra de
    // rolagem (mesmo escondida) de um QScrollArea.
    static Metrics forTabBar(QTabBar *bar);
    static ScrollTo scrollToForTabBar(QTabBar *bar);
    static Metrics forScrollBar(QScrollBar *bar);
    static ScrollTo scrollToForScrollBar(QScrollBar *bar);

    // Espessura da faixa (também a área clicável).
    static int thickness();

    // Recuo horizontal dentro do hospedeiro (a Saída tem margem no cabeçalho).
    void setInsets(int left, int right);
    // Sem âncora: distância até a borda de baixo do hospedeiro. Com âncora:
    // distância abaixo dela.
    void setBottomInset(int inset);
    // Posiciona a faixa logo ABAIXO de um filho direto do hospedeiro (ex.: a
    // barra de abas de um QTabWidget), com a largura dele.
    void setAnchor(QWidget *anchor);

    // Suprimida, a faixa NUNCA aparece, mesmo com overflow: o hospedeiro está
    // recolhido (a linha de abas some) e o "overflow" é só do espaço mínimo.
    void setSuppressed(bool suppressed);
    // O conteúdo passa da largura visível agora?
    bool overflowing() const;
    // Reposiciona, mostra/esconde conforme há overflow e repinta.
    void refresh();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct ThumbLayout {
        int x = 0;
        int width = 0;
        int maxOffset = 0;
    };
    bool thumbLayout(ThumbLayout &out) const;
    void reposition();
    void dragTo(int x);

    QWidget *m_host;
    QWidget *m_anchor = nullptr;
    Metrics m_metrics;
    ScrollTo m_scrollTo;
    int m_insetLeft = 0;
    int m_insetRight = 0;
    int m_bottomInset = 0;
    bool m_dragging = false;
    bool m_suppressed = false;
    int m_grabOffset = 0;
};

} // namespace kai::ui
