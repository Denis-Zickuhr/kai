#pragma once

#include <QList>
#include <QPoint>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QWidget>

#include "ui/layout/layout-model.h"
#include "ui/layout/view-mime.h"
#include "ui/layout/view-registry.h"

namespace kai::ui::layout {


// A faixa de abas de uma zona. Desenhada à mão (tokens de tema, densidade e raio) em vez de um QTabBar: precisa de ícone,
// fechar por aba, menu e de ser a ORIGEM do arrasto de um painel para outra zona.
class ZoneTabStrip : public QWidget {
    Q_OBJECT

public:
    struct Tab {
        QString id;
        QString title;
        QString icon;
        bool closable = true;
    };

    explicit ZoneTabStrip(QWidget *parent = nullptr);

    void setTabs(const QList<Tab> &tabs, const QString &active);
    void setBarHeight(int height);
    // A zona tem o foco do teclado: só nela o sublinhado da aba da frente usa a cor de destaque.
    void setZoneFocused(bool focused);
    bool zoneFocused() const { return m_zoneFocused; }
    // Fora do modo reorganizar as abas não iniciam arrasto.
    void setDragEnabled(bool on) { m_dragEnabled = on; }
    QString activeId() const { return m_active; }
    int count() const { return int(m_tabs.size()); }
    // Índice da aba sob `pos` (coordenadas da faixa) ou -1.
    int indexAt(const QPoint &pos) const;
    // Posição de inserção (0..count) para um painel solto em `x`.
    int insertionIndexAt(int x) const;
    QRect tabRect(int index) const;
    QRect closeRect(int index) const;
    QRect menuRect() const;
    QRect maximizeRect() const;
    // O painel maximizado no quadro (vazio = nenhum): a aba da frente que for ele mostra "restaurar" no lugar de "maximizar".
    void setMaximizedView(const QString &id);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void tabActivated(const QString &id);
    void tabCloseRequested(const QString &id);
    void tabMenuRequested(const QString &id, const QPoint &globalPos);
    void tabDoubleClicked(const QString &id);
    void tabMaximizeRequested(const QString &id);
    void dragStarted(const QString &id);
    // `action` Qt::IgnoreAction = ninguém aceitou o painel (largado fora de qualquer zona).
    void dragFinished(const QString &id, Qt::DropAction action, const QPoint &globalPos);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void relayout();
    void startDrag(int index);
    bool activeIsMaximized() const { return !m_maximizedId.isEmpty() && m_maximizedId == m_active; }

    bool m_dragEnabled = false;
    QList<Tab> m_tabs;
    QList<QRect> m_rects;
    QString m_active;
    int m_barHeight = 30;
    bool m_zoneFocused = false;
    int m_hover = -1;
    bool m_hoverClose = false;
    bool m_hoverMenu = false;
    bool m_hoverMaximize = false;
    bool m_pressOnMaximize = false;
    QString m_maximizedId;
    int m_pressIndex = -1;
    QPoint m_pressPos;
    bool m_pressOnClose = false;
};

// A pilha de um grupo de abas; avisa quando o filho pediu outro tamanho (o colapso da Saída limita a altura/largura dela).
class ZoneStack : public QStackedWidget {
    Q_OBJECT

public:
    using QStackedWidget::QStackedWidget;

signals:
    void layoutRequested();

protected:
    bool event(QEvent *event) override;
};

// Um grupo de abas: a faixa (só aparece com mais de um painel, ou no modo de reorganizar) e a pilha de painéis. Os
// widgets dos painéis NÃO pertencem à zona: o registro os cria uma vez e a zona os reparenta (mover não recria nada).
class ViewZone : public QWidget {
    Q_OBJECT

public:
    ViewZone(Zone zone, ViewRegistry *registry, QWidget *parent = nullptr);

    Zone zone() const { return m_zone; }
    // Mostra `ids` (na ordem) com `active` na frente. `stripAlways` força a faixa mesmo com um painel só.
    void setViews(const QStringList &ids, const QString &active, bool stripAlways);
    // Modo dividir: mostra todos os painéis da zona ao mesmo tempo (cada um numa caixa com cabeçalho), em vez de abas.
    // Vale a partir do próximo setViews; só tem efeito com mais de um painel.
    void setSplitMode(bool split) { m_wantSplit = split; }
    bool splitActive() const { return m_splitter != nullptr; }
    // O painel `id` está à vista nesta zona? (a aba da frente; no modo dividir, todos)
    bool showsView(const QString &id) const;
    // Posição de inserção (0..n) de um painel solto em `pos` (coordenadas da zona) SE o ponteiro está numa faixa/cabeçalho
    // de painel; -1 quando não está.
    int dropIndexAt(const QPoint &pos) const;
    // As caixas do modo dividir (cabeçalhos), para os testes.
    QList<ZoneTabStrip *> sectionHeaders() const;
    // O painel maximizado no quadro (vazio = nenhum). Antes de setViews: uma zona maximizada mostra a faixa mesmo sem o modo
    // reorganizar, senão ficaria sem o ícone de restaurar.
    void setMaximizedView(const QString &id);
    // Esconde a faixa de abas mesmo com vários painéis (a zona aberta a partir de uma faixa lateral: os ícones da faixa já trocam de painel).
    void setStripSuppressed(bool suppressed) { m_stripSuppressed = suppressed; }
    // Devolve os widgets dos painéis a `newParent` (escondidos), esvaziando a zona antes de ela ser descartada.
    void releaseViews(QWidget *newParent);
    QStringList viewIds() const { return m_ids; }
    QString activeView() const { return m_strip->activeId(); }
    ZoneTabStrip *strip() const { return m_strip; }
    QWidget *currentWidget() const { return m_stack->currentWidget(); }
    void setBarHeight(int height);
    void setZoneFocused(bool focused);
    // Reaplica o tamanho máximo que o painel da frente pede (a Saída recolhida).
    void syncConstraints();
    // O que o conteúdo pede de altura nunca é piso: a zona pode ser encolhida até quase só a faixa de abas (o conteúdo
    // que sobra é cortado). Sem isso, a Saída com uma resposta HTTP, ou o painel de Variáveis, travavam o divisor.
    QSize minimumSizeHint() const override;

    // Para os testes: as caixas do modo dividir estão empilhadas (e não lado a lado)?
    bool splitStacked() const { return m_splitter && m_splitter->orientation() == Qt::Vertical; }

signals:
    void viewActivated(const QString &id);
    void viewCloseRequested(const QString &id);
    void viewMenuRequested(const QString &id, const QPoint &globalPos);
    void viewMaximizeRequested(const QString &id);
    void dragStarted(const QString &id);
    void dragFinished(const QString &id, Qt::DropAction action, const QPoint &globalPos);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildStrip(const QString &active);
    void applySplit();
    void updateSplitOrientation();
    void clearSplit();
    // Faixa visível = o painel da frente cola nela (sem a borda de cima, cantos de cima retos).
    void applyGlue();

    Zone m_zone;
    ViewRegistry *m_registry;
    ZoneTabStrip *m_strip = nullptr;
    ZoneStack *m_stack = nullptr;
    QStringList m_ids;
    bool m_syncing = false;
    bool m_regluePending = false;
    bool m_stripSuppressed = false;
    QString m_maximizedView;
    bool m_dragEnabled = false;
    bool m_wantSplit = false;
    bool m_zoneFocused = false;
    int m_barHeight = 30;
    // O modo dividir: o divisor com uma caixa por painel (cabeçalho + o próprio painel).
    QSplitter *m_splitter = nullptr;
    struct Section {
        QWidget *box = nullptr;
        ZoneTabStrip *header = nullptr;
        QWidget *content = nullptr;
        QString id;
    };
    QList<Section> m_sections;
};

} // namespace kai::ui::layout
