#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/features/command-editor/parameter-editor-widget.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QListWidget>

namespace kai::ui {

// Parâmetros: compartilhados entre Shell e HTTP (feedback do usuário —
// params também em comandos HTTP, com replace {{param}} no path/URL, body
// e headers). Ficam fora das abas Shell/HTTP para servirem a ambos os
// tipos.
//
// Primeiro card no novo padrão visual (mockup enviado pelo usuário):
// cabeçalho com chevron/título/badge de contagem/botão de ação rápida,
// corpo com estado vazio OU a tabela — ver CollapsibleSectionCard. O
// editor em si (ParameterEditorWidget) não mudou por dentro; o card só
// decide se mostra ele ou o estado vazio, conforme a contagem.
void CommandEditorDialog::buildParamsTab()
{
    auto *paramsLayout = addNavPage(utils::tr(QStringLiteral("command.tab.params")), QStringLiteral("sliders-horizontal"));
    const int paramsNavRow = m_sideNav->count() - 1;
    auto *paramsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.params")), this);
    paramsCard->setEmptyStateText(utils::tr(QStringLiteral("params.empty_state")));
    paramsCard->setActionButtonText(utils::tr(QStringLiteral("params.add")));
    m_paramsEditor = new ParameterEditorWidget(paramsCard);
    paramsCard->setBody(m_paramsEditor);
    connect(paramsCard, &CollapsibleSectionCard::actionTriggered,
            m_paramsEditor, &ParameterEditorWidget::handleAddRowClicked);
    connect(m_paramsEditor, &ParameterEditorWidget::changed, this, [this, paramsCard, paramsNavRow]() {
        const int count = m_paramsEditor->parameters().size();
        paramsCard->setCount(count);
        bindNavItemCount(paramsNavRow, utils::tr(QStringLiteral("command.tab.params")), count);
    });
    paramsCard->setCount(0); // sincronizado de verdade após setParameters() (comando existente, ver setupUi)
    paramsCard->setExpanded(true, false);
    paramsCard->setCollapsible(false);
    paramsLayout->addWidget(paramsCard);
}

void CommandEditorDialog::setAvailableCollections(const QVector<core::Collection> &collections)
{
    if (!m_paramsEditor) {
        return;
    }
    // Injeta as coleções e reconstrói as linhas para a coluna "Fonte".
    // Usa os params ORIGINAIS (m_originalParams) em vez de reler a tabela:
    // no construtor, setParameters rodou sem coleções, então os combos
    // ficaram sem casar o collectionId — reler agora perderia a escolha.
    // Mescla: qualquer edição de nome/label/tipo feita antes de as
    // coleções chegarem é rara (o diálogo abre já com coleções), então
    // priorizamos preservar a fonte de dados escolhida.
    QVector<core::Parameter> params = m_originalParams;
    if (params.isEmpty()) {
        params = m_paramsEditor->parameters();
    }
    m_paramsEditor->setAvailableCollections(collections);
    m_paramsEditor->setParameters(params);
}

} // namespace kai::ui
