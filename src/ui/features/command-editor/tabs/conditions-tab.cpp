#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/features/command-editor/execution-conditions-editor-widget.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QListWidget>

namespace kai::ui {

// Antes do card de Hooks: a condição decide se o comando roda, os hooks rodam ao redor. Vale para o
// comando principal e para hooks.
void CommandEditorDialog::buildConditionsTab()
{
    auto *conditionsLayout = addNavPage(utils::tr(QStringLiteral("command.tab.conditions")), QStringLiteral("git-branch"));
    const int conditionsNavRow = m_sideNav->count() - 1;
    auto *conditionsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.conditions")), this);
    conditionsCard->setEmptyStateText(utils::tr(QStringLiteral("conditions.empty_state")));
    conditionsCard->setActionButtonText(utils::tr(QStringLiteral("conditions.add")));
    m_conditionsEditor = new ExecutionConditionsEditorWidget(conditionsCard);
    conditionsCard->setBody(m_conditionsEditor);
    connect(conditionsCard, &CollapsibleSectionCard::actionTriggered,
            m_conditionsEditor, &ExecutionConditionsEditorWidget::handleAddRowClicked);
    connect(m_conditionsEditor, &ExecutionConditionsEditorWidget::changed, this,
        [this, conditionsCard, conditionsNavRow]() {
        const int count = m_conditionsEditor->totalCount();
        conditionsCard->setCount(count);
        bindNavItemCount(conditionsNavRow, utils::tr(QStringLiteral("command.tab.conditions")), count);
    });
    conditionsCard->setCount(0);
    conditionsCard->setExpanded(true, false);
    conditionsCard->setCollapsible(false);
    conditionsLayout->addWidget(conditionsCard);
}

} // namespace kai::ui
