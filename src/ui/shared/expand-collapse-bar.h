#pragma once

#include <QElapsedTimer>
#include <QWidget>
#include <QPushButton>
#include <QBoxLayout>
#include <QColor>
#include <QVector>
#include <QPair>

namespace kai::ui {

// Grupo de ações "Exibição": expandir/colapsar o item selecionado e a
// árvore inteira (sempre na aba ATIVA — ver
// CommandTreeWidget::expandCurrentItem/collapseCurrentItem/expandAll/
// collapseAll), mais ocultar/exibir o item selecionado e mostrar/esconder
// os itens ocultos (CommandTreeWidget::setShowHidden). Reusável em
// qualquer um dos dois containers de posicionamento (ActionGroupContainer
// horizontal na upper bar, ou vertical na side bar — ver Settings "Onde
// exibir cada grupo de ações") via setOrientation(). Ícones via
// LucideIcons, recoloridos com o accent do tema.
class ExpandCollapseBar : public QWidget {
    Q_OBJECT

public:
    explicit ExpandCollapseBar(QWidget *parent = nullptr);

    void applyAccentColor(const QColor &color);
    // Alterna o layout interno entre linha (upper bar) e coluna (side bar).
    void setOrientation(Qt::Orientation orientation);

    // Reflete se o item atualmente selecionado já está oculto (o botão de
    // ocultar/exibir precisa mostrar o ícone certo: "eye-off" para ocultar,
    // "eye" para reexibir).
    void setSelectedHidden(bool hidden);
    // Reflete se "mostrar ocultos" está ativo (botão em estado "pressed").
    void setShowingHidden(bool showing);
    // Reflete se o filtro "só comandos em execução" está ativo.
    void setShowingRunningOnly(bool runningOnly);

signals:
    void expandSelectedRequested();
    void collapseSelectedRequested();
    void expandAllRequested();
    void collapseAllRequested();
    // Alterna hidden do comando selecionado (esconde/reexibe da árvore).
    void toggleHiddenSelectedRequested();
    // Alterna a exibição global dos itens ocultos.
    void toggleShowHiddenRequested();
    // Alterna o filtro "exibir apenas comandos em execução".
    void toggleRunningOnlyRequested();
    // Easter egg: cinco cliques em sequência rápida no olho (mostrar ocultos).
    void eyeEasterEggTriggered();

private:
    void setupUi();
    QPushButton *createIconButton(const QString &iconName, const QString &tooltip);

    QBoxLayout *m_layout{nullptr};

    QPushButton *m_btnExpandSelected{nullptr};
    QPushButton *m_btnCollapseSelected{nullptr};
    QPushButton *m_btnExpandAll{nullptr};
    QPushButton *m_btnCollapseAll{nullptr};
    QPushButton *m_btnToggleHidden{nullptr};
    QPushButton *m_btnToggleShowHidden{nullptr};
    QPushButton *m_btnToggleRunningOnly{nullptr};

    // Cliques seguidos no olho: cada um precisa vir logo depois do anterior.
    QElapsedTimer m_eyeLastClick;
    int m_eyeClicks = 0;
    void registerEyeClick();

    bool m_selectedHidden{false};

    // (botão, nome do ícone Lucide) — usado para re-renderizar todos os
    // ícones quando o accent color do tema muda. Os dois botões de
    // ocultar/mostrar não entram aqui pois têm ícone dinâmico (ver
    // setSelectedHidden/setShowingHidden).
    QVector<QPair<QPushButton *, QString>> m_iconButtons;
    QColor m_accent;
};

} // namespace kai::ui
