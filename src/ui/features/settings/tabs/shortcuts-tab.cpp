#include "ui/features/settings/tabs/shortcuts-tab.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/shortcut-capture-field.h"
#include "ui/shared/shortcuts-manager-widget.h"
#include "ui/features/settings/tabs/settings-form.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/token-style.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
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

    namespace sf = settings_form;
    QGridLayout *grid = nullptr;

    // Atalhos GLOBAIS do SO: usam QHotkey, uma máquina diferente dos QShortcut da tabela, então ficam num grupo próprio.
    // O primeiro mostra/esconde o Kai; o segundo abre só a paleta (Ctrl+K) de fora do Kai, sem trazer a janela.
    auto *globalGroup = sf::makeGroup(this, utils::tr(QStringLiteral("settings.shortcuts.section.global")), &grid);
    m_hotkeyField = new ShortcutCaptureField(currentSettings.globalHotkey, globalGroup);
    sf::addField(grid, globalGroup, utils::tr(QStringLiteral("settings.shortcut.global")), m_hotkeyField,
                 utils::tr(QStringLiteral("settings.shortcut.global.hint")));
    m_paletteHotkeyField = new ShortcutCaptureField(currentSettings.paletteHotkey, globalGroup);
    sf::addField(grid, globalGroup, utils::tr(QStringLiteral("settings.shortcut.palette")), m_paletteHotkeyField,
                 utils::tr(QStringLiteral("settings.shortcut.palette.hint")));
    layout->addWidget(globalGroup);

    // Tabela sempre à mostra: recolhida por padrão a busca filtrava linhas invisíveis.
    m_shortcutsManager = new ShortcutsManagerWidget(this);
    m_shortcutsManager->setShortcuts(currentSettings.shortcuts);
    QGridLayout *appGrid = nullptr;
    auto *appGroup = sf::makeGroup(this, utils::tr(QStringLiteral("settings.shortcuts.section.app")), &appGrid);
    appGrid->addWidget(m_shortcutsManager, 0, 0, 1, 2);
    appGrid->setRowStretch(0, 1);
    connect(searchField, &FuzzySearchBar::queryChanged, m_shortcutsManager, &ShortcutsManagerWidget::setFilterText);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        m_shortcutsManager->resetToDefaults();
        m_hotkeyField->setKeySequenceString(core::SettingsData().globalHotkey);
        m_paletteHotkeyField->setKeySequenceString(core::SettingsData().paletteHotkey);
    });
    layout->addWidget(appGroup, 1);
    // Sem wrapPage: a tabela já rola (SettingsDialog adiciona esta aba direto ao QStackedWidget).
}

} // namespace kai::ui
