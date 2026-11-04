#include "ui/features/command-editor/command-editor-dialog.h"

#include "ui/features/http/http-auth-editor-widget.h"
#include "ui/features/http/http-entries-editor-widget.h"
#include "ui/features/http/http-options-editor-widget.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/folder-picker-widget.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QListWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace kai::ui {

namespace {
constexpr int kInhColOn = 0;
constexpr int kInhColName = 1;
constexpr int kInhColSource = 2;
} // namespace

// Página "Requisição" (só HTTP): headers herdados, headers do comando, query e autenticação, cada um um
// card de primeiro nível. setExecutionMode() controla a visibilidade.
void CommandEditorDialog::buildHeadersTab()
{
    auto *requestLayout = addNavPage(utils::tr(QStringLiteral("command.group.request")), QStringLiteral("list"));
    m_navRowHeaders = m_sideNav->count() - 1;

    // --- Headers herdados das pastas (só aparece quando há algum); desmarcar desliga o header neste comando ---
    m_inheritedHeadersCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("http.inherited.title")), this);
    m_inheritedHeadersCard->setAlwaysShowBody(true);
    m_inheritedHeadersTable = new QTableWidget(0, 3, m_inheritedHeadersCard);
    m_inheritedHeadersTable->setObjectName(QStringLiteral("inheritedHeadersTable"));
    configureTable(m_inheritedHeadersTable, {
        {QString(), selectionColumnWidth(), false},
        {QString(), 200, true},
        {QString(), 140, false},
    });
    m_inheritedHeadersTable->horizontalHeader()->setVisible(false);
    m_inheritedHeadersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_inheritedHeadersTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_inheritedHeadersTable->horizontalHeader()->setStretchLastSection(false);
    m_inheritedHeadersTable->horizontalHeader()->setSectionResizeMode(kInhColName, QHeaderView::Stretch);
    m_inheritedHeadersTable->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    m_inheritedHeadersTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_inheritedHeadersCard->setBody(m_inheritedHeadersTable);
    m_inheritedHeadersCard->setExpanded(true, false);
    requestLayout->addWidget(m_inheritedHeadersCard);
    m_inheritedHeadersCard->setVisible(false);

    // Card com tabela de linhas (adicionar no cabeçalho do card, editar por formulário).
    auto makeEntriesCard = [this, requestLayout](const QString &titleKey, const QString &emptyKey,
                                                 HttpEntriesEditorWidget **editorOut) {
        auto *card = new CollapsibleSectionCard(utils::tr(titleKey), this);
        card->setEmptyStateText(utils::tr(emptyKey));
        card->setActionButtonText(utils::tr(QStringLiteral("keyvalue.add")));
        auto *editor = new HttpEntriesEditorWidget(card);
        editor->setShowOwnAddButton(false);
        card->setBody(editor);
        connect(card, &CollapsibleSectionCard::actionTriggered, editor, &HttpEntriesEditorWidget::handleAddRowClicked);
        connect(editor, &HttpEntriesEditorWidget::changed, this, [this, card, editor]() {
            card->setCount(editor->count());
            if (editor == m_headersEditor) {
                bindNavItemCount(m_navRowHeaders, utils::tr(QStringLiteral("command.group.request")), editor->count());
            }
        });
        card->setCount(0);
        card->setExpanded(true, false);
        requestLayout->addWidget(card);
        *editorOut = editor;
        return card;
    };
    m_headersCard = makeEntriesCard(QStringLiteral("command.group.headers"),
                                    QStringLiteral("command_editor.headers.empty_state"), &m_headersEditor);
    makeEntriesCard(QStringLiteral("http.group.query"), QStringLiteral("http.query.empty_state"), &m_queryEditor);

    auto *authCard = new CollapsibleSectionCard(utils::tr(QStringLiteral("http.group.auth")), this);
    authCard->setShowCountBadge(false);
    authCard->setAlwaysShowBody(true);
    m_authEditor = new HttpAuthEditorWidget(/*allowInherit=*/true, authCard);
    authCard->setBody(m_authEditor);
    authCard->setExpanded(true, false);
    requestLayout->addWidget(authCard);

    requestLayout->addStretch(1);
}

// Conexão (só HTTP): redirecionamentos, TLS, proxy, tempo limite e cookies.
void CommandEditorDialog::buildConnectionTab()
{
    auto *layout = addNavPage(utils::tr(QStringLiteral("command.group.connection")), QStringLiteral("globe"));
    m_navRowConnection = m_sideNav->count() - 1;

    auto *card = new CollapsibleSectionCard(utils::tr(QStringLiteral("command.group.connection")), this);
    card->setShowCountBadge(false);
    card->setAlwaysShowBody(true);
    m_optionsEditor = new HttpOptionsEditorWidget(card);
    card->setBody(m_optionsEditor);
    card->setExpanded(true, false);
    card->setCollapsible(false);
    layout->addWidget(card);
    layout->addStretch(1);
}

// Recalcula o que as pastas acima entregam ao comando (headers e autenticação).
// Linhas desmarcadas viram, ao salvar, um header desligado no próprio comando.
void CommandEditorDialog::refreshInheritedHttp()
{
    if (!m_inheritedHeadersTable || !m_folderField) {
        return;
    }
    const QVector<core::Folder> chain = core::folderChain(currentFolderId(), m_allFolders);

    // Guarda o que o usuário já desmarcou antes de reconstruir a lista (a pasta pode ter mudado).
    for (int row = 0; row < m_inheritedHeaders.size() && row < m_inheritedHeadersTable->rowCount(); ++row) {
        const QCheckBox *box = selectionCheckboxAt(m_inheritedHeadersTable, row, kInhColOn);
        const QString key = m_inheritedHeaders.at(row).entry.name.toLower();
        if (box && !box->isChecked()) {
            m_headersSwitchedOff.insert(key);
        } else if (box) {
            m_headersSwitchedOff.remove(key);
        }
    }

    m_inheritedHeaders = core::inheritedHttpHeaders(chain);
    m_inheritedHeadersTable->setRowCount(0);
    m_inheritedHeadersTable->setRowCount(m_inheritedHeaders.size());
    for (int row = 0; row < m_inheritedHeaders.size(); ++row) {
        const core::InheritedHttpHeader &h = m_inheritedHeaders.at(row);
        m_inheritedHeadersTable->setCellWidget(row, kInhColOn,
            makeSelectionCheckbox(m_inheritedHeadersTable, !m_headersSwitchedOff.contains(h.entry.name.toLower())));
        m_inheritedHeadersTable->setItem(row, kInhColName, new QTableWidgetItem(h.entry.name));
        m_inheritedHeadersTable->setItem(row, kInhColSource, new QTableWidgetItem(h.folderName));
    }
    m_inheritedHeadersCard->setCount(m_inheritedHeaders.size());
    m_inheritedHeadersCard->setVisible(!m_inheritedHeaders.isEmpty());

}

} // namespace kai::ui
