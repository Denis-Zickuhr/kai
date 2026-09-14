#include "ui/features/ai/ai-chat-window.h"

#include "core/config-manager.h"
#include "ui/features/ai/ai-chat-widgets.h"
#include "ui/features/ai/ai-tool-card.h"
#include "ui/features/ai/ai-ui-text.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "ui/features/http/http-ui.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtMath>

namespace kai::ui {

namespace tk = utils::tokens;
using core::AiMessage;

namespace {

constexpr int kColumnMaxWidth = 860;
constexpr int kSidebarWidth = 252;
constexpr int kWideBreakpoint = 760;
constexpr int kComposerMaxLines = 8;
constexpr int kStickThreshold = 56;

// Um passo mais redondo que o token (cartões grandes pedem), mas "reto" continua reto.
int softer(int radius, int limit)
{
    return radius <= 0 ? 0 : qMin(radius + 4, limit);
}

QString todayGroupKey(const QDateTime &when)
{
    const QDate d = when.toLocalTime().date();
    const QDate today = QDate::currentDate();
    if (d >= today) return QStringLiteral("ai.chat.group.today");
    if (d >= today.addDays(-1)) return QStringLiteral("ai.chat.group.yesterday");
    if (d >= today.addDays(-7)) return QStringLiteral("ai.chat.group.week");
    return QStringLiteral("ai.chat.group.older");
}

QString shortWhen(const QDateTime &when)
{
    const QDateTime local = when.toLocalTime();
    const QLocale locale;
    if (local.date() == QDate::currentDate()) {
        return locale.toString(local.time(), QLocale::ShortFormat);
    }
    return locale.toString(local, QLocale::ShortFormat);
}

} // namespace

AiChatWindow::AiChatWindow(engine::AgentToolHost *host, QWidget *parent, bool embedded)
    : QDialog(parent)
    , m_host(host)
    , m_toolbox(std::make_unique<engine::AgentToolbox>(host))
{
    if (embedded) {
        setWindowFlags(Qt::Widget);
    }
    setWindowTitle(utils::tr(QStringLiteral("ai.chat.title")));
    if (!embedded) {
        setWindowFlag(Qt::WindowMinMaxButtonsHint, true);
    }
    setSizeGripEnabled(true);
    setModal(false);
    resize(1040, 720);
    setMinimumSize(440, 420);

    m_confirm = [this](const QString &title, const QString &text) { return confirmYesNo(this, title, text); };
    m_store = std::make_unique<core::AiConversationStore>(core::ConfigManager().aiConversationsDirPath());
    m_session = new engine::AgentSession(m_toolbox.get(), nullptr, this);
    m_streamTimer = new QTimer(this);
    m_streamTimer->setSingleShot(true);
    m_streamTimer->setInterval(45);
    connect(m_streamTimer, &QTimer::timeout, this, &AiChatWindow::flushStreaming);
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &AiChatWindow::saveNow);

    buildUi();
    connectSession();
    reloadProfiles();
    reloadConversationList();
    newConversation();
    applyResponsiveLayout();
    centerOnParent(this);
}

AiChatWindow::~AiChatWindow()
{
    if (m_session && m_session->isBusy()) {
        m_session->cancel();
    }
    saveNow();
}

void AiChatWindow::setStoreDirectory(const QString &directory)
{
    m_store = std::make_unique<core::AiConversationStore>(directory);
    reloadConversationList();
}

QLabel *AiChatWindow::statusLabel() const
{
    return m_status;
}

int AiChatWindow::conversationCount() const
{
    int n = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (!m_list->item(i)->data(ConversationDelegate::HeaderRole).toBool()) ++n;
    }
    return n;
}

bool AiChatWindow::sidebarVisible() const
{
    return m_sidebarShown;
}

void AiChatWindow::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName(QStringLiteral("aiSidebar"));
    m_sidebar->setFixedWidth(kSidebarWidth);
    m_sidebar->setStyleSheet(QStringLiteral(
        "QWidget#aiSidebar { background: %1; border-right: 1px solid %2;"
        " border-top-left-radius: %3px; border-bottom-left-radius: %3px; }")
        .arg(tk::surface(), tk::borderColor()).arg(tk::radiusMd()));
    auto *sideLayout = new QVBoxLayout(m_sidebar);
    sideLayout->setContentsMargins(tk::space(2) + 2, tk::space(3), tk::space(2) + 2, tk::space(3));
    sideLayout->setSpacing(tk::space(2));
    auto *sideTop = new QHBoxLayout();
    sideTop->setSpacing(tk::space(2));
    m_new = new QPushButton(utils::tr(QStringLiteral("ai.chat.new")), m_sidebar);
    m_new->setObjectName(QStringLiteral("aiNewChatButton"));
    m_new->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(tk::accent()), 16));
    applyAiButtonStyle(m_new, AiButtonKind::Secondary, 36);
    m_new->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sideTop->addWidget(m_new, 1);
    m_delete = new QPushButton(m_sidebar);
    m_delete->setObjectName(QStringLiteral("aiDeleteChatButton"));
    m_delete->setIcon(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::errorFg()), 16));
    m_delete->setToolTip(utils::tr(QStringLiteral("ai.chat.delete")));
    applyAiButtonStyle(m_delete, AiButtonKind::Secondary, 36);
    m_delete->setFixedWidth(36);
    // Botão só de ícone: círculo (raio = metade do lado) em todos os estados; cantos "retos" continuam retos.
    m_delete->setStyleSheet(m_delete->styleSheet() + QStringLiteral(
        "QPushButton, QPushButton:hover, QPushButton:pressed, QPushButton:disabled { padding: 0px; border-radius: %1px; min-width: 36px; }")
        .arg(tk::radiusMd() <= 0 ? 0 : 17));
    m_delete->setEnabled(false);
    sideTop->addWidget(m_delete);
    sideLayout->addLayout(sideTop);
    m_list = new QListWidget(m_sidebar);
    m_list->setObjectName(QStringLiteral("aiConversationList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setMouseTracking(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setItemDelegate(new ConversationDelegate(m_list));
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget#aiConversationList { background: transparent; border: none; outline: none; padding: 0px; }"
        "QListWidget#aiConversationList::item { padding: 0px; border: none; }"));
    sideLayout->addWidget(m_list, 1);
    root->addWidget(m_sidebar);

    auto *main = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *top = new QWidget(main);
    top->setObjectName(QStringLiteral("aiTopBar"));
    top->setStyleSheet(QStringLiteral("QWidget#aiTopBar { border-bottom: 1px solid %1; }").arg(tk::borderColor()));
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
    topLayout->setSpacing(tk::space(2));
    m_sidebarToggle = new QToolButton(top);
    m_sidebarToggle->setObjectName(QStringLiteral("aiSidebarToggle"));
    m_sidebarToggle->setAutoRaise(true);
    m_sidebarToggle->setCursor(Qt::PointingHandCursor);
    m_sidebarToggle->setFixedSize(32, 32);
    m_sidebarToggle->setIcon(LucideIcons::icon(QStringLiteral("panel-left"), QColor(tk::mutedFg()), 18));
    m_sidebarToggle->setToolTip(utils::tr(QStringLiteral("ai.chat.sidebar.toggle")));
    m_sidebarToggle->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: %1px; padding: 0; }"
        "QToolButton:hover { background: %2; }").arg(tk::radiusMdForHeight(32)).arg(tk::hoverBg()));
    topLayout->addWidget(m_sidebarToggle);
    m_title = new ElidedLabel(top);
    m_title->setObjectName(QStringLiteral("aiChatTitle"));
    m_title->setBold(true);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    topLayout->addWidget(m_title, 1);
    m_profileCombo = new QComboBox(top);
    m_profileCombo->setObjectName(QStringLiteral("aiProfileCombo"));
    m_profileCombo->setMinimumWidth(150);
    m_profileCombo->setMaximumWidth(300);
    m_profileCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_profileCombo->setMinimumContentsLength(14);
    topLayout->addWidget(m_profileCombo);
    m_privacyBadge = new QLabel(top);
    m_privacyBadge->setObjectName(QStringLiteral("aiPrivacyBadge"));
    topLayout->addWidget(m_privacyBadge);
    m_secretsBadge = new QLabel(top);
    m_secretsBadge->setObjectName(QStringLiteral("aiSecretsBadge"));
    topLayout->addWidget(m_secretsBadge);
    m_askAgain = new QPushButton(utils::tr(QStringLiteral("ai.chat.ask_again")), top);
    m_askAgain->setObjectName(QStringLiteral("aiAskAgainButton"));
    m_askAgain->setToolTip(utils::tr(QStringLiteral("ai.chat.ask_again.tip")));
    applyAiButtonStyle(m_askAgain, AiButtonKind::Secondary, 28);
    m_askAgain->setVisible(false);
    topLayout->addWidget(m_askAgain);
    m_usage = new QLabel(top);
    m_usage->setProperty("kaiRole", QStringLiteral("caption"));
    topLayout->addWidget(m_usage);
    mainLayout->addWidget(top);

    m_scroll = new QScrollArea(main);
    m_scroll->setObjectName(QStringLiteral("aiMessagesScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea#aiMessagesScroll { background: transparent; border: none; }"));
    m_messages = new QWidget();
    m_messages->setObjectName(QStringLiteral("aiMessages"));
    m_messages->setStyleSheet(QStringLiteral("QWidget#aiMessages { background: transparent; }"));
    auto *centerLayout = new QHBoxLayout(m_messages);
    centerLayout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(3));
    centerLayout->setSpacing(0);
    centerLayout->addStretch(1);
    m_column = new QWidget(m_messages);
    m_column->setObjectName(QStringLiteral("aiColumn"));
    m_column->setStyleSheet(QStringLiteral("QWidget#aiColumn { background: transparent; }"));
    m_column->setMaximumWidth(kColumnMaxWidth);
    m_column->setMinimumWidth(0);
    m_column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    centerLayout->addWidget(m_column, 1000);
    centerLayout->addStretch(1);
    m_messagesLayout = new QVBoxLayout(m_column);
    m_messagesLayout->setContentsMargins(0, 0, 0, 0);
    m_messagesLayout->setSpacing(tk::space(4));
    m_messagesLayout->addStretch(1);
    m_scroll->setWidget(m_messages);
    mainLayout->addWidget(m_scroll, 1);
    m_scroll->installEventFilter(this);
    m_scroll->viewport()->installEventFilter(this);

    // Rolagem: acompanha o fim enquanto o usuário não sobe; "descer" aparece quando ele subiu.
    QScrollBar *bar = m_scroll->verticalScrollBar();
    connect(bar, &QScrollBar::rangeChanged, this, [this](int, int max) {
        if (m_stickBottom) {
            m_programmaticScroll = true;
            m_scroll->verticalScrollBar()->setValue(max);
            m_programmaticScroll = false;
        }
        updateJumpButton();
    });
    connect(bar, &QScrollBar::valueChanged, this, [this](int value) {
        if (m_programmaticScroll) return;
        m_stickBottom = m_scroll->verticalScrollBar()->maximum() - value <= kStickThreshold;
        updateJumpButton();
    });
    m_jump = new QToolButton(m_scroll);
    m_jump->setObjectName(QStringLiteral("aiJumpToBottom"));
    m_jump->setCursor(Qt::PointingHandCursor);
    m_jump->setFixedSize(34, 34);
    m_jump->setIcon(LucideIcons::icon(QStringLiteral("arrow-down"), QColor(tk::fg()), 16));
    m_jump->setToolTip(utils::tr(QStringLiteral("ai.chat.jump_bottom")));
    m_jump->setStyleSheet(QStringLiteral(
        "QToolButton { background: %1; border: 1px solid %2; border-radius: %4px; padding: 0; min-height: 34px; min-width: 34px; }"
        "QToolButton:hover { background: %3; }").arg(tk::surface2(), aiStrongBorder(), tk::hoverBg()).arg(tk::radiusMd() <= 0 ? 0 : 17));
    m_jump->setVisible(false);
    connect(m_jump, &QToolButton::clicked, this, [this]() { forceBottom(); });

    m_empty = new QWidget(m_column);
    makeTransparentHost(m_empty, QStringLiteral("aiEmptyState"));
    auto *emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(tk::space(2));
    emptyLayout->addStretch(2);
    auto *mark = new QLabel(m_empty);
    mark->setPixmap(LucideIcons::icon(QStringLiteral("sparkles"), QColor(tk::accent()), 30).pixmap(30, 30));
    mark->setAlignment(Qt::AlignHCenter);
    emptyLayout->addWidget(mark);
    auto *hello = new QLabel(utils::tr(QStringLiteral("ai.chat.empty.title")), m_empty);
    QFont hf = hello->font();
    hf.setPointSize(hf.pointSize() + 5);
    hf.setWeight(QFont::DemiBold);
    hello->setFont(hf);
    hello->setAlignment(Qt::AlignHCenter);
    emptyLayout->addWidget(hello);
    emptyLayout->addSpacing(tk::space(3));
    const struct { const char *key; const char *icon; } suggestions[] = {
        {"ai.chat.suggest.list", "list"}, {"ai.chat.suggest.create", "folder-plus"}, {"ai.chat.suggest.failed", "bug"}};
    for (const auto &s : suggestions) {
        auto *chip = new QPushButton(utils::tr(QString::fromLatin1(s.key)), m_empty);
        chip->setObjectName(QStringLiteral("aiSuggestion"));
        chip->setIcon(LucideIcons::icon(QString::fromLatin1(s.icon), QColor(tk::accent()), 16));
        applyAiButtonStyle(chip, AiButtonKind::Secondary, 40);
        chip->setStyleSheet(chip->styleSheet() + QStringLiteral("QPushButton { text-align: left; padding: 0 16px; font-weight: 500; }"));
        chip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(chip, &QPushButton::clicked, this, [this, chip]() {
            m_input->setPlainText(chip->text());
            m_input->setFocus();
        });
        auto *row = new QHBoxLayout();
        row->addStretch(1);
        chip->setMaximumWidth(460);
        chip->setMinimumWidth(260);
        row->addWidget(chip, 100);
        row->addStretch(1);
        emptyLayout->addLayout(row);
    }
    emptyLayout->addStretch(3);
    m_messagesLayout->insertWidget(0, m_empty);
    m_messagesLayout->setStretchFactor(m_empty, 100);

    m_noProfile = new QWidget(m_column);
    makeTransparentHost(m_noProfile, QStringLiteral("aiNoProfile"));
    auto *noLayout = new QVBoxLayout(m_noProfile);
    auto *noLabel = new QLabel(utils::tr(QStringLiteral("ai.chat.no_provider")), m_noProfile);
    noLabel->setAlignment(Qt::AlignHCenter);
    noLabel->setWordWrap(true);
    noLayout->addWidget(noLabel);
    m_messagesLayout->insertWidget(0, m_noProfile);
    m_noProfile->setVisible(false);

    auto *composerHost = new QWidget(main);
    composerHost->setObjectName(QStringLiteral("aiComposerHost"));
    composerHost->setStyleSheet(QStringLiteral("QWidget#aiComposerHost { background: transparent; }"));
    auto *hostLayout = new QHBoxLayout(composerHost);
    hostLayout->setContentsMargins(tk::space(4), 0, tk::space(4), tk::space(4));
    hostLayout->setSpacing(0);
    hostLayout->addStretch(1);
    auto *composerColumn = new QWidget(composerHost);
    composerColumn->setObjectName(QStringLiteral("aiComposerColumn"));
    composerColumn->setStyleSheet(QStringLiteral("QWidget#aiComposerColumn { background: transparent; }"));
    composerColumn->setMaximumWidth(kColumnMaxWidth);
    composerColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    hostLayout->addWidget(composerColumn, 1000);
    hostLayout->addStretch(1);
    auto *columnLayout = new QVBoxLayout(composerColumn);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(2);

    m_status = new ShimmerLabel(composerColumn);
    m_status->setObjectName(QStringLiteral("aiStatusLabel"));
    m_status->setIconName(QStringLiteral("sparkles"));
    m_status->setFixedHeight(24);
    m_status->setContentsMargins(tk::space(1), 0, 0, 0);
    columnLayout->addWidget(m_status);

    m_composer = new QFrame(composerColumn);
    m_composer->setObjectName(QStringLiteral("aiComposer"));
    m_composer->setStyleSheet(QStringLiteral(
        "QFrame#aiComposer { background: %1; border: 1px solid %2; border-radius: %3px; }"
        "QFrame#aiComposer[focused=\"true\"] { border: 1px solid %4; }")
        .arg(tk::surface(), aiStrongBorder()).arg(softer(tk::radiusLg(), 22)).arg(tk::accent()));
    auto *composerLayout = new QHBoxLayout(m_composer);
    composerLayout->setContentsMargins(tk::space(4), tk::space(2), tk::space(2), tk::space(2));
    composerLayout->setSpacing(tk::space(2));
    m_input = new QPlainTextEdit(m_composer);
    m_input->setObjectName(QStringLiteral("aiInputField"));
    m_input->setPlaceholderText(utils::tr(QStringLiteral("ai.chat.input.placeholder")));
    m_input->setFrameShape(QFrame::NoFrame);
    m_input->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#aiInputField { background: transparent; border: none; padding: 0px; }"));
    m_input->installEventFilter(this);
    composerLayout->addWidget(m_input, 1, Qt::AlignVCenter);
    m_send = new QPushButton(m_composer);
    m_send->setObjectName(QStringLiteral("aiSendButton"));
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setFixedSize(36, 36);
    m_send->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: %5px; padding: 0px; min-height: 36px; min-width: 36px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:disabled { background: %4; border: 1px solid %4; border-radius: %5px; }")
        .arg(tk::buttonColor(), QColor(tk::buttonColor()).lighter(135).name(), QColor(tk::buttonColor()).lighter(112).name(),
             tk::borderColor())
        .arg(tk::radiusMd() <= 0 ? 0 : 18));
    composerLayout->addWidget(m_send, 0, Qt::AlignBottom);
    columnLayout->addWidget(m_composer);
    mainLayout->addWidget(composerHost);
    root->addWidget(main, 1);

    connect(m_input->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this]() { adjustInputHeight(); });
    adjustInputHeight();
    connect(m_send, &QPushButton::clicked, this, &AiChatWindow::onSendClicked);
    connect(m_new, &QPushButton::clicked, this, &AiChatWindow::newConversation);
    connect(m_delete, &QPushButton::clicked, this, &AiChatWindow::deleteSelectedConversation);
    connect(m_sidebarToggle, &QToolButton::clicked, this, [this]() { setSidebarVisible(!m_sidebarShown, true); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item->data(ConversationDelegate::HeaderRole).toBool()) return;
        openConversation(item->data(ConversationDelegate::IdRole).toString());
    });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current) {
        m_delete->setEnabled(current != nullptr && !current->data(ConversationDelegate::HeaderRole).toBool());
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_list->itemAt(pos);
        if (!item || item->data(ConversationDelegate::HeaderRole).toBool()) return;
        m_list->setCurrentItem(item);
        QMenu menu(this);
        QAction *copy = menu.addAction(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::fg()), 16),
                                       utils::tr(QStringLiteral("ai.chat.copy_conversation")));
        menu.addSeparator();
        QAction *del = menu.addAction(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::errorFg()), 16),
                                      utils::tr(QStringLiteral("ai.chat.delete")));
        const QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
        if (chosen == copy) {
            copyConversation(item->data(ConversationDelegate::IdRole).toString());
        } else if (chosen == del) {
            deleteSelectedConversation();
        }
    });
    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &AiChatWindow::onProfileChosen);
    connect(m_askAgain, &QPushButton::clicked, this, [this]() {
        m_session->setAutoAllow(false);
        m_askAgain->setVisible(false);
    });
}

void AiChatWindow::adjustInputHeight()
{
    const int lineHeight = QFontMetrics(m_input->font()).lineSpacing();
    const int lines = qBound(1, qCeil(m_input->document()->size().height()), kComposerMaxLines);
    // Altura exata (linhas + margens do documento, sem folga): com folga o texto fica acima do centro do cartão.
    const int h = lines * lineHeight + 2 * qCeil(m_input->document()->documentMargin());
    if (m_input->height() != h) {
        m_input->setFixedHeight(h);
    }
    m_input->setVerticalScrollBarPolicy(qCeil(m_input->document()->size().height()) > kComposerMaxLines
                                            ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
}

bool AiChatWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input) {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier)) {
                onSendClicked();
                return true;
            }
        } else if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            m_composer->setProperty("focused", event->type() == QEvent::FocusIn);
            m_composer->style()->unpolish(m_composer);
            m_composer->style()->polish(m_composer);
        }
    } else if (watched == m_scroll && event->type() == QEvent::Resize) {
        m_jump->move((m_scroll->width() - m_jump->width()) / 2, m_scroll->height() - m_jump->height() - tk::space(3));
        m_jump->raise();
    } else if (watched == m_scroll->viewport() && event->type() == QEvent::Resize) {
        // Nada de conteúdo mais largo que a janela: o excedente seria cortado (e empurraria a conversa para o lado).
        m_messages->setMaximumWidth(m_scroll->viewport()->width());
    }
    return QDialog::eventFilter(watched, event);
}

void AiChatWindow::setEmbedded(bool embedded)
{
    m_embedded = embedded;
    setWindowFlags(embedded ? Qt::Widget : Qt::Dialog);
    setProperty("kaiEmbedded", embedded);
    setSizeGripEnabled(!embedded);
    setMinimumSize(embedded ? QSize(320, 320) : QSize(440, 420));
}

void AiChatWindow::done(int result)
{
    if (!m_embedded) {
        QDialog::done(result);
    }
}

// Esc fecha um diálogo, mas não um painel do quadro: quem fecha painel é o menu Exibir ou o X da aba.
void AiChatWindow::keyPressEvent(QKeyEvent *event)
{
    if (m_embedded && event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}

void AiChatWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    reloadProfiles();
    reloadConversationList();
    m_input->setFocus();
}

void AiChatWindow::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    applyResponsiveLayout();
}

void AiChatWindow::closeEvent(QCloseEvent *event)
{
    // Uma aprovação ou pergunta pendente não tem como ser respondida com a janela fechada.
    if (m_session->state() == engine::AgentSession::State::AwaitingApproval
        || m_session->state() == engine::AgentSession::State::AwaitingAnswer) {
        m_session->cancel();
    }
    saveNow();
    QDialog::closeEvent(event);
}

void AiChatWindow::setSidebarVisible(bool visible, bool manual)
{
    m_sidebarShown = visible;
    if (manual) m_sidebarManual = true;
    m_sidebar->setVisible(visible);
    m_sidebarToggle->setIcon(LucideIcons::icon(QStringLiteral("panel-left"), QColor(visible ? tk::accent() : tk::mutedFg()), 18));
}

void AiChatWindow::applyResponsiveLayout()
{
    const int w = width();
    const bool wide = w >= kWideBreakpoint;
    // A escolha do usuário vale até a janela cruzar o limite de novo.
    if (wide != m_wideLayout) {
        m_wideLayout = wide;
        m_sidebarManual = false;
    }
    if (!m_sidebarManual) {
        setSidebarVisible(wide, false);
    }
    m_usage->setVisible(w >= 900 && !m_usage->text().isEmpty());
    m_secretsBadge->setVisible(w >= 660 && !m_secretsBadge->text().isEmpty());
}

void AiChatWindow::updateJumpButton()
{
    m_jump->setVisible(!m_stickBottom && m_scroll->verticalScrollBar()->maximum() > 0);
    m_jump->raise();
}

void AiChatWindow::reloadProfiles()
{
    m_profiles = core::ConfigManager().loadAiSettings();
    const QString current = m_session ? m_session->profile().id : QString();
    m_switchingProfile = true;
    m_profileCombo->clear();
    for (const core::AiProfile &p : m_profiles.profiles) {
        if (p.isUsable()) {
            m_profileCombo->addItem(QStringLiteral("%1 — %2").arg(p.name, p.model), p.id);
        }
    }
    m_switchingProfile = false;
    const bool any = m_profileCombo->count() > 0;
    m_noProfile->setVisible(!any);
    if (any) {
        int idx = m_profileCombo->findData(current);
        if (idx < 0) idx = m_profileCombo->findData(m_profiles.defaultProfile() ? m_profiles.defaultProfile()->id : QString());
        m_switchingProfile = true;
        m_profileCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        m_switchingProfile = false;
        applyProfileToSession();
    }
    updateInputState();
    showEmptyStateIfNeeded();
}

void AiChatWindow::selectProfileById(const QString &id)
{
    const int idx = m_profileCombo->findData(id);
    if (idx < 0) return;
    m_switchingProfile = true;
    m_profileCombo->setCurrentIndex(idx);
    m_switchingProfile = false;
    applyProfileToSession();
}

void AiChatWindow::applyProfileToSession()
{
    const QString id = m_profileCombo->currentData().toString();
    if (const core::AiProfile *p = m_profiles.profileById(id)) {
        m_session->setProfile(*p);
        m_toolbox->setHideSecretVariables(p->hideSecretVariables);
    }
    updateBadges();
}

void AiChatWindow::onProfileChosen(int)
{
    if (m_switchingProfile) {
        return;
    }
    const QString newId = m_profileCombo->currentData().toString();
    const bool hasHistory = !m_session->conversation().messages.isEmpty();
    if (hasHistory && newId != m_session->profile().id) {
        if (!m_confirm(utils::tr(QStringLiteral("ai.chat.switch.title")), utils::tr(QStringLiteral("ai.chat.switch.body")))) {
            selectProfileById(m_session->profile().id);
            return;
        }
    }
    applyProfileToSession();
}

void AiChatWindow::updateBadges()
{
    const core::AiProfile &p = m_session->profile();
    if (p.id.isEmpty()) {
        m_privacyBadge->clear();
        m_secretsBadge->clear();
        return;
    }
    const bool local = p.isLocalEndpoint();
    const QString color = local ? tk::successFg() : tk::warningFg();
    m_privacyBadge->setText(local ? utils::tr(QStringLiteral("ai.badge.local")) : utils::tr(QStringLiteral("ai.badge.remote")));
    m_privacyBadge->setToolTip(local ? utils::tr(QStringLiteral("ai.badge.local.tip"))
                                     : utils::tr(QStringLiteral("ai.badge.remote.tip")).arg(QUrl(p.resolvedBaseUrl()).host()));
    m_privacyBadge->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; border: 1px solid %1; border-radius: %2px; padding: 2px 10px; background: transparent; font-weight: 600; }")
        .arg(color).arg(softer(tk::radiusSm(), 10)));
    if (p.hideSecretVariables) {
        m_secretsBadge->setText(utils::tr(QStringLiteral("ai.badge.secrets_hidden")));
        m_secretsBadge->setToolTip(utils::tr(QStringLiteral("ai.badge.secrets_hidden.tip")));
        m_secretsBadge->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; border: 1px solid %2; border-radius: %3px; padding: 2px 10px; background: transparent; }")
            .arg(tk::mutedFg(), aiStrongBorder()).arg(softer(tk::radiusSm(), 10)));
    } else {
        m_secretsBadge->clear();
        m_secretsBadge->setStyleSheet(QString());
    }
    applyResponsiveLayout();
}

void AiChatWindow::updateInputState()
{
    const bool usable = m_profileCombo->count() > 0;
    const bool busy = m_session->isBusy();
    m_input->setEnabled(usable);
    m_send->setEnabled(usable);
    m_send->setIcon(LucideIcons::icon(busy ? QStringLiteral("square") : QStringLiteral("arrow-up"),
                                      QColor(usable ? tk::buttonFg() : tk::mutedFg()), 18));
    m_send->setIconSize(QSize(18, 18));
    m_send->setToolTip(busy ? utils::tr(QStringLiteral("ai.chat.stop")) : utils::tr(QStringLiteral("ai.chat.send")));
    m_profileCombo->setEnabled(!busy && usable);
    m_new->setEnabled(true);
    m_askAgain->setVisible(m_session->autoAllowing());
}

void AiChatWindow::updateUsageLabel()
{
    const core::AiUsage &u = m_session->conversation().usage;
    m_usage->setText(u.isEmpty() ? QString()
                                 : utils::tr(QStringLiteral("ai.chat.usage")).arg(u.inputTokens).arg(u.outputTokens));
    m_usage->setVisible(width() >= 900 && !m_usage->text().isEmpty());
}

void AiChatWindow::updateTitle()
{
    QString title;
    for (const AiMessage &m : m_session->conversation().messages) {
        if (m.role == AiMessage::Role::User) {
            title = m.text.simplified();
            break;
        }
    }
    m_title->setElidedText(title.isEmpty() ? utils::tr(QStringLiteral("ai.chat.new")) : title);
}

void AiChatWindow::reloadConversationList()
{
    const QString currentId = m_session ? m_session->conversation().id : QString();
    m_list->blockSignals(true);
    m_list->clear();
    QString lastGroup;
    for (const core::AiConversationInfo &info : m_store->list()) {
        const QString group = todayGroupKey(info.updatedAt);
        if (group != lastGroup) {
            lastGroup = group;
            auto *header = new QListWidgetItem(utils::tr(group), m_list);
            header->setFlags(Qt::NoItemFlags);
            header->setData(ConversationDelegate::HeaderRole, true);
        }
        auto *item = new QListWidgetItem(info.title.isEmpty() ? utils::tr(QStringLiteral("ai.chat.untitled")) : info.title, m_list);
        item->setData(ConversationDelegate::IdRole, info.id);
        item->setData(ConversationDelegate::SubtitleRole, shortWhen(info.updatedAt));
        item->setToolTip(info.title);
        if (info.id == currentId) {
            m_list->setCurrentItem(item);
        }
    }
    m_list->blockSignals(false);
    m_delete->setEnabled(m_list->currentItem() != nullptr);
}

void AiChatWindow::newConversation()
{
    if (m_session->isBusy()) {
        m_session->cancel();
    }
    saveNow();
    core::AiConversation fresh;
    m_session->setConversation(fresh);
    m_session->setAutoAllow(false);
    m_dirty = false;
    clearMessages();
    updateUsageLabel();
    updateInputState();
    updateTitle();
    m_list->clearSelection();
    m_list->setCurrentItem(nullptr);
    showEmptyStateIfNeeded();
    forceBottom();
    m_input->setFocus();
}

void AiChatWindow::openConversation(const QString &id)
{
    if (id == m_session->conversation().id) {
        return;
    }
    if (m_session->isBusy()) {
        m_session->cancel();
    }
    saveNow();
    const core::AiConversation conversation = m_store->load(id);
    if (conversation.id.isEmpty()) {
        reloadConversationList();
        return;
    }
    m_session->setConversation(conversation);
    m_session->setAutoAllow(false);
    if (!conversation.profileId.isEmpty() && m_profileCombo->findData(conversation.profileId) >= 0) {
        selectProfileById(conversation.profileId);
    }
    rebuildFromConversation();
    m_dirty = false; // só ler a conversa não a coloca no topo da lista
    updateUsageLabel();
    updateInputState();
    updateTitle();
    reloadConversationList();
}

QString AiChatWindow::conversationAsText(const core::AiConversation &conversation)
{
    QStringList blocks;
    for (const AiMessage &m : conversation.messages) {
        if (m.role == AiMessage::Role::User) {
            blocks << QStringLiteral("**%1:**\n%2").arg(utils::tr(QStringLiteral("ai.chat.role.user")), m.text);
        } else if (m.role == AiMessage::Role::Assistant) {
            QStringList parts;
            if (!m.text.trimmed().isEmpty()) parts << m.text;
            for (const core::AiToolCall &call : m.toolCalls) {
                parts << QStringLiteral("_%1_").arg(aiToolLabel(call.name));
            }
            if (!parts.isEmpty()) {
                blocks << QStringLiteral("**%1:**\n%2").arg(utils::tr(QStringLiteral("ai.chat.role.assistant")), parts.join(QStringLiteral("\n\n")));
            }
        }
    }
    return blocks.join(QStringLiteral("\n\n"));
}

void AiChatWindow::copyConversation(const QString &id)
{
    const core::AiConversation conversation = id == m_session->conversation().id ? m_session->conversation() : m_store->load(id);
    QGuiApplication::clipboard()->setText(conversationAsText(conversation));
}

void AiChatWindow::deleteSelectedConversation()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item || item->data(ConversationDelegate::HeaderRole).toBool()) return;
    if (!m_confirm(utils::tr(QStringLiteral("ai.chat.delete.title")), utils::tr(QStringLiteral("ai.chat.delete.body")))) {
        return;
    }
    const QString id = item->data(ConversationDelegate::IdRole).toString();
    const bool current = id == m_session->conversation().id;
    m_store->remove(id);
    if (current) {
        // Descarta sem regravar a conversa que acabou de ser apagada.
        if (m_session->isBusy()) m_session->cancel();
        core::AiConversation fresh;
        m_session->setConversation(fresh);
        m_dirty = false;
        clearMessages();
        updateUsageLabel();
        updateTitle();
        showEmptyStateIfNeeded();
    }
    reloadConversationList();
}

void AiChatWindow::saveSoon()
{
    m_saveTimer->start();
}

void AiChatWindow::saveNow()
{
    m_saveTimer->stop();
    if (!m_dirty) {
        return;
    }
    const core::AiConversation &c = m_session->conversation();
    if (c.id.isEmpty() || c.messages.isEmpty()) {
        return;
    }
    m_store->save(c);
    m_dirty = false;
    reloadConversationList();
}

QList<AiToolCard *> AiChatWindow::toolCards() const
{
    return m_messages->findChildren<AiToolCard *>();
}

int AiChatWindow::messageWidgetCount() const
{
    // Tudo o que está no layout menos o estado vazio, o aviso de sem provedor e o espaçador final.
    return m_messagesLayout->count() - 3;
}

void AiChatWindow::clearMessages()
{
    m_cards.clear();
    m_tickets.clear();
    m_streaming.clear();
    m_streamText.clear();
    m_status->setStatus(QString());
    // O estado vazio, o aviso de sem provedor e o espaçador final ficam.
    for (int i = m_messagesLayout->count() - 2; i >= 0; --i) {
        QLayoutItem *item = m_messagesLayout->itemAt(i);
        QWidget *w = item ? item->widget() : nullptr;
        if (!w || w == m_empty || w == m_noProfile) continue;
        m_messagesLayout->takeAt(i);
        w->deleteLater();
    }
}

void AiChatWindow::showEmptyStateIfNeeded()
{
    const bool noMessages = m_session->conversation().messages.isEmpty();
    m_empty->setVisible(noMessages && m_profileCombo->count() > 0);
}

void AiChatWindow::insertMessageWidget(QWidget *widget)
{
    // Antes do espaçador final.
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, widget);
    m_empty->setVisible(false);
    followBottom();
}

QWidget *AiChatWindow::addUserBubble(const QString &text)
{
    auto *row = new UserRow(text, m_column);
    insertMessageWidget(row);
    return row;
}

AssistantBlock *AiChatWindow::addAssistantBubble(const QString &markdown)
{
    auto *block = new AssistantBlock(m_column);
    block->setText(markdown);
    insertMessageWidget(block);
    return block;
}

AiToolCard *AiChatWindow::addToolCard(const core::AiToolCall &call)
{
    auto *card = new AiToolCard(call, m_column);
    m_cards.insert(call.id, card);
    wireCard(card);
    insertMessageWidget(card);
    return card;
}

void AiChatWindow::wireCard(AiToolCard *card)
{
    const QString callId = card->call().id;
    connect(card, &AiToolCard::decided, this, [this, callId](bool allow, bool forConversation) {
        const QString ticket = m_tickets.take(callId);
        if (!ticket.isEmpty()) m_session->approve(ticket, allow, forConversation);
        updateInputState();
    });
    connect(card, &AiToolCard::answered, this, [this, callId](const QString &text) {
        const QString ticket = m_tickets.take(callId);
        if (!ticket.isEmpty()) m_session->answer(ticket, text);
    });
    connect(card, &AiToolCard::undoRequested, this, [this, card](const QString &changeId) {
        QString error;
        if (m_toolbox->undo(changeId, error)) {
            card->markUndone();
        } else {
            addNotice(error, true);
        }
    });
}

void AiChatWindow::addNotice(const QString &text, bool error, bool withRetry)
{
    auto *row = new QFrame(m_column);
    row->setObjectName(QStringLiteral("aiNoticeRow"));
    const QString color = error ? tk::errorFg() : tk::mutedFg();
    row->setStyleSheet(QStringLiteral(
        "QFrame#aiNoticeRow { background: transparent; border: 1px solid %1; border-radius: %2px; }"
        "QFrame#aiNoticeRow QLabel { background: transparent; border: none; }")
        .arg(error ? QColor(tk::errorFg()).darker(130).name() : tk::borderColor()).arg(tk::radiusMd()));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
    layout->setSpacing(tk::space(2));
    auto *icon = new QLabel(row);
    icon->setPixmap(LucideIcons::icon(error ? QStringLiteral("circle-alert") : QStringLiteral("info"), QColor(color), 16).pixmap(16, 16));
    icon->setAlignment(Qt::AlignTop);
    layout->addWidget(icon, 0, Qt::AlignTop);
    auto *label = new QLabel(text, row);
    label->setObjectName(error ? QStringLiteral("aiErrorNotice") : QStringLiteral("aiNotice"));
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setMinimumWidth(0);
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }").arg(error ? tk::errorFg() : tk::mutedFg()));
    layout->addWidget(label, 1);
    if (withRetry) {
        auto *retry = new QPushButton(utils::tr(QStringLiteral("ai.chat.retry")), row);
        retry->setObjectName(QStringLiteral("aiRetryButton"));
        applyAiButtonStyle(retry, AiButtonKind::Secondary, 28);
        connect(retry, &QPushButton::clicked, this, [this, row]() {
            row->deleteLater();
            m_session->retry();
        });
        layout->addWidget(retry, 0, Qt::AlignTop);
    }
    insertMessageWidget(row);
}

void AiChatWindow::rebuildFromConversation()
{
    clearMessages();
    const auto &messages = m_session->conversation().messages;
    for (int i = 0; i < messages.size(); ++i) {
        const AiMessage &m = messages.at(i);
        if (m.role == AiMessage::Role::User) {
            addUserBubble(m.text);
        } else if (m.role == AiMessage::Role::Assistant) {
            if (!m.text.trimmed().isEmpty()) addAssistantBubble(m.text)->setFinal(true);
            // Os resultados vêm na mensagem seguinte.
            const AiMessage *results = (i + 1 < messages.size() && messages.at(i + 1).role == AiMessage::Role::Tool)
                                           ? &messages.at(i + 1) : nullptr;
            for (const core::AiToolCall &call : m.toolCalls) {
                AiToolCard *card = addToolCard(call);
                if (results) {
                    for (const core::AiToolResult &r : results->toolResults) {
                        if (r.toolCallId != call.id) continue;
                        card->setResult(r);
                        for (const engine::AgentChange &c : m_toolbox->changes()) {
                            if (c.id == r.changeId) card->setUndoable(r.changeId, !c.undone);
                        }
                    }
                }
            }
        }
    }
    showEmptyStateIfNeeded();
    forceBottom();
}

void AiChatWindow::flushStreaming()
{
    if (!m_streaming && !m_streamText.isEmpty()) {
        m_streaming = addAssistantBubble(QString());
    }
    if (m_streaming) {
        m_streaming->setText(m_streamText);
        followBottom();
    }
}

void AiChatWindow::finalizeStreaming(const QString &finalText)
{
    m_streamTimer->stop();
    if (finalText.trimmed().isEmpty()) {
        if (m_streaming) m_streaming->deleteLater();
    } else if (m_streaming) {
        m_streaming->setText(finalText);
        m_streaming->setFinal(true);
    } else {
        addAssistantBubble(finalText)->setFinal(true);
    }
    m_streaming.clear();
    m_streamText.clear();
}

void AiChatWindow::followBottom()
{
    if (!m_stickBottom) {
        updateJumpButton();
        return;
    }
    QTimer::singleShot(0, this, [this]() {
        if (!m_stickBottom) return;
        QScrollBar *bar = m_scroll->verticalScrollBar();
        m_programmaticScroll = true;
        bar->setValue(bar->maximum());
        m_programmaticScroll = false;
    });
}

void AiChatWindow::forceBottom()
{
    m_stickBottom = true;
    m_jump->setVisible(false);
    followBottom();
}

void AiChatWindow::connectSession()
{
    using S = engine::AgentSession;
    connect(m_session, &S::stateChanged, this, [this](S::State state) {
        updateInputState();
        switch (state) {
        case S::State::Thinking: m_status->setStatus(utils::tr(QStringLiteral("ai.status.thinking")), true); break;
        case S::State::RunningTool: m_status->setStatus(utils::tr(QStringLiteral("ai.status.running")), true); break;
        case S::State::AwaitingApproval: m_status->setStatus(utils::tr(QStringLiteral("ai.status.approval"))); break;
        case S::State::AwaitingAnswer: m_status->setStatus(utils::tr(QStringLiteral("ai.status.answer"))); break;
        case S::State::Idle: m_status->setStatus(QString()); break;
        }
    });
    connect(m_session, &S::messageAdded, this, [this](int index) {
        const AiMessage &m = m_session->conversation().messages.at(index);
        if (m.role == AiMessage::Role::User) {
            addUserBubble(m.text);
            forceBottom();
            updateTitle();
        } else if (m.role == AiMessage::Role::Assistant) {
            finalizeStreaming(m.text);
            for (const core::AiToolCall &call : m.toolCalls) {
                addToolCard(call);
            }
        }
        updateUsageLabel();
        m_dirty = true;
        saveSoon();
    });
    connect(m_session, &S::assistantStarted, this, [this]() {
        m_streaming.clear();
        m_streamText.clear();
    });
    connect(m_session, &S::assistantTextDelta, this, [this](const QString &t) {
        m_streamText += t;
        if (!m_streamTimer->isActive()) m_streamTimer->start();
    });
    connect(m_session, &S::toolAnnounced, this, [this](const QString &name) {
        m_status->setStatus(utils::tr(QStringLiteral("ai.status.preparing")).arg(aiToolLabel(name)), true);
    });
    connect(m_session, &S::approvalRequested, this, [this](const S::Approval &a) {
        if (AiToolCard *card = m_cards.value(a.call.id)) {
            m_tickets.insert(a.call.id, a.ticket);
            card->showApproval(a.plan);
        }
        show();
        raise();
        forceBottom();
    });
    connect(m_session, &S::questionRequested, this, [this](const QString &ticket, const QString &question, const QStringList &options) {
        // A pergunta pertence à última chamada ask_user ainda sem resposta.
        for (AiToolCard *card : m_cards) {
            if (card->call().name == QLatin1String("ask_user") && card->state() == AiToolCard::State::Waiting) {
                m_tickets.insert(card->call().id, ticket);
                card->showQuestion(question, options);
                break;
            }
        }
        show();
        raise();
        forceBottom();
    });
    connect(m_session, &S::toolFinished, this, [this](const core::AiToolCall &call, const core::AiToolResult &result) {
        if (AiToolCard *card = m_cards.value(call.id)) {
            card->setResult(result);
            card->setUndoable(result.changeId, !result.changeId.isEmpty());
        }
        m_tickets.remove(call.id);
        followBottom();
    });
    connect(m_session, &S::retrying, this, [this](int attempt, int, const engine::AiError &e) {
        m_status->setStatus(utils::tr(QStringLiteral("ai.status.retrying")).arg(attempt + 1).arg(engine::AiClient::kMaxAttempts)
                            + QStringLiteral(" — ") + aiErrorText(e).section(QLatin1Char('\n'), 0, 0), true);
    });
    connect(m_session, &S::errorOccurred, this, [this](const engine::AiError &e) {
        finalizeStreaming(QString());
        addNotice(aiErrorText(e), true, /*withRetry=*/true);
    });
    connect(m_session, &S::stepLimitReached, this, [this]() {
        addNotice(utils::tr(QStringLiteral("ai.chat.step_limit")), false);
    });
    connect(m_session, &S::turnFinished, this, [this]() {
        finalizeStreaming(QString());
        m_status->setStatus(QString());
        updateInputState();
        saveNow();
        followBottom();
        m_input->setFocus();
    });
}

void AiChatWindow::onSendClicked()
{
    if (m_session->isBusy()) {
        m_session->cancel();
        return;
    }
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }
    m_input->clear();
    sendMessage(text);
}

void AiChatWindow::sendMessage(const QString &text)
{
    if (m_profileCombo->count() == 0 || m_session->isBusy()) {
        return;
    }
    if (m_session->profile().id.isEmpty()) {
        applyProfileToSession();
    }
    engine::AiPromptContext ctx;
    ctx.uiLanguage = utils::TranslationManager::instance().currentLanguage();
    ctx.extraInstructions = m_profiles.extraInstructions;
    const core::SettingsData settings = m_host->settings();
    for (const core::Environment &e : settings.environments) {
        if (e.id == settings.activeEnvironmentId) ctx.activeEnvironment = e.name;
    }
    m_session->setPromptContext(ctx);
    m_session->setLimits(m_profiles.maxAgentSteps);
    m_session->send(text);
}

} // namespace kai::ui
