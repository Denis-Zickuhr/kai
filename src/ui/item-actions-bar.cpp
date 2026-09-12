#include "ui/item-actions-bar.h"
#include "ui/lucide-icons.h"
#include "utils/translation-manager.h"

#include <QSize>

namespace kai::ui {

namespace {
constexpr int kIconSize = 16;
}

ItemActionsBar::ItemActionsBar(QWidget *parent)
    : QWidget(parent)
    , m_accent(189, 147, 249) // accent padrão (Dracula) até o tema carregar
{
    setupUi();
}

void ItemActionsBar::setupUi()
{
    m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);

    m_btnEditFolder = createIconButton(QStringLiteral("folder-pen"), utils::tr(QStringLiteral("sidebar.edit_folder")));
    m_btnNewFolder = createIconButton(QStringLiteral("folder-plus"), utils::tr(QStringLiteral("sidebar.new_folder")));
    m_btnNewCommand = createIconButton(QStringLiteral("square-terminal"), utils::tr(QStringLiteral("sidebar.new_command")));
    m_btnNewCollection = createIconButton(QStringLiteral("database"), utils::tr(QStringLiteral("sidebar.new_collection")));
    m_btnEditSelected = createIconButton(QStringLiteral("pencil"), utils::tr(QStringLiteral("tree.edit_command")));
    m_btnDeleteSelected = createIconButton(QStringLiteral("trash-2"), utils::tr(QStringLiteral("tree.delete_command")));

    connect(m_btnEditFolder, &QPushButton::clicked, this, &ItemActionsBar::editCurrentFolderRequested);
    connect(m_btnNewFolder, &QPushButton::clicked, this, &ItemActionsBar::newFolderRequested);
    connect(m_btnNewCommand, &QPushButton::clicked, this, &ItemActionsBar::newCommandRequested);
    connect(m_btnNewCollection, &QPushButton::clicked, this, &ItemActionsBar::newCollectionRequested);
    connect(m_btnEditSelected, &QPushButton::clicked, this, &ItemActionsBar::editSelectedRequested);
    connect(m_btnDeleteSelected, &QPushButton::clicked, this, &ItemActionsBar::deleteSelectedRequested);

    for (auto *btn : {m_btnEditFolder, m_btnNewFolder, m_btnNewCommand, m_btnNewCollection,
                       m_btnEditSelected, m_btnDeleteSelected}) {
        m_layout->addWidget(btn);
    }
    m_layout->addStretch(1);

    setRowContext(false);
}

void ItemActionsBar::setOrientation(Qt::Orientation orientation)
{
    m_layout->setDirection(orientation == Qt::Horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
}

void ItemActionsBar::setRowContext(bool hasSelection)
{
    // Editar/excluir valem para pasta, comando ou coleção — só dependem de
    // haver seleção.
    m_btnEditSelected->setEnabled(hasSelection);
    m_btnDeleteSelected->setEnabled(hasSelection);
}

QPushButton *ItemActionsBar::createIconButton(const QString &iconName, const QString &tooltip)
{
    auto *btn = new QPushButton(this);
    btn->setIcon(LucideIcons::icon(iconName, m_accent, kIconSize));
    btn->setIconSize(QSize(kIconSize, kIconSize));
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid transparent; border-radius: 4px;"
        " padding: 4px; min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.06); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.10); }"
    ));
    m_iconButtons.append({btn, iconName});
    return btn;
}

void ItemActionsBar::applyAccentColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    m_accent = color;
    for (const auto &pair : m_iconButtons) {
        if (pair.first) {
            pair.first->setIcon(LucideIcons::icon(pair.second, color, kIconSize));
        }
    }
}

} // namespace kai::ui
