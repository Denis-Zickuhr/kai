#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

#include "ui/layout/drop-overlay.h"
#include "ui/layout/floating-view-window.h"
#include "ui/layout/layout-model.h"
#include "ui/layout/view-registry.h"

class QFrame;
class QHBoxLayout;
class QMenu;
class QScrollArea;
class QSplitter;
class QVBoxLayout;

namespace kai::ui::layout {

class ViewZone;
class ZoneRail;

// O quadro da janela: monta a grade de células (colunas × linhas) com QSplitters a partir do plano de um LayoutModel e
// cuida de arrastar, fechar, flutuar e maximizar painéis. O quadro vive numa área de rolagem: com a janela menor que o
// tamanho confortável das células, ele rola em vez de espremer o conteúdo. Os painéis são criados UMA vez (pelo registro) e reparentados:
// mover nunca recria nem perde estado (uma execução em andamento segue viva).
//
// O host não conhece nenhum painel concreto. Quem muda o layout pela interface passa por aqui, e cada mudança do
// usuário emite `modelEdited()` para o dono gravar o layout.json (com debounce).
class DockHost : public QWidget {
    Q_OBJECT

public:
    // `floatParent`: a janela dona das flutuantes (esconder o Kai esconde elas).
    DockHost(ViewRegistry *registry, QWidget *floatParent, QWidget *parent = nullptr);
    ~DockHost() override;

    // Aplica um layout (já reparado). Não emite modelEdited.
    void setModel(const LayoutModel &model);
    const LayoutModel &model() const { return m_model; }
    ViewRegistry *registry() const { return m_registry; }

    // --- operações do usuário (todas emitem modelEdited quando mudam algo)
    // Abre o painel se estava fechado, ativa a aba e tira um maximizado que o esconderia. false = painel desconhecido.
    bool revealView(const QString &id);
    void closeView(const QString &id);
    void moveView(const QString &id, Zone zone, int index = -1);
    void floatView(const QString &id, const QPoint &globalPos = QPoint());
    void dockView(const QString &id);
    void setMaximized(const QString &id);
    void toggleMaximize(const QString &id);
    void restoreDefault();
    void applyLayout(const LayoutModel &layout);
    // Muda o tamanho da grade e o modo de expansão (os painéis das células que saem passam para a mais próxima).
    void setGrid(int cols, int rows, Expansion expansion);
    // Reconfere os painéis com o registro (um deixou de existir: o chat de IA sem provedor, um comando KIP apagado).
    void revalidate();

    // Modo de reorganizar: mostra a faixa de abas em todas as zonas (por padrão ela só aparece com mais de um painel).
    bool editMode() const { return m_model.tabsAlways(); }
    void setEditMode(bool on);

    // Recolhe uma célula da borda numa faixa fina de ícones (clicar num ícone abre o painel ao lado do conteúdo, empurrando-o,
    // com divisória para redimensionar; clicar de novo o fecha) ou a expande de vez. As células do meio nunca recolhem.
    void setZoneCollapsed(Zone zone, bool collapsed);
    // Modo dividir da zona: todos os painéis dela à vista, cada um numa caixa com cabeçalho (em vez de abas).
    void setZoneSplit(Zone zone, bool split);
    ZoneRail *railWidget(Edge edge) const { return m_rails.value(edge); }
    // A faixa onde a célula recolhida aparece (nullptr se ela não está recolhida).
    ZoneRail *railFor(Zone zone) const;
    // O painel de uma célula recolhida que está aberto agora (vazio = nenhum).
    QString revealedView(Zone zone) const { return m_revealed.value(zone); }
    void hideRevealed(std::optional<Zone> zone = std::nullopt);

    // --- consulta
    std::optional<Zone> zoneOf(const QString &id) const { return m_model.zoneOf(id); }
    // O painel está realmente na tela (na aba da frente da zona, ou flutuando com a janela visível)?
    bool isViewVisible(const QString &id) const;
    ViewZone *zoneWidget(Zone zone) const { return m_zones.value(zone); }
    FloatingViewWindow *floatingWindow(const QString &id) const { return m_floats.value(id).data(); }
    // O divisor entre as pilhas (colunas no modo vertical, linhas no horizontal).
    QSplitter *rootSplitter() const { return m_root; }
    // O divisor de dentro da pilha `index` do plano (nullptr se a pilha tem uma célula só).
    QSplitter *stackSplitter(int index) const;
    const GridPlan &plan() const { return m_plan; }
    QScrollArea *scrollArea() const { return m_scroll; }
    // O quadro que rola (o conteúdo da área de rolagem).
    QWidget *board() const { return m_board; }
    // A área onde a grade aparece (a janela de rolagem), nas coordenadas do host, e a célula nominal (a grade N×M
    // igual, mesmo para células vazias) dentro dela.
    QRect boardArea() const;
    QRect nominalRect(Zone zone) const;
    bool emptyStateShown() const;
    QWidget *emptyState() const { return m_empty; }
    DropOverlay *overlay() const { return m_overlay; }
    // Onde um painel solto em `hostPos` (coordenadas do host) cairia.
    DropTarget dropTargetAt(const QPoint &hostPos) const { return targetAt(hostPos); }
    // O alvo virtual "Flutuar" mostrado durante o arrasto (coordenadas do quadro).
    QRect floatPillRect() const;

    // O arrasto de um painel que NÃO nasceu numa faixa de abas do quadro (a guia de uma execução na barra da Saída): o
    // overlay de destino aparece durante o arrasto e a queda é aplicada ao fim.
    void beginViewDrag();
    void endViewDrag(const QString &id, Qt::DropAction action, const QPoint &globalPos);

    void setBarHeight(int height);
    // Largura atual de uma célula e ajuste dela (arrastar a borda da lista, ver MainWindow::dragTreeBorder): a vizinha no
    // mesmo divisor cede ou recebe o que sobra.
    int zoneWidth(Zone zone) const;
    bool setZoneWidth(Zone zone, int pixels);
    // Reaplica as proporções do modelo (depois de a Saída expandir ou da janela mudar).
    void refreshSizes() { applyRatios(); }

    // As flutuantes aparecem/somem junto com a janela dona.
    void setFloatingVisible(bool visible);
    // Copia de novo o stylesheet da janela dona para as flutuantes (elas não são filhas dela): tema ou aparência mudaram.
    void refreshFloatingStyle();

    // Preenche `menu` com as ações do painel (mover, flutuar, maximizar, fechar).
    void populatePanelMenu(QMenu *menu, const QString &id);
    // Reaplica os títulos/ícones das abas (mudou o idioma ou o tema).
    void refreshTabs();

signals:
    void modelEdited();
    void maximizedChanged(const QString &id);
    // Pediram para restaurar o layout padrão (botão do estado vazio).
    void restoreRequested();
    // A estrutura (zonas, visibilidade) mudou: quem depende de onde o painel está (eixo do colapso da Saída) reage.
    void arrangementChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onFocusChanged(QWidget *now);
    void applyZoneFocus();
    void revealZone(Zone zone, const QString &viewId);
    void rebuild();
    void updateBoardMinimum();
    QStringList railViews(Edge edge) const;
    QStringList railOpen(Edge edge) const;
    void refreshRail(Edge edge);
    // Onde um ícone solto na faixa `edge` na posição `index` (da lista da faixa) entra: a célula e a posição nela.
    std::pair<Zone, int> railDropPosition(Edge edge, int index) const;
    QList<int> distribute(const QList<double> &weights, const QList<QWidget *> &children, bool horizontal, int total) const;
    bool constrainedAlong(const QWidget *widget, bool horizontal) const;
    void syncFloating();
    void lightUpdate();
    QString structureSignature() const;
    void seedRevealedFromModel();
    void syncOpenToModel();
    ViewZone *createZone(Zone zone);
    void applyRatios();
    void captureRatios();
    void edited();

    DropTarget targetAt(const QPoint &hostPos) const;
    QList<std::pair<QRect, QString>> edgeHints() const;
    QRect zoneRect(Zone zone) const;
    Zone nominalZoneAt(const QPoint &hostPos) const;

    void showPanelMenu(const QString &id, const QPoint &globalPos);

    ViewRegistry *m_registry;
    QWidget *m_floatParent;
    LayoutModel m_model;
    QHash<Zone, ViewZone *> m_zones;
    QHash<QString, QPointer<FloatingViewWindow>> m_floats;
    QHash<Edge, ZoneRail *> m_rails;
    QWidget *m_row = nullptr;
    QHBoxLayout *m_rowLayout = nullptr;
    QVBoxLayout *m_outerLayout = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_board = nullptr;
    QVBoxLayout *m_boardLayout = nullptr;
    // Células recolhidas abertas a partir da faixa: célula -> painel da frente (o modelo lembra o aberto, ver ZoneState::open).
    QHash<Zone, QString> m_revealed;
    std::optional<Zone> m_focusedZone; // a zona que tem (ou teve por último) o foco do teclado
    QSplitter *m_root = nullptr;
    // O que o plano atual montou: por pilha, o divisor (nullptr com uma célula só), o widget que está no divisor raiz e as
    // zonas na ordem.
    struct StackWidgets {
        QSplitter *splitter = nullptr;
        QWidget *node = nullptr;
        QList<ViewZone *> zones;
    };
    QList<StackWidgets> m_stackWidgets;
    GridPlan m_plan;
    QWidget *m_stash = nullptr;
    QWidget *m_empty = nullptr;
    DropOverlay *m_overlay = nullptr;
    QString m_signature;
    bool m_applyingRatios = false;
    bool m_rebuilding = false;
    bool m_floatingVisible = true;
    int m_barHeight = 30;
    std::optional<DropTarget> m_pendingDrop;
    QString m_pendingDropView;
};

} // namespace kai::ui::layout
