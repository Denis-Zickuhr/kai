#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

class QMenu;
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
// aparece sob o mouse e na guia atual. A barra está sempre lá (com a saída expandida): sem nenhuma guia, mostra uma
// dica em vez de sumir. Cabendo tudo, nada rola; senão a roda do mouse rola e um botão com a lista
// completa aparece no fim da barra.
class OutputTabsBar : public QWidget {
    Q_OBJECT

public:
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

    // Chevron no começo da barra que recolhe/abre a lista de comandos (foco na saída e nos programas ativos). Fica
    // escondido até o dono ligar. `commandsEdge` = de que lado da barra a lista está (aponta o chevron para lá).
    void setCommandsToggleVisible(bool visible);
    void setCommandsToggleState(bool collapsed, Qt::Edge commandsEdge);
    bool commandsToggleVisible() const { return m_toggleVisible; }
    QRect toggleRect() const;
    // Ao lado do chevron: o "+" (busca/execução rápida de comandos) e, depois dele, o ícone de documentos (menu para
    // voltar à documentação de uma pasta). Os dois aparecem sempre que o chevron aparece.
    QRect quickRunRect() const;
    QRect docsRect() const;

    // Botão no fim da barra que recolhe/abre a Saída inteira (o chevron do cabeçalho da Saída some nos modos documento,
    // KIP e "sem execução", que escondem o cabeçalho). `iconName` aponta para onde a Saída recolhe. Escondido por padrão.
    void setOutputToggle(bool visible, const QString &iconName);
    QRect outputToggleRect() const;

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
    void commandsToggleRequested();
    void quickRunRequested();
    void docsRequested();
    void outputToggleRequested();
    // O usuário arrastou uma guia para outra posição (índice final na barra).
    void tabMoved(const QString &id, int newIndex);

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
    bool isOverflowing() const;
    int leadingWidth() const;
    int usableWidth() const; // a largura sem o botão da Saída no fim
    void updateVisibility();
    void updateTimer();
    void updateDrag(const QPoint &pos);
    void showOverflowMenu();

    QVector<OutputTabInfo> m_tabs;
    QString m_current;
    QHash<QString, bool> m_unread;
    QHash<QString, QDateTime> m_started;
    int m_scroll = 0;
    int m_barHeight = 28;
    int m_hover = -1;
    int m_pressed = -1;
    bool m_hoverClose = false;
    bool m_hoverOverflow = false;
    bool m_pressedOverflow = false;
    bool m_allowed = true;
    bool m_toggleVisible = false;
    bool m_toggleCollapsed = false;
    Qt::Edge m_commandsEdge = Qt::TopEdge;
    bool m_hoverToggle = false;
    bool m_pressedToggle = false;
    bool m_hoverQuickRun = false;
    bool m_pressedQuickRun = false;
    bool m_hoverDocs = false;
    bool m_pressedDocs = false;
    bool m_outputToggleVisible = false;
    QString m_outputToggleIcon = QStringLiteral("chevron-down");
    bool m_hoverOutputToggle = false;
    bool m_pressedOutputToggle = false;
    // Arrastar guias para reordenar: começa ao mover o mouse com uma guia pressionada.
    QPoint m_pressPos;
    int m_dragGrab = 0;       // distância do mouse à borda esquerda da guia no momento do clique
    int m_dragX = 0;          // borda esquerda atual da guia arrastada (acompanha o mouse)
    int m_dragOrigin = -1;    // posição da guia ao começar o arrasto
    bool m_dragActive = false;
    bool m_pressedClose = false;
    QTimer *m_timer = nullptr;
};

} // namespace kai::ui
