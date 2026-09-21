#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/features/command-editor/output-responders-editor-widget.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QListWidget>

namespace kai::ui {

void CommandEditorDialog::buildRespondersTab()
{
    auto *respLayout = addNavPage(utils::tr(QStringLiteral("command.tab.responders")), QStringLiteral("webhook"));
    const int respNavRow = m_sideNav->count() - 1;
    auto *respCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.responders")), this);
    respCard->setEmptyStateText(utils::tr(QStringLiteral("responders.empty_state")));
    respCard->setActionButtonText(utils::tr(QStringLiteral("responders.add")));
    m_respondersEditor = new OutputRespondersEditorWidget(respCard);
    respCard->setBody(m_respondersEditor);
    connect(respCard, &CollapsibleSectionCard::actionTriggered,
            m_respondersEditor, &OutputRespondersEditorWidget::handleAddRowClicked);
    connect(m_respondersEditor, &OutputRespondersEditorWidget::changed, this, [this, respCard, respNavRow]() {
        const int count = m_respondersEditor->responders().size();
        respCard->setCount(count);
        bindNavItemCount(respNavRow, utils::tr(QStringLiteral("command.tab.responders")), count);
    });
    respCard->setCount(0);
    respCard->setExpanded(true, false);
    respCard->setCollapsible(false);
    respLayout->addWidget(respCard);
}

} // namespace kai::ui
