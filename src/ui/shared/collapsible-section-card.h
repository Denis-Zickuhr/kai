#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QToolButton;
class QPushButton;
class QStackedWidget;

namespace kai::ui {

// Card de seção colapsável: cabeçalho (chevron, título, badge de contagem, botão de ação rápida) e corpo com estado
// vazio OU o conteúdo real, conforme a contagem. Não conhece o conteúdo: o dono passa um `body` via setBody() e chama
// setCount() quando a quantidade muda; o card alterna corpo/estado vazio e atualiza o badge. O botão de ação só emite
// actionTriggered(); quem sabe adicionar um item é o dono.
class CollapsibleSectionCard : public QWidget {
    Q_OBJECT

public:
    explicit CollapsibleSectionCard(const QString &title, QWidget *parent = nullptr);

    // Conteúdo real da seção, mostrado só quando count() > 0.
    void setBody(QWidget *body);
    // Texto do estado vazio, mostrado quando count() == 0 no lugar do corpo.
    void setEmptyStateText(const QString &text);
    // Subtítulo muted ao lado do título (opcional).
    void setSubtitle(const QString &text);
    // Desliga a troca automática corpo/estado-vazio por contagem (ver setCount): seções como Hooks sempre têm algo
    // interativo para mostrar; ali a contagem é só informativa (badge).
    void setAlwaysShowBody(bool alwaysShow);
    // Esconde o badge de contagem, para seções de campos fixos onde ele mostraria "0" para sempre.
    void setShowCountBadge(bool show);
    // Remove a capacidade de colapsar (sem chevron, cabeçalho não reage a clique, força expanded=true). Chamar depois
    // de setBody(); true por padrão.
    void setCollapsible(bool collapsible);
    // Sem fundo/borda pintados (paintEvent vira no-op). Um card direto sobre o fundo próprio de um QDialog mostraria
    // "fundinhos" quadrados nos cantos arredondados, onde drawRoundedRect deixa a área fora da curva sem pintar. Chamar
    // ANTES de setBody().
    void setFlat(bool flat);
    // Texto do botão de ação rápida do cabeçalho (ex: "+ Add Parameter"); sem chamar, o cabeçalho fica sem ele.
    void setActionButtonText(const QString &text);
    // Atualiza o badge de contagem e alterna corpo/estado-vazio.
    void setCount(int count);
    // `animate`: false aplica na hora, sem a animação de abrir/fechar; para a configuração inicial feita pelo dono no
    // construtor do diálogo (vários setExpanded(true) em sequência disparavam a animação de 160ms ao abrir a tela).
    // Deixe true para toggles reais do usuário.
    void setExpanded(bool expanded, bool animate = true);
    bool isExpanded() const { return m_expanded; }

signals:
    // Botão de ação do cabeçalho OU o "+" do estado vazio: mesma ação, dois pontos de entrada.
    void actionTriggered();
    // Emitido só quando setExpanded() realmente muda o estado; para quem precisa reagir à abertura (ex: foco no filtro
    // de um multi-select).
    void expandedChanged(bool expanded);

protected:
    // Clique em qualquer lugar do cabeçalho alterna a seção (filtro instalado em m_headerWidget); filhos interativos
    // tratam o próprio clique antes.
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Pinta fundo/borda arredondada à mão (ver o construtor): background-color via QSS quebra o switch de checkboxes descendentes.
    void paintEvent(QPaintEvent *event) override;

private:
    void fitEmptyStateHeight();
    void updateChevronIcon();

    QWidget *m_headerWidget = nullptr;
    QToolButton *m_chevronButton = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_countBadge = nullptr;
    QPushButton *m_actionButton = nullptr;
    QWidget *m_bodyWrapper = nullptr; // some/aparece inteiro ao colapsar
    QStackedWidget *m_contentStack = nullptr; // página 0 = vazio, 1 = corpo
    QWidget *m_emptyStateWidget = nullptr;
    QLabel *m_emptyStateLabel = nullptr;
    QToolButton *m_emptyStateIcon = nullptr;
    QWidget *m_body = nullptr;
    bool m_expanded = false; // construtor sempre nasce colapsado — ver setExpanded(false) no ctor
    bool m_alwaysShowBody = false;
    bool m_collapsible = true;
    bool m_flat = false;
};

} // namespace kai::ui
