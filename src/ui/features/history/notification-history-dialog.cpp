#include "ui/features/history/notification-history-dialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/panel-metrics.h"
#include "ui/shared/tinted-badge.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = utils::tokens;

namespace {

enum Role {
    RoleKind = Qt::UserRole + 1,   // 0 = cabeçalho de dia, 1 = notificação
    RoleId,
    RoleTitle,
    RoleBody,
    RoleWhen,
    RoleRead,
    RoleEvent,
};
constexpr int kKindHeader = 0;
constexpr int kKindRecord = 1;

struct EventMeta {
    QString icon;
    QColor color;
    QString labelKey;
};

// Ícone, cor e rótulo por tipo de evento (a chave vem de maybeShowNotification).
EventMeta eventMeta(const QString &eventKey)
{
    if (eventKey == QLatin1String("command_failure")) {
        return {QStringLiteral("circle-x"), QColor(tk::errorFg()), QStringLiteral("notifications.event.command_failure")};
    }
    if (eventKey == QLatin1String("background_crash")) {
        return {QStringLiteral("circle-alert"), QColor(tk::errorFg()), QStringLiteral("notifications.event.background_crash")};
    }
    if (eventKey == QLatin1String("background_success")) {
        return {QStringLiteral("circle-check"), QColor(tk::successFg()), QStringLiteral("notifications.event.background_success")};
    }
    if (eventKey == QLatin1String("config_recovered")) {
        return {QStringLiteral("shield-check"), QColor(tk::warningFg()), QStringLiteral("notifications.event.config_recovered")};
    }
    if (eventKey == QLatin1String("first_error_in_formatted_output")) {
        return {QStringLiteral("triangle-alert"), QColor(tk::warningFg()), QStringLiteral("notifications.event.first_error")};
    }
    if (eventKey.startsWith(QLatin1String("kip_notify"))) {
        const QString level = eventKey.mid(QStringLiteral("kip_notify_").size());
        if (level == QLatin1String("error")) {
            return {QStringLiteral("circle-x"), QColor(tk::errorFg()), QStringLiteral("notifications.event.kip")};
        }
        if (level == QLatin1String("warning")) {
            return {QStringLiteral("triangle-alert"), QColor(tk::warningFg()), QStringLiteral("notifications.event.kip")};
        }
        if (level == QLatin1String("success")) {
            return {QStringLiteral("circle-check"), QColor(tk::successFg()), QStringLiteral("notifications.event.kip")};
        }
        return {QStringLiteral("info"), QColor(tk::infoFg()), QStringLiteral("notifications.event.kip")};
    }
    if (eventKey == QLatin1String("easter_egg_eye")) {
        return {QStringLiteral("eye"), QColor(tk::warningFg()), QStringLiteral("notifications.event.easter_egg")};
    }
    if (eventKey == QLatin1String("cli_raise_error")) {
        return {QStringLiteral("circle-x"), QColor(tk::errorFg()), QStringLiteral("notifications.event.cli")};
    }
    if (eventKey == QLatin1String("cli_raise_warning")) {
        return {QStringLiteral("triangle-alert"), QColor(tk::warningFg()), QStringLiteral("notifications.event.cli")};
    }
    if (eventKey.startsWith(QLatin1String("cli_raise"))) {
        return {QStringLiteral("info"), QColor(tk::infoFg()), QStringLiteral("notifications.event.cli")};
    }
    return {QStringLiteral("bell"), QColor(tk::mutedFg()), QStringLiteral("notifications.event.generic")};
}

QPixmap tintedBadge(const EventMeta &meta, int diameter)
{
    return tintedBadgePixmap(meta.icon, meta.color, diameter);
}

QString previewOf(const QString &body)
{
    return body.simplified();
}

// Desenha cada linha: cabeçalho de dia (texto pequeno) ou cartão de
// notificação (ícone do tipo, ponto de não lida, título, prévia, hora).
class NotificationItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        const bool header = index.data(RoleKind).toInt() == kKindHeader;
        return QSize(0, header ? 30 : 64);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect rect = option.rect;

        if (index.data(RoleKind).toInt() == kKindHeader) {
            QFont font = option.font;
            font.setPointSizeF(qMax<qreal>(7.0, font.pointSizeF() * 0.85));
            font.setBold(true);
            font.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
            painter->setFont(font);
            painter->setPen(QColor(tk::mutedFg()));
            painter->drawText(rect.adjusted(tk::space(3), 0, -tk::space(3), -tk::space(1)),
                              Qt::AlignLeft | Qt::AlignBottom,
                              index.data(RoleTitle).toString().toUpper());
            painter->restore();
            return;
        }

        const bool read = index.data(RoleRead).toBool();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;

        const QRect card = rect.adjusted(tk::space(1), 2, -tk::space(1), -2);
        if (selected || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? tk::selBg() : tk::hoverBg()));
            painter->drawRoundedRect(card, tk::radiusSm(), tk::radiusSm());
        }

        // Ponto de não lida (cor primária do tema).
        const int dot = 8;
        if (!read) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(tk::accent()));
            painter->drawEllipse(QRectF(card.left() + tk::space(2), card.center().y() - dot / 2.0, dot, dot));
        }

        // Ícone do tipo num círculo tingido.
        const int badge = 34;
        const QPixmap badgePixmap = tintedBadge(eventMeta(index.data(RoleEvent).toString()), badge);
        const int badgeX = card.left() + tk::space(2) + dot + tk::space(2);
        painter->drawPixmap(badgeX, card.center().y() - badge / 2, badgePixmap);

        const int textLeft = badgeX + badge + tk::space(3);

        // Hora à direita.
        QFont smallFont = option.font;
        smallFont.setPointSizeF(qMax<qreal>(7.0, smallFont.pointSizeF() * 0.9));
        const QString when = NotificationHistoryDialog::relativeTime(index.data(RoleWhen).toDateTime(), QDateTime::currentDateTime());
        painter->setFont(smallFont);
        const int whenWidth = QFontMetrics(smallFont).horizontalAdvance(when);
        const QRect whenRect(card.right() - tk::space(3) - whenWidth, card.top() + tk::space(2), whenWidth, 18);
        painter->setPen(QColor(read ? tk::mutedFg() : tk::fg()));
        painter->drawText(whenRect, Qt::AlignRight | Qt::AlignVCenter, when);

        // Título (negrito quando não lida) e prévia do corpo.
        QFont titleFont = option.font;
        titleFont.setBold(!read);
        const int textRight = whenRect.left() - tk::space(2);
        const QRect titleRect(textLeft, card.top() + tk::space(2), qMax(0, textRight - textLeft), 20);
        painter->setFont(titleFont);
        painter->setPen(QColor(tk::fg()));
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(titleFont).elidedText(index.data(RoleTitle).toString(), Qt::ElideRight, titleRect.width()));

        const QRect previewRect(textLeft, titleRect.bottom() + 2, qMax(0, card.right() - tk::space(3) - textLeft), 20);
        painter->setFont(smallFont);
        painter->setPen(QColor(tk::mutedFg()));
        painter->drawText(previewRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(smallFont).elidedText(previewOf(index.data(RoleBody).toString()), Qt::ElideRight, previewRect.width()));

        painter->restore();
    }
};

// Botão de ação. Sem `text` vira botão só de ícone (quadrado, com tooltip).
// Nenhum é "default" do diálogo: o primeiro QPushButton virava botão primário
// (roxo) sozinho.
QPushButton *makeActionButton(QWidget *parent, const QString &iconName, const QString &text,
                              const QString &tooltip = QString())
{
    auto *button = new QPushButton(text, parent);
    button->setIcon(LucideIcons::icon(iconName, QColor(tk::mutedFg()), 16));
    button->setIconSize(QSize(16, 16));
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoDefault(false);
    button->setDefault(false);
    if (!tooltip.isEmpty()) {
        button->setToolTip(tooltip);
    }
    if (text.isEmpty()) {
        button->setFixedWidth(tk::controlHeight() + tk::space(2));
    }
    return button;
}

} // namespace

// --------------------------------------------------------------------- estáticos
QString NotificationHistoryDialog::relativeTime(const QDateTime &when, const QDateTime &now)
{
    if (!when.isValid()) {
        return QString();
    }
    const qint64 seconds = when.secsTo(now);
    if (seconds >= 0 && seconds < 60) {
        return utils::tr(QStringLiteral("notifications.time.now"));
    }
    if (seconds >= 60 && seconds < 3600) {
        return utils::tr(QStringLiteral("notifications.time.minutes_ago")).arg(seconds / 60);
    }
    return when.time().toString(QStringLiteral("HH:mm"));
}

QString NotificationHistoryDialog::dayLabel(const QDate &date, const QDate &today)
{
    if (date == today) {
        return utils::tr(QStringLiteral("notifications.day.today"));
    }
    if (date == today.addDays(-1)) {
        return utils::tr(QStringLiteral("notifications.day.yesterday"));
    }
    return date.toString(QStringLiteral("dd/MM/yyyy"));
}

// --------------------------------------------------------------------- construção
NotificationHistoryDialog::NotificationHistoryDialog(core::NotificationHistory *history, QWidget *parent)
    : QDialog(parent)
    , m_history(history)
{
    setWindowTitle(utils::tr(QStringLiteral("notifications.history.title")));
    setupUi();
    applyStyle();

    // Marca como lida só depois de a notificação ficar selecionada um instante
    // (passar com as setas por várias não marca todas).
    m_markReadTimer.setSingleShot(true);
    m_markReadTimer.setInterval(600);
    connect(&m_markReadTimer, &QTimer::timeout, this, [this]() {
        const core::NotificationRecord *record = selectedRecord();
        if (record && !record->read && m_history) {
            m_history->setRead(record->id, true);
        }
    });

    if (m_history) {
        // Mudanças vindas de fora (nova notificação com o diálogo aberto) e
        // as nossas chegam pelo mesmo sinal: uma só rota de atualização.
        connect(m_history, &core::NotificationHistory::unreadCountChanged, this, [this]() { scheduleReload(); });
    }

    reload();
    setMinimumSize(860, 480);
    resize(1000, 600);
    centerOnParent(this);
}

void NotificationHistoryDialog::setCommandResolver(CommandResolver resolver)
{
    m_resolver = std::move(resolver);
    updateDetail();
}

void NotificationHistoryDialog::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    outer->setSpacing(tk::space(3));

    // --- Topo: resumo | filtro | busca ---
    auto *top = new QHBoxLayout();
    top->setSpacing(tk::space(3));

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setProperty("kaiRole", QStringLiteral("subtitle"));
    top->addWidget(m_summaryLabel);
    top->addStretch(1);

    auto *filterFrame = new QFrame(this);
    filterFrame->setObjectName(QStringLiteral("notificationFilter"));
    auto *filterLayout = new QHBoxLayout(filterFrame);
    filterLayout->setContentsMargins(2, 2, 2, 2);
    filterLayout->setSpacing(2);
    auto makeFilterButton = [filterFrame](const QString &text) {
        auto *button = new QToolButton(filterFrame);
        button->setObjectName(QStringLiteral("notificationFilterButton"));
        button->setText(text);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    m_filterAllButton = makeFilterButton(utils::tr(QStringLiteral("notifications.filter.all")));
    m_filterUnreadButton = makeFilterButton(utils::tr(QStringLiteral("notifications.filter.unread")));
    m_filterAllButton->setChecked(true);
    auto *filterGroup = new QButtonGroup(this);
    filterGroup->setExclusive(true);
    filterGroup->addButton(m_filterAllButton);
    filterGroup->addButton(m_filterUnreadButton);
    filterLayout->addWidget(m_filterAllButton);
    filterLayout->addWidget(m_filterUnreadButton);
    connect(filterGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *button) {
        m_filter = button == m_filterUnreadButton ? Filter::Unread : Filter::All;
        reload();
    });
    top->addWidget(filterFrame);

    m_searchField = new QLineEdit(this);
    m_searchField->setClearButtonEnabled(true);
    m_searchField->setPlaceholderText(utils::tr(QStringLiteral("notifications.search.placeholder")));
    m_searchField->addAction(LucideIcons::icon(QStringLiteral("search"), QColor(tk::mutedFg()), 16),
                             QLineEdit::LeadingPosition);
    m_searchField->setMinimumWidth(240);
    connect(m_searchField, &QLineEdit::textChanged, this, [this]() { reload(); });
    top->addWidget(m_searchField);
    outer->addLayout(top);

    // --- Corpo: lista | detalhe ---
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(tk::space(2));

    auto *listCard = new QFrame(splitter);
    listCard->setObjectName(QStringLiteral("notificationCard"));
    // Recuo concêntrico à borda arredondada do cartão: filhos quadrados
    // (lista, barra de rolagem, painéis) não podem pintar por cima do arco.
    const int frameInset = panelFrameInset();
    auto *listCardLayout = new QVBoxLayout(listCard);
    listCardLayout->setContentsMargins(frameInset, frameInset, frameInset, frameInset);
    m_listStack = new QStackedWidget(listCard);

    m_list = new QListWidget(m_listStack);
    m_list->setObjectName(QStringLiteral("notificationList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new NotificationItemDelegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::currentItemChanged, this, [this]() {
        const QListWidgetItem *item = m_list->currentItem();
        m_selectedId = item && item->data(RoleKind).toInt() == kKindRecord ? item->data(RoleId).toString() : QString();
        updateDetail();
        m_markReadTimer.start();
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &NotificationHistoryDialog::showContextMenu);
    m_listStack->addWidget(m_list);

    auto *emptyList = new QWidget(m_listStack);
    auto *emptyListLayout = new QVBoxLayout(emptyList);
    emptyListLayout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    emptyListLayout->addStretch(1);
    auto *emptyIcon = new QLabel(emptyList);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setPixmap(LucideIcons::icon(QStringLiteral("inbox"), QColor(tk::mutedFg()), 32).pixmap(32, 32));
    emptyListLayout->addWidget(emptyIcon);
    m_emptyListText = new QLabel(emptyList);
    m_emptyListText->setAlignment(Qt::AlignCenter);
    m_emptyListText->setWordWrap(true);
    m_emptyListText->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    emptyListLayout->addWidget(m_emptyListText);
    emptyListLayout->addStretch(1);
    m_listStack->addWidget(emptyList);
    listCardLayout->addWidget(m_listStack);

    auto *detailCard = new QFrame(splitter);
    detailCard->setObjectName(QStringLiteral("notificationCard"));
    auto *detailCardLayout = new QVBoxLayout(detailCard);
    detailCardLayout->setContentsMargins(frameInset, frameInset, frameInset, frameInset);
    m_detailStack = new QStackedWidget(detailCard);

    // Página 0: nada selecionado.
    auto *placeholder = new QWidget(m_detailStack);
    auto *placeholderLayout = new QVBoxLayout(placeholder);
    placeholderLayout->addStretch(1);
    auto *placeholderIcon = new QLabel(placeholder);
    placeholderIcon->setAlignment(Qt::AlignCenter);
    placeholderIcon->setPixmap(LucideIcons::icon(QStringLiteral("mail-open"), QColor(tk::mutedFg()), 32).pixmap(32, 32));
    placeholderLayout->addWidget(placeholderIcon);
    auto *placeholderText = new QLabel(utils::tr(QStringLiteral("notifications.detail.placeholder")), placeholder);
    placeholderText->setAlignment(Qt::AlignCenter);
    placeholderText->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    placeholderLayout->addWidget(placeholderText);
    placeholderLayout->addStretch(1);
    m_detailStack->addWidget(placeholder);

    // Página 1: detalhe.
    auto *detail = new QWidget(m_detailStack);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(3));
    detailLayout->setSpacing(tk::space(3));

    auto *head = new QHBoxLayout();
    head->setSpacing(tk::space(3));
    m_detailIcon = new QLabel(detail);
    m_detailIcon->setFixedSize(44, 44);
    head->addWidget(m_detailIcon, 0, Qt::AlignTop);
    auto *headText = new QVBoxLayout();
    headText->setSpacing(tk::space(1));
    m_detailTitle = new QLabel(detail);
    m_detailTitle->setProperty("kaiRole", QStringLiteral("title"));
    m_detailTitle->setWordWrap(true);
    m_detailTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headText->addWidget(m_detailTitle);
    auto *meta = new QHBoxLayout();
    meta->setSpacing(tk::space(2));
    m_detailChip = new QLabel(detail);
    m_detailChip->setObjectName(QStringLiteral("notificationChip"));
    meta->addWidget(m_detailChip);
    m_detailWhen = new QLabel(detail);
    m_detailWhen->setProperty("kaiRole", QStringLiteral("caption"));
    meta->addWidget(m_detailWhen);
    meta->addStretch(1);
    headText->addLayout(meta);
    head->addLayout(headText, 1);
    detailLayout->addLayout(head);

    auto *separator = new QFrame(detail);
    separator->setObjectName(QStringLiteral("notificationSeparator"));
    separator->setFixedHeight(1);
    detailLayout->addWidget(separator);

    m_detailBody = new QTextBrowser(detail);
    m_detailBody->setObjectName(QStringLiteral("notificationBody"));
    m_detailBody->setFrameShape(QFrame::NoFrame);
    m_detailBody->setOpenLinks(false);
    detailLayout->addWidget(m_detailBody, 1);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(tk::space(2));
    m_toggleReadButton = makeActionButton(detail, QStringLiteral("mail-open"), QStringLiteral(" "));
    m_toggleReadButton->setObjectName(QStringLiteral("notificationToggleRead"));
    m_goToButton = makeActionButton(detail, QStringLiteral("arrow-right"), utils::tr(QStringLiteral("notifications.action.go_to_command")));
    m_goToButton->setObjectName(QStringLiteral("notificationGoTo"));
    // Copiar e excluir: só ícone (o painel é estreito); o texto vive no tooltip.
    m_copyButton = makeActionButton(detail, QStringLiteral("copy"), QString(), utils::tr(QStringLiteral("notifications.action.copy")));
    m_copyButton->setObjectName(QStringLiteral("notificationCopy"));
    m_deleteButton = makeActionButton(detail, QStringLiteral("trash-2"), QString(), utils::tr(QStringLiteral("notifications.action.delete")));
    m_deleteButton->setObjectName(QStringLiteral("notificationDelete"));
    m_deleteButton->setProperty("kaiRole", QStringLiteral("danger"));
    actions->addWidget(m_toggleReadButton);
    actions->addWidget(m_goToButton);
    actions->addWidget(m_copyButton);
    actions->addStretch(1);
    actions->addWidget(m_deleteButton);
    detailLayout->addLayout(actions);
    connect(m_toggleReadButton, &QPushButton::clicked, this, &NotificationHistoryDialog::toggleSelectedRead);
    connect(m_goToButton, &QPushButton::clicked, this, &NotificationHistoryDialog::goToSelectedCommand);
    connect(m_copyButton, &QPushButton::clicked, this, &NotificationHistoryDialog::copySelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &NotificationHistoryDialog::deleteSelected);
    m_detailStack->addWidget(detail);
    detailCardLayout->addWidget(m_detailStack);

    splitter->addWidget(listCard);
    splitter->addWidget(detailCard);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({380, 620});
    listCard->setMinimumWidth(300);
    detailCard->setMinimumWidth(480);
    outer->addWidget(splitter, 1);

    // --- Rodapé ---
    auto *footer = new QHBoxLayout();
    footer->setSpacing(tk::space(2));
    m_markAllButton = makeActionButton(this, QStringLiteral("mail-open"), utils::tr(QStringLiteral("notifications.history.action.mark_all_read")));
    m_clearButton = makeActionButton(this, QStringLiteral("trash-2"), utils::tr(QStringLiteral("notifications.history.action.clear")));
    m_clearButton->setProperty("kaiRole", QStringLiteral("danger"));
    m_markAllButton->setAutoDefault(false);
    m_clearButton->setAutoDefault(false);
    connect(m_markAllButton, &QPushButton::clicked, this, &NotificationHistoryDialog::markAllRead);
    connect(m_clearButton, &QPushButton::clicked, this, &NotificationHistoryDialog::clearAll);
    footer->addWidget(m_markAllButton);
    footer->addWidget(m_clearButton);
    footer->addStretch(1);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    footer->addWidget(buttonBox);
    outer->addLayout(footer);

    // Delete exclui a selecionada (atalho de lista de e-mail).
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_list);
    deleteShortcut->setContext(Qt::WidgetShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &NotificationHistoryDialog::deleteSelected);
}

void NotificationHistoryDialog::applyStyle()
{
    // Tudo derivado dos tokens; raios SEMPRE pelos tokens de canto.
    setStyleSheet(QStringLiteral(
        "QFrame#notificationCard { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
        "QListWidget#notificationList { background: transparent; border: none; outline: none; }"
        "QListWidget#notificationList::item { background: transparent; border: none; padding: 0px; }"
        "QListWidget#notificationList::item:selected { background: transparent; }"
        "QTextBrowser#notificationBody { background: transparent; border: none; }"
        "QFrame#notificationSeparator { background-color: %2; border: none; }"
        "QFrame#notificationFilter { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
        "QToolButton#notificationFilterButton { background: transparent; border: none; color: %4;"
        " padding: 3px %5px; border-radius: %6px; }"
        "QToolButton#notificationFilterButton:checked { background-color: %7; color: %8; font-weight: 600; }"
        "QToolButton#notificationFilterButton:hover:!checked { color: %8; }"
        "QLabel#notificationChip { border-radius: %6px; padding: 1px %5px; font-weight: 600; font-size: 8pt; }")
        .arg(tk::surface(), tk::borderColor())
        .arg(tk::radiusMd())
        .arg(tk::mutedFg())
        .arg(tk::space(3))
        .arg(qMax(0, tk::radiusMd() - 3))
        .arg(tk::selBg(), tk::fg()));
}

// --------------------------------------------------------------------- dados
const core::NotificationRecord *NotificationHistoryDialog::selectedRecord() const
{
    if (m_selectedId.isEmpty()) {
        return nullptr;
    }
    for (const core::NotificationRecord &record : m_records) {
        if (record.id == m_selectedId) {
            return &record;
        }
    }
    return nullptr;
}

void NotificationHistoryDialog::scheduleReload()
{
    if (m_reloadQueued) {
        return;
    }
    m_reloadQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_reloadQueued = false;
        reload();
    });
}

void NotificationHistoryDialog::reload()
{
    m_records = m_history ? m_history->load() : QVector<core::NotificationRecord>();

    const QString needle = m_searchField ? m_searchField->text().trimmed() : QString();
    const int scrollValue = m_list->verticalScrollBar()->value();
    {
        const QSignalBlocker blocker(m_list);
        m_list->clear();
        const QDate today = QDate::currentDate();
        QDate lastDay;
        int shown = 0;
        QListWidgetItem *toSelect = nullptr;
        for (const core::NotificationRecord &record : m_records) {
            if (m_filter == Filter::Unread && record.read) {
                continue;
            }
            if (!needle.isEmpty()
                && !record.title.contains(needle, Qt::CaseInsensitive)
                && !record.body.contains(needle, Qt::CaseInsensitive)) {
                continue;
            }
            const QDate day = record.createdAt.date();
            if (!lastDay.isValid() || day != lastDay) {
                auto *header = new QListWidgetItem(m_list);
                header->setData(RoleKind, kKindHeader);
                header->setData(RoleTitle, dayLabel(day, today));
                header->setFlags(Qt::NoItemFlags);
                lastDay = day;
            }
            auto *item = new QListWidgetItem(m_list);
            item->setData(RoleKind, kKindRecord);
            item->setData(RoleId, record.id);
            item->setData(RoleTitle, record.title);
            item->setData(RoleBody, record.body);
            item->setData(RoleWhen, record.createdAt);
            item->setData(RoleRead, record.read);
            item->setData(RoleEvent, record.eventKey);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (record.id == m_selectedId) {
                toSelect = item;
            }
            ++shown;
        }
        if (toSelect) {
            m_list->setCurrentItem(toSelect);
        } else {
            m_list->setCurrentItem(nullptr);
            m_selectedId.clear();
        }
        m_listStack->setCurrentIndex(shown > 0 ? 0 : 1);
        if (shown == 0) {
            m_emptyListText->setText(utils::tr(
                !needle.isEmpty() ? QStringLiteral("notifications.empty.no_match")
                : m_filter == Filter::Unread ? QStringLiteral("notifications.empty.no_unread")
                                              : QStringLiteral("notifications.empty.none")));
        }
    }
    m_list->verticalScrollBar()->setValue(scrollValue);

    int unread = 0;
    for (const core::NotificationRecord &record : m_records) {
        unread += record.read ? 0 : 1;
    }
    m_summaryLabel->setText(m_records.isEmpty()
        ? utils::tr(QStringLiteral("notifications.summary.empty"))
        : utils::tr(unread == 1 ? QStringLiteral("notifications.summary.one")
                                : QStringLiteral("notifications.summary.other")).arg(unread).arg(m_records.size()));
    m_markAllButton->setEnabled(unread > 0);
    m_clearButton->setEnabled(!m_records.isEmpty());

    updateDetail();
}

void NotificationHistoryDialog::updateDetail()
{
    const core::NotificationRecord *record = selectedRecord();
    if (!record) {
        m_detailStack->setCurrentIndex(0);
        return;
    }
    const EventMeta meta = eventMeta(record->eventKey);
    m_detailIcon->setPixmap(tintedBadge(meta, 44));
    m_detailTitle->setText(record->title);
    m_detailChip->setText(utils::tr(meta.labelKey));
    QColor chipBackground = meta.color;
    chipBackground.setAlpha(40);
    m_detailChip->setStyleSheet(QStringLiteral("background-color: rgba(%1, %2, %3, %4); color: %5;")
        .arg(chipBackground.red()).arg(chipBackground.green()).arg(chipBackground.blue()).arg(chipBackground.alpha())
        .arg(meta.color.name()));
    m_detailWhen->setText(record->createdAt.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")));
    m_detailBody->setPlainText(record->body);

    m_toggleReadButton->setText(utils::tr(record->read
        ? QStringLiteral("notifications.action.mark_unread")
        : QStringLiteral("notifications.action.mark_read")));
    m_toggleReadButton->setIcon(LucideIcons::icon(
        record->read ? QStringLiteral("mail") : QStringLiteral("mail-open"), QColor(tk::mutedFg()), 16));

    // "Ir para o comando" só quando o comando ainda existe.
    const QString commandName = (!record->commandId.isEmpty() && m_resolver) ? m_resolver(record->commandId) : QString();
    m_goToButton->setVisible(!commandName.isEmpty());
    m_goToButton->setToolTip(utils::tr(QStringLiteral("notifications.action.go_to_command.tooltip")).arg(commandName));

    m_detailStack->setCurrentIndex(1);
}

// --------------------------------------------------------------------- ações
void NotificationHistoryDialog::toggleSelectedRead()
{
    const core::NotificationRecord *record = selectedRecord();
    if (!record || !m_history) {
        return;
    }
    m_markReadTimer.stop();
    m_history->setRead(record->id, !record->read);
}

void NotificationHistoryDialog::deleteSelected()
{
    const core::NotificationRecord *record = selectedRecord();
    if (!record || !m_history) {
        return;
    }
    // Seleciona o vizinho para o foco não pular para o topo da lista.
    const int row = m_list->currentRow();
    const QString id = record->id;
    m_markReadTimer.stop();
    m_selectedId.clear();
    m_history->remove(id);
    reload();
    for (int offset = 0; offset < m_list->count(); ++offset) {
        const int candidate = qMin(row + offset, m_list->count() - 1);
        QListWidgetItem *item = m_list->item(candidate);
        if (item && item->data(RoleKind).toInt() == kKindRecord) {
            m_list->setCurrentItem(item);
            break;
        }
    }
}

void NotificationHistoryDialog::copySelected()
{
    const core::NotificationRecord *record = selectedRecord();
    if (!record) {
        return;
    }
    QApplication::clipboard()->setText(QStringLiteral("%1\n%2\n\n%3")
        .arg(record->title, record->createdAt.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")), record->body));
}

void NotificationHistoryDialog::goToSelectedCommand()
{
    const core::NotificationRecord *record = selectedRecord();
    if (!record || record->commandId.isEmpty()) {
        return;
    }
    const QString commandId = record->commandId;
    accept();
    emit commandRequested(commandId);
}

void NotificationHistoryDialog::markAllRead()
{
    if (m_history) {
        m_markReadTimer.stop();
        m_history->markAllRead();
    }
}

void NotificationHistoryDialog::clearAll()
{
    if (!m_history || m_records.isEmpty()) {
        return;
    }
    if (!confirmYesNo(this, utils::tr(QStringLiteral("notifications.clear.confirm.title")),
                      utils::tr(QStringLiteral("notifications.clear.confirm.text")).arg(m_records.size()))) {
        return;
    }
    m_markReadTimer.stop();
    m_selectedId.clear();
    m_history->clear();
}

void NotificationHistoryDialog::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item || item->data(RoleKind).toInt() != kKindRecord) {
        return;
    }
    m_list->setCurrentItem(item);
    const core::NotificationRecord *record = selectedRecord();
    if (!record) {
        return;
    }
    QMenu menu(this);
    menu.addAction(m_toggleReadButton->icon(), m_toggleReadButton->text(), this, &NotificationHistoryDialog::toggleSelectedRead);
    if (m_goToButton->isVisible()) {
        menu.addAction(m_goToButton->icon(), m_goToButton->text(), this, &NotificationHistoryDialog::goToSelectedCommand);
    }
    menu.addAction(m_copyButton->icon(), utils::tr(QStringLiteral("notifications.action.copy")),
                   this, &NotificationHistoryDialog::copySelected);
    menu.addSeparator();
    menu.addAction(m_deleteButton->icon(), utils::tr(QStringLiteral("notifications.action.delete")),
                   this, &NotificationHistoryDialog::deleteSelected);
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

} // namespace kai::ui
