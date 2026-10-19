#include "ui/features/history/process-list-dialog.h"

#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHelpEvent>
#include <QHideEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QShowEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>

#include "engine/process-runner.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/panel-metrics.h"
#include "utils/design-tokens.h"
#include "utils/duration-format.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = utils::tokens;
using RowAction = ProcessListDialog::RowAction;

namespace {

enum Role {
    RoleKind = Qt::UserRole + 1,   // 0 = cabeçalho de grupo, 1 = processo
    RoleId,
    RoleWhere,
    RoleStatus,
    RolePid,
    RoleForeground,
    RoleElapsedMs,
};
constexpr int kKindHeader = 0;
constexpr int kKindRow = 1;
constexpr int kRowHeight = 34;
constexpr int kHeaderHeight = 26;
constexpr int kStatusIcon = 16;
constexpr int kActionBox = 24;
constexpr int kActionIcon = 14;

struct StatusMeta {
    QString icon;
    QColor color;
    QString labelKey;
};

StatusMeta statusMeta(engine::ProcessStatus status)
{
    switch (status) {
    case engine::ProcessStatus::Running:
        return {QStringLiteral("loader"), QColor(tk::infoFg()), QStringLiteral("processes.status.running")};
    case engine::ProcessStatus::Success:
        return {QStringLiteral("circle-check"), QColor(tk::successFg()), QStringLiteral("processes.status.success")};
    case engine::ProcessStatus::Error:
        return {QStringLiteral("circle-x"), QColor(tk::errorFg()), QStringLiteral("processes.status.error")};
    }
    return {QStringLiteral("circle-alert"), QColor(tk::mutedFg()), QStringLiteral("processes.status.unknown")};
}

bool isRunningRow(const QModelIndex &index)
{
    return static_cast<engine::ProcessStatus>(index.data(RoleStatus).toInt()) == engine::ProcessStatus::Running;
}

QRect rowCard(const QRect &rect)
{
    return rect.adjusted(tk::space(1), 1, -tk::space(1), -1);
}

// Os botões da linha, da direita para a esquerda: "mais ações" sempre, parar só enquanto roda.
QRect actionBox(const QRect &rect, RowAction action, bool running)
{
    const QRect card = rowCard(rect);
    const int top = card.center().y() - kActionBox / 2 + 1;
    const int moreLeft = card.right() - tk::space(1) - kActionBox + 1;
    if (action == RowAction::More) {
        return QRect(moreLeft, top, kActionBox, kActionBox);
    }
    if (action == RowAction::Stop && running) {
        return QRect(moreLeft - 2 - kActionBox, top, kActionBox, kActionBox);
    }
    return {};
}

QFont smallFont(const QFont &base)
{
    QFont font = base;
    font.setPointSizeF(qMax<qreal>(7.0, font.pointSizeF() * 0.88));
    return font;
}

// "PID 1234 · Primeiro plano" (o PID só quando há).
QString detailsFor(qint64 pid, bool foreground)
{
    QStringList parts;
    if (pid > 0) {
        parts << utils::tr(QStringLiteral("processes.pid")).arg(pid);
    }
    parts << utils::tr(foreground ? QStringLiteral("processes.kind.foreground")
                                  : QStringLiteral("processes.kind.background"));
    return parts.join(QStringLiteral(" · "));
}

class ProcessItemDelegate : public QStyledItemDelegate {
public:
    ProcessItemDelegate(std::function<RowAction(const QModelIndex &)> hovered, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_hovered(std::move(hovered))
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        return QSize(0, index.data(RoleKind).toInt() == kKindHeader ? kHeaderHeight : kRowHeight);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect rect = option.rect;

        if (index.data(RoleKind).toInt() == kKindHeader) {
            QFont font = smallFont(option.font);
            font.setBold(true);
            font.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
            painter->setFont(font);
            painter->setPen(QColor(tk::mutedFg()));
            painter->drawText(rect.adjusted(tk::space(3), 0, -tk::space(3), -tk::space(1)),
                              Qt::AlignLeft | Qt::AlignBottom, index.data(Qt::DisplayRole).toString().toUpper());
            painter->restore();
            return;
        }

        const auto status = static_cast<engine::ProcessStatus>(index.data(RoleStatus).toInt());
        const bool running = status == engine::ProcessStatus::Running;
        const StatusMeta meta = statusMeta(status);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const bool showActions = selected || hovered;

        const QRect card = rowCard(rect);
        if (selected || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? tk::selBg() : tk::hoverBg()));
            painter->drawRoundedRect(card, tk::radiusSm(), tk::radiusSm());
        }

        const int iconX = card.left() + tk::space(2);
        painter->drawPixmap(iconX, card.center().y() - kStatusIcon / 2 + 1,
                            LucideIcons::icon(meta.icon, meta.color, kStatusIcon).pixmap(kStatusIcon, kStatusIcon));
        const int textLeft = iconX + kStatusIcon + tk::space(2);

        int right = card.right() - tk::space(2);
        if (showActions) {
            const RowAction hoveredAction = m_hovered ? m_hovered(index) : RowAction::None;
            for (RowAction action : {RowAction::More, RowAction::Stop}) {
                const QRect box = actionBox(rect, action, running);
                if (box.isEmpty()) {
                    continue;
                }
                const bool hot = hoveredAction == action;
                if (hot) {
                    QColor fill(tk::fg());
                    fill.setAlpha(28);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(fill);
                    painter->drawRoundedRect(box, tk::radiusSm(), tk::radiusSm());
                }
                const QString icon = action == RowAction::Stop ? QStringLiteral("square") : QStringLiteral("ellipsis");
                const QColor color(hot ? tk::fg() : tk::mutedFg());
                painter->drawPixmap(box.center().x() - kActionIcon / 2 + 1, box.center().y() - kActionIcon / 2 + 1,
                                    LucideIcons::icon(icon, color, kActionIcon).pixmap(kActionIcon, kActionIcon));
                right = box.left() - tk::space(2);
            }
        }

        // À direita: o tempo rodando (e "segundo plano", quando é) ou, já terminado, o estado na cor dele.
        const QFont metaFont = smallFont(option.font);
        QString metaText;
        if (running) {
            const qint64 elapsed = index.data(RoleElapsedMs).toLongLong();
            metaText = elapsed >= 0 ? utils::formatShortDuration(elapsed) : QString();
            if (!index.data(RoleForeground).toBool()) {
                metaText = metaText.isEmpty()
                    ? utils::tr(QStringLiteral("processes.kind.background"))
                    : utils::tr(QStringLiteral("processes.kind.background")) + QStringLiteral(" · ") + metaText;
            }
        } else {
            metaText = utils::tr(meta.labelKey);
        }
        const int metaWidth = QFontMetrics(metaFont).horizontalAdvance(metaText);
        const QRect metaRect(right - metaWidth + 1, card.top(), metaWidth, card.height());
        painter->setFont(metaFont);
        painter->setPen(running ? QColor(tk::mutedFg()) : meta.color);
        painter->drawText(metaRect, Qt::AlignRight | Qt::AlignVCenter, metaText);

        // Nome (apagado quando já terminou) e, se couber, a pasta logo depois.
        QFont nameFont = option.font;
        nameFont.setWeight(running ? QFont::DemiBold : QFont::Normal);
        const int nameRight = metaRect.left() - tk::space(3);
        const int available = qMax(0, nameRight - textLeft);
        const QFontMetrics nameMetrics(nameFont);
        const QString name = nameMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, available);
        painter->setFont(nameFont);
        painter->setPen(QColor(running ? tk::fg() : tk::mutedFg()));
        painter->drawText(QRect(textLeft, card.top(), available, card.height()), Qt::AlignLeft | Qt::AlignVCenter, name);

        const QString where = index.data(RoleWhere).toString();
        const int whereLeft = textLeft + nameMetrics.horizontalAdvance(name) + tk::space(2);
        const QFontMetrics whereMetrics(metaFont);
        if (!where.isEmpty() && nameRight - whereLeft > whereMetrics.horizontalAdvance(QStringLiteral("…")) * 4) {
            painter->setFont(metaFont);
            painter->setPen(QColor(tk::mutedFg()));
            painter->drawText(QRect(whereLeft, card.top(), nameRight - whereLeft, card.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              whereMetrics.elidedText(where, Qt::ElideRight, nameRight - whereLeft));
        }
        painter->restore();
    }

private:
    std::function<RowAction(const QModelIndex &)> m_hovered;
};

} // namespace

ProcessListDialog::ProcessListDialog(engine::ProcessManager *processManager, QWidget *parent, bool embedded)
    : QDialog(parent)
    , m_processManager(processManager)
{
    if (embedded) {
        setWindowFlags(Qt::Widget);
    }
    setWindowTitle(utils::tr(QStringLiteral("processes.title")));
    setSizeGripEnabled(true);
    setupUi();
    applyStyle();
    if (embedded) {
        setEmbedded(true);
    }

    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, [this]() { tick(); });

    refreshProcessList();
    if (!embedded) {
        resize(560, 380);
    }
    centerOnParent(this);
}

void ProcessListDialog::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    outer->setSpacing(tk::space(2));

    // --- Topo: resumo | filtro ---
    auto *top = new QHBoxLayout();
    top->setContentsMargins(tk::space(1), 0, 0, 0);
    top->setSpacing(tk::space(3));
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("processSummary"));
    top->addWidget(m_summaryLabel);
    top->addStretch(1);

    auto *filterFrame = new QFrame(this);
    filterFrame->setObjectName(QStringLiteral("processFilter"));
    auto *filterLayout = new QHBoxLayout(filterFrame);
    filterLayout->setContentsMargins(2, 2, 2, 2);
    filterLayout->setSpacing(2);
    auto makeFilterButton = [filterFrame](const QString &text) {
        auto *button = new QToolButton(filterFrame);
        button->setObjectName(QStringLiteral("processFilterButton"));
        button->setText(text);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    m_filterRunningButton = makeFilterButton(utils::tr(QStringLiteral("processes.filter.running")));
    m_filterAllButton = makeFilterButton(utils::tr(QStringLiteral("processes.filter.all")));
    m_filterRunningButton->setChecked(true);
    filterLayout->addWidget(m_filterRunningButton);
    filterLayout->addWidget(m_filterAllButton);
    auto setFilter = [this](Filter filter) {
        m_filter = filter;
        m_filterRunningButton->setChecked(filter == Filter::Running);
        m_filterAllButton->setChecked(filter == Filter::All);
        refreshProcessList();
    };
    connect(m_filterRunningButton, &QToolButton::clicked, this, [setFilter]() { setFilter(Filter::Running); });
    connect(m_filterAllButton, &QToolButton::clicked, this, [setFilter]() { setFilter(Filter::All); });
    top->addWidget(filterFrame);
    outer->addLayout(top);

    // --- Corpo: lista (+ painel de log, desligado por ora) ---
    auto *body = new QHBoxLayout();
    body->setSpacing(tk::space(3));

    // Como diálogo, a lista vai num cartão; como painel, o cartão já é o do quadro (sem moldura dupla).
    m_listCard = new QFrame(this);
    m_listCard->setObjectName(QStringLiteral("processCard"));
    auto *listCardLayout = new QVBoxLayout(m_listCard);
    const int frameInset = panelFrameInset();
    listCardLayout->setContentsMargins(frameInset, frameInset, frameInset, frameInset);
    m_listStack = new QStackedWidget(m_listCard);
    m_listStack->setObjectName(QStringLiteral("processStack")); // transparente: sem um retângulo escuro dentro do cartão

    m_list = new QListWidget(m_listStack);
    m_list->setObjectName(QStringLiteral("processList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new ProcessItemDelegate(
        [this](const QModelIndex &index) {
            return m_hoverIndex.isValid() && m_hoverIndex == index ? m_hoverAction : RowAction::None;
        },
        m_list));
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->viewport()->installEventFilter(this);
    connect(m_list, &QListWidget::currentItemChanged, this, [this]() {
        if (m_logsVisible && m_logView) {
            m_logView->setPlainText(m_logs.value(selectedId()));
        }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item && item->data(RoleKind).toInt() == kKindRow) {
            emit commandRequested(item->data(RoleId).toString());
        }
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_list->itemAt(pos);
        if (!item || item->data(RoleKind).toInt() != kKindRow) {
            return;
        }
        m_list->setCurrentItem(item);
        QMenu *menu = createActionsMenu(item->data(RoleId).toString(), this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(m_list->viewport()->mapToGlobal(pos));
    });
    m_listStack->addWidget(m_list);

    auto *empty = new QWidget(m_listStack);
    empty->setObjectName(QStringLiteral("processPage"));
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    emptyLayout->setSpacing(tk::space(2));
    emptyLayout->addStretch(1);
    m_emptyIcon = new QLabel(empty);
    m_emptyIcon->setObjectName(QStringLiteral("processEmptyIcon"));
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyIcon);
    m_emptyText = new QLabel(empty);
    m_emptyText->setObjectName(QStringLiteral("processEmptyText"));
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    emptyLayout->addWidget(m_emptyText);
    emptyLayout->addStretch(1);
    m_listStack->addWidget(empty);
    listCardLayout->addWidget(m_listStack);
    body->addWidget(m_listCard, 1);

    // Painel de log do processo selecionado: pronto, mas desligado por ora.
    m_logCard = new QFrame(this);
    m_logCard->setObjectName(QStringLiteral("processCard"));
    auto *logLayout = new QVBoxLayout(m_logCard);
    logLayout->setContentsMargins(frameInset, frameInset, frameInset, frameInset);
    auto *logTitle = new QLabel(utils::tr(QStringLiteral("processes.log_label")), m_logCard);
    logTitle->setProperty("kaiRole", QStringLiteral("caption"));
    logLayout->addWidget(logTitle);
    m_logView = new QPlainTextEdit(m_logCard);
    m_logView->setReadOnly(true);
    logLayout->addWidget(m_logView, 1);
    m_logCard->setVisible(false);
    body->addWidget(m_logCard, 2);
    outer->addLayout(body, 1);

    // Fechar, só como diálogo.
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    stripDialogButtonIcons(m_buttonBox);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(m_buttonBox);
}

void ProcessListDialog::applyStyle()
{
    // Tudo derivado dos tokens; raios SEMPRE pelos tokens de canto.
    setStyleSheet(QStringLiteral(
        "QFrame#processCard { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
        "QFrame#processCard[kaiBare=\"true\"] { background: transparent; border: none; }"
        "QStackedWidget#processStack, QWidget#processPage, QLabel#processEmptyIcon { background: transparent; }"
        "QLabel#processSummary { background: transparent; color: %4; }"
        "QLabel#processEmptyText { background: transparent; color: %4; }"
        "QListWidget#processList { background: transparent; border: none; outline: none; }"
        "QListWidget#processList::item { background: transparent; border: none; padding: 0px; }"
        "QListWidget#processList::item:selected { background: transparent; }"
        "QFrame#processFilter { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
        "QToolButton#processFilterButton { background: transparent; border: none; color: %4;"
        " padding: 1px %5px; border-radius: %6px; }"
        "QToolButton#processFilterButton:checked { background-color: %7; color: %8; font-weight: 600; }"
        "QToolButton#processFilterButton:hover:!checked { color: %8; }")
        .arg(tk::surface(), tk::borderColor())
        .arg(tk::radiusMd())
        .arg(tk::mutedFg())
        .arg(tk::space(2))
        .arg(qMax(0, tk::radiusMd() - 3))
        .arg(tk::selBg(), tk::fg()));
    if (m_logView) {
        m_logView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(tk::codeAreaQss()));
    }
    m_emptyIcon->setPixmap(LucideIcons::icon(QStringLiteral("activity"), QColor(tk::mutedFg()), 24).pixmap(24, 24));
    m_list->viewport()->update();
}

void ProcessListDialog::applyThemeVariables(const QMap<QString, QString> &variables)
{
    Q_UNUSED(variables);   // as cores vêm dos tokens, já publicados
    applyStyle();
}

void ProcessListDialog::setLogsVisible(bool visible)
{
    m_logsVisible = visible;
    if (m_logCard) {
        m_logCard->setVisible(visible);
    }
    if (!visible) {
        m_logs.clear();   // desligado: não guarda o texto
        if (m_logView) {
            m_logView->clear();
        }
    }
}

void ProcessListDialog::appendLogFor(const QString &commandId, const QString &text, bool isError)
{
    Q_UNUSED(isError);
    if (!m_logsVisible) {
        return;   // sem o painel não há por que acumular saída de processo
    }
    m_logs[commandId] += text;
    if (selectedId() == commandId && m_logView) {
        m_logView->setPlainText(m_logs.value(commandId));
        m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
    }
}

void ProcessListDialog::setLabelProvider(std::function<Label(const QString &)> provider)
{
    m_labelProvider = std::move(provider);
}

void ProcessListDialog::setForegroundProvider(std::function<QList<QPair<QString, qint64>>()> provider)
{
    m_foregroundProvider = std::move(provider);
}

QString ProcessListDialog::selectedId() const
{
    const QListWidgetItem *item = m_list ? m_list->currentItem() : nullptr;
    return item && item->data(RoleKind).toInt() == kKindRow ? item->data(RoleId).toString() : QString();
}

QListWidgetItem *ProcessListDialog::itemFor(const QString &commandId) const
{
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (item->data(RoleKind).toInt() == kKindRow && item->data(RoleId).toString() == commandId) {
            return item;
        }
    }
    return nullptr;
}

QRect ProcessListDialog::actionRect(const QString &commandId, RowAction action) const
{
    const QListWidgetItem *item = itemFor(commandId);
    if (!item) {
        return {};
    }
    const QModelIndex index = m_list->indexFromItem(item);
    return actionBox(m_list->visualRect(index), action, isRunningRow(index));
}

ProcessListDialog::RowAction ProcessListDialog::actionAt(const QPoint &viewportPos, QListWidgetItem **itemOut) const
{
    QListWidgetItem *item = m_list->itemAt(viewportPos);
    if (itemOut) {
        *itemOut = item;
    }
    if (!item || item->data(RoleKind).toInt() != kKindRow) {
        return RowAction::None;
    }
    const QModelIndex index = m_list->indexFromItem(item);
    const QRect rect = m_list->visualRect(index);
    for (RowAction action : {RowAction::Stop, RowAction::More}) {
        if (actionBox(rect, action, isRunningRow(index)).contains(viewportPos)) {
            return action;
        }
    }
    return RowAction::None;
}

QMenu *ProcessListDialog::createActionsMenu(const QString &commandId, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("processActionsMenu"));
    const QListWidgetItem *item = itemFor(commandId);
    const bool running = item && isRunningRow(m_list->indexFromItem(item));
    const qint64 pid = item ? item->data(RolePid).toLongLong() : 0;
    const QColor muted(tk::mutedFg());
    if (running) {
        menu->addAction(LucideIcons::icon(QStringLiteral("square"), muted, 16),
                        utils::tr(QStringLiteral("processes.action.stop")), this,
                        [this, commandId]() { emit stopRequested(commandId, false); });
        menu->addAction(LucideIcons::icon(QStringLiteral("octagon-x"), QColor(tk::errorFg()), 16),
                        utils::tr(QStringLiteral("processes.action.force")), this,
                        [this, commandId]() { emit stopRequested(commandId, true); });
        menu->addSeparator();
    }
    menu->addAction(LucideIcons::icon(QStringLiteral("arrow-right"), muted, 16),
                    utils::tr(QStringLiteral("processes.action.go_to")), this,
                    [this, commandId]() { emit commandRequested(commandId); });
    QAction *copy = menu->addAction(LucideIcons::icon(QStringLiteral("copy"), muted, 16),
                                    utils::tr(QStringLiteral("processes.action.copy_pid")), this,
                                    [pid]() { QApplication::clipboard()->setText(QString::number(pid)); });
    copy->setEnabled(pid > 0);
    return menu;
}

void ProcessListDialog::triggerAction(QListWidgetItem *item, RowAction action)
{
    if (!item || item->data(RoleKind).toInt() != kKindRow) {
        return;
    }
    const QString id = item->data(RoleId).toString();
    if (action == RowAction::Stop) {
        emit stopRequested(id, false);
    } else if (action == RowAction::More) {
        const QRect box = actionBox(m_list->visualItemRect(item), RowAction::More, isRunningRow(m_list->indexFromItem(item)));
        QMenu *menu = createActionsMenu(id, this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(m_list->viewport()->mapToGlobal(box.bottomLeft() + QPoint(0, 2)));
    }
}

void ProcessListDialog::setHover(const QPersistentModelIndex &index, RowAction action)
{
    if (m_hoverIndex == index && m_hoverAction == action) {
        return;
    }
    m_hoverIndex = index;
    m_hoverAction = action;
    m_list->viewport()->setCursor(action == RowAction::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
    m_list->viewport()->update();
}

bool ProcessListDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_list->viewport()) {
        return QDialog::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::MouseMove: {
        const QPoint pos = static_cast<QMouseEvent *>(event)->position().toPoint();
        QListWidgetItem *item = nullptr;
        const RowAction action = actionAt(pos, &item);
        setHover(item ? QPersistentModelIndex(m_list->indexFromItem(item)) : QPersistentModelIndex(), action);
        break;
    }
    case QEvent::Leave:
        setHover({}, RowAction::None);
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = nullptr;
        if (mouse->button() == Qt::LeftButton && actionAt(mouse->position().toPoint(), &item) != RowAction::None) {
            m_list->setCurrentItem(item);
            return true;   // o botão age no soltar; o duplo clique nele não vira "ir para o comando"
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = nullptr;
        const RowAction action = mouse->button() == Qt::LeftButton ? actionAt(mouse->position().toPoint(), &item)
                                                                   : RowAction::None;
        if (action != RowAction::None) {
            triggerAction(item, action);
            return true;
        }
        break;
    }
    case QEvent::ToolTip: {
        auto *help = static_cast<QHelpEvent *>(event);
        QListWidgetItem *item = nullptr;
        const RowAction action = actionAt(help->pos(), &item);
        if (!item || item->data(RoleKind).toInt() != kKindRow) {
            QToolTip::hideText();
            return true;
        }
        const QString text = action == RowAction::Stop ? utils::tr(QStringLiteral("processes.action.stop"))
            : action == RowAction::More               ? utils::tr(QStringLiteral("processes.action.more"))
                                                        : detailsFor(item->data(RolePid).toLongLong(),
                                                                     item->data(RoleForeground).toBool());
        QToolTip::showText(help->globalPos(), text, m_list->viewport());
        return true;
    }
    default:
        break;
    }
    return QDialog::eventFilter(watched, event);
}

void ProcessListDialog::refreshProcessList()
{
    const QString previous = selectedId();

    struct Row {
        QString id;
        engine::ProcessStatus status;
        qint64 pid;
        bool foreground;
    };
    QVector<Row> running;
    QVector<Row> finished;
    QSet<QString> listed;
    for (const QString &commandId : m_processManager->trackedCommandIds()) {
        const engine::ProcessStatus status = m_processManager->statusOf(commandId);
        qint64 pid = 0;
        if (status == engine::ProcessStatus::Running) {
            if (const engine::ProcessRunner *runner = m_processManager->runnerFor(commandId)) {
                pid = runner->processId();
            }
        }
        (status == engine::ProcessStatus::Running ? running : finished).append({commandId, status, pid, false});
        listed.insert(commandId);
    }
    // FOREGROUND: em execução, mas fora do ProcessManager.
    if (m_foregroundProvider) {
        for (const auto &entry : m_foregroundProvider()) {
            if (!listed.contains(entry.first)) {
                running.append({entry.first, engine::ProcessStatus::Running, entry.second, true});
            }
        }
    }

    QHash<QString, Label> labels;
    for (const QVector<Row> *rows : {&running, &finished}) {
        for (const Row &row : *rows) {
            Label label = m_labelProvider ? m_labelProvider(row.id) : Label{};
            if (label.name.isEmpty()) {
                label.name = row.id;
            }
            labels.insert(row.id, label);
        }
    }

    // Desde quando cada um está rodando (o início que o dono informa ou a primeira vez visto); some ao terminar.
    const QDateTime now = QDateTime::currentDateTime();
    QSet<QString> runningIds;
    for (const Row &row : running) {
        runningIds.insert(row.id);
        const QDateTime startedAt = labels.value(row.id).startedAt;
        if (startedAt.isValid()) {
            m_runningSince.insert(row.id, startedAt);
        } else if (!m_runningSince.contains(row.id)) {
            m_runningSince.insert(row.id, now);
        }
    }
    for (auto it = m_runningSince.begin(); it != m_runningSince.end();) {
        it = runningIds.contains(it.key()) ? std::next(it) : m_runningSince.erase(it);
    }
    // Mais antigos primeiro.
    std::stable_sort(running.begin(), running.end(), [this](const Row &a, const Row &b) {
        return m_runningSince.value(a.id) < m_runningSince.value(b.id);
    });

    const bool showFinished = m_filter == Filter::All;
    {
        const QSignalBlocker blocker(m_list);
        m_hoverIndex = QPersistentModelIndex();
        m_hoverAction = RowAction::None;
        m_list->clear();
        auto addHeader = [this](const QString &text) {
            auto *header = new QListWidgetItem(text, m_list);
            header->setData(RoleKind, kKindHeader);
            header->setFlags(Qt::NoItemFlags);
        };
        auto addRow = [this, &previous, &now, &labels](const Row &row) {
            const Label label = labels.value(row.id);
            auto *item = new QListWidgetItem(label.name, m_list);
            item->setData(RoleKind, kKindRow);
            item->setData(RoleId, row.id);
            item->setData(RoleWhere, label.where);
            item->setData(RoleStatus, static_cast<int>(row.status));
            item->setData(RolePid, row.pid);
            item->setData(RoleForeground, row.foreground);
            item->setData(RoleElapsedMs, m_runningSince.contains(row.id)
                                             ? m_runningSince.value(row.id).msecsTo(now) : qint64(-1));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (row.id == previous) {
                m_list->setCurrentItem(item);
            }
        };
        const bool grouped = showFinished && !running.isEmpty() && !finished.isEmpty();
        if (grouped) {
            addHeader(utils::tr(QStringLiteral("processes.group.running")));
        }
        for (const Row &row : running) {
            addRow(row);
        }
        if (showFinished && !finished.isEmpty()) {
            if (grouped || running.isEmpty()) {
                addHeader(utils::tr(QStringLiteral("processes.group.finished")));
            }
            for (const Row &row : finished) {
                addRow(row);
            }
        }
    }

    const int visibleRows = running.size() + (showFinished ? finished.size() : 0);
    m_listStack->setCurrentIndex(visibleRows > 0 ? 0 : 1);
    if (visibleRows == 0) {
        m_emptyText->setText(utils::tr(m_filter == Filter::Running && !finished.isEmpty()
            ? QStringLiteral("processes.empty.only_finished")
            : m_filter == Filter::Running ? QStringLiteral("processes.empty.running")
                                          : QStringLiteral("processes.empty.none")));
    }

    m_summaryLabel->setText(running.isEmpty() && finished.isEmpty()
        ? utils::tr(QStringLiteral("processes.summary.none"))
        : finished.isEmpty()
            ? utils::tr(QStringLiteral("processes.summary.running")).arg(running.size())
            : utils::tr(QStringLiteral("processes.summary.with_finished")).arg(running.size()).arg(finished.size()));
}

void ProcessListDialog::tick()
{
    // Só o tempo rodando muda a cada segundo.
    const QDateTime now = QDateTime::currentDateTime();
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (item->data(RoleKind).toInt() != kKindRow) {
            continue;
        }
        const auto it = m_runningSince.constFind(item->data(RoleId).toString());
        if (it != m_runningSince.constEnd()) {
            item->setData(RoleElapsedMs, it.value().msecsTo(now));
        }
    }
    m_list->viewport()->update();
}

void ProcessListDialog::setEmbedded(bool embedded)
{
    m_embedded = embedded;
    setWindowFlags(embedded ? Qt::Widget : Qt::Dialog);
    setProperty("kaiEmbedded", embedded);
    setSizeGripEnabled(!embedded);
    m_buttonBox->setVisible(!embedded);
    // Como painel, a lista fica direto no cartão do quadro.
    m_listCard->setProperty("kaiBare", embedded);
    m_listCard->layout()->setContentsMargins(embedded ? QMargins() : QMargins(panelFrameInset(), panelFrameInset(),
                                                                              panelFrameInset(), panelFrameInset()));
    m_listCard->style()->unpolish(m_listCard);
    m_listCard->style()->polish(m_listCard);
    layout()->setContentsMargins(embedded ? QMargins(tk::space(2), tk::space(2), tk::space(2), tk::space(2))
                                          : QMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3)));
    setMinimumSize(embedded ? QSize(240, 120) : QSize(420, 260));
}

void ProcessListDialog::done(int result)
{
    if (!m_embedded) {
        QDialog::done(result);
    }
}

// Esc fecha um diálogo, mas não um painel do quadro: quem fecha painel é o menu Exibir ou o X da aba.
// Na lista: Enter vai para o comando, Delete para o processo.
void ProcessListDialog::keyPressEvent(QKeyEvent *event)
{
    if (m_embedded && event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    const QString id = selectedId();
    if (!id.isEmpty() && m_list->hasFocus()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            emit commandRequested(id);
            return;
        }
        if (event->key() == Qt::Key_Delete && isRunningRow(m_list->indexFromItem(m_list->currentItem()))) {
            emit stopRequested(id, false);
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

void ProcessListDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    refreshProcessList();
    m_tickTimer.start();
}

void ProcessListDialog::hideEvent(QHideEvent *event)
{
    m_tickTimer.stop();
    QDialog::hideEvent(event);
}

} // namespace kai::ui
