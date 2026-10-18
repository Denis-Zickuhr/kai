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
class QSplitter;
class QVBoxLayout;

namespace kai::ui::layout {

class ViewZone;
class ZoneRail;

// O quadro da janela: monta as zonas (esquerda | [centro / baixo] | direita) com QSplitters a partir de um LayoutModel e
// cuida de arrastar, fechar, flutuar e maximizar painéis. Os painéis são criados UMA vez (pelo registro) e reparentados:
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
    // Reconfere os painéis com o registro (um deixou de existir: o chat de IA sem provedor, um comando KIP apagado).
    void revalidate();

    // Modo de reorganizar: mostra a faixa de abas em todas as zonas (por padrão ela só aparece com mais de um painel).
    bool editMode() const { return m_model.tabsAlways(); }
    void setEditMode(bool on);

    // Recolhe uma zona numa faixa fina de ícones na borda (clicar num ícone abre o painel ao lado do conteúdo, empurrando-o,
    // com divisória para redimensionar; clicar de novo o fecha) ou a expande de vez. O centro nunca recolhe.
    void setZoneCollapsed(Zone zone, bool collapsed);
    ZoneRail *railWidget(Zone zone) const { return m_rails.value(zone); }
    // O painel de uma zona recolhida que está aberto agora (vazio = nenhum).
    QString revealedView(Zone zone) const { return m_revealed.value(zone); }
    void hideRevealed(std::optional<Zone> zone = std::nullopt);

    // --- consulta
    std::optional<Zone> zoneOf(const QString &id) const { return m_model.zoneOf(id); }
    // O painel está realmente na tela (na aba da frente da zona, ou flutuando com a janela visível)?
    bool isViewVisible(const QString &id) const;
    ViewZone *zoneWidget(Zone zone) const { return m_zones.value(zone); }
    FloatingViewWindow *floatingWindow(const QString &id) const { return m_floats.value(id).data(); }
    QSplitter *rootSplitter() const { return m_root; }
    QSplitter *midSplitter() const { return m_mid; }
    // Quem divide o Fundo do resto: o miolo (Centro/Fundo) ou, sem Centro, o divisor que põe o Fundo sob as laterais.
    QSplitter *verticalSplitter() const { return m_mid ? m_mid : m_vert; }
    bool emptyStateShown() const;
    QWidget *emptyState() const { return m_empty; }
    DropOverlay *overlay() const { return m_overlay; }
    // Onde um painel solto em `hostPos` (coordenadas do host) cairia.
    DropTarget dropTargetAt(const QPoint &hostPos) const { return targetAt(hostPos); }

    // O arrasto de um painel que NÃO nasceu numa faixa de abas do quadro (a guia de uma execução na barra da Saída): o
    // overlay de destino aparece durante o arrasto e a queda é aplicada ao fim.
    void beginViewDrag();
    void endViewDrag(const QString &id, Qt::DropAction action, const QPoint &globalPos);

    void setBarHeight(int height);
    // Largura atual de uma zona lateral e ajuste dela (arrastar a borda da lista, ver MainWindow::dragTreeBorder).
    int zoneWidth(Zone zone) const;
    bool setZoneWidth(Zone zone, int pixels);
    // Reaplica as proporções do modelo (depois de a Saída expandir ou da janela mudar).
    void refreshSizes() { applyRatios(); }

    // As flutuantes aparecem/somem junto com a janela dona.
    void setFloatingVisible(bool visible);

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
    void syncFloating();
    void lightUpdate();
    QString structureSignature() const;
    ViewZone *createZone(Zone zone);
    void applyRatios();
    void captureRatios();
    bool zoneConstrained(Zone zone) const;
    void edited();

    DropTarget targetAt(const QPoint &hostPos) const;
    QList<std::pair<QRect, QString>> edgeHints() const;
    QRect zoneRect(Zone zone) const;
    QRect midRect() const;

    void showPanelMenu(const QString &id, const QPoint &globalPos);

    ViewRegistry *m_registry;
    QWidget *m_floatParent;
    LayoutModel m_model;
    QHash<Zone, ViewZone *> m_zones;
    QHash<QString, QPointer<FloatingViewWindow>> m_floats;
    QHash<Zone, ZoneRail *> m_rails;
    QWidget *m_row = nullptr;
    QHBoxLayout *m_rowLayout = nullptr;
    QVBoxLayout *m_outerLayout = nullptr;
    // Zonas recolhidas abertas a partir da faixa: zona -> painel da frente. Só de sessão (não vai para o layout.json).
    QHash<Zone, QString> m_revealed;
    std::optional<Zone> m_focusedZone; // a zona que tem (ou teve por último) o foco do teclado
    QSplitter *m_root = nullptr;
    QSplitter *m_mid = nullptr;
    QSplitter *m_vert = nullptr; // sem Centro: [laterais] sobre o Fundo, na largura toda
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
