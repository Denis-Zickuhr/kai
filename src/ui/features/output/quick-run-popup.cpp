#include "ui/features/output/quick-run-popup.h"

#include "ui/shared/fuzzy-search.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

constexpr int kPopupWidth = 440;
constexpr int kPaletteWidth = 560;
constexpr int kRowHeight = 30;
constexpr int kMaxRows = 9;
constexpr int kMaxResults = 200;
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kIdRole = Qt::UserRole;
constexpr int kKindRole = Qt::UserRole + 2;
constexpr int kIconRole = Qt::UserRole + 3;
constexpr int kIconSize = 16;

QString iconFor(QuickRunPopup::Kind kind)
{
    switch (kind) {
    case QuickRunPopup::Kind::Command: return QStringLiteral("square-terminal");
    case QuickRunPopup::Kind::Http: return QStringLiteral("globe");
    case QuickRunPopup::Kind::Folder: return QStringLiteral("folder");
    case QuickRunPopup::Kind::Collection: return QStringLiteral("database");
    case QuickRunPopup::Kind::Note: return QStringLiteral("notebook");
    case QuickRunPopup::Kind::Environment: return QStringLiteral("layers");
    case QuickRunPopup::Kind::Action: return QStringLiteral("zap");
    }
    return {};
}

QString labelFor(QuickRunPopup::Kind kind)
{
    switch (kind) {
    case QuickRunPopup::Kind::Command: return utils::tr(QStringLiteral("palette.filter.command"));
    case QuickRunPopup::Kind::Http: return utils::tr(QStringLiteral("palette.filter.http"));
    case QuickRunPopup::Kind::Folder: return utils::tr(QStringLiteral("palette.filter.folder"));
    case QuickRunPopup::Kind::Collection: return utils::tr(QStringLiteral("palette.filter.collection"));
    case QuickRunPopup::Kind::Note: return utils::tr(QStringLiteral("palette.filter.note"));
    case QuickRunPopup::Kind::Environment: return utils::tr(QStringLiteral("palette.filter.environment"));
    case QuickRunPopup::Kind::Action: return utils::tr(QStringLiteral("palette.filter.action"));
    }
    return {};
}

// Memória do filtro por chave de paleta (process-local): sobrevive entre aberturas da mesma paleta.
QHash<QString, QSet<int>> &filterMemory()
{
    static QHash<QString, QSet<int>> memory;
    return memory;
}

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
        QRect content = row;
        if (index.data(kIconRole).toBool()) {
            const QRect iconRect(row.left() + 10, row.top() + (row.height() - kIconSize) / 2, kIconSize, kIconSize);
            const auto kind = static_cast<QuickRunPopup::Kind>(index.data(kKindRole).toInt());
            LucideIcons::icon(iconFor(kind), QColor(selected ? tk::fg() : tk::mutedFg()), kIconSize).paint(painter, iconRect);
            content.setLeft(row.left() + kIconSize + 10);
        }
        const QString path = index.data(kPathRole).toString();
        const int pathWidth = path.isEmpty() ? 0 : std::min(metrics.horizontalAdvance(path) + 6, content.width() / 2);
        const QRect nameRect = content.adjusted(10, 0, -(pathWidth ? pathWidth + 20 : 10), 0);
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

    // Barra de chips de filtro por tipo (preenchida em setEntries conforme os tipos presentes).
    m_filterBar = new QWidget(this);
    m_filterBar->setObjectName(QStringLiteral("quickRunFilterBar"));
    auto *filterLayout = new QHBoxLayout(m_filterBar);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(4);
    m_filterBar->setVisible(false);
    layout->addWidget(m_filterBar);

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
        "#quickRunFilterChip { background-color: %1; color: %5; border: 1px solid %2; border-radius: %4px; padding: 3px 9px; }"
        "#quickRunFilterChip:checked { background-color: %6; color: %7; border: 1px solid %6; }"
        "QLabel { color: %5; background: transparent; }")
        .arg(tk::bg(), tk::borderColor(), tk::fg()).arg(tk::radiusSm())
        .arg(tk::mutedFg(), tk::accent(), tk::buttonFg()));
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
    m_mixed = std::any_of(m_entries.cbegin(), m_entries.cend(), [](const Entry &e) { return e.kind != Kind::Command; });
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.path.compare(b.path, Qt::CaseInsensitive) < 0;
    });

    // Tipos presentes (na ordem do enum), para montar os chips de filtro.
    m_availableKinds.clear();
    for (const Entry &entry : std::as_const(m_entries)) {
        if (!m_availableKinds.contains(entry.kind)) {
            m_availableKinds.append(entry.kind);
        }
    }
    std::sort(m_availableKinds.begin(), m_availableKinds.end());

    // Restaura o filtro lembrado desta paleta, descartando tipos que sumiram.
    const QSet<int> remembered = filterMemory().value(filterMemoryKey());
    QSet<int> stillValid;
    for (Kind kind : std::as_const(m_availableKinds)) {
        if (remembered.contains(static_cast<int>(kind))) {
            stillValid.insert(static_cast<int>(kind));
        }
    }
    m_activeKinds = stillValid;

    rebuildFilterChips();
    applyFilter();
}

QString QuickRunPopup::filterMemoryKey() const
{
    const QString name = objectName();
    return name.isEmpty() ? QStringLiteral("quickRun") : name;
}

void QuickRunPopup::rebuildFilterChips()
{
    auto *filterLayout = qobject_cast<QHBoxLayout *>(m_filterBar->layout());
    if (!filterLayout) {
        return;
    }
    // Limpa os chips antigos.
    QLayoutItem *item = nullptr;
    while ((item = filterLayout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    // Só vale a pena filtrar com mais de um tipo presente.
    if (m_availableKinds.size() < 2) {
        m_filterBar->setVisible(false);
        return;
    }

    for (Kind kind : std::as_const(m_availableKinds)) {
        auto *chip = new QPushButton(labelFor(kind), m_filterBar);
        chip->setObjectName(QStringLiteral("quickRunFilterChip"));
        chip->setCheckable(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFocusPolicy(Qt::NoFocus);
        chip->setIcon(LucideIcons::icon(iconFor(kind), QColor(tk::mutedFg()), 13));
        chip->setChecked(m_activeKinds.contains(static_cast<int>(kind)));
        const int kindInt = static_cast<int>(kind);
        connect(chip, &QPushButton::toggled, this, [this, kindInt](bool on) {
            if (on) {
                m_activeKinds.insert(kindInt);
            } else {
                m_activeKinds.remove(kindInt);
            }
            filterMemory()[filterMemoryKey()] = m_activeKinds;
            applyFilter();
        });
        filterLayout->addWidget(chip);
    }
    filterLayout->addStretch(1);
    m_filterBar->setVisible(true);
    refreshStyle();
}

void QuickRunPopup::applyFilter()
{
    const QString query = m_search->text().trimmed();
    m_list->clear();

    // Entradas que passam no filtro de tipo (vazio = todos).
    QVector<int> allowed;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_activeKinds.isEmpty() || m_activeKinds.contains(static_cast<int>(m_entries.at(i).kind))) {
            allowed.append(i);
        }
    }

    QVector<int> order;
    if (query.isEmpty()) {
        for (int idx : std::as_const(allowed)) {
            if (order.size() >= kMaxResults) {
                break;
            }
            order.append(idx);
        }
    } else {
        // O nome vem primeiro no texto buscado: casar o começo dele rende mais que casar só nas pastas.
        QStringList candidates;
        candidates.reserve(allowed.size());
        for (int idx : std::as_const(allowed)) {
            const Entry &entry = m_entries.at(idx);
            candidates << entry.name + QLatin1Char(' ') + entry.path;
        }
        const QVector<FuzzyMatchResult> found = FuzzyMatcher::search(query, candidates);
        for (int i = 0; i < found.size() && i < kMaxResults; ++i) {
            order.append(allowed.at(found.at(i).originalIndex));
        }
    }
    for (int index : std::as_const(order)) {
        const Entry &entry = m_entries.at(index);
        auto *item = new QListWidgetItem(entry.name, m_list);
        item->setData(kIdRole, entry.id);
        item->setData(kPathRole, entry.path);
        item->setData(kKindRole, static_cast<int>(entry.kind));
        item->setData(kIconRole, m_mixed);
        item->setToolTip(entry.path.isEmpty() ? entry.name : entry.path + QStringLiteral(" / ") + entry.name);
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    const int rows = std::min(m_list->count(), kMaxRows);
    m_list->setFixedHeight(rows * kRowHeight);
    m_list->setVisible(rows > 0);
    m_empty->setText(utils::tr(m_mixed ? QStringLiteral("palette.empty") : QStringLiteral("quick_run.empty")));
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
    const auto kind = static_cast<Kind>(item->data(kKindRole).toInt());
    hide();
    if (kind == Kind::Command || kind == Kind::Http) {
        emit commandChosen(id);
    }
    emit entryChosen(kind, id);
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

void QuickRunPopup::setPlaceholder(const QString &text)
{
    m_search->setPlaceholderText(text);
}

void QuickRunPopup::openBelow(const QPoint &anchorGlobal)
{
    setFixedWidth(kPopupWidth);
    present(anchorGlobal);
}

void QuickRunPopup::openOnScreen(const QRect &available)
{
    setFixedWidth(kPopupWidth);
    present(QPoint(available.center().x() - kPopupWidth / 2, available.top() + available.height() / 5));
}

void QuickRunPopup::openCentered(const QWidget *window)
{
    if (!window) {
        openBelow(QPoint(50, 50));
        return;
    }
    setFixedWidth(std::min(kPaletteWidth, std::max(kPopupWidth, window->width() - 40)));
    const QRect frame = window->isVisible() ? QRect(window->mapToGlobal(QPoint(0, 0)), window->size()) : window->geometry();
    present(QPoint(frame.left() + (frame.width() - width()) / 2, frame.top() + std::min(96, frame.height() / 6)));
}

void QuickRunPopup::present(const QPoint &topLeft)
{
    m_search->clear();
    applyFilter();
    adjustSize();
    QPoint pos = topLeft;
    if (const QScreen *screen = QGuiApplication::screenAt(topLeft)) {
        const QRect avail = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), avail.left(), std::max(avail.left(), avail.right() - width())));
        // Sem espaço embaixo (a barra está perto do fim da tela): abre para cima.
        if (pos.y() + height() > avail.bottom()) {
            pos.setY(std::max(avail.top(), topLeft.y() - height() - 32));
        }
    }
    move(pos);
    show();
    raise();
    m_search->setFocus();
}

} // namespace kai::ui
