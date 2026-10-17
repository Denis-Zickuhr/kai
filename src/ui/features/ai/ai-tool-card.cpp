#include "ui/features/ai/ai-tool-card.h"

#include "ui/features/ai/ai-chat-widgets.h"
#include "ui/features/ai/ai-ui-text.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

constexpr int kMaxDetailLines = 14;

QString argsPreview(const QJsonObject &args)
{
    QStringList parts;
    for (auto it = args.constBegin(); it != args.constEnd() && parts.size() < 3; ++it) {
        QString v;
        if (it.value().isString()) v = it.value().toString();
        else if (it.value().isObject() || it.value().isArray()) v = QStringLiteral("…");
        else v = it.value().toVariant().toString();
        v = v.simplified();
        if (v.size() > 40) v = v.left(39) + QStringLiteral("…");
        parts << QStringLiteral("%1=%2").arg(it.key(), v);
    }
    return parts.join(QStringLiteral("  "));
}

// "Concluído" diz pouco: para uma listagem, quantos itens vieram.
QString resultSummary(const QString &content)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString();
    }
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("items")).isArray()) {
        const int total = obj.value(QStringLiteral("total")).toInt(-1);
        const int count = total >= 0 ? total : obj.value(QStringLiteral("items")).toArray().size();
        return utils::tr(QStringLiteral("ai.card.summary.items")).arg(count);
    }
    return QString();
}

QString prettyResult(const QString &content)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && !doc.isNull()) {
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed();
    }
    return content;
}

} // namespace

AiToolCard::AiToolCard(const core::AiToolCall &call, QWidget *parent)
    : QFrame(parent)
    , m_call(call)
{
    setObjectName(QStringLiteral("aiToolCard"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
    root->setSpacing(tk::space(2));

    m_head = new QWidget(this);
    m_head->setObjectName(QStringLiteral("aiToolHead"));
    m_head->installEventFilter(this);
    m_head->setMinimumHeight(28); // o QSS dos botões só entra depois do primeiro layout: não deixa a linha nascer curta
    auto *head = new QHBoxLayout(m_head);
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(tk::space(2));
    m_icon = new QLabel(m_head);
    m_icon->setFixedSize(16, 16);
    // O QSS global dá 2px de padding a todo QLabel: num rótulo de 16x16 isso cortava o ícone (lado direito e embaixo).
    m_icon->setStyleSheet(QStringLiteral("QLabel { padding: 0px; margin: 0px; border: none; background: transparent; }"));
    head->addWidget(m_icon);
    m_title = new ElidedLabel(m_head);
    m_title->setBold(true);
    m_title->setElidedText(aiToolLabel(call.name));
    head->addWidget(m_title);
    m_args = new ElidedLabel(m_head);
    m_args->setTextColor(QColor(tk::mutedFg()));
    m_args->setElidedText(argsPreview(call.arguments));
    m_args->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    head->addWidget(m_args, 1);
    m_status = new QLabel(m_head);
    m_status->setProperty("kaiRole", QStringLiteral("caption"));
    head->addWidget(m_status);
    m_undo = new QPushButton(utils::tr(QStringLiteral("ai.card.undo")), m_head);
    m_undo->setObjectName(QStringLiteral("aiUndoButton"));
    m_undo->setVisible(false);
    applyAiButtonStyle(m_undo, AiButtonKind::Secondary, 24);
    connect(m_undo, &QPushButton::clicked, this, [this]() { emit undoRequested(m_changeId); });
    head->addWidget(m_undo);
    m_toggle = new QToolButton(m_head);
    m_toggle->setObjectName(QStringLiteral("aiToolToggle"));
    m_toggle->setAutoRaise(true);
    m_toggle->setCheckable(true);
    m_toggle->setCursor(Qt::PointingHandCursor);
    m_toggle->setFixedSize(24, 24);
    m_toggle->setToolTip(utils::tr(QStringLiteral("ai.card.details")));
    m_toggle->setVisible(false);
    m_toggle->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: %1px; padding: 0; min-height: 24px; min-width: 24px; }"
        "QToolButton:hover { background: %2; }").arg(tk::radiusSm()).arg(tk::hoverBg()));
    head->addWidget(m_toggle);
    root->addWidget(m_head);

    m_detail = new QPlainTextEdit(this);
    m_detail->setObjectName(QStringLiteral("aiToolDetail"));
    m_detail->setReadOnly(true);
    m_detail->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_detail->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_detail->setFont(tk::monoFont(qMax(8, tk::fontSizeSmallPt())));
    m_detail->setVisible(false);
    root->addWidget(m_detail);
    connect(m_detail->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this]() { updateDetailHeight(); });
    connect(m_toggle, &QToolButton::toggled, this, [this](bool on) {
        m_detail->setVisible(on);
        updateToggleIcon();
        updateDetailHeight();
    });

    // --- aprovação ---
    m_approvalBar = new QWidget(this);
    m_approvalBar->setObjectName(QStringLiteral("aiApprovalBar"));
    auto *bar = new QHBoxLayout(m_approvalBar);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(tk::space(2));
    m_allow = new QPushButton(utils::tr(QStringLiteral("ai.card.allow")), m_approvalBar);
    m_allow->setObjectName(QStringLiteral("aiAllowButton"));
    m_allowAll = new QPushButton(utils::tr(QStringLiteral("ai.card.allow_conversation")), m_approvalBar);
    m_allowAll->setObjectName(QStringLiteral("aiAllowAllButton"));
    m_deny = new QPushButton(utils::tr(QStringLiteral("ai.card.deny")), m_approvalBar);
    m_deny->setObjectName(QStringLiteral("aiDenyButton"));
    applyAiButtonStyle(m_allow, AiButtonKind::Primary);
    applyAiButtonStyle(m_allowAll, AiButtonKind::Secondary);
    applyAiButtonStyle(m_deny, AiButtonKind::Danger);
    bar->addWidget(m_allow);
    bar->addWidget(m_allowAll);
    bar->addStretch(1);
    bar->addWidget(m_deny);
    m_approvalBar->setVisible(false);
    root->addWidget(m_approvalBar);
    connect(m_allow, &QPushButton::clicked, this, [this]() { setRunning(); emit decided(true, false); });
    connect(m_allowAll, &QPushButton::clicked, this, [this]() { setRunning(); emit decided(true, true); });
    connect(m_deny, &QPushButton::clicked, this, [this]() { emit decided(false, false); });

    // --- pergunta ---
    m_questionBar = new QWidget(this);
    m_questionBar->setObjectName(QStringLiteral("aiQuestionBar"));
    auto *q = new QVBoxLayout(m_questionBar);
    q->setContentsMargins(0, 0, 0, 0);
    q->setSpacing(tk::space(2));
    m_questionLabel = new QLabel(m_questionBar);
    m_questionLabel->setWordWrap(true);
    m_questionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    q->addWidget(m_questionLabel);
    m_optionsHost = new QWidget(m_questionBar);
    m_optionsHost->setObjectName(QStringLiteral("aiOptionsHost"));
    auto *optionsLayout = new QHBoxLayout(m_optionsHost);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(tk::space(2));
    q->addWidget(m_optionsHost);
    m_answer = new QLineEdit(m_questionBar);
    m_answer->setObjectName(QStringLiteral("aiAnswerField"));
    m_answer->setPlaceholderText(utils::tr(QStringLiteral("ai.card.answer.placeholder")));
    q->addWidget(m_answer);
    connect(m_answer, &QLineEdit::returnPressed, this, [this]() {
        const QString text = m_answer->text().trimmed();
        if (!text.isEmpty()) {
            setRunning();
            emit answered(text);
        }
    });
    m_questionBar->setVisible(false);
    root->addWidget(m_questionBar);

    m_spinTimer = new QTimer(this);
    m_spinTimer->setInterval(80);
    connect(m_spinTimer, &QTimer::timeout, this, [this]() {
        m_spinAngle = (m_spinAngle + 45) % 360;
        paintStateIcon();
    });

    setState(State::Waiting);
}

bool AiToolCard::eventFilter(QObject *watched, QEvent *event)
{
    // Clicar na linha do cartão abre/fecha o detalhe, como o chevron.
    if (watched == m_head && event->type() == QEvent::MouseButtonRelease && m_toggle->isVisible()) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && !m_undo->geometry().contains(mouse->pos())) {
            m_toggle->toggle();
        }
    }
    return QFrame::eventFilter(watched, event);
}

void AiToolCard::updateToggleIcon()
{
    m_toggle->setIcon(LucideIcons::icon(m_toggle->isChecked() ? QStringLiteral("chevron-up") : QStringLiteral("chevron-down"),
                                        QColor(tk::mutedFg()), 14));
    m_head->setCursor(m_toggle->isVisible() ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void AiToolCard::updateDetailHeight()
{
    if (!m_detail->isVisible()) {
        return;
    }
    const int lines = qMax(1, qCeil(m_detail->document()->size().height()));
    const int lineHeight = QFontMetrics(m_detail->font()).lineSpacing();
    const int padding = 2 * (6 + 4) + 2 + 4; // padding do QSS + margem do documento + moldura + folga
    const int h = qMin(lines, kMaxDetailLines) * lineHeight + padding;
    m_detail->setVerticalScrollBarPolicy(lines > kMaxDetailLines ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    if (m_detail->height() != h) {
        m_detail->setFixedHeight(h);
    }
}

void AiToolCard::setDetail(const QString &text, bool diff)
{
    m_detail->clear();
    if (text.isEmpty()) {
        m_detail->setVisible(false);
        m_toggle->setVisible(false);
        m_toggle->setChecked(false);
        updateToggleIcon();
        return;
    }
    QTextCursor cursor(m_detail->document());
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        QTextCharFormat fmt;
        if (diff && !line.isEmpty()) {
            if (line.startsWith(QLatin1Char('+'))) fmt.setForeground(QColor(tk::successFg()));
            else if (line.startsWith(QLatin1Char('-'))) fmt.setForeground(QColor(tk::errorFg()));
            else if (line.startsWith(QLatin1Char('~'))) fmt.setForeground(QColor(tk::warningFg()));
        }
        cursor.insertText(line, fmt);
        if (i + 1 < lines.size()) cursor.insertBlock();
    }
    m_toggle->setVisible(true);
    updateToggleIcon();
    updateDetailHeight();
}

void AiToolCard::setState(State state)
{
    m_state = state;
    refreshStyle();
}

void AiToolCard::paintStateIcon()
{
    QString icon = QStringLiteral("loader");
    QColor color(tk::mutedFg());
    switch (m_state) {
    case State::Waiting:
    case State::Running: break;
    case State::Approval:
        icon = QStringLiteral("triangle-alert");
        color = QColor(m_danger ? tk::errorFg() : tk::warningFg());
        break;
    case State::Question:
        icon = QStringLiteral("circle-help");
        color = QColor(tk::accent());
        break;
    case State::Done:
        icon = QStringLiteral("circle-check");
        color = QColor(tk::successFg());
        break;
    case State::Failed:
        icon = QStringLiteral("circle-x");
        color = QColor(tk::errorFg());
        break;
    case State::Denied:
        icon = QStringLiteral("circle-x");
        break;
    }
    const qreal ratio = devicePixelRatioF();
    QPixmap pm = LucideIcons::icon(icon, color, 16).pixmap(QSize(16, 16), ratio);
    if (m_state == State::Waiting || m_state == State::Running) {
        QPixmap rotated(pm.size());
        rotated.setDevicePixelRatio(ratio);
        rotated.fill(Qt::transparent);
        QPainter p(&rotated);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.translate(8, 8);
        p.rotate(m_spinAngle);
        p.translate(-8, -8);
        p.drawPixmap(0, 0, pm);
        pm = rotated;
    }
    m_icon->setPixmap(pm);
}

void AiToolCard::refreshStyle()
{
    QString border = tk::borderColor();
    QString statusText;
    switch (m_state) {
    case State::Waiting:
        statusText = utils::tr(QStringLiteral("ai.card.waiting"));
        break;
    case State::Approval:
        border = m_danger ? tk::errorFg() : tk::warningFg();
        statusText = utils::tr(QStringLiteral("ai.card.needs_approval"));
        break;
    case State::Question:
        border = tk::accent();
        statusText = utils::tr(QStringLiteral("ai.card.question"));
        break;
    case State::Running:
        statusText = utils::tr(QStringLiteral("ai.card.running"));
        break;
    case State::Done:
        statusText = m_summary.isEmpty() ? utils::tr(QStringLiteral("ai.card.done")) : m_summary;
        break;
    case State::Failed:
        border = QColor(tk::errorFg()).darker(130).name();
        statusText = utils::tr(QStringLiteral("ai.card.failed"));
        break;
    case State::Denied:
        statusText = utils::tr(QStringLiteral("ai.card.denied"));
        break;
    }
    const bool animate = m_state == State::Waiting || m_state == State::Running;
    if (animate && isVisible()) m_spinTimer->start();
    else if (!animate) m_spinTimer->stop();
    paintStateIcon();
    m_status->setText(statusText);
    const bool pending = m_state == State::Approval || m_state == State::Question;
    setStyleSheet(QStringLiteral(
        "QFrame#aiToolCard { background: %1; border: 1px solid %2; border-radius: %3px; }"
        "QFrame#aiToolCard QLabel { background: transparent; }"
        "QFrame#aiToolCard QWidget#aiToolHead, QFrame#aiToolCard QWidget#aiApprovalBar, QFrame#aiToolCard QWidget#aiQuestionBar,"
        " QFrame#aiToolCard QWidget#aiOptionsHost { background: transparent; }"
        "QFrame#aiToolCard QPlainTextEdit#aiToolDetail { background: %4; color: %5; border: 1px solid %6; border-radius: %7px; padding: 6px; }")
        .arg(pending ? tk::surface2() : tk::surface(), border).arg(tk::radiusMd())
        .arg(tk::codeBg(), tk::codeFg(), tk::borderColor()).arg(tk::radiusSm()));
    applyAiButtonStyle(m_allow, m_danger ? AiButtonKind::PrimaryDanger : AiButtonKind::Primary);
}

void AiToolCard::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    if ((m_state == State::Waiting || m_state == State::Running) && !m_spinTimer->isActive()) {
        m_spinTimer->start();
    }
    updateDetailHeight();
}

void AiToolCard::hideEvent(QHideEvent *event)
{
    QFrame::hideEvent(event);
    m_spinTimer->stop();
}

void AiToolCard::showApproval(const engine::ToolPlan &plan)
{
    m_danger = plan.risk == engine::ToolRisk::Danger;
    m_title->setElidedText(plan.title);
    m_args->setElidedText(QString());
    m_title->setToolTip(aiToolLabel(m_call.name));
    setDetail(plan.detail, /*diff=*/true);
    if (!plan.detail.isEmpty()) {
        m_toggle->setChecked(true); // a prévia é o que se aprova: já aberta
    }
    m_allowAll->setVisible(!m_danger);
    m_allow->setText(m_danger ? utils::tr(QStringLiteral("ai.card.confirm")) : utils::tr(QStringLiteral("ai.card.allow")));
    m_approvalBar->setVisible(true);
    m_questionBar->setVisible(false);
    setState(State::Approval);
}

void AiToolCard::showQuestion(const QString &question, const QStringList &options)
{
    m_title->setElidedText(utils::tr(QStringLiteral("ai.card.question")));
    m_args->setElidedText(QString());
    m_questionLabel->setText(question);
    if (auto *layout = qobject_cast<QHBoxLayout *>(m_optionsHost->layout())) {
        while (QLayoutItem *item = layout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        for (const QString &option : options) {
            auto *button = new QPushButton(option, m_optionsHost);
            applyAiButtonStyle(button, AiButtonKind::Secondary);
            connect(button, &QPushButton::clicked, this, [this, option]() { setRunning(); emit answered(option); });
            layout->addWidget(button);
        }
        layout->addStretch(1);
    }
    m_optionsHost->setVisible(!options.isEmpty());
    m_questionBar->setVisible(true);
    m_approvalBar->setVisible(false);
    setState(State::Question);
    m_answer->setFocus();
}

void AiToolCard::setRunning()
{
    m_approvalBar->setVisible(false);
    m_questionBar->setVisible(false);
    setState(State::Running);
}

void AiToolCard::setResult(const core::AiToolResult &result)
{
    m_approvalBar->setVisible(false);
    m_questionBar->setVisible(false);
    m_changeId = result.changeId;
    if (m_call.name == QLatin1String("ask_user")) {
        const QString question = m_questionLabel->text().isEmpty() ? utils::tr(QStringLiteral("ai.card.question")) : m_questionLabel->text();
        m_title->setElidedText(question);
        m_args->setElidedText(QStringLiteral("→ ") + result.content);
    }
    m_summary = result.isError || result.denied ? QString() : resultSummary(result.content);
    // Para ações com prévia, o detalhe aprovado continua sendo o mais útil; senão mostra o resultado.
    if (m_detail->toPlainText().isEmpty()) {
        setDetail(prettyResult(result.content), false);
    }
    // Erro: a mensagem é o que importa, já à vista.
    m_toggle->setChecked(result.isError && !result.denied && m_toggle->isVisible());
    setState(result.denied ? State::Denied : (result.isError ? State::Failed : State::Done));
}

void AiToolCard::setUndoable(const QString &changeId, bool undoable)
{
    m_changeId = changeId;
    m_undo->setVisible(undoable && !changeId.isEmpty());
}

void AiToolCard::markUndone()
{
    m_undo->setVisible(false);
    m_status->setText(utils::tr(QStringLiteral("ai.card.undone")));
}

} // namespace kai::ui
