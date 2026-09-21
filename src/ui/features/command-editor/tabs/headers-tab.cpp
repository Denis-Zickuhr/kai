#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/key-value-editor-widget.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QListWidget>

namespace kai::ui {

// Headers (só faz sentido pro tipo HTTP) — card de PRIMEIRO NÍVEL, irmão
// de Parâmetros/Auto-responsores/Hooks (pedido do usuário: "devem ter seu
// bloco e ficar como no de baixo" — antes ficava ANINHADO dentro do card
// "Execution Config"). setExecutionMode() controla a visibilidade
// conforme Shell/HTTP.
void CommandEditorDialog::buildHeadersTab()
{
    auto *headersLayout = addNavPage(utils::tr(QStringLiteral("command.group.headers")), QStringLiteral("list"));
    m_navRowHeaders = m_sideNav->count() - 1;

    m_headersCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.headers")), this);
    m_headersCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.headers.empty_state")));
    m_headersCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    m_headersEditor = new KeyValueEditorWidget(m_headersCard, QStringLiteral("Header"),
        utils::tr(QStringLiteral("keyvalue.header.value")));
    m_headersEditor->setShowOwnAddButton(false);
    m_headersCard->setBody(m_headersEditor);
    connect(m_headersCard, &CollapsibleSectionCard::actionTriggered,
            m_headersEditor, &KeyValueEditorWidget::handleAddRowClicked);
    connect(m_headersEditor, &KeyValueEditorWidget::changed, this, [this]() {
        const int count = m_headersEditor->values().size();
        m_headersCard->setCount(count);
        bindNavItemCount(m_navRowHeaders, utils::tr(QStringLiteral("command.group.headers")), count);
    });
    m_headersCard->setCount(0);
    m_headersCard->setExpanded(true, false);
    m_headersCard->setCollapsible(false); // sozinho na própria aba — pedido do usuário: não precisa mais colapsar
    // SEM addStretch() aqui (ao contrário da aba Geral) — pedido do
    // usuário: "faça os elementos das abas ocuparem tudo... falta um
    // pedaço embaixo". Numa página com um card SÓ, deixar o QVBoxLayout
    // sem stretch faz esse único item herdar TODO o espaço sobrando — o
    // card cresce até preencher a página, em vez de parar do tamanho do
    // conteúdo e sobrar fundo cru visível embaixo.
    headersLayout->addWidget(m_headersCard);
}

} // namespace kai::ui
