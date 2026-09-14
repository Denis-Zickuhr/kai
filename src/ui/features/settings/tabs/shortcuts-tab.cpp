#include "ui/features/settings/tabs/shortcuts-tab.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/shortcut-capture-field.h"
#include "ui/shared/shortcuts-manager-widget.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/shared/lucide-icons.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QColor>

namespace kai::ui {

ShortcutsTab::ShortcutsTab(const core::SettingsData &currentSettings, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    // Busca ao vivo + "Restaurar Padrões" lado a lado (mockup enviado pelo
    // usuário — o botão de restaurar faltava nesta tela até agora).
    // Reaproveita FuzzySearchBar (mesmo componente/algoritmo da busca
    // principal do app).
    auto *searchRow = new QHBoxLayout();
    searchRow->setSpacing(8);
    auto *searchField = new FuzzySearchBar(this);
    searchField->setPlaceholderText(utils::tr(QStringLiteral("settings.shortcuts.search.placeholder")));
    searchRow->addWidget(searchField, 1);
    auto *resetButton = new QPushButton(utils::tr(QStringLiteral("settings.shortcuts.reset")), this);
    resetButton->setIcon(LucideIcons::icon(QStringLiteral("rotate-ccw"), QColor(utils::tokens::mutedFg()), 14));
    searchRow->addWidget(resetButton);
    layout->addLayout(searchRow);

    // Atalho GLOBAL do SO (mostra/esconde o Kai) fica À PARTE da tabela —
    // usa QHotkey (registro no sistema operacional), uma máquina
    // completamente diferente dos QShortcut do Shortcuts Manager v2 logo
    // abaixo, então não faz sentido fingir que é "mais uma linha". Usa a
    // borda neutra padrão do tema (a borda de accent foi removida a pedido
    // do usuário).
    auto *globalGroup = new QGroupBox(utils::tr(QStringLiteral("settings.group.global_shortcut")), this);
    auto *globalForm = new QFormLayout(globalGroup);
    globalForm->addRow(layout_helpers::makeHintBanner(globalGroup,
        utils::tr(QStringLiteral("settings.shortcut.global.hint"))));
    m_hotkeyField = new ShortcutCaptureField(currentSettings.globalHotkey, globalGroup);
    globalForm->addRow(utils::tr(QStringLiteral("settings.shortcut.global")), m_hotkeyField);
    layout->addWidget(globalGroup);

    // Shortcuts Manager v2 (pedido do usuário): tabela rolável com TODAS
    // as ações, atalhos atuais como chips removíveis, "+" pra adicionar
    // mais um (multi-binding) — ver ShortcutsManagerWidget. Envolvida num
    // CollapsibleSectionCard (pedido do usuário: poder colapsar/expandir a
    // tabela; o card já dá fundo surface2 + borda arredondada que segue a
    // preferência de canto, uniformizando a cor de fundo).
    m_shortcutsManager = new ShortcutsManagerWidget(this);
    m_shortcutsManager->setShortcuts(currentSettings.shortcuts);
    auto *shortcutsCard = new CollapsibleSectionCard(
        utils::tr(QStringLiteral("settings.shortcuts.table_title")), this);
    shortcutsCard->setAlwaysShowBody(true);
    shortcutsCard->setExpanded(false, false); // colapsado por padrão (pedido do usuário)
    shortcutsCard->setBody(m_shortcutsManager);
    connect(searchField, &FuzzySearchBar::queryChanged, m_shortcutsManager, &ShortcutsManagerWidget::setFilterText);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        m_shortcutsManager->resetToDefaults();
        m_hotkeyField->setKeySequenceString(core::SettingsData().globalHotkey);
    });
    layout->addWidget(shortcutsCard, 0, Qt::AlignTop);
    layout->addStretch(1);
    // sem wrapPage: a própria tabela já rola internamente (SettingsDialog
    // adiciona esta aba direto ao QStackedWidget, sem QScrollArea).
}

} // namespace kai::ui
