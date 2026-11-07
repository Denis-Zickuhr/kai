#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/features/command-editor/pipeline-steps-editor-widget.h"
#include "ui/shared/collapsible-section-card.h"
#include "utils/translation-manager.h"

#include <QListWidget>
#include <QVBoxLayout>

namespace kai::ui {

void CommandEditorDialog::buildStepsTab()
{
    auto *layout = addNavPage(utils::tr(QStringLiteral("command.tab.steps")), QStringLiteral("list-checks"));
    m_navRowSteps = m_sideNav->count() - 1;
    auto *card = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.steps")), this);
    card->setEmptyStateText(utils::tr(QStringLiteral("pipeline.steps.empty_state")));
    card->setActionButtonText(utils::tr(QStringLiteral("pipeline.steps.add")));
    m_stepsEditor = new PipelineStepsEditorWidget(card);
    card->setBody(m_stepsEditor);
    connect(card, &CollapsibleSectionCard::actionTriggered, m_stepsEditor, &PipelineStepsEditorWidget::handleAddRowClicked);
    connect(m_stepsEditor, &PipelineStepsEditorWidget::changed, this, [this, card]() {
        const int count = m_stepsEditor->steps().size();
        card->setCount(count);
        bindNavItemCount(m_navRowSteps, utils::tr(QStringLiteral("command.tab.steps")), count);
    });
    card->setCount(0);
    card->setExpanded(true, false);
    card->setCollapsible(false);
    layout->addWidget(card);
}

} // namespace kai::ui
