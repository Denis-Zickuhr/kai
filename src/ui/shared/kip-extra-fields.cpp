#include "ui/shared/kip-extra-fields.h"

#include "core/unified-diff.h"
#include "ui/shared/data-table-widget.h"
#include "ui/shared/diff-view.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QButtonGroup>
#include <QEnterEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace kai::ui::fields {

namespace tk = kai::utils::tokens;

namespace {

constexpr int kNodeIdRole = Qt::UserRole;
constexpr int kNodeToneRole = Qt::UserRole + 1;

QColor blend(const QColor &base, const QColor &over, qreal amount)
{
    return QColor::fromRgbF(base.redF() + (over.redF() - base.redF()) * amount,
                            base.greenF() + (over.greenF() - base.greenF()) * amount,
                            base.blueF() + (over.blueF() - base.blueF()) * amount);
}

QLineEdit *makeSearchEdit(QWidget *parent)
{
    auto *search = new QLineEdit(parent);
    search->setPlaceholderText(utils::tr(QStringLiteral("params.multi_search.placeholder")));
    search->setClearButtonEnabled(true);
    search->setStyleSheet(QStringLiteral(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: %5px %6px; }")
        .arg(tk::bg(), tk::fg(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::space(2)).arg(tk::space(3)));
    search->addAction(LucideIcons::icon(QStringLiteral("search"), QColor(tk::mutedFg()), 16), QLineEdit::LeadingPosition);
    return search;
}

// Badge dos nós da árvore (mesma pílula das células `badge` das tabelas).
class TreeBadgeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        opt.text.clear();
        QStyle *style = opt.widget ? opt.widget->style() : nullptr;
        if (style) style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        if (text.isEmpty()) return;
        const QString tone = index.data(kNodeToneRole).toString();
        const QColor c = kipToneColor(tone);
        const bool neutral = tone.isEmpty() || tone == QLatin1String("neutral");
        QFont small = opt.font;
        small.setPointSize(qMax(7, tk::fontSizeSmallPt()));
        small.setWeight(QFont::DemiBold);
        const QFontMetrics fm(small);
        const int w = fm.horizontalAdvance(text) + 12;
        const int h = fm.height() + 2;
        const QRect pill(opt.rect.right() - w - 6, opt.rect.center().y() - h / 2, w, h);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        QColor bg = c;
        bg.setAlpha(40);
        painter->setBrush(neutral ? QColor(tk::surface2()) : bg);
        painter->drawRoundedRect(pill, tk::radiusSm(), tk::radiusSm());
        painter->setFont(small);
        painter->setPen(neutral ? QColor(tk::fg()) : c);
        painter->drawText(pill, Qt::AlignCenter, text);
        painter->restore();
    }
};

} // namespace

// ============================================================================================================
// ChoiceCard

ChoiceCard::ChoiceCard(const core::KipOption &option, QWidget *parent)
    : QAbstractButton(parent)
    , m_option(option)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setText(option.label.isEmpty() ? option.value : option.label);
    setToolTip(option.description);
    setAccessibleName(text());
    setAccessibleDescription(option.description);
}

QSize ChoiceCard::sizeHint() const
{
    const int pad = tk::space(3);
    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    const QFontMetrics fm(bold);
    QFont small = font();
    small.setPointSize(qMax(7, tk::fontSizeSmallPt()));
    const QFontMetrics sm(small);
    int h = pad * 2 + fm.height();
    if (!m_option.icon.isEmpty()) h += 18 + tk::space(2);
    if (!m_option.description.isEmpty()) h += tk::space(1) + 2 * sm.height();
    return QSize(170, h);
}

QSize ChoiceCard::minimumSizeHint() const
{
    return QSize(110, sizeHint().height());
}

void ChoiceCard::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ChoiceCard::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ChoiceCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent(tk::accent());
    const bool on = isChecked();
    const qreal radius = qMin<qreal>(tk::radiusMd(), height() / 2.0 - 1);
    const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
    QColor border(tk::borderColor());
    if (on) border = accent;
    else if (m_hover && isEnabled()) border = blend(QColor(tk::borderColor()), accent, 0.45);
    p.setPen(QPen(border, on ? 2.0 : 1.0));
    p.setBrush(on ? blend(QColor(tk::bg()), accent, 0.07) : QColor(tk::bg()));
    p.drawRoundedRect(box, radius, radius);
    if (hasFocus() && !on) {
        QColor ring(tk::accent());
        ring.setAlpha(150);
        p.setPen(QPen(ring, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box, radius, radius);
    }

    const int pad = tk::space(3);
    int y = pad;
    const QColor fg = isEnabled() ? QColor(tk::fg()) : QColor(tk::mutedFg());
    if (!m_option.icon.isEmpty()) {
        LucideIcons::icon(m_option.icon, on ? accent : QColor(tk::mutedFg()), 18).paint(&p, QRect(pad, y, 18, 18));
        y += 18 + tk::space(2);
    }
    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    p.setFont(bold);
    p.setPen(fg);
    const QFontMetrics fm(bold);
    const int textWidth = width() - 2 * pad - (on ? 22 : 0);
    p.drawText(QRect(pad, y, textWidth, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(text(), Qt::ElideRight, textWidth));
    y += fm.height();
    if (!m_option.description.isEmpty()) {
        QFont small = font();
        small.setPointSize(qMax(7, tk::fontSizeSmallPt()));
        p.setFont(small);
        p.setPen(QColor(tk::mutedFg()));
        const QFontMetrics sm(small);
        y += tk::space(1);
        p.drawText(QRect(pad, y, width() - 2 * pad, 2 * sm.height()), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   m_option.description);
    }
    if (on) {
        const QRectF circle(width() - pad - 18, pad, 18, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(circle);
        QPen mark(QColor(tk::buttonFg()), 2);
        mark.setCapStyle(Qt::RoundCap);
        mark.setJoinStyle(Qt::RoundJoin);
        p.setPen(mark);
        const QPointF c = circle.center();
        QPainterPath check;
        check.moveTo(c.x() - 4, c.y() + 0.5);
        check.lineTo(c.x() - 1, c.y() + 3.5);
        check.lineTo(c.x() + 4, c.y() - 2.5);
        p.drawPath(check);
    }
}

// ============================================================================================================
// ChoiceButtonsField

ChoiceButtonsField::ChoiceButtonsField(const QVector<core::KipOption> &options, bool segmented, const QString &initial,
                                       QWidget *parent)
    : QWidget(parent)
{
    setObjectName(segmented ? QStringLiteral("kipSegmentedField") : QStringLiteral("kipCardsField"));
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    if (segmented) {
        auto *outer = new QHBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto *bar = new QWidget(this);
        bar->setObjectName(QStringLiteral("kipSegmented"));
        bar->setAttribute(Qt::WA_StyledBackground, true);
        const int radius = tk::radiusMdForHeight(tk::controlHeight());
        bar->setStyleSheet(QStringLiteral(
            "QWidget#kipSegmented { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
            "QWidget#kipSegmented QToolButton { background: transparent; color: %4; border: 1px solid transparent;"
            " border-radius: %5px; padding: %6px %7px; font-weight: 500; }"
            "QWidget#kipSegmented QToolButton:hover { color: %8; }"
            "QWidget#kipSegmented QToolButton:checked { background-color: %9; color: %8; border: 1px solid %2; }"
            "QWidget#kipSegmented QToolButton:focus { border: 1px solid %10; }")
            .arg(tk::surface2(), tk::borderColor()).arg(radius)
            .arg(tk::mutedFg()).arg(qMax(0, radius - 3)).arg(tk::space(1)).arg(tk::space(3))
            .arg(tk::fg(), tk::bg(), tk::accent()));
        auto *row = new QHBoxLayout(bar);
        row->setContentsMargins(2, 2, 2, 2);
        row->setSpacing(2);
        for (const core::KipOption &o : options) {
            auto *b = new QToolButton(bar);
            b->setText(o.label.isEmpty() ? o.value : o.label);
            b->setToolTip(o.description);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::StrongFocus);
            if (!o.icon.isEmpty()) {
                b->setIcon(LucideIcons::icon(o.icon, QColor(tk::mutedFg()), 16));
                b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            }
            row->addWidget(b);
            m_buttons.append(b);
            m_values.append(o.value);
        }
        outer->addWidget(bar);
        outer->addStretch(1);
    } else {
        auto *grid = new QGridLayout(this);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(tk::space(2));
        const int columns = options.size() <= 4 ? qMax(1, int(options.size())) : 3;
        for (int i = 0; i < options.size(); ++i) {
            auto *card = new ChoiceCard(options.at(i), this);
            grid->addWidget(card, i / columns, i % columns);
            m_buttons.append(card);
            m_values.append(options.at(i).value);
        }
    }
    for (int i = 0; i < m_buttons.size(); ++i) {
        m_group->addButton(m_buttons.at(i), i);
        m_buttons.at(i)->installEventFilter(this);
    }
    setValue(initial);
    connect(m_group, &QButtonGroup::idToggled, this, [this](int, bool checked) {
        if (checked) emit valueChanged();
    });
}

QString ChoiceButtonsField::value() const
{
    const int id = m_group->checkedId();
    return id >= 0 ? m_values.value(id) : QString();
}

void ChoiceButtonsField::setValue(const QString &value)
{
    const int index = m_values.indexOf(value);
    if (index >= 0) {
        m_buttons.at(index)->setChecked(true);
    } else if (QAbstractButton *checked = m_group->checkedButton()) {
        m_group->setExclusive(false); // um grupo exclusivo não deixa desmarcar o último
        checked->setChecked(false);
        m_group->setExclusive(true);
    }
}

QAbstractButton *ChoiceButtonsField::button(const QString &value) const
{
    const int index = m_values.indexOf(value);
    return index >= 0 ? m_buttons.at(index) : nullptr;
}

QWidget *ChoiceButtonsField::focusTarget() const
{
    if (QAbstractButton *checked = m_group->checkedButton()) return checked;
    return m_buttons.isEmpty() ? nullptr : m_buttons.first();
}

bool ChoiceButtonsField::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        const int index = m_buttons.indexOf(static_cast<QAbstractButton *>(watched));
        int next = -1;
        if (key == Qt::Key_Right || key == Qt::Key_Down) next = index + 1;
        else if (key == Qt::Key_Left || key == Qt::Key_Up) next = index - 1;
        if (index >= 0 && next >= 0 && next < m_buttons.size()) {
            m_buttons.at(next)->setChecked(true);
            m_buttons.at(next)->setFocus(Qt::TabFocusReason);
            return true;
        }
        if (index >= 0 && key == Qt::Key_Space) {
            m_buttons.at(index)->setChecked(true);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ============================================================================================================
// TreeChoiceField

TreeChoiceField::TreeChoiceField(const core::KipField &field, const QJsonValue &initial, QWidget *parent)
    : QWidget(parent)
    , m_multiple(field.multiple)
{
    setObjectName(QStringLiteral("kipTreeField"));
    setStyleSheet(QStringLiteral("QWidget#kipTreeField { background: transparent; }"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(1));
    const int total = core::kipTreeNodeIds(field.nodes).size();
    if (field.searchable.value_or(total > 8)) {
        m_filter = makeSearchEdit(this);
        layout->addWidget(m_filter);
        connect(m_filter, &QLineEdit::textChanged, this, &TreeChoiceField::applyFilter);
    }
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("kipTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 64);
    m_tree->setItemDelegateForColumn(1, new TreeBadgeDelegate(m_tree));
    m_tree->setSelectionMode(m_multiple ? QAbstractItemView::NoSelection : QAbstractItemView::SingleSelection);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(false);
    // Pasta com parte dos filhos marcada: o QSS do app só desenha marcado/desmarcado; o "meio" ganha um traço.
    m_tree->setStyleSheet(QStringLiteral(
        "QTreeWidget#kipTree { background-color: %1; border: 1px solid %2; border-radius: %3px; padding: %4px; }"
        "QTreeWidget#kipTree::indicator:indeterminate { background: %5; border: 1.5px solid %5;"
        " border-radius: %6px; image: url(:/icons/lucide/minus-on.svg); }")
        .arg(tk::bg(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::space(1))
        .arg(tk::accent()).arg(qMin(4, tk::radiusSm())));
    m_applying = true;
    addNodes(nullptr, field.nodes);
    setValue(initial);
    m_applying = false;
    const int visible = qBound(4, total, 10);
    m_tree->setFixedHeight(visible * standardRowHeight() + 2 * tk::space(1) + 4);
    layout->addWidget(m_tree);
    if (m_multiple) {
        m_count = new QLabel(this);
        m_count->setAlignment(Qt::AlignRight);
        m_count->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; background: transparent;")
                                   .arg(tk::mutedFg()).arg(tk::fontSizeSmallPt()));
        layout->addWidget(m_count);
        updateCount();
    }
    connect(m_tree, &QTreeWidget::itemChanged, this, [this]() {
        if (m_applying) return;
        updateCount();
        emit valueChanged();
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (!m_applying && !m_multiple) emit valueChanged();
    });
}

void TreeChoiceField::addNodes(QTreeWidgetItem *parent, const QVector<core::KipTreeNode> &nodes)
{
    for (const core::KipTreeNode &node : nodes) {
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, node.label);
        item->setData(0, kNodeIdRole, node.id);
        item->setToolTip(0, node.id);
        const QString icon = node.icon.isEmpty() ? (node.children.isEmpty() ? QString() : QStringLiteral("folder")) : node.icon;
        if (!icon.isEmpty()) item->setIcon(0, LucideIcons::icon(icon, QColor(tk::mutedFg()), 16));
        item->setText(1, node.badge);
        item->setData(1, kNodeToneRole, node.badgeTone);
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (m_multiple) {
            flags |= Qt::ItemIsUserCheckable;
            if (!node.children.isEmpty()) flags |= Qt::ItemIsAutoTristate;
            item->setCheckState(0, Qt::Unchecked);
        }
        item->setFlags(flags);
        addNodes(item, node.children);
        item->setExpanded(node.expanded);
    }
}

namespace {
void forEachItem(QTreeWidgetItem *item, const std::function<void(QTreeWidgetItem *)> &fn)
{
    fn(item);
    for (int i = 0; i < item->childCount(); ++i) forEachItem(item->child(i), fn);
}
} // namespace

QJsonValue TreeChoiceField::value() const
{
    if (!m_multiple) {
        const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();
        return selected.isEmpty() ? QString() : selected.first()->data(0, kNodeIdRole).toString();
    }
    QJsonArray ids;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        forEachItem(m_tree->topLevelItem(i), [&ids](QTreeWidgetItem *item) {
            if (item->checkState(0) == Qt::Checked) ids.append(item->data(0, kNodeIdRole).toString());
        });
    }
    return ids;
}

void TreeChoiceField::setValue(const QJsonValue &value)
{
    const bool was = m_applying;
    m_applying = true;
    QStringList wanted;
    if (value.isArray()) {
        for (const QJsonValue &v : value.toArray()) wanted << v.toString();
    } else if (value.isString() && !value.toString().isEmpty()) {
        wanted << value.toString();
    }
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        forEachItem(m_tree->topLevelItem(i), [&](QTreeWidgetItem *item) {
            const bool on = wanted.contains(item->data(0, kNodeIdRole).toString());
            if (m_multiple) {
                if (item->childCount() == 0) item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
                else if (on) item->setCheckState(0, Qt::Checked);
            } else {
                item->setSelected(on);
            }
            if (on) {
                for (QTreeWidgetItem *p = item->parent(); p; p = p->parent()) p->setExpanded(true);
            }
        });
    }
    m_applying = was;
    updateCount();
}

void TreeChoiceField::applyFilter(const QString &text)
{
    const QString needle = text.trimmed().toLower();
    std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) -> bool {
        bool childMatch = false;
        for (int i = 0; i < item->childCount(); ++i) childMatch = visit(item->child(i)) || childMatch;
        const bool self = needle.isEmpty() || item->text(0).toLower().contains(needle)
            || item->data(0, kNodeIdRole).toString().toLower().contains(needle);
        item->setHidden(!(self || childMatch));
        if (!needle.isEmpty() && childMatch) item->setExpanded(true);
        return self || childMatch;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) visit(m_tree->topLevelItem(i));
}

void TreeChoiceField::updateCount()
{
    if (!m_count) return;
    int leaves = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        forEachItem(m_tree->topLevelItem(i), [&leaves](QTreeWidgetItem *item) {
            if (item->childCount() == 0 && item->checkState(0) == Qt::Checked) ++leaves;
        });
    }
    m_count->setText(utils::tr(QStringLiteral("kip.tree.selected")).arg(leaves));
}

// ============================================================================================================
// DiffChoiceField

DiffChoiceField::DiffChoiceField(const core::KipField &field, const QJsonValue &initial, QWidget *parent)
    : QWidget(parent)
    , m_files(field.select == QLatin1String("files"))
{
    setObjectName(QStringLiteral("kipDiffField"));
    setStyleSheet(QStringLiteral("QWidget#kipDiffField { background: transparent; }"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(1));
    m_view = new DiffView(this);
    m_view->setSelectionMode(m_files ? DiffView::Selection::Files : DiffView::Selection::Hunks);
    m_view->setCollapseAfter(0); // no campo, começa tudo recolhido (uma linha por arquivo/trecho)
    m_view->setFiles(core::parseUnifiedDiff(field.patch));
    if (m_files) {
        for (int i = 0; i < m_view->files().size(); ++i) m_view->setFileExpanded(i, false);
    } else {
        for (int i = 0; i < m_view->files().size(); ++i) m_view->setFileExpanded(i, true);
    }
    m_view->setMaxVisibleHeight(420);
    layout->addWidget(m_view);
    m_summary = new QLabel(this);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::mutedFg()));
    layout->addWidget(m_summary);
    if (field.truncated) {
        auto *warn = new QLabel(utils::tr(QStringLiteral("kip.diff.truncated")), this);
        warn->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::warningFg()));
        layout->addWidget(warn);
    }
    setValue(initial);
    connect(m_view, &DiffView::selectionChanged, this, [this]() {
        updateSummary();
        emit valueChanged();
    });
}

QJsonValue DiffChoiceField::value() const
{
    return QJsonArray::fromStringList(m_view->selectedIds());
}

void DiffChoiceField::setValue(const QJsonValue &value)
{
    QStringList ids;
    for (const QJsonValue &v : value.toArray()) ids << v.toString();
    m_view->setSelectedIds(ids);
    updateSummary();
}

void DiffChoiceField::setInteractive(bool interactive)
{
    m_view->setInteractive(interactive);
}

void DiffChoiceField::updateSummary()
{
    const int selected = m_view->selectedIds().size();
    const int total = m_view->allIds().size();
    const QString count = utils::tr(m_files ? QStringLiteral("kip.summary.diff_files")
                                            : QStringLiteral("kip.summary.diff_hunks"))
                              .arg(selected).arg(total);
    m_summary->setText(QStringLiteral("%1 &nbsp;·&nbsp; <span style=\"color:%2;\">+%3</span> "
                                      "<span style=\"color:%4;\">−%5</span>")
                           .arg(count.toHtmlEscaped(), tk::successFg())
                           .arg(m_view->selectedAdditions())
                           .arg(tk::errorFg())
                           .arg(m_view->selectedDeletions()));
}

} // namespace kai::ui::fields
