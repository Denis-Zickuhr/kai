#include "ui/features/settings/tabs/layout-tab.h"
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
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QFileDialog>
#include <QColor>
#include <QSize>
#include "ui/shared/file-dialogs.h"

namespace kai::ui {

namespace tk = utils::tokens;

GridPreview::GridPreview(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("gridPreview"));
    setMinimumHeight(minimumSizeHint().height());
}

void GridPreview::setModel(const layout::LayoutModel &model)
{
    m_model = model;
    update();
}

void GridPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF area = QRectF(rect()).adjusted(1, 1, -1, -1);
    const int cols = m_model.cols();
    const int rows = m_model.rows();
    const double radius = qMin<double>(tk::radiusSm(), 6.0);

    // A grade nominal: o que o usuário configurou, célula a célula.
    QColor faint(tk::mutedFg());
    faint.setAlpha(110);
    p.setPen(QPen(faint, 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QRectF cell(area.x() + area.width() * col / cols, area.y() + area.height() * row / rows,
                              area.width() / cols, area.height() / rows);
            p.drawRoundedRect(cell.adjusted(2, 2, -2, -2), radius, radius);
        }
    }

    // O que aparece: cada célula com painel, esticada pelo modo de expansão sobre as vazias.
    const layout::GridPlan plan = m_model.plan();
    if (plan.empty()) {
        return;
    }
    const bool vertical = plan.expansion == layout::Expansion::Vertical;
    QColor fill(tk::accent());
    fill.setAlpha(70);
    p.setPen(QPen(QColor(tk::accent()), 1));
    p.setBrush(fill);
    double offset = 0.0;
    for (int i = 0; i < plan.stacks.size(); ++i) {
        const layout::GridStack &stack = plan.stacks.at(i);
        const double lineSpan = (vertical ? area.width() : area.height()) * plan.lineWeights.at(i);
        double inner = 0.0;
        for (int j = 0; j < stack.cells.size(); ++j) {
            const double span = (vertical ? area.height() : area.width()) * stack.weights.at(j);
            const QRectF cell = vertical ? QRectF(area.x() + offset, area.y() + inner, lineSpan, span)
                                         : QRectF(area.x() + inner, area.y() + offset, span, lineSpan);
            p.drawRoundedRect(cell.adjusted(4, 4, -4, -4), radius, radius);
            inner += span;
        }
        offset += lineSpan;
    }
}

LayoutTab::LayoutTab(const core::SettingsData &currentSettings, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

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

    // Linha 1: Painéis (restaurar o layout) e Ações de Item
    m_restoreLayoutButton = new QPushButton(utils::tr(QStringLiteral("settings.layout.restore")), layoutGroup);
    m_restoreLayoutButton->setObjectName(QStringLiteral("restoreLayoutButton"));
    m_restoreLayoutButton->setCursor(Qt::PointingHandCursor);
    connect(m_restoreLayoutButton, &QPushButton::clicked, this, &LayoutTab::restoreLayoutRequested);
    addFieldToGrid(1, 0, QStringLiteral("settings.layout.panels"), m_restoreLayoutButton);

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
        const QString file = pickOpenFile(this,
            utils::tr(QStringLiteral("settings.commands_background.browse")),
            utils::tr(QStringLiteral("settings.commands_background.filter")), m_commandsBgImageField->text());
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

    // Linha 5: Linhas de conexão da árvore (Nativa/Nenhuma/Contínua)
    m_treeConnectorStyleField = new QComboBox(layoutGroup);
    m_treeConnectorStyleField->addItem(utils::tr(QStringLiteral("settings.tree_lines.native")), 0);
    m_treeConnectorStyleField->addItem(utils::tr(QStringLiteral("settings.tree_lines.none")), 1);
    m_treeConnectorStyleField->addItem(utils::tr(QStringLiteral("settings.tree_lines.continuous")), 2);
    {
        const int idx = m_treeConnectorStyleField->findData(currentSettings.treeConnectorStyle);
        m_treeConnectorStyleField->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addFieldToGrid(5, 0, QStringLiteral("settings.tree_lines"), m_treeConnectorStyleField, 2);

    layout->addWidget(layoutGroup);

    // --- Grupo: Grade de painéis (colunas × linhas e o modo de expansão das células vazias)
    auto *gridGroup = new QGroupBox(utils::tr(QStringLiteral("settings.layout.grid.title")), this);
    auto *gridLayout = new QGridLayout(gridGroup);
    gridLayout->setContentsMargins(12, 14, 12, 12);
    gridLayout->setHorizontalSpacing(14);
    gridLayout->setVerticalSpacing(8);
    gridLayout->setColumnStretch(0, 1);
    gridLayout->setColumnStretch(1, 1);
    const auto addGridField = [gridGroup, gridLayout](int row, int col, const QString &labelKey, QWidget *field) {
        auto *label = new QLabel(utils::tr(labelKey), gridGroup);
        label->setProperty("kaiRole", QStringLiteral("caption"));
        gridLayout->addWidget(label, row * 2, col);
        gridLayout->addWidget(field, row * 2 + 1, col);
    };
    m_gridCols = new QSpinBox(gridGroup);
    m_gridCols->setObjectName(QStringLiteral("gridCols"));
    m_gridCols->setRange(1, layout::kMaxGridLines);
    m_gridCols->setValue(layout::LayoutModel::kDefaultCols);
    addGridField(0, 0, QStringLiteral("settings.layout.grid.cols"), m_gridCols);
    m_gridRows = new QSpinBox(gridGroup);
    m_gridRows->setObjectName(QStringLiteral("gridRows"));
    m_gridRows->setRange(1, layout::kMaxGridLines);
    m_gridRows->setValue(layout::LayoutModel::kDefaultRows);
    addGridField(0, 1, QStringLiteral("settings.layout.grid.rows"), m_gridRows);
    m_gridExpansion = new QComboBox(gridGroup);
    m_gridExpansion->setObjectName(QStringLiteral("gridExpansion"));
    m_gridExpansion->addItem(utils::tr(QStringLiteral("settings.layout.grid.expand.vertical")),
                             int(layout::Expansion::Vertical));
    m_gridExpansion->addItem(utils::tr(QStringLiteral("settings.layout.grid.expand.horizontal")),
                             int(layout::Expansion::Horizontal));
    addGridField(1, 0, QStringLiteral("settings.layout.grid.expansion"), m_gridExpansion);
    m_gridPreview = new GridPreview(gridGroup);
    gridLayout->addWidget(m_gridPreview, 4, 0, 1, 2);
    layout->addWidget(gridGroup);

    const auto emitGrid = [this]() {
        if (m_syncingGrid) {
            return;
        }
        const auto expansion = layout::Expansion(m_gridExpansion->currentData().toInt());
        m_layoutModel.setGrid(m_gridCols->value(), m_gridRows->value(), expansion);
        m_gridPreview->setModel(m_layoutModel);
        emit gridChangeRequested(m_gridCols->value(), m_gridRows->value(), int(expansion));
    };
    connect(m_gridCols, &QSpinBox::valueChanged, this, emitGrid);
    connect(m_gridRows, &QSpinBox::valueChanged, this, emitGrid);
    connect(m_gridExpansion, &QComboBox::currentIndexChanged, this, emitGrid);
    m_gridPreview->setModel(m_layoutModel);

    layout->addStretch();
}

void LayoutTab::setLayoutModel(const layout::LayoutModel &model)
{
    m_layoutModel = model;
    m_syncingGrid = true;
    m_gridCols->setValue(model.cols());
    m_gridRows->setValue(model.rows());
    m_gridExpansion->setCurrentIndex(m_gridExpansion->findData(int(model.expansion())));
    m_syncingGrid = false;
    m_gridPreview->setModel(m_layoutModel);
}

} // namespace kai::ui
