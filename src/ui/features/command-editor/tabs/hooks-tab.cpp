#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/features/command-editor/hooks-editor-widget.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QListWidget>

namespace kai::ui {

// "Execution Hooks" — mesmo padrão de card colapsável dos outros dois
// acima (mockup enviado pelo usuário), com uma diferença: SEMPRE mostra o
// corpo (setAlwaysShowBody), mesmo com 0 hooks marcados — a lista de
// comandos disponíveis pra marcar precisa continuar visível (diferente de
// Parâmetros/Auto-responsores, que não têm nada pra mostrar até o usuário
// "+Add"). Sem botão de ação no cabeçalho (não há "adicionar" aqui, só
// marcar/desmarcar).
void CommandEditorDialog::buildHooksTab()
{
    auto *hooksLayout = addNavPage(utils::tr(QStringLiteral("command.tab.hooks")), QStringLiteral("link-2"));
    const int hooksNavRow = m_sideNav->count() - 1;
    auto *hooksCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.hooks")), this);
    hooksCard->setAlwaysShowBody(true);
    m_hooksEditor = new HooksEditorWidget(hooksCard);
    hooksCard->setBody(m_hooksEditor);
    connect(m_hooksEditor, &HooksEditorWidget::changed, this, [this, hooksCard, hooksNavRow]() {
        const int count = m_hooksEditor->totalCount();
        hooksCard->setCount(count);
        bindNavItemCount(hooksNavRow, utils::tr(QStringLiteral("command.tab.hooks")), count);
    });
    hooksCard->setCount(0);
    hooksCard->setExpanded(true, false);
    hooksCard->setCollapsible(false);
    hooksLayout->addWidget(hooksCard);
}

} // namespace kai::ui
