#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/features/environments/declared-env-vars-editor-widget.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QListWidget>

namespace kai::ui {

// "Variáveis exportáveis" — a LISTA BRANCA de "Export variables" (achado
// de segurança real: exportar TUDO que o ambiente mudasse vazava env de
// sistema/distro/WSL e quebrava comandos downstream — ver DeclaredEnvVar
// no core). Só faz sentido pro tipo Shell (é o resultado de rodar um
// PROCESSO), espelha o mesmo padrão do card de Extractors (HTTP).
void CommandEditorDialog::buildDeclaredEnvVarsTab()
{
    auto *declaredEnvVarsLayout = addNavPage(
        utils::tr(QStringLiteral("command.tab.exportable_vars")), QStringLiteral("share-2"));
    m_navRowDeclaredEnvVars = m_sideNav->count() - 1;
    m_declaredEnvVarsCard = new CollapsibleSectionCard(
        utils::tr(QStringLiteral("command.group.declared_env_vars")), this);
    m_declaredEnvVarsCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.declared_env_vars.empty_state")));
    m_declaredEnvVarsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    m_declaredEnvVarsEditor = new DeclaredEnvVarsEditorWidget(m_declaredEnvVarsCard);
    m_declaredEnvVarsCard->setBody(m_declaredEnvVarsEditor);
    connect(m_declaredEnvVarsCard, &CollapsibleSectionCard::actionTriggered,
            m_declaredEnvVarsEditor, &DeclaredEnvVarsEditorWidget::handleAddRowClicked);
    connect(m_declaredEnvVarsEditor, &DeclaredEnvVarsEditorWidget::changed, this, [this]() {
        const int count = m_declaredEnvVarsEditor->totalCount();
        m_declaredEnvVarsCard->setCount(count);
        bindNavItemCount(m_navRowDeclaredEnvVars, utils::tr(QStringLiteral("command.tab.exportable_vars")), count);
    });
    m_declaredEnvVarsCard->setCount(0);
    m_declaredEnvVarsCard->setExpanded(true, false);
    m_declaredEnvVarsCard->setCollapsible(false);
    declaredEnvVarsLayout->addWidget(m_declaredEnvVarsCard);
}

} // namespace kai::ui
