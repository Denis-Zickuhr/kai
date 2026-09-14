#include "ui/features/http/http-defaults-editor-widget.h"

#include <QVBoxLayout>

#include "ui/features/http/http-auth-editor-widget.h"
#include "ui/features/http/http-entries-editor-widget.h"
#include "ui/features/http/http-options-editor-widget.h"
#include "ui/shared/collapsible-section-card.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

HttpDefaultsEditorWidget::HttpDefaultsEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(3));

    m_headersCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.headers")), this);
    m_headersCard->setEmptyStateText(utils::tr(QStringLiteral("command_editor.headers.empty_state")));
    m_headersCard->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
    m_headers = new HttpEntriesEditorWidget(m_headersCard);
    m_headers->setShowOwnAddButton(false);
    m_headersCard->setBody(m_headers);
    connect(m_headersCard, &CollapsibleSectionCard::actionTriggered, m_headers,
            &HttpEntriesEditorWidget::handleAddRowClicked);
    connect(m_headers, &HttpEntriesEditorWidget::changed, this, [this]() {
        m_headersCard->setCount(m_headers->count());
        emit changed();
    });
    m_headersCard->setCount(0);
    m_headersCard->setExpanded(true, false);
    layout->addWidget(m_headersCard);

    auto *authCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("http.group.auth")), this);
    authCard->setShowCountBadge(false);
    authCard->setAlwaysShowBody(true);
    m_auth = new HttpAuthEditorWidget(/*allowInherit=*/true, authCard);
    authCard->setBody(m_auth);
    authCard->setExpanded(true, false);
    connect(m_auth, &HttpAuthEditorWidget::changed, this, &HttpDefaultsEditorWidget::changed);
    layout->addWidget(authCard);

    auto *optionsCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("http.group.options")), this);
    optionsCard->setShowCountBadge(false);
    optionsCard->setAlwaysShowBody(true);
    m_options = new HttpOptionsEditorWidget(optionsCard);
    optionsCard->setBody(m_options);
    optionsCard->setExpanded(true, false);
    connect(m_options, &HttpOptionsEditorWidget::changed, this, &HttpDefaultsEditorWidget::changed);
    layout->addWidget(optionsCard);

    layout->addStretch(1);
}

void HttpDefaultsEditorWidget::setDefaults(const core::HttpDefaults &defaults)
{
    m_headers->setEntries(defaults.headers);
    m_auth->setAuth(defaults.auth);
    m_options->setOptions(defaults.options);
}

core::HttpDefaults HttpDefaultsEditorWidget::defaults() const
{
    core::HttpDefaults d;
    d.headers = m_headers->entries();
    d.auth = m_auth->auth();
    d.options = m_options->options();
    return d;
}

int HttpDefaultsEditorWidget::definedCount() const
{
    const core::HttpDefaults d = defaults();
    int n = d.headers.size();
    if (!d.auth.isDefault()) ++n;
    if (!d.options.isEmpty()) ++n;
    return n;
}

} // namespace kai::ui
