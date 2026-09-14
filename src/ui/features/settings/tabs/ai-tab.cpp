#include "ui/features/settings/tabs/ai-tab.h"

#include "ui/features/ai/ai-profile-dialog.h"
#include "ui/features/http/http-ui.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = utils::tokens;
using core::AiProfile;

AiTab::AiTab(const core::AiSettings &settings, int conversationCount, QWidget *parent)
    : QWidget(parent)
    , m_profiles(settings.profiles)
    , m_defaultId(settings.defaultProfileId)
    , m_mcpToken(settings.mcpToken)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    m_card = new CollapsibleSectionCard(utils::tr(QStringLiteral("ai.settings.providers")), this);
    m_card->setEmptyStateText(utils::tr(QStringLiteral("ai.settings.empty")));
    m_card->setActionButtonText(utils::tr(QStringLiteral("ai.settings.add")));
    m_table = new QTableWidget(0, 2, m_card);
    configureTable(m_table, {{utils::tr(QStringLiteral("ai.settings.providers")), 240, true}, {QString(), rowActionsColumnWidth(), false}});
    m_table->setObjectName(QStringLiteral("aiProfilesTable"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { editProfile(row); });
    m_card->setBody(m_table);
    connect(m_card, &CollapsibleSectionCard::actionTriggered, this, &AiTab::addProfile);
    m_card->setCount(0);
    m_card->setExpanded(true, false);
    m_card->setCollapsible(false);
    layout->addWidget(m_card);

    auto *general = layout_helpers::makeSurfaceCard(this);
    auto *generalLayout = new QVBoxLayout(general);
    generalLayout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    generalLayout->setSpacing(tk::space(3));

    m_default = new QComboBox(general);
    m_default->setObjectName(QStringLiteral("aiDefaultField"));
    generalLayout->addWidget(layout_helpers::wrapWithLabel(general, utils::tr(QStringLiteral("ai.settings.default")), m_default));

    m_maxSteps = new QSpinBox(general);
    m_maxSteps->setObjectName(QStringLiteral("aiMaxStepsField"));
    m_maxSteps->setRange(1, core::AiSettings::kMaxAgentStepsLimit);
    m_maxSteps->setValue(settings.maxAgentSteps);
    generalLayout->addWidget(layout_helpers::wrapWithLabel(general, utils::tr(QStringLiteral("ai.settings.max_steps")), m_maxSteps));

    m_instructions = new QPlainTextEdit(settings.extraInstructions, general);
    m_instructions->setObjectName(QStringLiteral("aiInstructionsField"));
    m_instructions->setPlaceholderText(utils::tr(QStringLiteral("ai.settings.instructions.placeholder")));
    m_instructions->setMinimumHeight(90);
    m_instructions->setMaximumHeight(140);
    generalLayout->addWidget(layout_helpers::wrapWithLabel(general, utils::tr(QStringLiteral("ai.settings.instructions")), m_instructions));
    layout->addWidget(general);

    auto *mcp = layout_helpers::makeSurfaceCard(this);
    auto *mcpLayout = new QVBoxLayout(mcp);
    mcpLayout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    mcpLayout->setSpacing(tk::space(2));
    auto *mcpTitle = new QLabel(utils::tr(QStringLiteral("ai.mcp.title")), mcp);
    QFont mf = mcpTitle->font();
    mf.setBold(true);
    mcpTitle->setFont(mf);
    mcpLayout->addWidget(mcpTitle);
    m_mcpEnabled = new QCheckBox(utils::tr(QStringLiteral("ai.mcp.enable")), mcp);
    m_mcpEnabled->setObjectName(QStringLiteral("aiMcpEnableField"));
    m_mcpEnabled->setProperty("kaiRole", QStringLiteral("switch"));
    m_mcpEnabled->setChecked(settings.mcpEnabled);
    m_mcpEnabled->setToolTip(utils::tr(QStringLiteral("ai.mcp.enable.tip")));
    mcpLayout->addWidget(m_mcpEnabled);
    m_mcpHideSecrets = new QCheckBox(utils::tr(QStringLiteral("ai.mcp.hide_secrets")), mcp);
    m_mcpHideSecrets->setObjectName(QStringLiteral("aiMcpHideSecretsField"));
    m_mcpHideSecrets->setProperty("kaiRole", QStringLiteral("switch"));
    m_mcpHideSecrets->setChecked(settings.mcpHideSecrets);
    mcpLayout->addWidget(m_mcpHideSecrets);
    auto *mcpButtons = new QHBoxLayout();
    mcpButtons->setSpacing(tk::space(2));
    m_mcpCopy = new QPushButton(utils::tr(QStringLiteral("ai.mcp.copy")), mcp);
    m_mcpCopy->setObjectName(QStringLiteral("aiMcpCopyButton"));
    m_mcpNewToken = new QPushButton(utils::tr(QStringLiteral("ai.mcp.new_token")), mcp);
    m_mcpNewToken->setObjectName(QStringLiteral("aiMcpNewTokenButton"));
    mcpButtons->addWidget(m_mcpCopy);
    mcpButtons->addWidget(m_mcpNewToken);
    mcpButtons->addStretch(1);
    mcpLayout->addLayout(mcpButtons);
    layout->addWidget(mcp);
    auto updateMcp = [this]() {
        const bool on = m_mcpEnabled->isChecked();
        m_mcpHideSecrets->setEnabled(on);
        m_mcpCopy->setEnabled(on);
        m_mcpNewToken->setEnabled(on);
        // A chave nasce ao ligar (e só então vai para o arquivo).
        if (on && m_mcpToken.isEmpty()) {
            m_mcpToken = core::AiSettings::newMcpToken();
        }
    };
    connect(m_mcpEnabled, &QCheckBox::toggled, this, updateMcp);
    connect(m_mcpNewToken, &QPushButton::clicked, this, [this]() { m_mcpToken = core::AiSettings::newMcpToken(); });
    connect(m_mcpCopy, &QPushButton::clicked, this, []() {
        QGuiApplication::clipboard()->setText(AiTab::mcpClientConfig(QCoreApplication::applicationFilePath()));
    });
    updateMcp();

    auto *history = layout_helpers::makeSurfaceCard(this);
    auto *historyLayout = new QHBoxLayout(history);
    historyLayout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    historyLayout->setSpacing(tk::space(3));
    auto *labelHost = new QWidget(history);
    makeTransparentHost(labelHost, QStringLiteral("aiHistoryHost"));
    auto *labelLayout = new QVBoxLayout(labelHost);
    labelLayout->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(utils::tr(QStringLiteral("ai.settings.history")), labelHost);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    labelLayout->addWidget(title);
    m_historyLabel = new QLabel(labelHost);
    m_historyLabel->setProperty("kaiRole", QStringLiteral("caption"));
    labelLayout->addWidget(m_historyLabel);
    historyLayout->addWidget(labelHost, 1);
    m_clear = new QPushButton(utils::tr(QStringLiteral("ai.settings.clear")), history);
    m_clear->setObjectName(QStringLiteral("aiClearHistoryButton"));
    historyLayout->addWidget(m_clear);
    layout->addWidget(history);
    connect(m_clear, &QPushButton::clicked, this, [this]() {
        if (confirmYesNo(this, utils::tr(QStringLiteral("ai.settings.clear.title")),
                         utils::tr(QStringLiteral("ai.settings.clear.body")))) {
            emit clearConversationsRequested();
        }
    });
    refreshConversationCount(conversationCount);

    layout->addStretch(1);
    rebuildTable();
}

void AiTab::refreshConversationCount(int count)
{
    m_historyLabel->setText(utils::tr(QStringLiteral("ai.settings.history.count")).arg(count));
    m_clear->setEnabled(count > 0);
}

void AiTab::showClearedCount(int count)
{
    Q_UNUSED(count)
    refreshConversationCount(0);
}

void AiTab::rebuildDefaultCombo()
{
    m_default->clear();
    for (const AiProfile &p : m_profiles) {
        m_default->addItem(p.name, p.id);
    }
    const int idx = m_default->findData(m_defaultId);
    m_default->setCurrentIndex(idx >= 0 ? idx : 0);
    m_default->setEnabled(m_profiles.size() > 1);
}

void AiTab::rebuildTable()
{
    m_table->setRowCount(0);
    m_table->setRowCount(m_profiles.size());
    for (int row = 0; row < m_profiles.size(); ++row) {
        const AiProfile &p = m_profiles.at(row);
        QString shown = p.name;
        if (p.isLocalEndpoint()) {
            shown += QStringLiteral("  (%1)").arg(utils::tr(QStringLiteral("ai.badge.local")));
        }
        if (!p.isUsable()) {
            shown += QStringLiteral("  (%1)").arg(utils::tr(QStringLiteral("ai.settings.incomplete")));
        }
        auto *item = new QTableWidgetItem(shown);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, item);
        m_table->setCellWidget(row, 1, makeRowActionsCell(m_table,
            [this, row]() { editProfile(row); }, [this, row]() { removeProfile(row); }));
    }
    m_table->setFixedHeight(qMax(1, int(m_profiles.size())) * standardRowHeight() + 6);
    m_card->setCount(m_profiles.size());
    rebuildDefaultCombo();
}

void AiTab::addProfile()
{
    AiProfile fresh;
    fresh.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    AiProfileDialog dialog(fresh, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_profiles.append(dialog.profile());
    if (m_defaultId.isEmpty()) {
        m_defaultId = m_profiles.last().id;
    }
    rebuildTable();
}

void AiTab::editProfile(int row)
{
    if (row < 0 || row >= m_profiles.size()) {
        return;
    }
    AiProfileDialog dialog(m_profiles.at(row), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_profiles[row] = dialog.profile();
    rebuildTable();
}

void AiTab::removeProfile(int row)
{
    if (row < 0 || row >= m_profiles.size()) {
        return;
    }
    if (m_profiles.at(row).id == m_defaultId) {
        m_defaultId.clear();
    }
    m_profiles.remove(row);
    if (m_defaultId.isEmpty() && !m_profiles.isEmpty()) {
        m_defaultId = m_profiles.first().id;
    }
    rebuildTable();
}

QString AiTab::mcpClientConfig(const QString &kaiExecutable)
{
    const QJsonObject server{{QStringLiteral("command"), kaiExecutable}, {QStringLiteral("args"), QJsonArray{QStringLiteral("mcp")}}};
    const QJsonObject root{{QStringLiteral("mcpServers"), QJsonObject{{QStringLiteral("kai"), server}}}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

core::AiSettings AiTab::settings() const
{
    core::AiSettings s;
    s.profiles = m_profiles;
    s.defaultProfileId = m_default->currentData().toString();
    s.maxAgentSteps = m_maxSteps->value();
    s.extraInstructions = m_instructions->toPlainText().trimmed();
    s.mcpEnabled = m_mcpEnabled->isChecked();
    s.mcpToken = s.mcpEnabled ? m_mcpToken : QString();
    s.mcpHideSecrets = m_mcpHideSecrets->isChecked();
    return s;
}

} // namespace kai::ui
