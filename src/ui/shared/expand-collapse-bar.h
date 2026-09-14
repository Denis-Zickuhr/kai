#pragma once

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

signals:
    void expandSelectedRequested();
    void collapseSelectedRequested();
    void expandAllRequested();
    void collapseAllRequested();
    // Alterna hidden do comando selecionado (esconde/reexibe da árvore).
    void toggleHiddenSelectedRequested();
    // Alterna a exibição global dos itens ocultos.
    void toggleShowHiddenRequested();

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

    bool m_selectedHidden{false};

    // (botão, nome do ícone Lucide) — usado para re-renderizar todos os
    // ícones quando o accent color do tema muda. Os dois botões de
    // ocultar/mostrar não entram aqui pois têm ícone dinâmico (ver
    // setSelectedHidden/setShowingHidden).
    QVector<QPair<QPushButton *, QString>> m_iconButtons;
    QColor m_accent;
};

} // namespace kai::ui
