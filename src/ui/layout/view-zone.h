#pragma once

#include <QList>
#include <QPoint>
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
    QString activeId() const { return m_active; }
    int count() const { return int(m_tabs.size()); }
    // Índice da aba sob `pos` (coordenadas da faixa) ou -1.
    int indexAt(const QPoint &pos) const;
    // Posição de inserção (0..count) para um painel solto em `x`.
    int insertionIndexAt(int x) const;
    QRect tabRect(int index) const;
    QRect closeRect(int index) const;
    QRect menuRect() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void tabActivated(const QString &id);
    void tabCloseRequested(const QString &id);
    void tabMenuRequested(const QString &id, const QPoint &globalPos);
    void tabDoubleClicked(const QString &id);
    void dragStarted(const QString &id);
    // `action` Qt::IgnoreAction = ninguém aceitou o painel (largado fora de qualquer zona).
    void dragFinished(const QString &id, Qt::DropAction action, const QPoint &globalPos);

protected:
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

    QList<Tab> m_tabs;
    QList<QRect> m_rects;
    QString m_active;
    int m_barHeight = 30;
    bool m_zoneFocused = false;
    int m_hover = -1;
    bool m_hoverClose = false;
    bool m_hoverMenu = false;
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
    // Esconde a faixa de abas mesmo com vários painéis (a zona aberta a partir de uma faixa lateral: os ícones da faixa já trocam de painel).
    void setStripSuppressed(bool suppressed) { m_stripSuppressed = suppressed; }
    // Devolve os widgets dos painéis a `newParent` (escondidos), esvaziando a zona antes de ela ser descartada.
    void releaseViews(QWidget *newParent);
    QStringList viewIds() const { return m_ids; }
    QString activeView() const { return m_strip->activeId(); }
    ZoneTabStrip *strip() const { return m_strip; }
    QWidget *currentWidget() const { return m_stack->currentWidget(); }
    void setBarHeight(int height);
    void setZoneFocused(bool focused) { m_strip->setZoneFocused(focused); }
    // Reaplica o tamanho máximo que o painel da frente pede (a Saída recolhida).
    void syncConstraints();

signals:
    void viewActivated(const QString &id);
    void viewCloseRequested(const QString &id);
    void viewMenuRequested(const QString &id, const QPoint &globalPos);
    void viewMaximizeRequested(const QString &id);
    void dragStarted(const QString &id);
    void dragFinished(const QString &id, Qt::DropAction action, const QPoint &globalPos);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildStrip(const QString &active);
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
};

} // namespace kai::ui::layout
