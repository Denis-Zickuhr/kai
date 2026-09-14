#include "ui/features/settings/tabs/appearance-tab.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QColor>
#include <QSize>

namespace kai::ui {

AppearanceTab::AppearanceTab(const core::SettingsData &currentSettings, const QStringList &themes,
                              const QString &currentThemeName, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    // --- Grupo: Tema ---
    auto *themeGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.theme")), this);
    auto *themeRow = new QHBoxLayout(themeGroup);
    themeRow->setContentsMargins(12, 14, 12, 12);
    themeRow->setSpacing(10);

    auto *themeCol = new QVBoxLayout();
    themeCol->setSpacing(4);
    auto *themeLabel = new QLabel(utils::tr(QStringLiteral("settings.active_theme")), themeGroup);
    themeLabel->setProperty("kaiRole", QStringLiteral("caption"));
    themeCol->addWidget(themeLabel);

    m_themeField = new QComboBox(themeGroup);
    m_themeField->addItems(themes);
    const int themeIndex = m_themeField->findText(currentThemeName);
    if (themeIndex >= 0) {
        m_themeField->setCurrentIndex(themeIndex);
    } else if (!currentThemeName.isEmpty()) {
        m_themeField->addItem(currentThemeName);
        m_themeField->setCurrentText(currentThemeName);
    }
    themeCol->addWidget(m_themeField);
    themeRow->addLayout(themeCol, 1);

    auto *importButton = new QPushButton(utils::tr(QStringLiteral("settings.theme.import")), themeGroup);
    importButton->setIcon(LucideIcons::icon(QStringLiteral("upload"), QColor(utils::tokens::mutedFg()), 14));
    importButton->setToolTip(utils::tr(QStringLiteral("settings.theme.import.tip")));
    connect(importButton, &QPushButton::clicked, this, &AppearanceTab::importThemeClicked);
    themeRow->addWidget(importButton, 0, Qt::AlignBottom);

    layout->addWidget(themeGroup);


    // --- Grupo: Janela ---
    auto *windowGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.window")), this);
    auto *windowForm = new QFormLayout(windowGroup);
    windowForm->setSpacing(10);
    windowForm->setContentsMargins(12, 14, 12, 12);

    m_windowModeField = new QComboBox(windowGroup);
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.size")), QStringLiteral("size"));
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.maximized")), QStringLiteral("maximized"));
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.fullscreen")), QStringLiteral("fullscreen"));
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.remember")), QStringLiteral("remember"));
    {
        const int idx = m_windowModeField->findData(currentSettings.windowMode);
        m_windowModeField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    windowForm->addRow(utils::tr(QStringLiteral("settings.window.open_as")), m_windowModeField);

    m_windowPresetField = new QComboBox(windowGroup);
    m_windowPresetField->addItem(utils::tr(QStringLiteral("settings.window.custom")), QSize());
    const QVector<QSize> presets = {
        QSize(1024, 640), QSize(1280, 760), QSize(1440, 900),
        QSize(1600, 900), QSize(1920, 1080),
    };
    for (const QSize &p : presets) {
        m_windowPresetField->addItem(utils::tr(QStringLiteral("settings.window.size_format")).arg(p.width()).arg(p.height()), p);
    }
    windowForm->addRow(utils::tr(QStringLiteral("settings.window.size_label")), m_windowPresetField);

    auto *sizeRow = new QWidget(windowGroup);
    sizeRow->setObjectName(QStringLiteral("windowSizeRow"));
    sizeRow->setStyleSheet(QStringLiteral("QWidget#windowSizeRow { background: transparent; }"));
    auto *sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(8);

    m_windowWidthField = new QSpinBox(sizeRow);
    m_windowWidthField->setRange(720, 10000);
    m_windowWidthField->setSingleStep(20);
    m_windowWidthField->setValue(currentSettings.windowWidth);
    m_windowWidthField->setSuffix(QStringLiteral(" px"));

    m_windowHeightField = new QSpinBox(sizeRow);
    m_windowHeightField->setRange(480, 10000);
    m_windowHeightField->setSingleStep(20);
    m_windowHeightField->setValue(currentSettings.windowHeight);
    m_windowHeightField->setSuffix(QStringLiteral(" px"));

    sizeLayout->addWidget(new QLabel(utils::tr(QStringLiteral("settings.window.width")), sizeRow));
    sizeLayout->addWidget(m_windowWidthField, 1);
    sizeLayout->addWidget(new QLabel(utils::tr(QStringLiteral("settings.window.height")), sizeRow));
    sizeLayout->addWidget(m_windowHeightField, 1);
    windowForm->addRow(QString(), sizeRow);

    connect(m_windowPresetField, &QComboBox::currentIndexChanged, this, [this](int) {
        const QSize chosen = m_windowPresetField->currentData().toSize();
        if (chosen.isValid() && !chosen.isEmpty()) {
            m_windowWidthField->setValue(chosen.width());
            m_windowHeightField->setValue(chosen.height());
        }
    });

    auto updateSizeEnabled = [this, sizeRow]() {
        const QString mode = m_windowModeField->currentData().toString();
        const bool usesSize = (mode == QStringLiteral("size") || mode == QStringLiteral("remember"));
        sizeRow->setEnabled(usesSize);
        m_windowPresetField->setEnabled(usesSize);
    };
    connect(m_windowModeField, &QComboBox::currentIndexChanged, this, [updateSizeEnabled](int) { updateSizeEnabled(); });
    updateSizeEnabled();

    m_autoHideField = new QCheckBox(utils::tr(QStringLiteral("settings.window.auto_hide")), windowGroup);
    m_autoHideField->setProperty("kaiRole", QStringLiteral("switch"));
    m_autoHideField->setChecked(currentSettings.autoHideOnFocusLoss);
    m_autoHideField->setToolTip(utils::tr(QStringLiteral("settings.window.auto_hide.tip")));
    windowForm->addRow(QString(), m_autoHideField);

    m_startVisibleField = new QCheckBox(utils::tr(QStringLiteral("settings.window.start_visible")), windowGroup);
    m_startVisibleField->setProperty("kaiRole", QStringLiteral("switch"));
    m_startVisibleField->setChecked(currentSettings.startVisible);
    m_startVisibleField->setToolTip(utils::tr(QStringLiteral("settings.window.start_visible.tip")));
    windowForm->addRow(QString(), m_startVisibleField);


    layout->addWidget(windowGroup);

    // --- Grupo: Efeitos Visuais (FX) ---
    auto *fxGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.fx")), this);
    auto *fxLayout = new QVBoxLayout(fxGroup);
    fxLayout->setSpacing(8);
    fxLayout->setContentsMargins(12, 14, 12, 12);

    auto *fxHint = layout_helpers::makeHintBanner(fxGroup, utils::tr(QStringLiteral("settings.fx.hint")));
    fxLayout->addWidget(fxHint);

    m_fxShadowsField = new QCheckBox(utils::tr(QStringLiteral("settings.fx.shadows")), fxGroup);
    m_fxShadowsField->setProperty("kaiRole", QStringLiteral("switch"));
    m_fxShadowsField->setChecked(currentSettings.fxShadows);
    fxLayout->addWidget(m_fxShadowsField);

    m_fxTranslucencyField = new QCheckBox(utils::tr(QStringLiteral("settings.fx.translucency")), fxGroup);
    m_fxTranslucencyField->setProperty("kaiRole", QStringLiteral("switch"));
    m_fxTranslucencyField->setChecked(currentSettings.fxTranslucency);
    fxLayout->addWidget(m_fxTranslucencyField);

    m_fxBlurField = new QCheckBox(utils::tr(QStringLiteral("settings.fx.blur")), fxGroup);
    m_fxBlurField->setProperty("kaiRole", QStringLiteral("switch"));
    m_fxBlurField->setChecked(currentSettings.fxBlur);
    m_fxBlurField->setToolTip(utils::tr(QStringLiteral("settings.fx.blur.hint")));
    fxLayout->addWidget(m_fxBlurField);

    m_fxAnimationsField = new QCheckBox(utils::tr(QStringLiteral("settings.fx.animations")), fxGroup);
    m_fxAnimationsField->setProperty("kaiRole", QStringLiteral("switch"));
    m_fxAnimationsField->setChecked(currentSettings.fxAnimations);
    fxLayout->addWidget(m_fxAnimationsField);

    // Gradientes de tema (pedido do usuário): cada tema já define os pares
    // de cor pra janela/header/sidebar/badges — este switch é só o
    // interruptor mestre pra quem prefere um visual mais chapado/sólido.
    m_gradientsEnabledField = new QCheckBox(utils::tr(QStringLiteral("settings.fx.gradients")), fxGroup);
    m_gradientsEnabledField->setProperty("kaiRole", QStringLiteral("switch"));
    m_gradientsEnabledField->setChecked(currentSettings.gradientsEnabled);
    m_gradientsEnabledField->setToolTip(utils::tr(QStringLiteral("settings.fx.gradients.hint")));
    fxLayout->addWidget(m_gradientsEnabledField);

    layout->addWidget(fxGroup);
    layout->addStretch();
}

} // namespace kai::ui
