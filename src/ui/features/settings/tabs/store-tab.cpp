#include "ui/features/settings/tabs/store-tab.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/store-local.h"
#include "core/store-texts.h"
#include "ui/features/settings/tabs/settings-form.h"
#include "ui/features/store/store-provider-dialog.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/table-utils.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace {
constexpr int kColName = 0;
constexpr int kColKind = 1;
constexpr int kColTrust = 2;
constexpr int kColActions = 3;
} // namespace

StoreTab::StoreTab(const QVector<core::StoreProviderConfig> &providers, QWidget *parent)
    : QWidget(parent), m_providers(providers)
{
    namespace sf = settings_form;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(sf::kPageMargin, sf::kPageMargin, sf::kPageMargin, sf::kPageMargin);
    layout->setSpacing(sf::kPageSpacing);

    m_card = new CollapsibleSectionCard(utils::tr(QStringLiteral("store.settings.providers")), this);
    m_card->setCollapsible(false);
    m_card->setEmptyStateText(utils::tr(QStringLiteral("store.settings.empty")));
    m_card->setActionButtonText(utils::tr(QStringLiteral("store.settings.add")));
    connect(m_card, &CollapsibleSectionCard::actionTriggered, this, &StoreTab::addProvider);

    m_table = new QTableWidget(0, 4, this);
    configureTable(m_table, {
        {QString(), 220, true},
        {QString(), 90, false},
        {QString(), 110, false},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { editProvider(row); });
    m_card->setBody(m_table);
    layout->addWidget(m_card);

    auto *clear = new QPushButton(utils::tr(QStringLiteral("store.settings.clear_cache")), this);
    connect(clear, &QPushButton::clicked, this, [clear]() {
        core::clearStoreCache();
        clear->setText(utils::tr(QStringLiteral("store.settings.cache_cleared")));
    });
    auto *row = new QHBoxLayout();
    row->addWidget(clear);
    row->addStretch(1);
    layout->addLayout(row);
    layout->addStretch(1);

    rebuildTable();
}

void StoreTab::rebuildTable()
{
    m_table->setRowCount(0);
    m_table->setRowCount(m_providers.size());
    const auto item = [](const QString &text) {
        auto *it = new QTableWidgetItem(text);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        return it;
    };
    for (int row = 0; row < m_providers.size(); ++row) {
        const core::StoreProviderConfig &p = m_providers.at(row);
        m_table->setItem(row, kColName, item(p.enabled ? p.name : QStringLiteral("%1  (%2)").arg(p.name, utils::tr(QStringLiteral("store.settings.disabled")))));
        m_table->setItem(row, kColKind, item(utils::tr(QStringLiteral("store.provider.kind.") + core::storeKindName(p.kind))));
        m_table->setItem(row, kColTrust, item(core::storeTrustLabel(core::storeTrustName(p.trust))));
        m_table->setCellWidget(row, kColActions, makeRowActionsCell(m_table,
            [this, row]() { editProvider(row); },
            [this, row]() { removeProvider(row); }));
    }
    m_card->setCount(m_providers.size());
}

void StoreTab::addProvider()
{
    StoreProviderDialog dialog(core::StoreProviderConfig{}, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_providers << dialog.provider();
    rebuildTable();
}

bool StoreTab::editProvider(int row)
{
    if (row < 0 || row >= m_providers.size()) {
        return false;
    }
    StoreProviderDialog dialog(m_providers.at(row), this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    m_providers[row] = dialog.provider();
    rebuildTable();
    return true;
}

void StoreTab::removeProvider(int row)
{
    if (row < 0 || row >= m_providers.size()) {
        return;
    }
    m_providers.remove(row);
    rebuildTable();
}

} // namespace kai::ui
