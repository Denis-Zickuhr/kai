#include "ui/shared/data-table-widget.h"

#include "ui/shared/lucide-icons.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QToolTip>

namespace kai::ui::fields {

namespace tk = kai::utils::tokens;

namespace {

constexpr int kIconSize = 26;
constexpr int kIconGap = 2;
constexpr int kMaxVisibleIcons = 3;
const QString kMoreId = QStringLiteral("__more");

QColor withAlpha(QColor c, int alpha)
{
    c.setAlpha(alpha);
    return c;
}

// Cor opaca: `over` por cima de `base` com a fração `amount` (a faixa de confirmação cobre a linha inteira).
QColor blend(const QColor &base, const QColor &over, qreal amount)
{
    return QColor::fromRgbF(base.redF() + (over.redF() - base.redF()) * amount,
                            base.greenF() + (over.greenF() - base.greenF()) * amount,
                            base.blueF() + (over.blueF() - base.blueF()) * amount);
}

} // namespace

QColor kipToneColor(const QString &tone)
{
    if (tone == QLatin1String("info")) return QColor(tk::infoFg());
    if (tone == QLatin1String("success")) return QColor(tk::successFg());
    if (tone == QLatin1String("warning")) return QColor(tk::warningFg());
    if (tone == QLatin1String("error")) return QColor(tk::errorFg());
    if (tone == QLatin1String("accent")) return QColor(tk::accent());
    return QColor(tk::mutedFg());
}

void DataTableWidget::fitVisibleRows(int rows)
{
    m_visibleRows = rows;
    refit();
}

void DataTableWidget::setExtraHeight(int px)
{
    m_extraHeight = qMax(0, px);
    refit();
}

QString DataTableWidget::rowKey(int row) const
{
    const QTableWidgetItem *it = item(row, 0);
    return it ? it->data(kRowKeyRole).toString() : QString();
}

int DataTableWidget::rowForKey(const QString &key) const
{
    for (int r = 0; r < rowCount(); ++r) {
        if (rowKey(r) == key) return r;
    }
    return -1;
}

void DataTableWidget::setBusyRow(const QString &key)
{
    if (m_busyRow != key) {
        m_busyRow = key;
        viewport()->update();
    }
}

void DataTableWidget::setFlashRow(const QString &key)
{
    if (m_flashRow != key) {
        m_flashRow = key;
        viewport()->update();
    }
}

bool DataTableWidget::event(QEvent *e)
{
    const bool handled = QTableWidget::event(e);
    if (e->type() == QEvent::StyleChange || e->type() == QEvent::Polish || e->type() == QEvent::Show
        || e->type() == QEvent::FontChange) {
        refit();
    }
    return handled;
}

bool DataTableWidget::viewportEvent(QEvent *e)
{
    switch (e->type()) {
    case QEvent::MouseMove: {
        const int row = rowAt(static_cast<QMouseEvent *>(e)->position().toPoint().y());
        if (row != m_hoverRow) {
            m_hoverRow = row;
            viewport()->update();
            emit hoverRowChanged(row);
        }
        break;
    }
    case QEvent::Leave:
        if (m_hoverRow != -1) {
            m_hoverRow = -1;
            viewport()->update();
            emit hoverRowChanged(-1);
        }
        break;
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(e);
        if (mouse->button() == Qt::LeftButton && handleCopyClick(mouse->position().toPoint())) {
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QTableWidget::viewportEvent(e);
}

bool DataTableWidget::handleCopyClick(const QPoint &pos)
{
    const QModelIndex index = indexAt(pos);
    if (!index.isValid() || columnFormat(index.column()) != QLatin1String("copy")) {
        return false;
    }
    if (!DataCellDelegate::copyIconRect(visualRect(index)).contains(pos)) {
        return false;
    }
    if (const QTableWidgetItem *it = item(index.row(), index.column())) {
        QApplication::clipboard()->setText(it->text());
        QToolTip::showText(viewport()->mapToGlobal(pos), utils::tr(QStringLiteral("kip.action.copied")), this);
    }
    return true;
}

void DataTableWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        copyCurrentCell();
        return;
    }
    QTableWidget::keyPressEvent(event);
}

void DataTableWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QTableWidgetItem *it = event->reason() == QContextMenuEvent::Keyboard ? currentItem() : itemAt(event->pos());
    if (!it) {
        return;
    }
    setCurrentItem(it);
    QMenu menu(this);
    QAction *copy = menu.addAction(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 16),
                                   utils::tr(QStringLiteral("kip.table.copy_cell")));
    if (m_menuHook) {
        m_menuHook(menu, it->row());
    }
    const QPoint where = event->reason() == QContextMenuEvent::Keyboard
        ? viewport()->mapToGlobal(visualItemRect(it).bottomLeft())
        : event->globalPos();
    if (menu.exec(where) == copy) {
        copyCurrentCell();
    }
}

void DataTableWidget::refit()
{
    if (m_visibleRows <= 0) {
        return;
    }
    const int h = horizontalHeader()->sizeHint().height() + m_visibleRows * verticalHeader()->defaultSectionSize()
        + 2 * frameWidth() + 4 + m_extraHeight;
    if (h != height() || h != minimumHeight() || h != maximumHeight()) {
        setFixedHeight(h);
    }
}

void DataTableWidget::copyCurrentCell()
{
    if (QTableWidgetItem *it = currentItem()) {
        QApplication::clipboard()->setText(it->text());
    }
}

DataCellDelegate::DataCellDelegate(DataTableWidget *table)
    : QStyledItemDelegate(table)
    , m_table(table)
{
}

void DataCellDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const
{
    editor->setGeometry(option.rect);
}

QRect DataCellDelegate::copyIconRect(const QRect &cell)
{
    const int side = 20;
    return QRect(cell.right() - side - 6, cell.center().y() - side / 2, side, side);
}

void DataCellDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const QString text = opt.text;
    opt.text.clear();
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const QString key = m_table->rowKey(index.row());
    if (!key.isEmpty() && key == m_table->flashRow()) {
        painter->fillRect(opt.rect, withAlpha(QColor(tk::successFg()), 34));
    }
    const bool busy = !key.isEmpty() && key == m_table->busyRow();
    const QString format = m_table->columnFormat(index.column());
    QRect r = opt.rect.adjusted(tk::space(2), 0, -tk::space(2), 0);
    painter->save();
    QColor color(busy ? tk::mutedFg() : tk::fg());

    if (format == QLatin1String("badge")) {
        if (!text.isEmpty()) {
            const QString tone = index.data(kBadgeToneRole).toString();
            const QColor c = kipToneColor(tone);
            QFont small = opt.font;
            small.setPointSize(qMax(7, tk::fontSizeSmallPt()));
            small.setWeight(QFont::Medium);
            const QFontMetrics fm(small);
            const int dot = 6;
            const int w = qMin(r.width(), fm.horizontalAdvance(text) + dot + 6 + 16);
            const int h = fm.height() + 4;
            const QRect pill(r.left(), r.center().y() - h / 2, w, h);
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            const bool neutral = tone.isEmpty() || tone == QLatin1String("neutral");
            painter->setBrush(neutral ? QColor(tk::surface2()) : withAlpha(c, 36));
            painter->drawRoundedRect(pill, tk::radiusSm(), tk::radiusSm());
            painter->setBrush(neutral ? QColor(tk::mutedFg()) : c);
            painter->drawEllipse(QRectF(pill.left() + 8, pill.center().y() - dot / 2.0 + 0.5, dot, dot));
            painter->setFont(small);
            painter->setPen(neutral ? QColor(tk::fg()) : c);
            const QRect textRect = pill.adjusted(8 + dot + 6, 0, -6, 0);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                              fm.elidedText(text, Qt::ElideRight, textRect.width()));
        }
        painter->restore();
        return;
    }

    QFont font = opt.font;
    Qt::Alignment align = Qt::AlignVCenter | Qt::AlignLeft;
    if (format == QLatin1String("mono") || format == QLatin1String("copy")) {
        font = tk::monoFont(qMax(8, tk::fontSizePt() - 1));
    } else if (format == QLatin1String("muted")) {
        color = QColor(tk::mutedFg());
    } else if (format == QLatin1String("number")) {
        align = Qt::AlignVCenter | Qt::AlignRight;
    }
    if (format == QLatin1String("copy") && (opt.state & QStyle::State_MouseOver)) {
        const QRect icon = copyIconRect(opt.rect);
        LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 16).paint(painter, icon.adjusted(2, 2, -2, -2));
        r.setRight(icon.left() - 4);
    }
    painter->setFont(font);
    painter->setPen(color);
    const QFontMetrics fm(font);
    painter->drawText(r, align, fm.elidedText(text, Qt::ElideRight, r.width()));
    painter->restore();
}

RowActionsController::RowActionsController(DataTableWidget *table, int actionsColumn,
                                           const QVector<core::KipChip> &actions)
    : QObject(table)
    , m_table(table)
    , m_column(actionsColumn)
    , m_actions(actions)
{
    table->setMouseTracking(true);
    table->viewport()->installEventFilter(this);
    table->setItemDelegateForColumn(actionsColumn, new RowActionsDelegate(this));
    m_spin.setInterval(50);
    connect(&m_spin, &QTimer::timeout, this, [this]() {
        m_angle = (m_angle + 24) % 360;
        const int row = m_table->rowForKey(m_row);
        if (row >= 0) {
            m_table->viewport()->update(m_table->visualRect(m_table->model()->index(row, m_column)));
        }
    });
    m_flash.setSingleShot(true);
    m_flash.setInterval(1500);
    connect(&m_flash, &QTimer::timeout, this, [this]() { m_table->setFlashRow(QString()); });

    table->setMenuHook([this](QMenu &menu, int row) {
        const QStringList allowed = allowedFor(row);
        if (allowed.isEmpty()) return;
        menu.addSeparator();
        const QString key = m_table->rowKey(row);
        for (const core::KipChip &a : m_actions) {
            if (!allowed.contains(a.id)) continue;
            const QString icon = a.icon.isEmpty() ? QStringLiteral("play") : a.icon;
            QAction *act = menu.addAction(LucideIcons::icon(icon, QColor(a.danger ? tk::errorFg() : tk::mutedFg()), 16),
                                          a.label.isEmpty() ? a.id : a.label);
            act->setEnabled(m_enabled);
            connect(act, &QAction::triggered, this, [this, key, id = a.id]() { emit actionClicked(key, id); });
        }
    });
}

int RowActionsController::columnWidthFor(int iconCount)
{
    const int shown = qMin(iconCount, kMaxVisibleIcons);
    return shown * (kIconSize + kIconGap) + tk::space(3);
}

const core::KipChip *RowActionsController::actionNamed(const QString &id) const
{
    for (const core::KipChip &a : m_actions) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

QStringList RowActionsController::allowedFor(int row) const
{
    const QTableWidgetItem *it = m_table->item(row, m_column);
    return it ? it->data(kRowActionsRole).toStringList() : QStringList();
}

QStringList RowActionsController::visibleIcons(int row) const
{
    QStringList allowed = allowedFor(row);
    if (allowed.size() > kMaxVisibleIcons) {
        // A ação rodando continua à vista (o spinner fica nela).
        QStringList shown = allowed.mid(0, kMaxVisibleIcons - 1);
        if (m_table->rowKey(row) == m_row && !m_action.isEmpty() && !shown.contains(m_action)) {
            shown.last() = m_action;
        }
        shown << kMoreId;
        return shown;
    }
    return allowed;
}

QRect RowActionsController::iconRect(const QRect &cell, int index, int count) const
{
    const int total = count * kIconSize + (count - 1) * kIconGap;
    const int x = cell.right() - tk::space(2) - total + index * (kIconSize + kIconGap) + 1;
    return QRect(x, cell.center().y() - kIconSize / 2 + 1, kIconSize, kIconSize);
}

void RowActionsController::sync(const QString &row, const QString &action, Phase phase, bool enabled)
{
    m_enabled = enabled;
    const Phase previous = m_phase;
    m_phase = row.isEmpty() ? Phase::None : phase;
    m_row = m_phase == Phase::None ? QString() : row;
    m_action = m_phase == Phase::None ? QString() : action;

    if (m_phase == Phase::Confirming) {
        const int r = m_table->rowForKey(m_row);
        const core::KipChip *def = actionNamed(m_action);
        if (r >= 0 && def && (r != m_confirmRow || !m_confirm)) {
            hideConfirm();
            showConfirm(r, *def);
        }
    } else {
        hideConfirm();
    }

    if (m_phase == Phase::Running) {
        if (!m_spin.isActive()) m_spin.start();
        m_table->setBusyRow(m_row);
    } else {
        m_spin.stop();
        m_table->setBusyRow(QString());
    }

    if (m_phase == Phase::Success) {
        const QString key = m_row + QLatin1Char('\x1f') + m_action;
        if (previous != Phase::Success || key != m_flashKey) {
            m_flashKey = key;
            m_table->setFlashRow(m_row);
            m_flash.start();
        }
    } else {
        m_flashKey.clear();
        if (m_phase != Phase::None || previous != Phase::Success) {
            m_flash.stop();
            m_table->setFlashRow(QString());
        }
    }
    m_table->viewport()->update();
}

void RowActionsController::showConfirm(int row, const core::KipChip &action)
{
    const core::KipChipConfirm confirm = action.confirm.value_or(core::KipChipConfirm{});
    const QString label = action.label.isEmpty() ? action.id : action.label;
    const QString key = m_table->rowKey(row);
    const QColor tint = action.danger ? QColor(tk::errorFg()) : QColor(tk::warningFg());

    auto *strip = new QWidget();
    strip->setObjectName(QStringLiteral("kipRowConfirm"));
    strip->setAttribute(Qt::WA_StyledBackground, true);
    // Botões compactos para caber na linha: o QSS do app dá a eles uma altura mínima maior (mesmo raio do token).
    const int buttonHeight = qMax(26, tk::controlHeight() - 6);
    strip->setStyleSheet(QStringLiteral("QWidget#kipRowConfirm { background-color: %1; border: none; }"
                                        "QWidget#kipRowConfirm QLabel { background: transparent; border: none; }"
                                        "QWidget#kipRowConfirm QPushButton { min-height: %2px; max-height: %2px;"
                                        " padding: 0px %3px; border-radius: %4px; }")
                             .arg(blend(QColor(tk::surface()), tint, 0.14).name())
                             .arg(buttonHeight - 2)
                             .arg(tk::space(3))
                             .arg(tk::radiusMdForHeight(buttonHeight)));
    auto *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(tk::space(3), tk::space(1), tk::space(2), tk::space(1));
    layout->setSpacing(tk::space(2));
    auto *icon = new QLabel(strip);
    icon->setPixmap(LucideIcons::icon(QStringLiteral("triangle-alert"), tint, 16).pixmap(16, 16));
    layout->addWidget(icon, 0, Qt::AlignVCenter);
    const QString question = !confirm.title.isEmpty()
        ? confirm.title.toHtmlEscaped()
        : utils::tr(QStringLiteral("kip.row.confirm_question"))
              .arg(label.toHtmlEscaped(),
                   QStringLiteral("<span style=\"font-family:%1;\">%2</span>").arg(tk::monoFamily(), key.toHtmlEscaped()));
    QString html = QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(tk::fg(), question);
    if (!confirm.text.isEmpty()) {
        html += QStringLiteral(" <span style=\"color:%1;\">%2</span>").arg(tk::mutedFg(), confirm.text.toHtmlEscaped());
    }
    auto *text = new QLabel(html, strip);
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(false);
    text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    text->setToolTip(confirm.text);
    layout->addWidget(text, 1, Qt::AlignVCenter);
    auto *cancel = new QPushButton(confirm.cancelLabel.isEmpty() ? utils::tr(QStringLiteral("kip.chip.cancel"))
                                                                 : confirm.cancelLabel, strip);
    auto *run = new QPushButton(confirm.confirmLabel.isEmpty() ? utils::tr(QStringLiteral("kip.chip.run"))
                                                               : confirm.confirmLabel, strip);
    run->setProperty("kaiRole", action.danger ? QStringLiteral("danger") : QStringLiteral("primary"));
    for (QPushButton *b : {cancel, run}) {
        b->setFixedHeight(buttonHeight);
        b->setCursor(Qt::PointingHandCursor);
        b->installEventFilter(this);
        layout->addWidget(b);
    }
    connect(cancel, &QPushButton::clicked, this, &RowActionsController::cancelled);
    connect(run, &QPushButton::clicked, this, &RowActionsController::confirmed);

    m_table->setSpan(row, 0, 1, m_table->columnCount());
    m_table->setCellWidget(row, 0, strip);
    const int base = m_table->verticalHeader()->defaultSectionSize();
    const int height = qMax(base, buttonHeight + 2 * tk::space(1) + 2);
    m_table->setRowHeight(row, height);
    m_table->setExtraHeight(height - base);
    m_table->scrollToItem(m_table->item(row, 0));
    m_confirm = strip;
    m_confirmRow = row;
    // Destrutivo: o foco vai para "Manter" — Enter nunca confirma sem querer.
    QPointer<QPushButton> focusTarget = cancel;
    QTimer::singleShot(0, this, [focusTarget]() {
        if (focusTarget) focusTarget->setFocus(Qt::OtherFocusReason);
    });
}

void RowActionsController::hideConfirm()
{
    if (m_confirmRow < 0) return;
    if (m_confirmRow < m_table->rowCount()) {
        m_table->removeCellWidget(m_confirmRow, 0);
        m_table->setSpan(m_confirmRow, 0, 1, 1);
        m_table->setRowHeight(m_confirmRow, m_table->verticalHeader()->defaultSectionSize());
    }
    m_table->setExtraHeight(0);
    m_confirm = nullptr;
    m_confirmRow = -1;
}

void RowActionsController::showOverflowMenu(int row, const QPoint &globalPos)
{
    QMenu menu(m_table);
    const QString key = m_table->rowKey(row);
    const QStringList shown = visibleIcons(row);
    for (const QString &id : allowedFor(row)) {
        if (shown.contains(id)) continue;
        const core::KipChip *a = actionNamed(id);
        if (!a) continue;
        const QString icon = a->icon.isEmpty() ? QStringLiteral("play") : a->icon;
        QAction *act = menu.addAction(LucideIcons::icon(icon, QColor(a->danger ? tk::errorFg() : tk::mutedFg()), 16),
                                      a->label.isEmpty() ? a->id : a->label);
        act->setEnabled(m_enabled);
        connect(act, &QAction::triggered, this, [this, key, id]() { emit actionClicked(key, id); });
    }
    menu.exec(globalPos);
}

bool RowActionsController::eventFilter(QObject *watched, QEvent *event)
{
    // Enter num botão da faixa de confirmação aperta ESTE botão — nunca sobe até o formulário (que enviaria a tela).
    if (event->type() == QEvent::KeyPress) {
        auto *button = qobject_cast<QPushButton *>(watched);
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (button && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
            button->click();
            return true;
        }
    }
    if (watched != m_table->viewport()) {
        return QObject::eventFilter(watched, event);
    }
    const auto hit = [this](const QPoint &pos, int *rowOut) -> QString {
        const QModelIndex index = m_table->indexAt(pos);
        if (!index.isValid() || index.column() != m_column) return QString();
        const QStringList icons = visibleIcons(index.row());
        const QRect cell = m_table->visualRect(index);
        for (int i = 0; i < icons.size(); ++i) {
            if (iconRect(cell, i, icons.size()).contains(pos)) {
                *rowOut = index.row();
                return icons.at(i);
            }
        }
        return QString();
    };
    switch (event->type()) {
    case QEvent::MouseMove: {
        int row = -1;
        const QPoint pos = static_cast<QMouseEvent *>(event)->position().toPoint();
        const QString id = hit(pos, &row);
        int icon = -1;
        if (!id.isEmpty()) icon = visibleIcons(row).indexOf(id);
        if (icon != m_hoverIcon) {
            m_hoverIcon = icon;
            m_table->viewport()->setCursor(icon >= 0 && m_enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
            m_table->viewport()->update();
        }
        break;
    }
    case QEvent::ToolTip: {
        auto *help = static_cast<QHelpEvent *>(event);
        int row = -1;
        const QString id = hit(help->pos(), &row);
        if (id.isEmpty()) break;
        QString tip = utils::tr(QStringLiteral("kip.row.more_actions"));
        if (const core::KipChip *a = actionNamed(id)) {
            tip = a->label.isEmpty() ? a->id : a->label;
            if (!a->description.isEmpty()) tip += QStringLiteral("\n") + a->description;
        }
        QToolTip::showText(help->globalPos(), tip, m_table->viewport());
        return true;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) break;
        int row = -1;
        const QString id = hit(mouse->position().toPoint(), &row);
        if (id.isEmpty()) break;
        if (m_enabled && event->type() == QEvent::MouseButtonPress) {
            if (id == kMoreId) {
                showOverflowMenu(row, mouse->globalPosition().toPoint());
            } else {
                emit actionClicked(m_table->rowKey(row), id);
            }
        }
        return true; // o clique no ícone não muda a seleção da tabela
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

RowActionsDelegate::RowActionsDelegate(RowActionsController *controller)
    : QStyledItemDelegate(controller->table())
    , m_controller(controller)
{
}

void RowActionsDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text.clear();
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    DataTableWidget *table = m_controller->table();
    const int row = index.row();
    const QString key = table->rowKey(row);
    if (!key.isEmpty() && key == table->flashRow()) {
        painter->fillRect(opt.rect, withAlpha(QColor(tk::successFg()), 34));
    }
    using Phase = RowActionsController::Phase;
    const bool mine = !key.isEmpty() && key == m_controller->runRow();
    const bool running = mine && m_controller->phase() == Phase::Running;
    const bool succeeded = mine && m_controller->phase() == Phase::Success && table->flashRow() == key;
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered = row == table->hoverRow();
    if (!(running || succeeded || selected || hovered)) {
        return;
    }
    const QStringList icons = m_controller->visibleIcons(row);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < icons.size(); ++i) {
        const QString &id = icons.at(i);
        const QRect rect = m_controller->iconRect(opt.rect, i, icons.size());
        if (running && id == m_controller->runAction()) {
            const QRectF ring = QRectF(rect).adjusted(7, 7, -7, -7);
            painter->setPen(QPen(QColor(tk::borderColor()), 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(ring);
            QPen arc(QColor(tk::accent()), 2);
            arc.setCapStyle(Qt::RoundCap);
            painter->setPen(arc);
            painter->drawArc(ring, -m_controller->spinnerAngle() * 16, 100 * 16);
            continue;
        }
        if (succeeded && id == m_controller->runAction()) {
            LucideIcons::icon(QStringLiteral("check"), QColor(tk::successFg()), 16).paint(painter, rect.adjusted(5, 5, -5, -5));
            continue;
        }
        const bool iconHovered = hovered && i == m_controller->hoverIcon() && m_controller->enabled();
        bool danger = false;
        QString name = QStringLiteral("ellipsis");
        if (id != QLatin1String("__more")) {
            for (const core::KipChip &a : m_controller->actions()) {
                if (a.id == id) {
                    danger = a.danger;
                    name = a.icon.isEmpty() ? QStringLiteral("play") : a.icon;
                }
            }
        }
        if (iconHovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(tk::hoverBg()));
            painter->drawRoundedRect(rect, tk::radiusSm(), tk::radiusSm());
        }
        // A lixeira não fica vermelha em repouso: só no hover.
        QColor color(iconHovered ? (danger ? tk::errorFg() : tk::fg()) : tk::mutedFg());
        if (!m_controller->enabled()) color = withAlpha(color, 110);
        LucideIcons::icon(name, color, 16).paint(painter, rect.adjusted(5, 5, -5, -5));
    }
    painter->restore();
}

} // namespace kai::ui::fields
