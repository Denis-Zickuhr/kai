#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QToolButton;
class QPushButton;
class QStackedWidget;

namespace kai::ui {

// Card de seção colapsável — primeira peça de um novo padrão visual
// (repaginação pedida pelo usuário, mockup enviado: cabeçalho com
// chevron/título/badge de contagem/botão de ação rápida; corpo com
// estado vazio OU o conteúdo real, conforme a contagem). Ponto de
// partida: diálogo de edição de Comando (seções "Parâmetros Dinâmicos" e
// "Auto-responsores de Saída") — outras telas devem migrar pra este
// MESMO componente depois que o padrão for validado aqui, em vez de cada
// uma reinventar seu próprio card.
//
// Não sabe nada do conteúdo em si: o dono passa um `body` (ex: um
// ParameterEditorWidget já pronto, sem nenhuma mudança interna) via
// setBody(), e chama setCount() sempre que a quantidade de itens mudar —
// o card decide sozinho se mostra o corpo ou o estado vazio, e atualiza o
// badge. O botão de ação do cabeçalho ("+ Add Parameter") só emite
// actionTriggered(); quem sabe COMO adicionar um item é o dono (conecta
// isso ao slot de "adicionar" já existente no editor, sem duplicar
// lógica).
class CollapsibleSectionCard : public QWidget {
    Q_OBJECT

public:
    explicit CollapsibleSectionCard(const QString &title, QWidget *parent = nullptr);

    // Conteúdo real da seção (ex: o ParameterEditorWidget inteiro, tabela
    // + barra de ações próprias) — mostrado só quando count() > 0.
    void setBody(QWidget *body);
    // Texto do estado vazio (ex: "No parameters added yet") — mostrado
    // quando count() == 0, no lugar do corpo.
    void setEmptyStateText(const QString &text);
    // Subtítulo muted ao lado do título (ex: "(commands in the same
    // folder)", mockup dos Hooks) — opcional, sem chamar fica sem nada.
    void setSubtitle(const QString &text);
    // Desliga a troca automática corpo/estado-vazio por contagem (ver
    // setCount): algumas seções (ex: Hooks) sempre têm algo interativo
    // pra mostrar — uma lista de itens disponíveis pra marcar — mesmo com
    // ZERO marcados; a contagem ali só é informativa (via o badge), não
    // significa "nada pra ver".
    void setAlwaysShowBody(bool alwaysShow);
    // Texto do botão de ação rápida no cabeçalho (ex: "+ Add Parameter").
    // Chamar só se a seção tiver uma ação de adicionar; sem chamar, o
    // cabeçalho fica só com chevron/título/badge.
    void setActionButtonText(const QString &text);
    // Atualiza o badge de contagem e alterna corpo/estado-vazio.
    void setCount(int count);
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }

signals:
    // Botão de ação do cabeçalho OU o "+" do estado vazio — mesma ação,
    // dois pontos de entrada (mockup: o estado vazio também é clicável).
    void actionTriggered();

protected:
    // Clique em QUALQUER lugar do cabeçalho (não só o chevron) alterna a
    // seção — pedido do usuário. Instalado como filtro no m_headerWidget;
    // cliques que caem em cima de um FILHO interativo (chevron/badge/botão
    // de ação) são tratados pelo próprio filho primeiro e nunca chegam
    // aqui, então não há conflito.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
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
    bool m_expanded = true;
    bool m_alwaysShowBody = false;
};

} // namespace kai::ui
