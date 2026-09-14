#pragma once

#include <QWidget>

#include <optional>

#include "core/config-manager.h"
#include "ui/layout/layout-model.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace kai::ui {

class GridPreview;

// Aba "Layout": densidade e cantos, restaurar painéis, grade de painéis (colunas, linhas, absorção das células vazias,
// com miniatura), posição dos grupos de ações, imagem de fundo da lista e linhas de conexão da árvore.
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
    // O rascunho da grade (colunas, linhas, modo, tamanhos Fit) difere do layout de agora?
    bool gridDirty() const;
    // Confirma o rascunho: emite gridChangeRequested (e um zoneSpanRequested por célula esticada). Descartar = não chamar.
    void commitGrid();

signals:
    void restoreLayoutRequested();
    // A grade foi confirmada (só no OK; Cancelar descarta o rascunho). `expansion` é um layout::Expansion como inteiro.
    void gridChangeRequested(int cols, int rows, int expansion);
    // Modo Fit, depois do anterior: quanto a célula (linha, coluna) ocupa.
    void zoneSpanRequested(int row, int col, int colSpan, int rowSpan);

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
    QList<layout::Zone> changedSpans() const;
    layout::LayoutModel m_layoutModel;
    layout::LayoutModel m_initialModel;
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
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

signals:
    // Modo Fit: o usuário arrastou uma célula para ocupar colunas × linhas.
    void spanChanged(kai::ui::layout::Zone zone, int colSpan, int rowSpan);

private:
    QRectF cellBox(const layout::FitCell &cell) const;
    layout::Zone zoneAtPoint(const QPointF &pos) const;
    layout::LayoutModel m_model;
    std::optional<layout::Zone> m_dragZone;
};

} // namespace kai::ui
