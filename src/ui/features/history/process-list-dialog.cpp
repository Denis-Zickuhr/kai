#include "ui/features/history/process-list-dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHideEvent>
#include <QShowEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "engine/process-runner.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/panel-metrics.h"
#include "ui/shared/tinted-badge.h"
#include "utils/design-tokens.h"
#include "utils/duration-format.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = utils::tokens;

namespace {

enum Role {
    RoleKind = Qt::UserRole + 1,   // 0 = cabeçalho de grupo, 1 = processo
    RoleId,
    RoleName,
    RoleStatus,
    RolePid,
    RoleForeground,
    RoleElapsedMs,
};
constexpr int kKindHeader = 0;
constexpr int kKindRow = 1;

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

// "PID 1234 · Primeiro plano · 2m 05s" (partes ausentes são omitidas).
QString subtitleFor(qint64 pid, bool foreground, qint64 elapsedMs)
{
    QStringList parts;
    if (pid > 0) {
        parts << utils::tr(QStringLiteral("processes.pid")).arg(pid);
    }
    parts << utils::tr(foreground ? QStringLiteral("processes.kind.foreground")
                                  : QStringLiteral("processes.kind.background"));
    if (elapsedMs >= 0) {
        parts << utils::formatShortDuration(elapsedMs);
    }
    return parts.join(QStringLiteral(" · "));
}

class ProcessItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        return QSize(0, index.data(RoleKind).toInt() == kKindHeader ? 30 : 60);
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
                              index.data(RoleName).toString().toUpper());
            painter->restore();
            return;
        }

        const auto status = static_cast<engine::ProcessStatus>(index.data(RoleStatus).toInt());
        const bool running = status == engine::ProcessStatus::Running;
        const StatusMeta meta = statusMeta(status);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;

        const QRect card = rect.adjusted(tk::space(1), 2, -tk::space(1), -2);
        if (selected || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? tk::selBg() : tk::hoverBg()));
            painter->drawRoundedRect(card, tk::radiusSm(), tk::radiusSm());
        }

        const int badge = 34;
        const int badgeX = card.left() + tk::space(3);
        painter->drawPixmap(badgeX, card.center().y() - badge / 2,
                            tintedBadgePixmap(meta.icon, meta.color, badge));
        const int textLeft = badgeX + badge + tk::space(3);

        // Estado à direita, na cor do estado.
        QFont stateFont = option.font;
        stateFont.setBold(true);
        const QString stateText = utils::tr(meta.labelKey);
        const int stateWidth = QFontMetrics(stateFont).horizontalAdvance(stateText);
        const QRect stateRect(card.right() - tk::space(3) - stateWidth, card.top() + tk::space(2), stateWidth, 20);
        painter->setFont(stateFont);
        painter->setPen(meta.color);
        painter->drawText(stateRect, Qt::AlignRight | Qt::AlignVCenter, stateText);

        // Nome (apagado quando já terminou) e linha de detalhes.
        QFont nameFont = option.font;
        nameFont.setBold(running);
        const int textRight = stateRect.left() - tk::space(2);
        const QRect nameRect(textLeft, card.top() + tk::space(2), qMax(0, textRight - textLeft), 20);
        painter->setFont(nameFont);
        painter->setPen(QColor(running ? tk::fg() : tk::mutedFg()));
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(nameFont).elidedText(index.data(RoleName).toString(), Qt::ElideRight, nameRect.width()));

        QFont smallFont = option.font;
        smallFont.setPointSizeF(qMax<qreal>(7.0, smallFont.pointSizeF() * 0.9));
        const QRect detailRect(textLeft, nameRect.bottom() + 2, qMax(0, card.right() - tk::space(3) - textLeft), 20);
        painter->setFont(smallFont);
        painter->setPen(QColor(tk::mutedFg()));
        painter->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(smallFont).elidedText(
                              subtitleFor(index.data(RolePid).toLongLong(), index.data(RoleForeground).toBool(),
                                          running ? index.data(RoleElapsedMs).toLongLong() : -1),
                              Qt::ElideRight, detailRect.width()));
        painter->restore();
    }
};

// Sem `text` vira botão só de ícone (quadrado, com o texto no tooltip).
QPushButton *makeButton(QWidget *parent, const QString &iconName, const QString &text, const QString &objectName,
                        const QString &tooltip = QString())
{
    auto *button = new QPushButton(text, parent);
    if (!tooltip.isEmpty()) {
        button->setToolTip(tooltip);
    }
    if (text.isEmpty()) {
        button->setFixedWidth(tk::controlHeight() + tk::space(2));
    }
    button->setObjectName(objectName);
    button->setIcon(LucideIcons::icon(iconName, QColor(tk::mutedFg()), 16));
    button->setIconSize(QSize(16, 16));
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoDefault(false);
    button->setDefault(false);
    return button;
}

} // namespace

ProcessListDialog::ProcessListDialog(engine::ProcessManager *processManager, QWidget *parent)
    : QDialog(parent)
    , m_processManager(processManager)
{
    setWindowTitle(utils::tr(QStringLiteral("processes.title")));
    setSizeGripEnabled(true);
    setupUi();
    applyStyle();

    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, [this]() { tick(); });

    refreshProcessList();
    setMinimumSize(640, 400);
    resize(760, 480);
    centerOnParent(this);
}

void ProcessListDialog::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    outer->setSpacing(tk::space(3));

    // --- Topo: resumo | filtro ---
    auto *top = new QHBoxLayout();
    top->setSpacing(tk::space(3));
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setProperty("kaiRole", QStringLiteral("subtitle"));
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

    const int frameInset = panelFrameInset();
    auto *listCard = new QFrame(this);
    listCard->setObjectName(QStringLiteral("processCard"));
    auto *listCardLayout = new QVBoxLayout(listCard);
    listCardLayout->setContentsMargins(frameInset, frameInset, frameInset, frameInset);
    m_listStack = new QStackedWidget(listCard);

    m_list = new QListWidget(m_listStack);
    m_list->setObjectName(QStringLiteral("processList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new ProcessItemDelegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::currentItemChanged, this, [this]() {
        updateActions();
        if (m_logsVisible && m_logView) {
            m_logView->setPlainText(m_logs.value(selectedId()));
        }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item && item->data(RoleKind).toInt() == kKindRow) {
            emit commandRequested(item->data(RoleId).toString());
        }
    });
    m_listStack->addWidget(m_list);

    auto *empty = new QWidget(m_listStack);
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    emptyLayout->addStretch(1);
    auto *emptyIcon = new QLabel(empty);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setPixmap(LucideIcons::icon(QStringLiteral("activity"), QColor(tk::mutedFg()), 32).pixmap(32, 32));
    emptyLayout->addWidget(emptyIcon);
    m_emptyText = new QLabel(empty);
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    m_emptyText->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    emptyLayout->addWidget(m_emptyText);
    emptyLayout->addStretch(1);
    m_listStack->addWidget(empty);
    listCardLayout->addWidget(m_listStack);
    body->addWidget(listCard, 1);

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
    m_logView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(tk::codeAreaQss()));
    logLayout->addWidget(m_logView, 1);
    m_logCard->setVisible(false);
    body->addWidget(m_logCard, 2);
    outer->addLayout(body, 1);

    // --- Ações sobre o selecionado + Fechar ---
    auto *actions = new QHBoxLayout();
    actions->setSpacing(tk::space(2));
    m_stopButton = makeButton(this, QStringLiteral("square"), utils::tr(QStringLiteral("processes.action.stop")),
                              QStringLiteral("processStop"));
    m_forceButton = makeButton(this, QStringLiteral("octagon-x"), utils::tr(QStringLiteral("processes.action.force")),
                               QStringLiteral("processForceStop"));
    m_forceButton->setProperty("kaiRole", QStringLiteral("danger"));
    m_goToButton = makeButton(this, QStringLiteral("arrow-right"), utils::tr(QStringLiteral("processes.action.go_to")),
                              QStringLiteral("processGoTo"));
    m_copyPidButton = makeButton(this, QStringLiteral("copy"), QString(), QStringLiteral("processCopyPid"),
                                 utils::tr(QStringLiteral("processes.action.copy_pid")));
    connect(m_stopButton, &QPushButton::clicked, this, [this]() {
        if (!selectedId().isEmpty()) {
            emit stopRequested(selectedId(), false);
        }
    });
    connect(m_forceButton, &QPushButton::clicked, this, [this]() {
        if (!selectedId().isEmpty()) {
            emit stopRequested(selectedId(), true);
        }
    });
    connect(m_goToButton, &QPushButton::clicked, this, [this]() {
        if (!selectedId().isEmpty()) {
            emit commandRequested(selectedId());
        }
    });
    connect(m_copyPidButton, &QPushButton::clicked, this, [this]() {
        if (const QListWidgetItem *item = m_list->currentItem()) {
            const qint64 pid = item->data(RolePid).toLongLong();
            if (pid > 0) {
                QApplication::clipboard()->setText(QString::number(pid));
            }
        }
    });
    actions->addWidget(m_stopButton);
    actions->addWidget(m_forceButton);
    actions->addWidget(m_goToButton);
    actions->addWidget(m_copyPidButton);
    actions->addStretch(1);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    actions->addWidget(buttonBox);
    outer->addLayout(actions);

    updateActions();
}

void ProcessListDialog::applyStyle()
{
    // Tudo derivado dos tokens; raios SEMPRE pelos tokens de canto.
    setStyleSheet(QStringLiteral(
        "QFrame#processCard { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
        "QListWidget#processList { background: transparent; border: none; outline: none; }"
        "QListWidget#processList::item { background: transparent; border: none; padding: 0px; }"
        "QListWidget#processList::item:selected { background: transparent; }"
        "QFrame#processFilter { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
        "QToolButton#processFilterButton { background: transparent; border: none; color: %4;"
        " padding: 3px %5px; border-radius: %6px; }"
        "QToolButton#processFilterButton:checked { background-color: %7; color: %8; font-weight: 600; }"
        "QToolButton#processFilterButton:hover:!checked { color: %8; }")
        .arg(tk::surface(), tk::borderColor())
        .arg(tk::radiusMd())
        .arg(tk::mutedFg())
        .arg(tk::space(3))
        .arg(qMax(0, tk::radiusMd() - 3))
        .arg(tk::selBg(), tk::fg()));
    if (m_logView) {
        m_logView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(tk::codeAreaQss()));
    }
    const QColor muted(tk::mutedFg());
    m_stopButton->setIcon(LucideIcons::icon(QStringLiteral("square"), muted, 16));
    m_forceButton->setIcon(LucideIcons::icon(QStringLiteral("octagon-x"), muted, 16));
    m_goToButton->setIcon(LucideIcons::icon(QStringLiteral("arrow-right"), muted, 16));
    m_copyPidButton->setIcon(LucideIcons::icon(QStringLiteral("copy"), muted, 16));
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

void ProcessListDialog::setCommandName(const QString &commandId, const QString &displayName)
{
    m_displayNames[commandId] = displayName;
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

void ProcessListDialog::updateActions()
{
    const QListWidgetItem *item = m_list->currentItem();
    const bool hasRow = item && item->data(RoleKind).toInt() == kKindRow;
    const bool running = hasRow
        && static_cast<engine::ProcessStatus>(item->data(RoleStatus).toInt()) == engine::ProcessStatus::Running;
    m_stopButton->setEnabled(running);
    m_forceButton->setEnabled(running);
    m_goToButton->setEnabled(hasRow);
    m_copyPidButton->setEnabled(hasRow && item->data(RolePid).toLongLong() > 0);
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

    // Desde quando cada um está rodando (primeira vez visto); some ao terminar.
    const QDateTime now = QDateTime::currentDateTime();
    QSet<QString> runningIds;
    for (const Row &row : running) {
        runningIds.insert(row.id);
        if (!m_runningSince.contains(row.id)) {
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
        m_list->clear();
        auto addHeader = [this](const QString &text) {
            auto *header = new QListWidgetItem(m_list);
            header->setData(RoleKind, kKindHeader);
            header->setData(RoleName, text);
            header->setFlags(Qt::NoItemFlags);
        };
        auto addRow = [this, &previous, &now](const Row &row) {
            auto *item = new QListWidgetItem(m_list);
            item->setData(RoleKind, kKindRow);
            item->setData(RoleId, row.id);
            item->setData(RoleName, m_displayNames.value(row.id, row.id));
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
    updateActions();
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
