#include "ui/features/ai/ai-profile-dialog.h"

#include "engine/ai/ai-client.h"
#include "ui/features/ai/ai-ui-text.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>
#include <memory>

namespace kai::ui {

namespace tk = utils::tokens;
using core::AiProfile;
using core::AiWireFormat;

namespace {

struct Preset {
    const char *id;
    const char *labelKey;
    AiWireFormat wire;
    const char *url; // vazio = o padrão do formato
    bool tools;
};

constexpr Preset kPresets[] = {
    {"anthropic", "ai.profile.preset.anthropic", AiWireFormat::Anthropic, "", true},
    {"openai", "ai.profile.preset.openai", AiWireFormat::OpenAiCompatible, "", true},
    {"ollama", "ai.profile.preset.ollama", AiWireFormat::OpenAiCompatible, "http://localhost:11434/v1", true},
    {"lmstudio", "ai.profile.preset.lmstudio", AiWireFormat::OpenAiCompatible, "http://localhost:1234/v1", true},
    {"openrouter", "ai.profile.preset.openrouter", AiWireFormat::OpenAiCompatible, "https://openrouter.ai/api/v1", true},
    {"custom", "ai.profile.preset.custom", AiWireFormat::OpenAiCompatible, "", true},
};

QCheckBox *makeSwitch(const QString &text, QWidget *parent)
{
    auto *box = new QCheckBox(text, parent);
    box->setProperty("kaiRole", QStringLiteral("switch"));
    return box;
}

// Qual predefinição descreve este perfil (pelo formato e pelo endereço).
int presetIndexFor(const AiProfile &p)
{
    const QString url = p.baseUrl.trimmed();
    for (int i = 0; i < int(std::size(kPresets)); ++i) {
        const Preset &preset = kPresets[i];
        if (QString::fromLatin1(preset.id) == QLatin1String("custom")) continue;
        if (preset.wire == p.wireFormat && QString::fromLatin1(preset.url) == url) return i;
    }
    return int(std::size(kPresets)) - 1;
}

} // namespace

AiProfileDialog::AiProfileDialog(const AiProfile &profile, QWidget *parent)
    : QDialog(parent)
    , m_client(new engine::AiClient(this))
    , m_id(profile.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : profile.id)
{
    setWindowTitle(utils::tr(QStringLiteral("ai.profile.title")));
    setSizeGripEnabled(true);
    buildUi(profile);
    setMinimumWidth(560);
    resize(640, 740);
    centerOnParent(this);
}

void AiProfileDialog::buildUi(const AiProfile &profile)
{
    m_loading = true;
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(3));
    outer->setSpacing(tk::space(3));

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setObjectName(QStringLiteral("aiProfileScroll"));
    m_scroll = scroll;
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea#aiProfileScroll, QScrollArea#aiProfileScroll > QWidget#qt_scrollarea_viewport { background: transparent; }"));
    auto *body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("aiProfileBody"));
    body->setStyleSheet(QStringLiteral("QWidget#aiProfileBody { background: transparent; }"));
    scroll->setWidget(body);
    m_body = body;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(tk::space(3));
    outer->addWidget(scroll, 1);

    auto *card = layout_helpers::makeSurfaceCard(body);
    auto *grid = new QGridLayout(card);
    grid->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    grid->setHorizontalSpacing(tk::space(4));
    grid->setVerticalSpacing(tk::space(3));
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    bodyLayout->addWidget(card);
    bodyLayout->addStretch(1);

    int row = 0;
    m_name = new QLineEdit(profile.name, card);
    m_name->setPlaceholderText(utils::tr(QStringLiteral("ai.profile.name.placeholder")));
    grid->addWidget(layout_helpers::wrapWithLabel(card, utils::tr(QStringLiteral("field.label.name")), m_name), row, 0);

    m_provider = new QComboBox(card);
    for (const Preset &p : kPresets) {
        m_provider->addItem(utils::tr(QString::fromLatin1(p.labelKey)), QString::fromLatin1(p.id));
    }
    m_provider->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_provider->setMinimumContentsLength(12);
    m_provider->setCurrentIndex(presetIndexFor(profile));
    grid->addWidget(layout_helpers::wrapWithLabel(card, utils::tr(QStringLiteral("ai.profile.provider")), m_provider), row++, 1);

    m_baseUrl = new QLineEdit(profile.baseUrl, card);
    m_baseUrl->setPlaceholderText(profile.wireFormat == AiWireFormat::Anthropic ? QStringLiteral("https://api.anthropic.com")
                                                                                : QStringLiteral("https://api.openai.com/v1"));
    m_urlRow = layout_helpers::wrapWithLabel(card, utils::tr(QStringLiteral("ai.profile.base_url")), m_baseUrl);
    grid->addWidget(m_urlRow, row++, 0, 1, 2);

    auto *modelHost = new QWidget(card);
    modelHost->setObjectName(QStringLiteral("aiModelHost"));
    modelHost->setStyleSheet(QStringLiteral("QWidget#aiModelHost { background: transparent; }"));
    auto *modelRow = new QHBoxLayout(modelHost);
    modelRow->setContentsMargins(0, 0, 0, 0);
    modelRow->setSpacing(tk::space(2));
    m_model = new QComboBox(modelHost);
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);
    // Nomes de modelo compridos (lista do provedor) não podem alargar o formulário: o combo encolhe e rola.
    m_model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_model->setMinimumContentsLength(12);
    m_model->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_model->setCurrentText(profile.model);
    modelRow->addWidget(m_model, 1);
    m_listModels = new QPushButton(utils::tr(QStringLiteral("ai.profile.models")), modelHost);
    modelRow->addWidget(m_listModels);
    grid->addWidget(layout_helpers::wrapWithLabel(card, utils::tr(QStringLiteral("ai.profile.model")), modelHost), row++, 0, 1, 2);

    auto *keyHost = new QWidget(card);
    keyHost->setObjectName(QStringLiteral("aiKeyHost"));
    keyHost->setStyleSheet(QStringLiteral("QWidget#aiKeyHost { background: transparent; }"));
    auto *keyLayout = new QHBoxLayout(keyHost);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    keyLayout->setSpacing(tk::space(2));
    m_key = new QLineEdit(profile.apiKey, keyHost);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(utils::tr(QStringLiteral("ai.profile.key.placeholder")));
    keyLayout->addWidget(m_key, 1);
    m_showKey = new QPushButton(utils::tr(QStringLiteral("ai.profile.show_key")), keyHost);
    m_showKey->setCheckable(true);
    keyLayout->addWidget(m_showKey);
    m_keyRow = layout_helpers::wrapWithLabel(card, utils::tr(QStringLiteral("ai.profile.api_key")), keyHost);
    grid->addWidget(m_keyRow, row++, 0, 1, 2);

    m_keyEnv = new QLineEdit(profile.apiKeyEnv, card);
    m_keyEnv->setPlaceholderText(QStringLiteral("ANTHROPIC_API_KEY"));
    grid->addWidget(layout_helpers::wrapWithLabel(card, utils::tr(QStringLiteral("ai.profile.key_env")), m_keyEnv), row++, 0, 1, 2);

    m_maxTokens = new QSpinBox(card);
    m_maxTokens->setRange(64, AiProfile::kMaxTokensLimit);
    m_maxTokens->setValue(profile.maxTokens);
    grid->addWidget(layout_helpers::wrapWithLabel(card, utils::tr(QStringLiteral("ai.profile.max_tokens")), m_maxTokens), row, 0);
    m_context = new QSpinBox(card);
    m_context->setRange(0, 4000000);
    m_context->setSingleStep(1000);
    m_context->setSpecialValueText(utils::tr(QStringLiteral("ai.profile.context.unknown")));
    m_context->setValue(profile.contextTokens);
    grid->addWidget(layout_helpers::wrapWithLabel(card, utils::tr(QStringLiteral("ai.profile.context")), m_context), row++, 1);
    m_timeout = new QSpinBox(card);
    m_timeout->setRange(5, 900);
    m_timeout->setSuffix(QStringLiteral(" s"));
    m_timeout->setValue(profile.firstTokenTimeoutSec);
    grid->addWidget(layout_helpers::wrapWithLabel(card, utils::tr(QStringLiteral("ai.profile.timeout")), m_timeout), row, 0);

    m_tools = makeSwitch(utils::tr(QStringLiteral("ai.profile.tools")), card);
    m_tools->setChecked(profile.supportsTools);
    m_tools->setToolTip(utils::tr(QStringLiteral("ai.profile.tools.tip")));
    grid->addWidget(m_tools, row++, 1, Qt::AlignBottom | Qt::AlignLeft); // ao lado do tempo: uma linha a menos
    m_hideSecrets = makeSwitch(utils::tr(QStringLiteral("ai.profile.hide_secrets")), card);
    m_hideSecrets->setChecked(profile.hideSecretVariables);
    m_hideSecrets->setToolTip(utils::tr(QStringLiteral("ai.profile.hide_secrets.tip")));
    grid->addWidget(m_hideSecrets, row++, 0, 1, 2);

    auto *testHost = new QWidget(card);
    testHost->setObjectName(QStringLiteral("aiTestHost"));
    testHost->setStyleSheet(QStringLiteral("QWidget#aiTestHost, QWidget#aiTestHost QLabel { background: transparent; }"));
    auto *testRow = new QHBoxLayout(testHost);
    testRow->setContentsMargins(0, 0, 0, 0);
    testRow->setSpacing(tk::space(3));
    m_test = new QPushButton(utils::tr(QStringLiteral("ai.profile.test")), testHost);
    testRow->addWidget(m_test);
    m_status = new QLabel(testHost);
    m_status->setWordWrap(true);
    testRow->addWidget(m_status, 1);
    grid->addWidget(testHost, row++, 0, 1, 2);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(m_provider, &QComboBox::currentIndexChanged, this, [this]() { applyPreset(); });
    connect(m_showKey, &QPushButton::toggled, this, [this](bool on) {
        m_key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(m_listModels, &QPushButton::clicked, this, &AiProfileDialog::listModels);
    connect(m_test, &QPushButton::clicked, this, &AiProfileDialog::testConnection);
    connect(m_baseUrl, &QLineEdit::textChanged, this, [this]() { updateVisibility(); });
    connect(m_client, &engine::AiClient::modelsListed, this, [this](const QStringList &models) {
        const QString current = m_model->currentText();
        m_model->clear();
        m_model->addItems(models);
        m_model->setCurrentText(current);
        m_listModels->setEnabled(true);
        setStatus(utils::tr(QStringLiteral("ai.profile.models.count")).arg(models.size()), false);
    });
    connect(m_client, &engine::AiClient::modelsFailed, this, [this](const engine::AiError &e) {
        m_listModels->setEnabled(true);
        setStatus(aiErrorText(e), true);
    });
    m_loading = false;
    updateVisibility();
}

void AiProfileDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // O QSS dá altura mínima aos campos, mas só depois de polidos: cada campo guarda a própria altura polida como
    // mínimo, e o corpo usa o mínimo REAL do layout (que muda ao mostrar/ocultar o endereço). Travar a altura do corpo
    // num valor fixo deixava as linhas se sobrepondo quando o formulário crescia (trocar o provedor para "Outro").
    lockFieldHeights();
    QTimer::singleShot(0, this, [this]() {
        lockFieldHeights();
        // Fonte maior ou tema mais gordo: a janela nunca fica mais estreita que o formulário (senão cortaria à direita).
        const int needed = m_body->minimumSizeHint().width() + width() - m_scroll->viewport()->width();
        if (needed > minimumWidth()) {
            setMinimumWidth(needed);
        }
    });
}

void AiProfileDialog::lockFieldHeights()
{
    m_body->ensurePolished();
    for (QWidget *w : m_body->findChildren<QWidget *>()) {
        const bool field = qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w)
                           || qobject_cast<QPushButton *>(w);
        if (!field || w->objectName().startsWith(QLatin1String("qt_"))) {
            continue;
        }
        w->ensurePolished();
        const int h = w->sizeHint().height();
        if (w->minimumHeight() < h) {
            w->setMinimumHeight(h);
        }
    }
    m_body->layout()->invalidate();
}

void AiProfileDialog::applyPreset()
{
    if (m_loading) {
        return;
    }
    const Preset &p = kPresets[m_provider->currentIndex()];
    m_loading = true;
    m_baseUrl->setText(QString::fromLatin1(p.url));
    m_baseUrl->setPlaceholderText(p.wire == AiWireFormat::Anthropic ? QStringLiteral("https://api.anthropic.com")
                                                                    : QStringLiteral("https://api.openai.com/v1"));
    m_tools->setChecked(p.tools);
    m_loading = false;
    updateVisibility();
}

void AiProfileDialog::updateVisibility()
{
    const Preset &p = kPresets[m_provider->currentIndex()];
    // O endereço só se edita quando não é o padrão do provedor (local, outro compatível, proxy).
    const bool custom = QString::fromLatin1(p.id) == QLatin1String("custom") || QString::fromLatin1(p.id) == QLatin1String("ollama")
                        || QString::fromLatin1(p.id) == QLatin1String("lmstudio") || !m_baseUrl->text().trimmed().isEmpty();
    m_urlRow->setVisible(custom);
    const AiProfile d = draft();
    m_key->setPlaceholderText(d.needsKey() ? utils::tr(QStringLiteral("ai.profile.key.placeholder"))
                                           : utils::tr(QStringLiteral("ai.profile.key.optional")));
}

AiProfile AiProfileDialog::draft() const
{
    const Preset &p = kPresets[m_provider->currentIndex()];
    AiProfile out;
    out.id = m_id;
    out.name = m_name->text().trimmed();
    out.wireFormat = p.wire;
    out.baseUrl = m_baseUrl->text().trimmed();
    out.model = m_model->currentText().trimmed();
    out.apiKey = m_key->text().trimmed();
    out.apiKeyEnv = m_keyEnv->text().trimmed();
    out.supportsTools = m_tools->isChecked();
    out.hideSecretVariables = m_hideSecrets->isChecked();
    out.maxTokens = m_maxTokens->value();
    out.contextTokens = m_context->value();
    out.firstTokenTimeoutSec = m_timeout->value();
    return out.clamped();
}

AiProfile AiProfileDialog::profile() const
{
    AiProfile p = draft();
    if (p.name.isEmpty()) {
        p.name = p.model;
    }
    return p;
}

void AiProfileDialog::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                .arg(error ? tk::errorFg() : tk::mutedFg()));
}

void AiProfileDialog::listModels()
{
    m_listModels->setEnabled(false);
    setStatus(utils::tr(QStringLiteral("ai.profile.testing")), false);
    m_client->listModels(draft());
}

void AiProfileDialog::testConnection()
{
    const AiProfile p = draft();
    if (p.model.isEmpty()) {
        setStatus(utils::tr(QStringLiteral("ai.profile.error.model")), true);
        return;
    }
    m_test->setEnabled(false);
    setStatus(utils::tr(QStringLiteral("ai.profile.testing")), false);

    engine::AiRequest req;
    req.profile = p;
    req.stream = false;
    req.maxTokens = 16;
    req.system = QStringLiteral("Reply with the single word: ok");
    core::AiMessage m;
    m.role = core::AiMessage::Role::User;
    m.text = QStringLiteral("ping");
    req.messages << m;

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    auto done = std::make_shared<bool>(false);
    auto finish = [this, timer, done](bool ok, const QString &text) {
        if (*done) return;
        *done = true;
        m_test->setEnabled(true);
        setStatus(ok ? utils::tr(QStringLiteral("ai.profile.test.ok")).arg(timer->elapsed()) : text, !ok);
    };
    // Cliente próprio do teste: não compete com a listagem de modelos.
    auto *probe = new engine::AiClient(this);
    probe->setRetryBaseDelayMs(300);
    connect(probe, &engine::AiClient::completed, this, [finish, probe]() { finish(true, {}); probe->deleteLater(); });
    connect(probe, &engine::AiClient::failed, this, [finish, probe](const engine::AiError &e) {
        finish(false, aiErrorText(e));
        probe->deleteLater();
    });
    probe->send(req);
}

void AiProfileDialog::accept()
{
    const AiProfile p = draft();
    QString problem;
    if (p.model.isEmpty()) {
        problem = utils::tr(QStringLiteral("ai.profile.error.model"));
    } else if (QString::fromLatin1(kPresets[m_provider->currentIndex()].id) == QLatin1String("custom") && p.baseUrl.isEmpty()) {
        problem = utils::tr(QStringLiteral("ai.profile.error.url"));
    }
    if (!problem.isEmpty()) {
        setStatus(problem, true);
        return;
    }
    QDialog::accept();
}

} // namespace kai::ui
