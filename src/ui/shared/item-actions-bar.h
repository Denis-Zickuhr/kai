#pragma once

#include <QWidget>
#include <QPushButton>
#include <QBoxLayout>
#include <QColor>
#include <QVector>
#include <QPair>

namespace kai::ui {

// Grupo de ações "Item" (CRUD): editar a pasta da aba atual, nova pasta,
// novo comando, nova coleção, editar/excluir o comando selecionado.
// "Editar body (rápido)" mora no grupo Execução (ActionSidebar) — pedido
// do usuário. Reusável em qualquer um dos dois containers de
// posicionamento (ActionGroupContainer horizontal na upper bar, ou
// vertical na side bar — ver Settings "Onde exibir cada grupo de ações")
// via setOrientation(). Ícones via LucideIcons, recoloridos com o accent
// do tema.
class ItemActionsBar : public QWidget {
    Q_OBJECT

public:
    explicit ItemActionsBar(QWidget *parent = nullptr);

    void applyAccentColor(const QColor &color);
    // Alterna o layout interno entre linha (upper bar) e coluna (side bar).
    void setOrientation(Qt::Orientation orientation);

    // Habilita/desabilita as ações que dependem do item selecionado
    // (editar/excluir, válidas para pasta/comando/coleção); as de criação
    // ficam sempre habilitadas.
    void setRowContext(bool hasSelection);

signals:
    void editCurrentFolderRequested();
    void newFolderRequested();
    void newCommandRequested();
    void newCollectionRequested();
    void editSelectedRequested();
    void deleteSelectedRequested();

private:
    void setupUi();
    QPushButton *createIconButton(const QString &iconName, const QString &tooltip);

    QBoxLayout *m_layout{nullptr};

    QPushButton *m_btnEditFolder{nullptr};
    QPushButton *m_btnNewFolder{nullptr};
    QPushButton *m_btnNewCommand{nullptr};
    QPushButton *m_btnNewCollection{nullptr};
    QPushButton *m_btnEditSelected{nullptr};
    QPushButton *m_btnDeleteSelected{nullptr};

    QVector<QPair<QPushButton *, QString>> m_iconButtons;
    QColor m_accent;
};

} // namespace kai::ui
