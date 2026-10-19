#pragma once

#include <QWidget>

#include "core/config-manager.h"
#include "ui/layout/layout-model.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace kai::ui {

class GridPreview;

// Aba "Layout" das Configurações: densidade e cantos, restaurar o layout dos painéis, a grade de painéis (colunas, linhas e
// como as células vazias são absorvidas, com uma miniatura), onde
// fica cada grupo de ações, imagem de fundo da lista de comandos e linhas de
// conexão da árvore. (Saiu da aba Aparência, que ficou grande demais.)
class LayoutTab : public QWidget {
    Q_OBJECT

public:
    explicit LayoutTab(const core::SettingsData &currentSettings, QWidget *parent = nullptr);

    QComboBox *densityField() const { return m_densityField; }
    QComboBox *cornerStyleField() const { return m_cornerStyleField; }
    QComboBox *treeConnectorStyleField() const { return m_treeConnectorStyleField; }
    QLineEdit *commandsBgImageField() const { return m_commandsBgImageField; }
    QSlider *commandsBgOpacityField() const { return m_commandsBgOpacityField; }
    QComboBox *itemActionsPlacementField() const { return m_itemActionsPlacementField; }
    QComboBox *displayActionsPlacementField() const { return m_displayActionsPlacementField; }
    QComboBox *executionActionsPlacementField() const { return m_executionActionsPlacementField; }
    QPushButton *restoreLayoutButton() const { return m_restoreLayoutButton; }
    QSpinBox *gridColsField() const { return m_gridCols; }
    QSpinBox *gridRowsField() const { return m_gridRows; }
    QComboBox *gridExpansionField() const { return m_gridExpansion; }
    GridPreview *gridPreview() const { return m_gridPreview; }

    // O layout de agora: preenche os campos da grade e a miniatura (que mostra como ficaria com os valores escolhidos).
    void setLayoutModel(const layout::LayoutModel &model);

signals:
    void restoreLayoutRequested();
    // A grade mudou: vale na hora, como o restaurar (não espera o OK).
    // `expansion` é um layout::Expansion como inteiro.
    void gridChangeRequested(int cols, int rows, int expansion);

private:
    QComboBox *m_densityField = nullptr;
    QComboBox *m_cornerStyleField = nullptr;
    QComboBox *m_treeConnectorStyleField = nullptr;
    QLineEdit *m_commandsBgImageField = nullptr;
    QSlider *m_commandsBgOpacityField = nullptr;
    QComboBox *m_itemActionsPlacementField = nullptr;
    QComboBox *m_displayActionsPlacementField = nullptr;
    QComboBox *m_executionActionsPlacementField = nullptr;
    QPushButton *m_restoreLayoutButton = nullptr;
    QSpinBox *m_gridCols = nullptr;
    QSpinBox *m_gridRows = nullptr;
    QComboBox *m_gridExpansion = nullptr;
    GridPreview *m_gridPreview = nullptr;
    layout::LayoutModel m_layoutModel;
    bool m_syncingGrid = false;
};

// A miniatura da grade: as células nominais tracejadas por baixo e, por cima, o que realmente aparece (as vazias absorvidas
// pelas vizinhas conforme o modo de expansão).
class GridPreview : public QWidget {
    Q_OBJECT

public:
    explicit GridPreview(QWidget *parent = nullptr);
    void setModel(const layout::LayoutModel &model);
    QSize sizeHint() const override { return QSize(240, 120); }
    QSize minimumSizeHint() const override { return QSize(120, 90); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    layout::LayoutModel m_model;
};

} // namespace kai::ui
