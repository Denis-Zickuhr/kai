#include "ui/features/settings/tabs/actions-tab.h"

#include "ui/shared/actions-editor-widget.h"
#include "ui/shared/collapsible-section-card.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>

namespace kai::ui {

// Mesmo padrão da aba de Parâmetros do comando: card com contador e "Adicionar" no
// cabeçalho, tabela só com o nome e um formulário pra criar/editar (aqui com a opção
// "só em pastas-projeto").
ActionsTab::ActionsTab(const QVector<core::GlobalAction> &actions, const QVector<core::Command> &commands,
                       const QVector<core::Folder> &folders, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *card = new CollapsibleSectionCard(utils::tr(QStringLiteral("settings.group.actions")), this);
    card->setEmptyStateText(utils::tr(QStringLiteral("settings.actions.hint")));
    card->setActionButtonText(utils::tr(QStringLiteral("actions_editor.add")));
    m_editor = new ActionsEditorWidget(ActionsEditorWidget::Mode::Global, card);
    m_editor->setObjectName(QStringLiteral("globalActionsEditor"));
    m_editor->setAvailableCommands(commands, folders);
    card->setBody(m_editor);
    connect(card, &CollapsibleSectionCard::actionTriggered, m_editor, &ActionsEditorWidget::handleAddRowClicked);
    connect(m_editor, &ActionsEditorWidget::changed, this, [this, card]() { card->setCount(m_editor->count()); });
    m_editor->setGlobalActions(actions);
    card->setCount(m_editor->count());
    card->setExpanded(true, false);
    card->setCollapsible(false);
    layout->addWidget(card);
}

QVector<core::GlobalAction> ActionsTab::globalActions() const
{
    return m_editor->globalActions();
}

} // namespace kai::ui
