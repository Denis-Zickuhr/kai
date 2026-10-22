#include "ui/features/settings/tabs/appearance-tab.h"
#include "ui/features/settings/tabs/settings-form.h"
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
    namespace sf = settings_form;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(sf::kPageMargin, sf::kPageMargin, sf::kPageMargin, sf::kPageMargin);
    layout->setSpacing(sf::kPageSpacing);

    QGridLayout *grid = nullptr;

    // --- Grupo: Tema ---
    auto *themeGroup = sf::makeGroup(this, utils::tr(QStringLiteral("settings.group.theme")), &grid);
    auto *themeRow = new QWidget(themeGroup);
    themeRow->setObjectName(QStringLiteral("themeRow"));
    themeRow->setStyleSheet(QStringLiteral("QWidget#themeRow { background: transparent; }"));
    auto *themeRowLayout = new QHBoxLayout(themeRow);
    themeRowLayout->setContentsMargins(0, 0, 0, 0);
    themeRowLayout->setSpacing(utils::tokens::space(2));

    m_themeField = new QComboBox(themeRow);
    m_themeField->addItems(themes);
    const int themeIndex = m_themeField->findText(currentThemeName);
    if (themeIndex >= 0) {
        m_themeField->setCurrentIndex(themeIndex);
    } else if (!currentThemeName.isEmpty()) {
        m_themeField->addItem(currentThemeName);
        m_themeField->setCurrentText(currentThemeName);
    }
    themeRowLayout->addWidget(m_themeField, 1);

    auto *importButton = new QPushButton(utils::tr(QStringLiteral("settings.theme.import")), themeRow);
    importButton->setIcon(LucideIcons::icon(QStringLiteral("upload"), QColor(utils::tokens::mutedFg()), 14));
    importButton->setToolTip(utils::tr(QStringLiteral("settings.theme.import.tip")));
    connect(importButton, &QPushButton::clicked, this, &AppearanceTab::importThemeClicked);
    themeRowLayout->addWidget(importButton);
    sf::addField(grid, themeGroup, utils::tr(QStringLiteral("settings.active_theme")), themeRow);
    layout->addWidget(themeGroup);

    // --- Grupo: Janela ---
    auto *windowGroup = sf::makeGroup(this, utils::tr(QStringLiteral("settings.group.window")), &grid);

    m_windowModeField = new QComboBox(windowGroup);
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.size")), QStringLiteral("size"));
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.maximized")), QStringLiteral("maximized"));
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.fullscreen")), QStringLiteral("fullscreen"));
    m_windowModeField->addItem(utils::tr(QStringLiteral("settings.window.remember")), QStringLiteral("remember"));
    {
        const int idx = m_windowModeField->findData(currentSettings.windowMode);
        m_windowModeField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    sf::addField(grid, windowGroup, utils::tr(QStringLiteral("settings.window.open_as")), m_windowModeField);

    auto *sizeRow = new QWidget(windowGroup);
    sizeRow->setObjectName(QStringLiteral("windowSizeRow"));
    sizeRow->setStyleSheet(QStringLiteral("QWidget#windowSizeRow { background: transparent; }"));
    auto *sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(utils::tokens::space(2));

    m_windowPresetField = new QComboBox(sizeRow);
    m_windowPresetField->addItem(utils::tr(QStringLiteral("settings.window.custom")), QSize());
    const QVector<QSize> presets = {
        QSize(1024, 640), QSize(1280, 760), QSize(1440, 900),
        QSize(1600, 900), QSize(1920, 1080),
    };
    for (const QSize &p : presets) {
        m_windowPresetField->addItem(utils::tr(QStringLiteral("settings.window.size_format")).arg(p.width()).arg(p.height()), p);
    }

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

    sizeLayout->addWidget(m_windowPresetField, 1);
    sizeLayout->addWidget(new QLabel(utils::tr(QStringLiteral("settings.window.width")), sizeRow));
    sizeLayout->addWidget(m_windowWidthField);
    sizeLayout->addWidget(new QLabel(utils::tr(QStringLiteral("settings.window.height")), sizeRow));
    sizeLayout->addWidget(m_windowHeightField);
    sf::addField(grid, windowGroup, utils::tr(QStringLiteral("settings.window.size_label")), sizeRow);

    connect(m_windowPresetField, &QComboBox::currentIndexChanged, this, [this](int) {
        const QSize chosen = m_windowPresetField->currentData().toSize();
        if (chosen.isValid() && !chosen.isEmpty()) {
            m_windowWidthField->setValue(chosen.width());
            m_windowHeightField->setValue(chosen.height());
        }
    });

    auto updateSizeEnabled = [this, sizeRow]() {
        const QString mode = m_windowModeField->currentData().toString();
        sizeRow->setEnabled(mode == QStringLiteral("size") || mode == QStringLiteral("remember"));
    };
    connect(m_windowModeField, &QComboBox::currentIndexChanged, this, [updateSizeEnabled](int) { updateSizeEnabled(); });
    updateSizeEnabled();

    m_autoHideField = sf::addSwitch(grid, windowGroup, utils::tr(QStringLiteral("settings.window.auto_hide")),
        currentSettings.autoHideOnFocusLoss, utils::tr(QStringLiteral("settings.window.auto_hide.tip")));
    m_startVisibleField = sf::addSwitch(grid, windowGroup, utils::tr(QStringLiteral("settings.window.start_visible")),
        currentSettings.startVisible, utils::tr(QStringLiteral("settings.window.start_visible.tip")));
    layout->addWidget(windowGroup);

    // --- Grupo: Efeitos Visuais (FX) ---
    auto *fxGroup = sf::makeGroup(this, utils::tr(QStringLiteral("settings.group.fx")), &grid);
    fxGroup->setToolTip(utils::tr(QStringLiteral("settings.fx.hint")));
    m_fxShadowsField = sf::addSwitch(grid, fxGroup, utils::tr(QStringLiteral("settings.fx.shadows")),
                                     currentSettings.fxShadows);
    m_fxTranslucencyField = sf::addSwitch(grid, fxGroup, utils::tr(QStringLiteral("settings.fx.translucency")),
                                          currentSettings.fxTranslucency);
    m_fxBlurField = sf::addSwitch(grid, fxGroup, utils::tr(QStringLiteral("settings.fx.blur")),
                                  currentSettings.fxBlur, utils::tr(QStringLiteral("settings.fx.blur.hint")));
    m_fxAnimationsField = sf::addSwitch(grid, fxGroup, utils::tr(QStringLiteral("settings.fx.animations")),
                                        currentSettings.fxAnimations);
    // Interruptor mestre dos gradientes de tema, para quem prefere um visual mais chapado.
    m_gradientsEnabledField = sf::addSwitch(grid, fxGroup, utils::tr(QStringLiteral("settings.fx.gradients")),
                                            currentSettings.gradientsEnabled,
                                            utils::tr(QStringLiteral("settings.fx.gradients.hint")));
    layout->addWidget(fxGroup);
    layout->addStretch();
}

} // namespace kai::ui
