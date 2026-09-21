#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/features/environments/env-extractors-editor-widget.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QListWidget>

namespace kai::ui {

// Extractors (só faz sentido pro tipo HTTP) — card de PRIMEIRO NÍVEL,
// irmão de Parâmetros/Auto-responsores/Hooks. setExecutionMode() controla
// a visibilidade conforme Shell/HTTP.
void CommandEditorDialog::buildExtractorsTab()
{
    auto *extractorsLayout = addNavPage(utils::tr(QStringLiteral("command.group.extractors")), QStringLiteral("braces"));
    m_navRowExtractors = m_sideNav->count() - 1;
    m_extractorsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.extractors")), this);
    m_extractorsCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.extractors.empty_state")));
    m_extractorsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    // Widget dedicado (não mais KeyValueEditorWidget genérico) — o campo
    // "persist" não cabe num QMap<QString,QString> simples.
    m_envExtractorsEditor = new EnvExtractorsEditorWidget(m_extractorsCard);
    m_extractorsCard->setBody(m_envExtractorsEditor);
    connect(m_extractorsCard, &CollapsibleSectionCard::actionTriggered,
            m_envExtractorsEditor, &EnvExtractorsEditorWidget::handleAddRowClicked);
    connect(m_envExtractorsEditor, &EnvExtractorsEditorWidget::changed, this, [this]() {
        const int count = m_envExtractorsEditor->totalCount();
        m_extractorsCard->setCount(count);
        bindNavItemCount(m_navRowExtractors, utils::tr(QStringLiteral("command.group.extractors")), count);
    });
    m_extractorsCard->setCount(0);
    m_extractorsCard->setExpanded(true, false);
    m_extractorsCard->setCollapsible(false);
    extractorsLayout->addWidget(m_extractorsCard);
}

void CommandEditorDialog::populateEnvExtractorsEditor(const QVector<core::EnvExtractor> &extractors)
{
    m_envExtractorsEditor->setExtractors(extractors);
}

QVector<core::EnvExtractor> CommandEditorDialog::readEnvExtractors() const
{
    return m_envExtractorsEditor->extractors();
}

} // namespace kai::ui
