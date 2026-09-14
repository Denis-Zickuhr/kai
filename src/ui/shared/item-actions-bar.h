#pragma once

#include <QWidget>
#include <QPushButton>
#include <QBoxLayout>
#include <QColor>
#include <QVector>
#include <QPair>

namespace kai::ui {

// Grupo de ações "Item" (CRUD): editar a pasta da aba atual, nova pasta/comando/coleção, editar/excluir o
// comando selecionado. Reusável nos dois containers (horizontal na upper bar, vertical na side bar) via
// setOrientation().
class ItemActionsBar : public QWidget {
    Q_OBJECT

public:
    explicit ItemActionsBar(QWidget *parent = nullptr);

    void applyAccentColor(const QColor &color);
    // Alterna o layout interno entre linha (upper bar) e coluna (side bar).
    void setOrientation(Qt::Orientation orientation);

    // Habilita as ações que dependem do item selecionado (editar/excluir); as de criação ficam sempre habilitadas.
    void setRowContext(bool hasSelection);

signals:
    void editCurrentFolderRequested();
    void newFolderRequested();
    void newCommandRequested();
    void newCollectionRequested();
    void newNoteRequested();
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
    QPushButton *m_btnNewNote{nullptr};
    QPushButton *m_btnEditSelected{nullptr};
    QPushButton *m_btnDeleteSelected{nullptr};

    QVector<QPair<QPushButton *, QString>> m_iconButtons;
    QColor m_accent;
};

} // namespace kai::ui
