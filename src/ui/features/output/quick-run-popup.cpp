#include "ui/features/output/quick-run-popup.h"

#include "ui/shared/fuzzy-search.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

constexpr int kPopupWidth = 440;
constexpr int kRowHeight = 30;
constexpr int kMaxRows = 9;
constexpr int kMaxResults = 200;
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kIdRole = Qt::UserRole;

// Nome à esquerda, pastas em cinza à direita; o realce da linha usa o raio do tema.
class EntryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QRect row = option.rect.adjusted(4, 1, -4, -1);
        if (selected || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? tk::selBg() : tk::hoverBg()));
            painter->drawRoundedRect(row, tk::radiusSm(), tk::radiusSm());
        }
        painter->setFont(option.font); // as medidas e o desenho usam a mesma fonte (senão o texto é cortado à toa)
        const QFontMetrics metrics(option.font);
        const QString path = index.data(kPathRole).toString();
        const int pathWidth = path.isEmpty() ? 0 : std::min(metrics.horizontalAdvance(path) + 6, row.width() / 2);
        const QRect nameRect = row.adjusted(10, 0, -(pathWidth ? pathWidth + 20 : 10), 0);
        painter->setPen(QColor(tk::fg()));
        painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                          metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, nameRect.width()));
        if (pathWidth) {
            painter->setPen(QColor(tk::mutedFg()));
            painter->drawText(QRect(row.right() - pathWidth - 10, row.top(), pathWidth, row.height()),
                              Qt::AlignVCenter | Qt::AlignRight, metrics.elidedText(path, Qt::ElideLeft, pathWidth));
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(0, kRowHeight); }
};

} // namespace

QuickRunPopup::QuickRunPopup(QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("quickRunPopup"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedWidth(kPopupWidth);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("quickRunSearch"));
    m_search->setPlaceholderText(utils::tr(QStringLiteral("quick_run.placeholder")));
    m_search->setClearButtonEnabled(false);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("quickRunList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new EntryDelegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_list);

    m_empty = new QLabel(utils::tr(QStringLiteral("quick_run.empty")), this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setMinimumHeight(kRowHeight);
    layout->addWidget(m_empty);

    connect(m_search, &QLineEdit::textChanged, this, &QuickRunPopup::applyFilter);
    connect(m_search, &QLineEdit::returnPressed, this, &QuickRunPopup::chooseCurrent);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { chooseCurrent(); });
    refreshStyle();
}

void QuickRunPopup::refreshStyle()
{
    setStyleSheet(QStringLiteral(
        "#quickRunSearch { background-color: %1; color: %3; border: 1px solid %2; border-radius: %4px; padding: 6px 10px; }"
        "#quickRunList { background: transparent; border: none; outline: none; }"
        "QLabel { color: %5; background: transparent; }")
        .arg(tk::bg(), tk::borderColor(), tk::fg()).arg(tk::radiusSm()).arg(tk::mutedFg()));
    update();
}

QStringList QuickRunPopup::visibleIds() const
{
    QStringList ids;
    for (int i = 0; i < m_list->count(); ++i) {
        ids << m_list->item(i)->data(kIdRole).toString();
    }
    return ids;
}

void QuickRunPopup::setEntries(const QVector<Entry> &entries)
{
    m_entries = entries;
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.path.compare(b.path, Qt::CaseInsensitive) < 0;
    });
    applyFilter();
}

void QuickRunPopup::applyFilter()
{
    const QString query = m_search->text().trimmed();
    m_list->clear();
    QVector<int> order;
    if (query.isEmpty()) {
        for (int i = 0; i < m_entries.size() && i < kMaxResults; ++i) {
            order.append(i);
        }
    } else {
        // O nome vem primeiro no texto buscado: casar o começo dele rende mais que casar só nas pastas.
        QStringList candidates;
        for (const Entry &entry : std::as_const(m_entries)) {
            candidates << entry.name + QLatin1Char(' ') + entry.path;
        }
        const QVector<FuzzyMatchResult> found = FuzzyMatcher::search(query, candidates);
        for (int i = 0; i < found.size() && i < kMaxResults; ++i) {
            order.append(found.at(i).originalIndex);
        }
    }
    for (int index : std::as_const(order)) {
        const Entry &entry = m_entries.at(index);
        auto *item = new QListWidgetItem(entry.name, m_list);
        item->setData(kIdRole, entry.id);
        item->setData(kPathRole, entry.path);
        item->setToolTip(entry.path.isEmpty() ? entry.name : entry.path + QStringLiteral(" / ") + entry.name);
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    const int rows = std::min(m_list->count(), kMaxRows);
    m_list->setFixedHeight(rows * kRowHeight);
    m_list->setVisible(rows > 0);
    m_empty->setVisible(rows == 0);
    adjustSize();
}

void QuickRunPopup::chooseCurrent()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item) {
        return;
    }
    const QString id = item->data(kIdRole).toString();
    hide();
    emit commandChosen(id);
}

bool QuickRunPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const int count = m_list->count();
        if (count > 0 && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
            const int step = key->key() == Qt::Key_Down ? 1 : -1;
            m_list->setCurrentRow((m_list->currentRow() + step + count) % count);
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void QuickRunPopup::paintEvent(QPaintEvent *)
{
    // Pintado aqui (e não pelo stylesheet) porque a janela é translúcida: o fundo e a borda seguem o raio do tema.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor(tk::borderColor()));
    painter.setBrush(QColor(tk::surface2()));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), tk::radiusMd(), tk::radiusMd());
}

void QuickRunPopup::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    m_search->setFocus();
}

void QuickRunPopup::openBelow(const QPoint &anchorGlobal)
{
    m_search->clear();
    applyFilter();
    adjustSize();
    QPoint pos = anchorGlobal;
    if (const QScreen *screen = QGuiApplication::screenAt(anchorGlobal)) {
        const QRect avail = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), avail.left(), std::max(avail.left(), avail.right() - width())));
        // Sem espaço embaixo (a barra está perto do fim da tela): abre para cima.
        if (pos.y() + height() > avail.bottom()) {
            pos.setY(std::max(avail.top(), anchorGlobal.y() - height() - 32));
        }
    }
    move(pos);
    show();
    raise();
    m_search->setFocus();
}

} // namespace kai::ui
