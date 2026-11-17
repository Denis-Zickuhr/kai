#pragma once

#include <QWidget>

#include "core/config-manager.h"

class QComboBox;
class QLineEdit;
class QSlider;
class QSpinBox;
class QCheckBox;

namespace kai::ui {

// Aba "Aparência": tema ativo + import, janela (modo/tamanho) e efeitos visuais (FX). O layout vive na LayoutTab.
class AppearanceTab : public QWidget {
    Q_OBJECT

public:
    AppearanceTab(const core::SettingsData &currentSettings, const QStringList &themes,
                  const QString &currentThemeName, QWidget *parent = nullptr);

    QComboBox *themeField() const { return m_themeField; }
    QComboBox *windowModeField() const { return m_windowModeField; }
    QSpinBox *windowWidthField() const { return m_windowWidthField; }
    QSpinBox *windowHeightField() const { return m_windowHeightField; }
    QCheckBox *autoHideField() const { return m_autoHideField; }
    QCheckBox *startVisibleField() const { return m_startVisibleField; }

signals:
    // O import de tema mexe em estado do SettingsDialog (refreshThemeList/importThemeFromPath); a aba só avisa o clique.
    void importThemeClicked();

private:
    QComboBox *m_themeField = nullptr;
    QComboBox *m_windowModeField = nullptr;
    QComboBox *m_windowPresetField = nullptr;
    QSpinBox *m_windowWidthField = nullptr;
    QSpinBox *m_windowHeightField = nullptr;
    QCheckBox *m_autoHideField = nullptr;
    QCheckBox *m_startVisibleField = nullptr;
};

} // namespace kai::ui
