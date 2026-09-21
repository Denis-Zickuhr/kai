#pragma once

#include <QWidget>

#include "core/config-manager.h"

class QComboBox;
class QLineEdit;
class QSlider;

namespace kai::ui {

// Aba "Layout" das Configurações: densidade e cantos, posição da Saída, onde
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
    QComboBox *outputPositionField() const { return m_outputPositionField; }

private:
    QComboBox *m_densityField = nullptr;
    QComboBox *m_cornerStyleField = nullptr;
    QComboBox *m_treeConnectorStyleField = nullptr;
    QLineEdit *m_commandsBgImageField = nullptr;
    QSlider *m_commandsBgOpacityField = nullptr;
    QComboBox *m_itemActionsPlacementField = nullptr;
    QComboBox *m_displayActionsPlacementField = nullptr;
    QComboBox *m_executionActionsPlacementField = nullptr;
    QComboBox *m_outputPositionField = nullptr;
};

} // namespace kai::ui
