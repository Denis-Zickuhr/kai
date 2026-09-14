#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/features/command-editor/execution-conditions-editor-widget.h"
#include "ui/features/environments/env-extractors-editor-widget.h"
#include "ui/features/http/http-ui.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace kai::ui {

// Extractors (só HTTP); setExecutionMode() controla a visibilidade.
void CommandEditorDialog::buildExtractorsTab()
{
    auto *extractorsLayout = addNavPage(utils::tr(QStringLiteral("command.group.response")), QStringLiteral("braces"));
    m_navRowExtractors = m_sideNav->count() - 1;
    m_extractorsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.extractors")), this);
    m_extractorsCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.extractors.empty_state")));
    m_extractorsCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    // Widget dedicado: o campo "persist" não cabe num QMap<QString,QString>.
    m_envExtractorsEditor = new EnvExtractorsEditorWidget(m_extractorsCard);
    m_extractorsCard->setBody(m_envExtractorsEditor);
    connect(m_extractorsCard, &CollapsibleSectionCard::actionTriggered,
            m_envExtractorsEditor, &EnvExtractorsEditorWidget::handleAddRowClicked);
    connect(m_envExtractorsEditor, &EnvExtractorsEditorWidget::changed, this, [this]() {
        const int count = m_envExtractorsEditor->totalCount();
        m_extractorsCard->setCount(count);
        bindNavItemCount(m_navRowExtractors, utils::tr(QStringLiteral("command.group.response")),
                         count + (m_assertionsEditor ? m_assertionsEditor->totalCount() : 0));
    });
    m_extractorsCard->setCount(0);
    m_extractorsCard->setExpanded(true, false);
    extractorsLayout->addWidget(m_extractorsCard);

    // Asserções: o que a resposta precisa satisfazer. Com alguma ligada, é ela
    // quem decide se a requisição deu certo (dá para esperar um 404, por exemplo).
    auto *assertionsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("http.group.assertions")), this);
    assertionsCard->setEmptyStateText(utils::tr(QStringLiteral("http.assertions.empty_state")));
    assertionsCard->setActionButtonText(utils::tr(QStringLiteral("conditions.add")));
    m_assertionsEditor = new ExecutionConditionsEditorWidget(assertionsCard);
    m_assertionsEditor->setBehaviorControlsVisible(false);
    assertionsCard->setBody(m_assertionsEditor);
    connect(assertionsCard, &CollapsibleSectionCard::actionTriggered,
            m_assertionsEditor, &ExecutionConditionsEditorWidget::handleAddRowClicked);
    connect(m_assertionsEditor, &ExecutionConditionsEditorWidget::changed, this, [this, assertionsCard]() {
        assertionsCard->setCount(m_assertionsEditor->totalCount());
        bindNavItemCount(m_navRowExtractors, utils::tr(QStringLiteral("command.group.response")),
                         m_envExtractorsEditor->totalCount() + m_assertionsEditor->totalCount());
    });
    assertionsCard->setCount(0);
    assertionsCard->setExpanded(true, false);
    extractorsLayout->addWidget(assertionsCard);

    // Guardar a resposta num arquivo (downloads): grava conforme chega, sem segurá-la em memória.
    auto *saveCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("http.group.save_response")), this);
    saveCard->setShowCountBadge(false);
    saveCard->setAlwaysShowBody(true);
    auto *saveBody = new QWidget(saveCard);
    makeTransparentHost(saveBody, QStringLiteral("httpSaveResponse"));
    auto *saveLayout = new QVBoxLayout(saveBody);
    saveLayout->setContentsMargins(0, 0, 0, 0);
    m_responseFileField = new QLineEdit(saveBody);
    m_responseFileField->setPlaceholderText(utils::tr(QStringLiteral("http.save_response.placeholder")));
    saveLayout->addWidget(m_responseFileField);
    saveCard->setBody(saveBody);
    saveCard->setExpanded(true, false);
    extractorsLayout->addWidget(saveCard);
    extractorsLayout->addStretch(1);
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
