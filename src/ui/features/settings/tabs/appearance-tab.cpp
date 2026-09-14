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

    // --- Grupo: Layout (Mudado de 3 para 2 Colunas) ---
    auto *layoutGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.layout")), this);
    auto *layoutGrid = new QGridLayout(layoutGroup);
    layoutGrid->setContentsMargins(12, 14, 12, 12);
    layoutGrid->setHorizontalSpacing(14); // Maior separação entre colunas
    layoutGrid->setVerticalSpacing(8);

    // Agora dividimos em apenas 2 colunas para dar mais fôlego horizontal aos elementos
    for (int col = 0; col < 2; ++col) {
        layoutGrid->setColumnStretch(col, 1);
    }

    auto addFieldToGrid = [layoutGroup, layoutGrid](int row, int col, const QString &labelKey, QWidget *field, int colSpan = 1) {
        auto *label = new QLabel(utils::tr(labelKey), layoutGroup);
        label->setProperty("kaiRole", QStringLiteral("caption"));

        layoutGrid->addWidget(label, row * 2, col, 1, colSpan);
        layoutGrid->addWidget(field, row * 2 + 1, col, 1, colSpan);
    };

    // Linha 0: Densidade e Canto
    m_densityField = new QComboBox(layoutGroup);
    m_densityField->addItem(utils::tr(QStringLiteral("settings.density.comfortable")), QStringLiteral("comfortable"));
    m_densityField->addItem(utils::tr(QStringLiteral("settings.density.compact")), QStringLiteral("compact"));
    {
        const int idx = m_densityField->findData(currentSettings.uiDensity);
        m_densityField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_densityField->setToolTip(utils::tr(QStringLiteral("settings.density.hint")));
    addFieldToGrid(0, 0, QStringLiteral("settings.density"), m_densityField);

    m_cornerStyleField = new QComboBox(layoutGroup);
    m_cornerStyleField->addItem(utils::tr(QStringLiteral("settings.corner.straight")), 0);
    m_cornerStyleField->addItem(utils::tr(QStringLiteral("settings.corner.soft")), 1);
    m_cornerStyleField->addItem(utils::tr(QStringLiteral("settings.corner.rounded")), 2);
    {
        const int idx = m_cornerStyleField->findData(currentSettings.uiCornerStyle);
        m_cornerStyleField->setCurrentIndex(idx >= 0 ? idx : 1);
    }
    addFieldToGrid(0, 1, QStringLiteral("settings.corners"), m_cornerStyleField);

    // Linha 1: Posição do Output e Ações de Item
    m_outputPositionField = new QComboBox(layoutGroup);
    m_outputPositionField->addItem(utils::tr(QStringLiteral("settings.output_position.bottom")), QStringLiteral("bottom"));
    m_outputPositionField->addItem(utils::tr(QStringLiteral("settings.output_position.left")), QStringLiteral("left"));
    m_outputPositionField->addItem(utils::tr(QStringLiteral("settings.output_position.right")), QStringLiteral("right"));
    {
        const int idx = m_outputPositionField->findData(currentSettings.outputPosition);
        m_outputPositionField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addFieldToGrid(1, 0, QStringLiteral("settings.output_position"), m_outputPositionField);

    auto makePlacementCombo = [layoutGroup](const QString &currentValue) {
        auto *combo = new QComboBox(layoutGroup);
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.upper")), QStringLiteral("upper"));
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.bottom")), QStringLiteral("bottom"));
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.left")), QStringLiteral("left"));
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.side")), QStringLiteral("side"));
        combo->addItem(utils::tr(QStringLiteral("settings.action_groups.hidden")), QStringLiteral("hidden"));
        const int idx = combo->findData(currentValue);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
        return combo;
    };

    m_itemActionsPlacementField = makePlacementCombo(currentSettings.itemActionsPlacement);
    addFieldToGrid(1, 1, QStringLiteral("settings.action_groups.item"), m_itemActionsPlacementField);

    // Linha 2: Ações de Display e Ações de Execução
    m_displayActionsPlacementField = makePlacementCombo(currentSettings.displayActionsPlacement);
    addFieldToGrid(2, 0, QStringLiteral("settings.action_groups.display"), m_displayActionsPlacementField);

    m_executionActionsPlacementField = makePlacementCombo(currentSettings.executionActionsPlacement);
    addFieldToGrid(2, 1, QStringLiteral("settings.action_groups.execution"), m_executionActionsPlacementField);

    // Linha 3: Imagem de Fundo (Ocupa as 2 colunas para não cortar caminhos longos)
    m_commandsBgImageField = new QLineEdit(layoutGroup);
    m_commandsBgImageField->setText(currentSettings.commandsBackgroundImage);
    m_commandsBgImageField->setPlaceholderText(utils::tr(QStringLiteral("settings.commands_background.placeholder")));
    m_commandsBgImageField->setReadOnly(true);
    addFieldToGrid(3, 0, QStringLiteral("settings.commands_background.image"), m_commandsBgImageField, 2);

    // Linha 4: Botões do Fundo (Esq) + Slider de Opacidade (Dir)
    auto *bgButtons = new QWidget(layoutGroup);
    bgButtons->setObjectName(QStringLiteral("bgButtonsRow"));
    bgButtons->setStyleSheet(QStringLiteral("QWidget#bgButtonsRow { background: transparent; }"));
    auto *bgButtonsLayout = new QHBoxLayout(bgButtons);
    bgButtonsLayout->setContentsMargins(0, 0, 0, 0);
    bgButtonsLayout->setSpacing(8);

    auto *bgBrowse = new QPushButton(utils::tr(QStringLiteral("settings.commands_background.browse")), bgButtons);
    auto *bgClear = new QPushButton(utils::tr(QStringLiteral("settings.commands_background.clear")), bgButtons);

    const QString bgBtnQss = QStringLiteral(
        "QPushButton#bgActionButton { background-color: %1; color: %2;"
        " border: 1px solid %3; border-radius: %4px; padding: %5px %6px; }"
        "QPushButton#bgActionButton:hover { border-color: %7; }")
        .arg(utils::tokens::bg(), utils::tokens::fg(), utils::tokens::borderColor())
        .arg(utils::tokens::radiusMd())
        .arg(utils::tokens::space(2)).arg(utils::tokens::space(3))
        .arg(utils::tokens::accent());

    for (QPushButton *b : {bgBrowse, bgClear}) {
        b->setObjectName(QStringLiteral("bgActionButton"));
        b->setAttribute(Qt::WA_StyledBackground, true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(bgBtnQss);
    }
    connect(bgBrowse, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this,
            utils::tr(QStringLiteral("settings.commands_background.browse")), QString(),
            utils::tr(QStringLiteral("settings.commands_background.filter")));
        if (!file.isEmpty()) {
            m_commandsBgImageField->setText(file);
        }
    });
    connect(bgClear, &QPushButton::clicked, this, [this]() {
        m_commandsBgImageField->clear();
    });
    bgButtonsLayout->addWidget(bgBrowse);
    bgButtonsLayout->addWidget(bgClear);
    bgButtonsLayout->addStretch();
    addFieldToGrid(4, 0, QStringLiteral("settings.commands_background.file"), bgButtons);

    auto *opacityRow = new QWidget(layoutGroup);
    opacityRow->setObjectName(QStringLiteral("bgOpacityRow"));
    opacityRow->setStyleSheet(QStringLiteral("QWidget#bgOpacityRow { background: transparent; }"));
    auto *opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->setSpacing(8);
    m_commandsBgOpacityField = new QSlider(Qt::Horizontal, opacityRow);
    m_commandsBgOpacityField->setRange(0, 100);
    m_commandsBgOpacityField->setValue(qBound(0, currentSettings.commandsBackgroundOpacity, 100));

    m_commandsBgOpacityField->setStyleSheet(QStringLiteral(
        "QSlider { background: transparent; }"
        "QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: %1; }"
        "QSlider::sub-page:horizontal { height: 4px; border-radius: 2px; background: %2; }"
        "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0;"
        " border-radius: 7px; background: %2; }")
        .arg(utils::tokens::surface2(), utils::tokens::accent()));
    auto *opacityValue = new QLabel(QStringLiteral("%1%").arg(m_commandsBgOpacityField->value()), opacityRow);
    connect(m_commandsBgOpacityField, &QSlider::valueChanged, opacityValue, [opacityValue](int v) {
        opacityValue->setText(QStringLiteral("%1%").arg(v));
    });
    opacityLayout->addWidget(m_commandsBgOpacityField, 1);
    opacityLayout->addWidget(opacityValue);
    addFieldToGrid(4, 1, QStringLiteral("settings.commands_background.opacity"), opacityRow);

    // Linha 5: Tamanho máx de logs (Span de 2 colunas)
    m_outputMaxLogSizeField = new QSpinBox(layoutGroup);
    m_outputMaxLogSizeField->setRange(64, 64 * 1024);
    m_outputMaxLogSizeField->setSingleStep(256);
    m_outputMaxLogSizeField->setSuffix(QStringLiteral(" KB"));
    m_outputMaxLogSizeField->setValue(currentSettings.outputMaxLogSizeKb);
    addFieldToGrid(5, 0, QStringLiteral("settings.output_max_log_size"), m_outputMaxLogSizeField, 2);

    // Linha 6: Linhas de conexão da árvore (Nativa/Nenhuma/Contínua)
    m_treeConnectorStyleField = new QComboBox(layoutGroup);
    m_treeConnectorStyleField->addItem(utils::tr(QStringLiteral("settings.tree_lines.native")), 0);
    m_treeConnectorStyleField->addItem(utils::tr(QStringLiteral("settings.tree_lines.none")), 1);
    m_treeConnectorStyleField->addItem(utils::tr(QStringLiteral("settings.tree_lines.continuous")), 2);
    {
        const int idx = m_treeConnectorStyleField->findData(currentSettings.treeConnectorStyle);
        m_treeConnectorStyleField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addFieldToGrid(6, 0, QStringLiteral("settings.tree_lines"), m_treeConnectorStyleField, 2);

    layout->addWidget(layoutGroup);

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

    layout->addWidget(fxGroup);
    layout->addStretch();
}

} // namespace kai::ui
