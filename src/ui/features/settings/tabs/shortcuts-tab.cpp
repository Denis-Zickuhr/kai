#include "ui/features/settings/tabs/shortcuts-tab.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/shortcut-capture-field.h"
#include "ui/shared/shortcuts-manager-widget.h"
#include "ui/features/settings/tabs/settings-form.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/shared/lucide-icons.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QColor>

namespace kai::ui {

ShortcutsTab::ShortcutsTab(const core::SettingsData &currentSettings, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(settings_form::kPageMargin, settings_form::kPageMargin,
                               settings_form::kPageMargin, settings_form::kPageMargin);
    layout->setSpacing(settings_form::kPageSpacing);

    // Busca ao vivo (por ação ou por combinação de teclas) + "Restaurar Padrões".
    auto *searchRow = new QHBoxLayout();
    searchRow->setSpacing(utils::tokens::space(2));
    auto *searchField = new FuzzySearchBar(this);
    searchField->setObjectName(QStringLiteral("shortcutsSearch"));
    searchField->setPlaceholderText(utils::tr(QStringLiteral("settings.shortcuts.search.placeholder")));
    searchRow->addWidget(searchField, 1);
    auto *resetButton = new QPushButton(utils::tr(QStringLiteral("settings.shortcuts.reset")), this);
    resetButton->setIcon(LucideIcons::icon(QStringLiteral("rotate-ccw"), QColor(utils::tokens::mutedFg()), 14));
    searchRow->addWidget(resetButton);
    layout->addLayout(searchRow);

    // Atalho GLOBAL do SO (mostra/esconde o Kai): usa QHotkey, uma máquina diferente dos
    // QShortcut da tabela, então fica numa linha à parte.
    auto *globalRow = new QHBoxLayout();
    globalRow->setSpacing(utils::tokens::space(2));
    auto *globalLabel = new QLabel(utils::tr(QStringLiteral("settings.shortcut.global")), this);
    globalLabel->setToolTip(utils::tr(QStringLiteral("settings.shortcut.global.hint")));
    m_hotkeyField = new ShortcutCaptureField(currentSettings.globalHotkey, this);
    m_hotkeyField->setToolTip(utils::tr(QStringLiteral("settings.shortcut.global.hint")));
    globalRow->addWidget(globalLabel);
    globalRow->addWidget(m_hotkeyField, 1);
    layout->addLayout(globalRow);

    // Tabela sempre à mostra: recolhida por padrão a busca filtrava linhas invisíveis.
    m_shortcutsManager = new ShortcutsManagerWidget(this);
    m_shortcutsManager->setShortcuts(currentSettings.shortcuts);
    auto *tableCard = new QFrame(this);
    tableCard->setObjectName(QStringLiteral("shortcutsCard"));
    tableCard->setStyleSheet(QStringLiteral(
        "QFrame#shortcutsCard { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
        .arg(utils::tokens::surface2(), utils::tokens::borderColor()).arg(utils::tokens::radiusLg()));
    auto *cardLayout = new QVBoxLayout(tableCard);
    cardLayout->setContentsMargins(4, 4, 4, 4);
    cardLayout->addWidget(m_shortcutsManager);
    connect(searchField, &FuzzySearchBar::queryChanged, m_shortcutsManager, &ShortcutsManagerWidget::setFilterText);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        m_shortcutsManager->resetToDefaults();
        m_hotkeyField->setKeySequenceString(core::SettingsData().globalHotkey);
    });
    layout->addWidget(tableCard, 1);
    // sem wrapPage: a própria tabela já rola internamente (SettingsDialog
    // adiciona esta aba direto ao QStackedWidget, sem QScrollArea).
}

} // namespace kai::ui
