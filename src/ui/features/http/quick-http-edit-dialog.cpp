#include "ui/features/http/quick-http-edit-dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include "ui/features/http/http-auth-editor-widget.h"
#include "ui/features/http/http-body-editor-widget.h"
#include "ui/features/http/http-entries-editor-widget.h"
#include "ui/features/http/http-ui.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

using layout_helpers::wrapWithLabel;

QuickHttpEditDialog::QuickHttpEditDialog(const QString &commandName, const core::HttpConfig &config, QWidget *parent)
    : QDialog(parent)
    , m_original(config)
{
    setWindowTitle(utils::tr(QStringLiteral("quick_body_editor.title")).arg(commandName));
    resize(760, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(utils::tokens::space(3));

    auto *row = new QHBoxLayout();
    row->setSpacing(utils::tokens::space(3));
    m_method = new QComboBox(this);
    m_method->addItems({QStringLiteral("GET"), QStringLiteral("POST"), QStringLiteral("PUT"), QStringLiteral("PATCH"),
                        QStringLiteral("DELETE"), QStringLiteral("QUERY"), QStringLiteral("HEAD"), QStringLiteral("OPTIONS")});
    m_method->setEditable(true);
    m_method->setInsertPolicy(QComboBox::NoInsert);
    m_method->setMinimumWidth(110);
    m_method->setCurrentText(config.methodName());
    row->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("command.field.method")), m_method), 0);
    m_url = new QLineEdit(config.url, this);
    row->addWidget(wrapWithLabel(this, utils::tr(QStringLiteral("command.field.url")), m_url), 1);
    layout->addLayout(row);

    // Mesma navegação lateral dos editores de comando e de pasta.
    auto *body = new QWidget(this);
    SidebarTabsHost host = buildSidebarTabsHost(body, 170);
    m_nav = host.nav;

    auto *bodyLayout = addSidebarTabPage(host, utils::tr(QStringLiteral("command.group.body")), QStringLiteral("file-text"));
    auto *bodyCard = layout_helpers::makeSurfaceCard(body);
    auto *bodyCardLayout = new QVBoxLayout(bodyCard);
    bodyCardLayout->setContentsMargins(utils::tokens::space(3), utils::tokens::space(3),
                                       utils::tokens::space(3), utils::tokens::space(3));
    m_body = new HttpBodyEditorWidget(bodyCard);
    m_body->setConfig(config);
    m_body->setLabelVisible(false);
    bodyCardLayout->addWidget(m_body);
    bodyLayout->addWidget(bodyCard);
    bodyLayout->addStretch(1);

    // Query e headers: card com "+ Adicionar" no cabeçalho, como na página Requisição do editor completo.
    auto makeEntriesPage = [&](const QString &titleKey, const QString &emptyKey, const QString &icon,
                               const QVector<core::HttpEntry> &entries) {
        auto *pageLayout = addSidebarTabPage(host, utils::tr(titleKey), icon);
        auto *card = new CollapsibleSectionCard(utils::tr(titleKey), body);
        card->setEmptyStateText(utils::tr(emptyKey));
        card->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
        auto *editor = new HttpEntriesEditorWidget(card);
        editor->setShowOwnAddButton(false);
        card->setBody(editor);
        connect(card, &CollapsibleSectionCard::actionTriggered, editor, &HttpEntriesEditorWidget::handleAddRowClicked);
        connect(editor, &HttpEntriesEditorWidget::changed, this, [this, card, editor]() {
            card->setCount(editor->count());
            updateNavTitles();
        });
        editor->setEntries(entries);
        card->setExpanded(true, false);
        pageLayout->addWidget(card);
        pageLayout->addStretch(1);
        return editor;
    };
    m_query = makeEntriesPage(QStringLiteral("http.group.query"), QStringLiteral("http.query.empty_state"),
                              QStringLiteral("link"), config.queryParams);
    m_headers = makeEntriesPage(QStringLiteral("command.group.headers"), QStringLiteral("command_editor.headers.empty_state"),
                                QStringLiteral("list"), config.headers);

    auto *authLayout = addSidebarTabPage(host, utils::tr(QStringLiteral("http.group.auth")), QStringLiteral("key-round"));
    auto *authCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("http.group.auth")), body);
    authCard->setShowCountBadge(false);
    authCard->setAlwaysShowBody(true);
    m_auth = new HttpAuthEditorWidget(/*allowInherit=*/true, authCard);
    m_auth->setAuth(config.auth);
    authCard->setBody(m_auth);
    authCard->setExpanded(true, false);
    authCard->setCollapsible(false);
    authLayout->addWidget(authCard);
    authLayout->addStretch(1);

    updateNavTitles();
    m_nav->setCurrentRow(0);
    layout->addWidget(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    centerOnParent(this);
}

void QuickHttpEditDialog::updateNavTitles()
{
    if (!m_query || !m_headers) {
        return; // ainda montando as páginas
    }
    auto titled = [](const QString &key, int count) {
        const QString base = utils::tr(key);
        return count > 0 ? QStringLiteral("%1 (%2)").arg(base, QString::number(count)) : base;
    };
    m_nav->item(1)->setText(titled(QStringLiteral("http.group.query"), m_query->count()));
    m_nav->item(2)->setText(titled(QStringLiteral("command.group.headers"), m_headers->count()));
}

core::HttpConfig QuickHttpEditDialog::config() const
{
    core::HttpConfig config = m_original;
    config.setMethodName(m_method->currentText());
    config.url = m_url->text();
    config.queryParams = m_query->entries();
    config.headers = m_headers->entries();
    config.auth = m_auth->auth();
    m_body->applyTo(config);
    return config;
}

} // namespace kai::ui
