#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QMenu;
class QPainter;
class QTimer;

namespace kai::ui {

// Estado mostrado no ponto de status de cada guia.
enum class OutputTabStatus { Idle, Running, Waiting, Success, Failed, Skipped };

struct OutputTabInfo {
    QString id;       // id do comando (o virtual `act:...` numa ação de pasta)
    QString title;    // nome mostrado na guia
    QString tooltip;  // texto do balão
    OutputTabStatus status = OutputTabStatus::Idle;
};

// Barra de guias das SAÍDAS: um atalho para alternar entre os comandos em execução e os já executados nesta
// sessão. Fica logo acima da saída, plana como o container de ações da aba de comandos (sem ilha, só uma linha
// separadora embaixo). Fechar uma guia só a tira da barra: não para o processo e o log continua guardado; clicar no
// comando de novo traz a guia de volta (quem decide é o MainWindow, esta classe só desenha e avisa).
//
// Cada guia mostra um ponto de status (rodando, aguardando resposta, sucesso, erro, pulado), o nome, o tempo
// decorrido enquanto roda e uma marca discreta quando chegou saída nova numa guia que não está em foco. O "×"
// aparece sob o mouse e na guia atual (nas outras, o mesmo lugar mostra a marca de saída nova). A largura de cada guia
// só cresce enquanto ela roda (o cronômetro não faz as vizinhas dançarem) e os botões das pontas nunca saem do lugar: as
// guias rolam entre eles e esmaecem nas bordas em vez de colar nos ícones. A barra está sempre lá (com a saída expandida): sem nenhuma guia, mostra uma
// dica em vez de sumir. Cabendo tudo, nada rola; senão a roda do mouse rola e um botão com a lista
// completa aparece no fim da barra.
class OutputTabsBar : public QWidget {
    Q_OBJECT

public:
    // Puxar a guia para virar painel só vale quando isto retorna true (modo reorganizar).
    void setExternalDragAllowed(std::function<bool()> allowed) { m_externalDragAllowed = std::move(allowed); }
    explicit OutputTabsBar(QWidget *parent = nullptr);

    // Substitui o conjunto de guias (ordem = ordem na barra). Guarda o início de cada execução vista rodando, para o
    // cronômetro.
    void setTabs(const QVector<OutputTabInfo> &tabs);
    const QVector<OutputTabInfo> &tabs() const { return m_tabs; }
    QStringList ids() const;
    bool hasTab(const QString &id) const;

    // A guia em foco (a saída mostrada); vazio = nenhuma. Limpa a marca de saída nova dela.
    void setCurrent(const QString &id);
    QString currentId() const { return m_current; }

    // Chegou saída numa guia: se não é a atual, ganha a marca de "novidade".
    void markActivity(const QString &id);
    bool hasActivity(const QString &id) const { return m_unread.contains(id); }
    // Uma nova execução começou: o cronômetro da guia volta a zero.
    void noteRunStarted(const QString &id);

    // O "+" no começo da barra (busca/execução rápida de comandos). Escondido até o dono ligar.
    void setQuickRunVisible(bool visible);
    bool quickRunVisible() const { return m_quickRunVisible; }
    QRect quickRunRect() const;

    // A barra só aparece se isto for true (a saída colapsada a esconde).
    void setStripAllowed(bool allowed);

    // Altura da barra (igual à das barras vizinhas) e reaplicação do tema/cantos.
    void setBarHeight(int height);
    void refreshStyle();

    // A guia sob o ponto (coordenadas do widget), ou "" — usado pelos testes.
    QString tabAt(const QPoint &pos) const;
    QRect tabRect(const QString &id) const;
    QRect closeRect(const QString &id) const;
    // O botão com a lista de todas as guias (só existe quando elas não cabem): pintado pela própria barra, com a
    // mesma altura e o mesmo topo das guias. Retângulo nulo quando não há overflow.
    QRect overflowRect() const;

    // Texto curto de um estado ("rodando", "falhou"...), traduzido.
    static QString statusText(OutputTabStatus status);

signals:
    void tabActivated(const QString &id);
    void tabCloseRequested(const QString &id);
    void tabContextRequested(const QString &id, const QPoint &globalPos);
    void quickRunRequested();
    // O usuário arrastou uma guia para outra posição (índice final na barra).
    void tabMoved(const QString &id, int newIndex);
    // O usuário puxou a guia PARA FORA da barra: começa um arrasto de painel (a execução vira um painel `run:<id>` ao ser
    // solta numa zona do quadro). `action` Ignore = largada onde nada a aceitou.
    void tabDragStarted(const QString &id);
    void tabDragFinished(const QString &id, Qt::DropAction action, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Layout {
        QVector<QRect> tabs;    // em coordenadas do widget (já com a rolagem)
        QVector<QRect> closes;
        int contentWidth = 0;
        int viewportWidth = 0;
    };
    Layout computeLayout() const;
    int indexOf(const QString &id) const;
    int indexAt(const QPoint &pos) const;
    QString elapsedText(const OutputTabInfo &tab) const;
    QColor dotColor(OutputTabStatus status) const;
    void ensureCurrentVisible();
    void clampScroll();
    void paintTabs(QPainter &painter, const Layout &layout) const;
    bool isOverflowing() const;
    int leadingWidth() const;
    int usableWidth() const;
    void updateVisibility();
    void updateTimer();
    void updateDrag(const QPoint &pos);
    void startExternalDrag();
    void showOverflowMenu();

    QVector<OutputTabInfo> m_tabs;
    QString m_current;
    QHash<QString, bool> m_unread;
    QHash<QString, QDateTime> m_started;
    // Maior largura que o cronômetro de cada guia já ocupou nesta execução: a guia só cresce, nunca encolhe a cada tique.
    mutable QHash<QString, int> m_elapsedWidth;
    int m_scroll = 0;
    int m_barHeight = 28;
    int m_hover = -1;
    int m_pressed = -1;
    bool m_hoverClose = false;
    bool m_hoverOverflow = false;
    bool m_pressedOverflow = false;
    bool m_allowed = true;
    bool m_quickRunVisible = false;
    bool m_hoverQuickRun = false;
    bool m_pressedQuickRun = false;
    // Arrastar guias para reordenar: começa ao mover o mouse com uma guia pressionada.
    QPoint m_pressPos;
    int m_dragGrab = 0;       // distância do mouse à borda esquerda da guia no momento do clique
    int m_dragX = 0;          // borda esquerda atual da guia arrastada (acompanha o mouse)
    int m_dragOrigin = -1;    // posição da guia ao começar o arrasto
    bool m_dragActive = false;
    std::function<bool()> m_externalDragAllowed;
    bool m_pressedClose = false;
    QTimer *m_timer = nullptr;
};

} // namespace kai::ui
