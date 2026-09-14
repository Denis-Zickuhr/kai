#include "ui/shared/expand-collapse-bar.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QSize>

namespace kai::ui {

namespace {
constexpr int kIconSize = 16;

QString buttonStyle()
{
    return QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid transparent; border-radius: %1px;"
        " padding: 4px; min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.06); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.10); }"
        // Toggle LIGADO (mostrar ocultos / só em execução): fundo e contorno na
        // cor de destaque — um filtro ativo não pode passar despercebido.
        "QPushButton:checked { background-color: rgba(255, 255, 255, 0.10); border: 1px solid %2; }"
    ).arg(utils::tokens::radiusSm()).arg(utils::tokens::accent());
}
}

ExpandCollapseBar::ExpandCollapseBar(QWidget *parent)
    : QWidget(parent)
    , m_accent(189, 147, 249) // accent padrão (Dracula) até o tema carregar
{
    setupUi();
}

void ExpandCollapseBar::setupUi()
{
    m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);

    m_btnExpandSelected = createIconButton(QStringLiteral("chevron-down"), utils::tr(QStringLiteral("tree.expand_selected")));
    m_btnCollapseSelected = createIconButton(QStringLiteral("chevron-right"), utils::tr(QStringLiteral("tree.collapse_selected")));
    m_btnExpandAll = createIconButton(QStringLiteral("expand"), utils::tr(QStringLiteral("tree.expand_all")));
    m_btnCollapseAll = createIconButton(QStringLiteral("minimize"), utils::tr(QStringLiteral("tree.collapse_all")));
    m_btnToggleHidden = createIconButton(QStringLiteral("eye-off"), utils::tr(QStringLiteral("tree.hide_selected")));
    m_btnToggleShowHidden = createIconButton(QStringLiteral("eye"), utils::tr(QStringLiteral("tree.show_hidden")));
    m_btnToggleShowHidden->setCheckable(true);
    m_btnToggleRunningOnly = createIconButton(QStringLiteral("activity"), utils::tr(QStringLiteral("tree.show_running_only")));
    m_btnToggleRunningOnly->setCheckable(true);

    connect(m_btnExpandSelected, &QPushButton::clicked, this, &ExpandCollapseBar::expandSelectedRequested);
    connect(m_btnCollapseSelected, &QPushButton::clicked, this, &ExpandCollapseBar::collapseSelectedRequested);
    connect(m_btnExpandAll, &QPushButton::clicked, this, &ExpandCollapseBar::expandAllRequested);
    connect(m_btnCollapseAll, &QPushButton::clicked, this, &ExpandCollapseBar::collapseAllRequested);
    connect(m_btnToggleHidden, &QPushButton::clicked, this, &ExpandCollapseBar::toggleHiddenSelectedRequested);
    connect(m_btnToggleShowHidden, &QPushButton::clicked, this, &ExpandCollapseBar::toggleShowHiddenRequested);
    connect(m_btnToggleShowHidden, &QPushButton::clicked, this, [this]() { registerEyeClick(); });
    connect(m_btnToggleRunningOnly, &QPushButton::clicked, this, &ExpandCollapseBar::toggleRunningOnlyRequested);

    for (auto *btn : {m_btnExpandSelected, m_btnCollapseSelected, m_btnExpandAll, m_btnCollapseAll,
                       m_btnToggleHidden, m_btnToggleShowHidden, m_btnToggleRunningOnly}) {
        m_layout->addWidget(btn);
    }
    m_layout->addStretch(1);

    setStyleSheet(buttonStyle());
}

void ExpandCollapseBar::setOrientation(Qt::Orientation orientation)
{
    m_layout->setDirection(orientation == Qt::Horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
}

void ExpandCollapseBar::setSelectedHidden(bool hidden)
{
    m_selectedHidden = hidden;
    m_btnToggleHidden->setIcon(LucideIcons::icon(
        hidden ? QStringLiteral("eye") : QStringLiteral("eye-off"), m_accent, kIconSize));
    m_btnToggleHidden->setToolTip(utils::tr(
        hidden ? QStringLiteral("tree.show_selected") : QStringLiteral("tree.hide_selected")));
}

void ExpandCollapseBar::registerEyeClick()
{
    constexpr qint64 kMaxGapMs = 700;   // entre um clique e o seguinte
    constexpr int kClicksNeeded = 5;
    if (!m_eyeLastClick.isValid() || m_eyeLastClick.elapsed() > kMaxGapMs) {
        m_eyeClicks = 0;
    }
    m_eyeLastClick.restart();
    if (++m_eyeClicks >= kClicksNeeded) {
        m_eyeClicks = 0;
        emit eyeEasterEggTriggered();
    }
}

void ExpandCollapseBar::setShowingHidden(bool showing)
{
    m_btnToggleShowHidden->setChecked(showing);
}

void ExpandCollapseBar::setShowingRunningOnly(bool runningOnly)
{
    m_btnToggleRunningOnly->setChecked(runningOnly);
}

QPushButton *ExpandCollapseBar::createIconButton(const QString &iconName, const QString &tooltip)
{
    auto *btn = new QPushButton(this);
    btn->setIcon(LucideIcons::icon(iconName, m_accent, kIconSize));
    btn->setIconSize(QSize(kIconSize, kIconSize));
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    m_iconButtons.append({btn, iconName});
    return btn;
}

void ExpandCollapseBar::applyAccentColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    m_accent = color;
    // O estilo (contorno do toggle ligado, na cor de destaque) é montado a
    // partir dos tokens: refaz na troca de tema.
    setStyleSheet(buttonStyle());
    for (const auto &pair : m_iconButtons) {
        if (pair.first) {
            pair.first->setIcon(LucideIcons::icon(pair.second, color, kIconSize));
        }
    }
    // Os dois botões de ocultar/mostrar têm ícone dinâmico — recoloridos
    // via re-chamada com o estado atual em vez de estarem em m_iconButtons.
    setSelectedHidden(m_selectedHidden);
}

} // namespace kai::ui
