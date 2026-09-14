#pragma once

#include <QWidget>

#include "core/config-manager.h"

class QComboBox;
class QLineEdit;
class QSlider;
class QSpinBox;
class QCheckBox;

namespace kai::ui {

// Aba "Aparência": tema ativo + import, janela (modo/tamanho) e efeitos
// visuais (FX). O layout (densidade, cantos, posição das ações/Saída, imagem
// de fundo) vive na aba própria LayoutTab.
class AppearanceTab : public QWidget {
    Q_OBJECT

public:
    AppearanceTab(const core::SettingsData &currentSettings, const QStringList &themes,
                  const QString &currentThemeName, QWidget *parent = nullptr);

    QComboBox *themeField() const { return m_themeField; }
    QComboBox *windowModeField() const { return m_windowModeField; }
    QSpinBox *windowWidthField() const { return m_windowWidthField; }
    QSpinBox *windowHeightField() const { return m_windowHeightField; }
    QCheckBox *fxShadowsField() const { return m_fxShadowsField; }
    QCheckBox *fxTranslucencyField() const { return m_fxTranslucencyField; }
    QCheckBox *fxBlurField() const { return m_fxBlurField; }
    QCheckBox *fxAnimationsField() const { return m_fxAnimationsField; }
    QCheckBox *gradientsEnabledField() const { return m_gradientsEnabledField; }
    QCheckBox *autoHideField() const { return m_autoHideField; }
    QCheckBox *startVisibleField() const { return m_startVisibleField; }

signals:
    // O import de tema mexe em estado do SettingsDialog (m_themesDirPath,
    // refreshThemeList/importThemeFromPath), não algo que esta aba possa
    // resolver sozinha — ela só avisa o clique.
    void importThemeClicked();

private:
    QComboBox *m_themeField = nullptr;
    QComboBox *m_windowModeField = nullptr;
    QComboBox *m_windowPresetField = nullptr;
    QSpinBox *m_windowWidthField = nullptr;
    QSpinBox *m_windowHeightField = nullptr;
    QCheckBox *m_fxShadowsField = nullptr;
    QCheckBox *m_fxTranslucencyField = nullptr;
    QCheckBox *m_fxBlurField = nullptr;
    QCheckBox *m_fxAnimationsField = nullptr;
    QCheckBox *m_gradientsEnabledField = nullptr;
    QCheckBox *m_autoHideField = nullptr;
    QCheckBox *m_startVisibleField = nullptr;
};

} // namespace kai::ui
