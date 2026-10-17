#include "ui/features/ai/ai-assist-dialog.h"

#include "core/config-manager.h"
#include "engine/ai/agent-toolbox.h"
#include "engine/ai/ai-client.h"
#include "engine/ai/ai-prompts.h"
#include "engine/ai/command-json.h"
#include "engine/ai/manifesto-library.h"
#include "ui/features/ai/ai-ui-text.h"
#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = utils::tokens;

bool aiAssistAvailable()
{
    return core::ConfigManager().loadAiSettings().isConfigured();
}

AiAssistDialog::AiAssistDialog(const core::Command &current, const QString &folderId,
                               const QVector<core::Command> &allCommands, QWidget *parent)
    : QDialog(parent)
    , m_current(current)
    , m_folderId(folderId)
    , m_allCommands(allCommands)
    , m_settings(core::ConfigManager().loadAiSettings())
    , m_client(new engine::AiClient(this))
{
    setWindowTitle(utils::tr(QStringLiteral("ai.assist.title")));
    setSizeGripEnabled(true);
    resize(680, 600);
    setMinimumSize(520, 420);
    buildUi();
    centerOnParent(this);

    connect(m_client, &engine::AiClient::textDelta, this, [this](const QString &t) {
        m_streamed += t.size();
        setStatus(utils::tr(QStringLiteral("ai.assist.generating")).arg(m_streamed), false);
    });
    connect(m_client, &engine::AiClient::retrying, this, [this](int attempt, int, const engine::AiError &) {
        setStatus(utils::tr(QStringLiteral("ai.status.retrying")).arg(attempt + 1).arg(engine::AiClient::kMaxAttempts), false);
    });
    connect(m_client, &engine::AiClient::completed, this, [this](const engine::AiTurn &turn) { handleCompleted(turn.text); });
    connect(m_client, &engine::AiClient::failed, this, [this](const engine::AiError &e) {
        m_generate->setText(utils::tr(QStringLiteral("ai.assist.generate")));
        setStatus(e.kind == engine::AiError::Kind::Cancelled ? QString() : aiErrorText(e), e.kind != engine::AiError::Kind::Cancelled);
    });
}

void AiAssistDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(3));
    root->setSpacing(tk::space(3));

    auto *top = new QHBoxLayout();
    top->setSpacing(tk::space(2));
    m_profile = new QComboBox(this);
    m_profile->setObjectName(QStringLiteral("aiAssistProfile"));
    for (const core::AiProfile &p : m_settings.profiles) {
        if (p.isUsable()) m_profile->addItem(QStringLiteral("%1 — %2").arg(p.name, p.model), p.id);
    }
    const core::AiProfile *def = m_settings.defaultProfile();
    if (def) m_profile->setCurrentIndex(qMax(0, m_profile->findData(def->id)));
    top->addWidget(m_profile, 1);
    m_badge = new QLabel(this);
    m_badge->setObjectName(QStringLiteral("aiAssistBadge"));
    top->addWidget(m_badge);
    root->addLayout(top);
    auto updateBadge = [this]() {
        const core::AiProfile *p = m_settings.profileById(m_profile->currentData().toString());
        if (!p) { m_badge->clear(); return; }
        const bool local = p->isLocalEndpoint();
        m_badge->setText(local ? utils::tr(QStringLiteral("ai.badge.local")) : utils::tr(QStringLiteral("ai.badge.remote")));
        m_badge->setToolTip(local ? utils::tr(QStringLiteral("ai.badge.local.tip"))
                                  : utils::tr(QStringLiteral("ai.badge.remote.tip")).arg(QUrl(p->resolvedBaseUrl()).host()));
        m_badge->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; border: 1px solid %1; border-radius: %2px; padding: 1px 8px; background: transparent; font-weight: 600; }")
            .arg(local ? tk::successFg() : tk::warningFg()).arg(tk::radiusSm()));
    };
    connect(m_profile, &QComboBox::currentIndexChanged, this, updateBadge);
    updateBadge();

    m_request = new QPlainTextEdit(this);
    m_request->setObjectName(QStringLiteral("aiAssistRequest"));
    m_request->setPlaceholderText(utils::tr(QStringLiteral("ai.assist.placeholder")));
    m_request->setFixedHeight(96);
    root->addWidget(layout_helpers::wrapWithLabel(this, utils::tr(QStringLiteral("ai.assist.request")), m_request));

    auto *actions = new QHBoxLayout();
    m_generate = new QPushButton(utils::tr(QStringLiteral("ai.assist.generate")), this);
    m_generate->setObjectName(QStringLiteral("aiAssistGenerate"));
    m_generate->setEnabled(m_profile->count() > 0);
    actions->addWidget(m_generate);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("aiAssistStatus"));
    m_status->setWordWrap(true);
    actions->addWidget(m_status, 1);
    root->addLayout(actions);

    m_explanation = new QLabel(this);
    m_explanation->setObjectName(QStringLiteral("aiAssistExplanation"));
    m_explanation->setWordWrap(true);
    m_explanation->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_explanation->setVisible(false);
    root->addWidget(m_explanation);

    m_diff = new QPlainTextEdit(this);
    m_diff->setObjectName(QStringLiteral("aiAssistDiff"));
    m_diff->setReadOnly(true);
    m_diff->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_diff->setFont(QFont(tk::monoFamily()));
    m_diff->setVisible(false);
    root->addWidget(m_diff, 1);
    root->addStretch(0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttons);
    m_apply = buttons->addButton(utils::tr(QStringLiteral("ai.assist.apply")), QDialogButtonBox::AcceptRole);
    m_apply->setObjectName(QStringLiteral("aiAssistApply"));
    m_apply->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(m_generate, &QPushButton::clicked, this, &AiAssistDialog::generate);
}

void AiAssistDialog::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(error ? tk::errorFg() : tk::mutedFg()));
}

void AiAssistDialog::generate()
{
    if (m_client->isBusy()) {
        m_client->cancel();
        return;
    }
    m_userRequest = m_request->toPlainText().trimmed();
    if (m_userRequest.isEmpty()) {
        return;
    }
    m_hasResult = false;
    m_repaired = false;
    m_apply->setEnabled(false);
    m_diff->setVisible(false);
    m_explanation->setVisible(false);
    m_thread.clear();
    core::AiMessage user;
    user.role = core::AiMessage::Role::User;
    user.text = engine::buildAssistUserPrompt(m_userRequest, engine::commandToAiJson(m_current, m_allCommands));
    m_thread << user;
    sendRequest();
}

void AiAssistDialog::sendRequest()
{
    const core::AiProfile *profile = m_settings.profileById(m_profile->currentData().toString());
    if (!profile) {
        return;
    }
    static const QRegularExpression httpHint(QStringLiteral("http|api|curl|request|endpoint|rest|webhook|requisi|post|get\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    const bool http = m_current.type == core::CommandType::Http || httpHint.match(m_userRequest).hasMatch();
    engine::AiPromptContext ctx;
    ctx.uiLanguage = utils::TranslationManager::instance().currentLanguage();
    ctx.today = QDate::currentDate().toString(Qt::ISODate);
    ctx.extraInstructions = m_settings.extraInstructions;
    engine::ManifestoLibrary manifesto;

    engine::AiRequest req;
    req.profile = *profile;
    req.system = engine::buildAssistSystemPrompt(ctx, http, manifesto);
    req.messages = m_thread;
    req.stream = true;
    m_streamed = 0;
    m_generate->setText(utils::tr(QStringLiteral("ai.chat.stop")));
    setStatus(utils::tr(QStringLiteral("ai.status.thinking")), false);
    m_client->send(req);
}

void AiAssistDialog::handleCompleted(const QString &text)
{
    m_generate->setText(utils::tr(QStringLiteral("ai.assist.generate")));
    const engine::AssistReply reply = engine::parseAssistReply(text);
    if (!reply.ok) {
        setStatus(utils::tr(QStringLiteral("ai.assist.no_command")), true);
        m_explanation->setText(reply.explanation.isEmpty() ? text.left(600) : reply.explanation);
        m_explanation->setVisible(true);
        return;
    }
    core::Command out;
    QString error;
    if (!engine::commandFromAiJson(reply.command, m_folderId, &m_current, m_allCommands, {}, out, error)) {
        if (!m_repaired) {
            // Uma volta de correção automática, devolvendo ao modelo o que o Kai recusou.
            m_repaired = true;
            core::AiMessage assistant;
            assistant.role = core::AiMessage::Role::Assistant;
            assistant.text = text;
            core::AiMessage fix;
            fix.role = core::AiMessage::Role::User;
            fix.text = QStringLiteral("Kai rejected that command: %1\nReturn the corrected FULL command in the same format.").arg(error);
            m_thread << assistant << fix;
            sendRequest();
            return;
        }
        setStatus(utils::tr(QStringLiteral("ai.assist.invalid")).arg(error), true);
        return;
    }
    showResult(reply.explanation, out);
}

void AiAssistDialog::showResult(const QString &explanation, const core::Command &command)
{
    m_result = command;
    m_hasResult = true;
    setStatus(QString(), false);
    m_explanation->setText(explanation);
    m_explanation->setVisible(!explanation.isEmpty());

    const QJsonObject before = engine::commandToAiJson(m_current, m_allCommands);
    const QJsonObject after = engine::commandToAiJson(command, m_allCommands);
    QString diff = engine::describeJsonDiff(before, after);
    // Ids e pasta são do Kai, não da proposta.
    QStringList lines;
    for (const QString &line : diff.split(QLatin1Char('\n'))) {
        static const QRegularExpression internal(QStringLiteral("^[~+-] (id|folder_id):"));
        if (internal.match(line).hasMatch()) continue;
        lines << line;
    }
    diff = lines.join(QLatin1Char('\n')).trimmed();
    m_diff->clear();
    QTextCursor cursor(m_diff->document());
    if (diff.isEmpty()) {
        cursor.insertText(utils::tr(QStringLiteral("ai.assist.no_changes")));
    }
    for (const QString &line : diff.split(QLatin1Char('\n'))) {
        QTextCharFormat fmt;
        if (line.startsWith(QLatin1Char('+'))) fmt.setForeground(QColor(tk::successFg()));
        else if (line.startsWith(QLatin1Char('-'))) fmt.setForeground(QColor(tk::errorFg()));
        else if (line.startsWith(QLatin1Char('~'))) fmt.setForeground(QColor(tk::warningFg()));
        cursor.insertText(line, fmt);
        cursor.insertBlock();
    }
    m_diff->setVisible(true);
    m_apply->setEnabled(!diff.isEmpty());
}

} // namespace kai::ui
