#include "ui/features/store/store-provider-dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/store-local.h"
#include "core/store-texts.h"
#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

StoreProviderDialog::StoreProviderDialog(const core::StoreProviderConfig &initial, QWidget *parent)
    : QDialog(parent), m_initial(initial)
{
    namespace lh = layout_helpers;
    const bool isNew = initial.id.isEmpty();
    setWindowTitle(utils::tr(isNew ? QStringLiteral("store.provider.title_add") : QStringLiteral("store.provider.title_edit")));
    setMinimumWidth(460);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 12);
    outer->setSpacing(utils::tokens::space(3));

    if (isNew) {
        auto *warning = new QLabel(utils::tr(QStringLiteral("store.provider.warning")), this);
        warning->setWordWrap(true);
        warning->setProperty("kaiRole", QStringLiteral("caption"));
        outer->addWidget(warning);
    }

    m_name = new QLineEdit(initial.name, this);
    outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.provider.name")), m_name));

    m_location = new QLineEdit(initial.location, this);
    m_location->setPlaceholderText(utils::tr(QStringLiteral("store.provider.location.placeholder")));
    connect(m_location, &QLineEdit::textEdited, this, &StoreProviderDialog::onLocationEdited);
    outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.provider.location")), m_location));

    m_kind = new QComboBox(this);
    m_kind->addItem(utils::tr(QStringLiteral("store.provider.kind.file")), QStringLiteral("file"));
    m_kind->addItem(utils::tr(QStringLiteral("store.provider.kind.repo")), QStringLiteral("repo"));
    m_kind->addItem(utils::tr(QStringLiteral("store.provider.kind.api")), QStringLiteral("api"));
    m_kind->setCurrentIndex(qMax(0, m_kind->findData(core::storeKindName(initial.kind))));
    connect(m_kind, &QComboBox::activated, this, [this]() { m_kindTouched = true; });
    outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.provider.kind")), m_kind));

    m_token = new QLineEdit(initial.token, this);
    m_token->setEchoMode(QLineEdit::Password);
    m_token->setPlaceholderText(utils::tr(QStringLiteral("store.provider.token.placeholder")));
    outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.provider.token")), m_token));

    m_trust = new QComboBox(this);
    for (const QString &t : {QStringLiteral("official"), QStringLiteral("verified"), QStringLiteral("community"), QStringLiteral("custom")}) {
        m_trust->addItem(core::storeTrustLabel(t), t);
    }
    m_trust->setCurrentIndex(qMax(0, m_trust->findData(core::storeTrustName(initial.trust))));
    outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.provider.trust")), m_trust));

    auto *testRow = new QHBoxLayout();
    m_test = new QPushButton(utils::tr(QStringLiteral("store.provider.test")), this);
    connect(m_test, &QPushButton::clicked, this, &StoreProviderDialog::testConnection);
    testRow->addWidget(m_test);
    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    m_result->setProperty("kaiRole", QStringLiteral("caption"));
    testRow->addWidget(m_result, 1);
    outer->addLayout(testRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_name->text().trimmed().isEmpty() || m_location->text().trimmed().isEmpty()) {
            m_result->setText(utils::tr(QStringLiteral("store.provider.required")));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
}

core::StoreProviderConfig StoreProviderDialog::provider() const
{
    core::StoreProviderConfig p = m_initial;
    if (p.id.isEmpty()) {
        p.id = core::StoreLocalState::newProviderId();
    }
    p.name = m_name->text().trimmed();
    p.location = m_location->text().trimmed();
    p.kind = core::storeKindFromName(m_kind->currentData().toString()).value_or(core::StoreProviderKind::File);
    p.token = m_token->text();
    p.trust = core::storeTrustFromName(m_trust->currentData().toString());
    return p;
}

void StoreProviderDialog::onLocationEdited()
{
    if (m_kindTouched) {
        return;
    }
    const QString kind = core::storeKindName(core::suggestStoreKind(m_location->text()));
    const int index = m_kind->findData(kind);
    if (index >= 0) {
        m_kind->setCurrentIndex(index);
    }
}

void StoreProviderDialog::testConnection()
{
    if (m_client) {
        m_client->cancel();
        m_client->deleteLater();
        m_client = nullptr;
    }
    const core::StoreProviderConfig p = provider();
    m_result->setText(utils::tr(QStringLiteral("store.provider.testing")));
    m_test->setEnabled(false);
    engine::StoreClient::Options options;
    m_client = new engine::StoreClient(p, options, this);
    m_client->loadCatalog([this](const engine::StoreCatalogResult &r) {
        m_test->setEnabled(true);
        if (!r.ok) {
            m_result->setText(core::storeErrorText(r.error, r.errorDetail));
            return;
        }
        QString signature;
        switch (r.signature) {
        case engine::StoreSignatureState::None: signature = utils::tr(QStringLiteral("store.signature.none")); break;
        case engine::StoreSignatureState::Ok: signature = utils::tr(QStringLiteral("store.signature.ok")); break;
        case engine::StoreSignatureState::Invalid: signature = utils::tr(QStringLiteral("store.signature.invalid")); break;
        }
        m_result->setText(utils::tr(QStringLiteral("store.provider.test_ok"))
                              .arg(r.catalog.header.name.isEmpty() ? m_name->text() : r.catalog.header.name)
                              .arg(r.catalog.packages.size())
                              .arg(signature));
    });
}

} // namespace kai::ui
