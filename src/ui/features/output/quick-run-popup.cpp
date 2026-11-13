#include "ui/features/output/quick-run-popup.h"

#include "ui/shared/fuzzy-search.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QHash>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

constexpr int kPopupWidth = 440;
constexpr int kPaletteWidth = 620;
constexpr int kRowHeight = 36;
constexpr int kListPad = 6;       // respiro ao redor das linhas (padding da lista)
constexpr int kChipSide = 14;     // margem lateral da barra de chips (alinha com o texto da busca)
constexpr int kChipH = 26;
constexpr int kChipIconSize = 14;
constexpr int kChipGapX = 6;
constexpr int kChipGapY = 6;
constexpr int kChipBarPadY = 10;
constexpr int kActionBox = 24;     // caixa de cada ação inline
constexpr int kActionGap = 2;
constexpr int kActionIconSize = 14;
constexpr int kMaxRows = 9;
constexpr int kMaxResults = 200;
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kIdRole = Qt::UserRole;
constexpr int kKindRole = Qt::UserRole + 2;
constexpr int kIconRole = Qt::UserRole + 3;
constexpr int kChipTextRole = Qt::UserRole + 4;
constexpr int kChipColorRole = Qt::UserRole + 5;
constexpr int kChipIconRole = Qt::UserRole + 6;
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

struct ActionDef {
    const char *id;
    const char *icon;
    const char *labelKey;
};

// Ações inline por tipo. A padrão (Enter) não entra aqui: é a do próprio tipo (executar, abrir, ativar...).
QVector<ActionDef> actionsFor(QuickRunPopup::Kind kind)
{
    using K = QuickRunPopup::Kind;
    static const ActionDef edit{"edit", "pencil", "palette.action.edit"};
    static const ActionDef reveal{"reveal", "folder-search", "palette.action.reveal"};
    switch (kind) {
    case K::Command:
    case K::Http:
    case K::Folder:
    case K::Collection:
    case K::Note:
        return {edit, reveal};
    case K::Environment:
        return {edit};
    case K::Action:
        break;
    }
    return {};
}

// Caixas das ações, alinhadas à direita da linha (a primeira é a mais à esquerda).
QVector<QRect> actionRects(const QRect &row, int count)
{
    QVector<QRect> rects(count);
    int right = row.right() - 6;
    for (int i = count - 1; i >= 0; --i) {
        rects[i] = QRect(right - kActionBox + 1, row.top() + (row.height() - kActionBox) / 2, kActionBox, kActionBox);
        right -= kActionBox + kActionGap;
    }
    return rects;
}

// rgba() para o stylesheet (cor do tema com transparência, ex: fundo do chip ligado).
QString rgba(const QString &color, int alpha)
{
    const QColor c(color);
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// Ícone do tipo + nome à esquerda, contexto (pastas/atalho) em cinza à direita. A linha ativa ganha o fundo de seleção
// com o raio do tema e o ícone na cor de destaque.
class EntryDelegate : public QStyledItemDelegate {
public:
    EntryDelegate(const QuickRunActionState *state, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_state(state)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QRect row = option.rect.adjusted(0, 1, 0, -1);
        if (selected || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? tk::selBg() : tk::hoverBg()));
            painter->drawRoundedRect(row, tk::radiusSm(), tk::radiusSm());
        }
        painter->setFont(option.font);
        const QFontMetrics metrics(option.font);
        constexpr int kPadX = 10;
        int left = row.left() + kPadX;
        if (index.data(kIconRole).toBool()) {
            const QRect iconRect(left, row.top() + (row.height() - kIconSize) / 2, kIconSize, kIconSize);
            const auto entryKind = static_cast<QuickRunPopup::Kind>(index.data(kKindRole).toInt());
            LucideIcons::icon(iconFor(entryKind), QColor(selected ? tk::accent() : tk::mutedFg()), kIconSize)
                .paint(painter, iconRect);
            left = iconRect.right() + 1 + 10;
        }
        const QString chipText = index.data(kChipTextRole).toString();
        const QString chipIconName = index.data(kChipIconRole).toString();
        if (!chipText.isEmpty() || !chipIconName.isEmpty()) {
            // Mesmo chip da árvore: pílula tingida com o texto em maiúsculas, ou só o logo da linguagem.
            QColor chipColor = index.data(kChipColorRole).value<QColor>();
            if (!chipColor.isValid()) {
                chipColor = QColor(tk::mutedFg());
            }
            QFont chipFont = option.font;
            chipFont.setPointSizeF(std::max(6.0, option.font.pointSizeF() - 1.0));
            chipFont.setBold(true);
            const QFontMetrics chipMetrics(chipFont);
            const bool iconChip = !chipIconName.isEmpty();
            const QString text = chipText.toUpper();
            const int chipW = (iconChip ? 14 : chipMetrics.horizontalAdvance(text)) + 12;
            const int chipH = chipMetrics.height() + 2;
            const QRect chipRect(left, row.top() + (row.height() - chipH) / 2, chipW, chipH);
            QColor fill = chipColor;
            fill.setAlpha(42);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(chipRect, tk::radiusSm(), tk::radiusSm());
            if (iconChip) {
                LucideIcons::icon(chipIconName, chipColor, 14)
                    .paint(painter, QRect(chipRect.left() + (chipW - 14) / 2, chipRect.top() + (chipH - 14) / 2, 14, 14));
            } else {
                painter->setFont(chipFont);
                painter->setPen(chipColor);
                painter->drawText(chipRect, Qt::AlignCenter, text);
                painter->setFont(option.font);
            }
            left = chipRect.right() + 1 + 8;
        }
        const auto kind = static_cast<QuickRunPopup::Kind>(index.data(kKindRole).toInt());
        int right = row.right() - kPadX;
        if (m_state && m_state->enabled && (selected || hovered)) {
            const QVector<ActionDef> actions = actionsFor(kind);
            const QVector<QRect> boxes = actionRects(row, int(actions.size()));
            for (int i = 0; i < boxes.size(); ++i) {
                const bool hot = m_state->hoverRow == index.row() && m_state->hoverIndex == i;
                const bool active = selected && m_state->activeIndex == i;
                if (hot || active) {
                    QColor fill(tk::accent());
                    fill.setAlpha(active ? 60 : 36);
                    painter->setPen(active ? QPen(QColor(tk::accent()), 1) : QPen(Qt::NoPen));
                    painter->setBrush(fill);
                    painter->drawRoundedRect(QRectF(boxes.at(i)).adjusted(0.5, 0.5, -0.5, -0.5), tk::radiusSm(), tk::radiusSm());
                }
                const QRect iconRect(boxes.at(i).left() + (kActionBox - kActionIconSize) / 2,
                                     boxes.at(i).top() + (kActionBox - kActionIconSize) / 2, kActionIconSize, kActionIconSize);
                LucideIcons::icon(QString::fromLatin1(actions.at(i).icon),
                                  QColor(hot || active ? tk::accent() : tk::mutedFg()), kActionIconSize)
                    .paint(painter, iconRect);
            }
            if (!boxes.isEmpty()) {
                right = boxes.first().left() - 8;
            }
        }
        const QString path = index.data(kPathRole).toString();
        // Folga na medida: com a largura exata o elidedText corta o começo ("...rl+F" em vez de "Ctrl+F").
        const int pathWidth = path.isEmpty() ? 0 : std::min(metrics.horizontalAdvance(path) + 10, (right - left) / 2);
        const QRect nameRect(left, row.top(), right - left - (pathWidth ? pathWidth + 14 : 0), row.height());
        painter->setPen(QColor(tk::fg()));
        painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                          metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, nameRect.width()));
        if (pathWidth) {
            painter->setPen(QColor(tk::mutedFg()));
            painter->drawText(QRect(right - pathWidth, row.top(), pathWidth, row.height()),
                              Qt::AlignVCenter | Qt::AlignRight, metrics.elidedText(path, Qt::ElideLeft, pathWidth));
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(0, kRowHeight); }

private:
    const QuickRunActionState *m_state = nullptr;
};

} // namespace

QuickRunPopup::QuickRunPopup(QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("quickRunPopup"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedWidth(kPopupWidth);

    // Busca no topo, de borda a borda (com a linha divisória embaixo), chips de tipo e a lista com respiro.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, kListPad);
    layout->setSpacing(0);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("quickRunSearch"));
    m_search->setPlaceholderText(utils::tr(QStringLiteral("quick_run.placeholder")));
    m_search->setClearButtonEnabled(false);
    m_search->addAction(LucideIcons::icon(QStringLiteral("search"), QColor(tk::mutedFg()), 16),
                        QLineEdit::LeadingPosition);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    // Barra de chips de filtro por tipo: posicionados à mão em layoutChips() (quebram de linha com altura exata).
    m_filterBar = new QWidget(this);
    m_filterBar->setObjectName(QStringLiteral("quickRunFilterBar"));
    m_filterBar->setAttribute(Qt::WA_StyledBackground, true);
    m_filterBar->setVisible(false);
    layout->addWidget(m_filterBar);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("quickRunList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new EntryDelegate(&m_actionState, m_list));
    m_list->setMouseTracking(true);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->viewport()->setMouseTracking(true);
    m_list->viewport()->installEventFilter(this);
    // O contêiner da barra é um QWidget comum e o QSS global o pinta de "bg": virava uma faixa escura sobre o popup.
    if (QWidget *barHost = m_list->verticalScrollBar()->parentWidget(); barHost && barHost != m_list) {
        barHost->setObjectName(QStringLiteral("quickRunScrollHost"));
    }
    layout->addWidget(m_list);

    m_empty = new QLabel(utils::tr(QStringLiteral("quick_run.empty")), this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setMinimumHeight(kRowHeight * 2);
    layout->addWidget(m_empty);

    connect(m_search, &QLineEdit::textChanged, this, &QuickRunPopup::applyFilter);
    connect(m_search, &QLineEdit::returnPressed, this, &QuickRunPopup::chooseCurrent);
    connect(m_list, &QListWidget::currentRowChanged, this, [this]() {
        m_actionState.activeIndex = -1;
        m_list->viewport()->update();
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { chooseCurrent(); });
    refreshStyle();
}

void QuickRunPopup::refreshStyle()
{
    // %1 borda · %2 texto · %3 raio · %4 apagado · %5 destaque · %6 destaque translúcido · %7 hover · %8..%10 tamanhos
    setStyleSheet(QStringLiteral(
        "#quickRunSearch { background-color: %18; color: %2; border: none; border-bottom: 1px solid %1;"
        " border-radius: 0; border-top-left-radius: %17px; border-top-right-radius: %17px; padding: 14px 16px; font-size: %8pt; }"
        "#quickRunFilterBar { background: transparent; border: none; border-bottom: 1px solid %1; }"
        "#quickRunList { background: transparent; border: none; outline: none; padding: %9px; }"
        "#quickRunFilterChip { background-color: %18; color: %4; border: 1px solid %1; border-radius: %3px;"
        " padding: 0 10px; font-size: %10pt; }"
        "#quickRunFilterChip:hover { background-color: %7; color: %2; }"
        "#quickRunFilterChip:checked { background-color: %6; color: %2; border: 1px solid %5; }"
        // Barra igual à do resto do app (alça em pílula pelo raio da preferência), recuada do arco dos cantos do popup.
        "#quickRunScrollHost { background: transparent; }"
        "#quickRunList QScrollBar:vertical { background: transparent; width: %12px; margin: %11px 4px %11px 6px; }"
        "#quickRunList QScrollBar::handle:vertical { background: %13; border-radius: %14px; min-height: %15px; }"
        "#quickRunList QScrollBar::handle:vertical:hover { background: %16; }"
        "#quickRunList QScrollBar::add-line:vertical, #quickRunList QScrollBar::sub-line:vertical { height: 0; width: 0; }"
        "#quickRunList QScrollBar::add-page:vertical, #quickRunList QScrollBar::sub-page:vertical { background: transparent; }"
        "QLabel { color: %4; background: transparent; }")
        .arg(tk::borderColor(), tk::fg()).arg(tk::radiusSm())
        .arg(tk::mutedFg(), tk::accent(), rgba(tk::accent(), 40), tk::hoverBg())
        .arg(tk::fontSizePt() + 1).arg(kListPad).arg(std::max(7, tk::fontSizePt() - 1))
        .arg(std::max(3, tk::radiusLg() / 2 + 1)) // a barra fica dentro do arco dos cantos do popup
        .arg(tk::space(3) + 10) // alça de space(3) + 6px de respiro à esquerda e 4px à direita (margens do QSS comem a largura)
        .arg(rgba(tk::mutedFg(), 110)).arg(tk::space(3) / 2).arg(tk::space(6))
        .arg(rgba(tk::mutedFg(), 190))
        .arg(std::max(0, tk::radiusLg() - 1)).arg(tk::surface()));
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
    for (QPushButton *chip : std::as_const(m_chips)) {
        chip->deleteLater();
    }
    m_chips.clear();

    // Só vale a pena filtrar com mais de um tipo presente.
    if (m_availableKinds.size() < 2) {
        m_filterBar->setVisible(false);
        return;
    }

    // Um chip por vez, como na tela de armazenamento: "Todos" (sem filtro) vem primeiro e vale quando nenhum tipo está ligado.
    constexpr int kAllChip = -1;
    QVector<int> kinds{kAllChip};
    for (Kind kind : std::as_const(m_availableKinds)) {
        kinds.append(static_cast<int>(kind));
    }
    for (int kindInt : std::as_const(kinds)) {
        const QString label = kindInt == kAllChip ? utils::tr(QStringLiteral("palette.filter.all")) : labelFor(static_cast<Kind>(kindInt));
        auto *chip = new QPushButton(label, m_filterBar);
        chip->setObjectName(QStringLiteral("quickRunFilterChip"));
        chip->setCheckable(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFocusPolicy(Qt::NoFocus);
        chip->setProperty("kind", kindInt);
        chip->setChecked(kindInt == kAllChip ? m_activeKinds.isEmpty() : m_activeKinds.contains(kindInt));
        if (kindInt != kAllChip) {
            chip->setIconSize(QSize(kChipIconSize, kChipIconSize));
            // O ícone acompanha o estado: cor de destaque quando ligado, apagado quando desligado.
            const QString iconName = iconFor(static_cast<Kind>(kindInt));
            auto paintIcon = [chip, iconName](bool on) {
                chip->setIcon(LucideIcons::icon(iconName, QColor(on ? tk::accent() : tk::mutedFg()), kChipIconSize));
            };
            paintIcon(chip->isChecked());
            connect(chip, &QPushButton::toggled, chip, paintIcon);
        }
        connect(chip, &QPushButton::clicked, this, [this, kindInt]() {
            m_activeKinds.clear();
            if (kindInt != kAllChip) {
                m_activeKinds.insert(kindInt);
            }
            for (QPushButton *other : std::as_const(m_chips)) {
                const int otherKind = other->property("kind").toInt();
                other->setChecked(otherKind == kAllChip ? m_activeKinds.isEmpty() : m_activeKinds.contains(otherKind));
            }
            filterMemory()[filterMemoryKey()] = m_activeKinds;
            applyFilter();
        });
        m_chips.append(chip);
    }
    m_filterBar->setVisible(true);
    refreshStyle();
    layoutChips();
    for (QPushButton *chip : std::as_const(m_chips)) {
        chip->show();
    }
}

// Tamanho de cada chip = ícone + nome + respiro (medido com a fonte do stylesheet) e quebra de linha pela largura atual;
// a altura da barra é exata, sem depender de heightForWidth.
void QuickRunPopup::layoutChips()
{
    if (m_chips.isEmpty()) {
        return;
    }
    QFont font = m_filterBar->font();
    font.setPointSize(std::max(7, tk::fontSizePt() - 1));
    const QFontMetrics metrics(font);
    const int inner = width() - 2 * kChipSide;
    int x = kChipSide;
    int y = kChipBarPadY;
    int rows = 1;
    for (QPushButton *chip : std::as_const(m_chips)) {
        const bool hasIcon = !chip->icon().isNull();
        const int w = 2 + 20 + (hasIcon ? kChipIconSize + 6 : 0) + metrics.horizontalAdvance(chip->text()) + 8; // +8: folga contra corte do texto
        chip->setFixedSize(w, kChipH);
        if (x > kChipSide && x - kChipSide + w > inner) {
            x = kChipSide;
            y += kChipH + kChipGapY;
            ++rows;
        }
        chip->move(x, y);
        x += w + kChipGapX;
    }
    m_filterBar->setFixedHeight(2 * kChipBarPadY + rows * kChipH + (rows - 1) * kChipGapY);
}

void QuickRunPopup::applyFilter()
{
    const QString query = m_search->text().trimmed();
    m_list->clear();
    m_actionState.activeIndex = -1;
    m_actionState.hoverRow = -1;
    m_actionState.hoverIndex = -1;

    // Entradas que passam no filtro de tipo (vazio = todos).
    QVector<int> allowed;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_activeKinds.isEmpty() || m_activeKinds.contains(static_cast<int>(m_entries.at(i).kind))) {
            allowed.append(i);
        }
    }

    QVector<int> order;
    if (query.isEmpty()) {
        // Os mais usados primeiro; estável, então o resto segue por tipo e nome.
        std::stable_sort(allowed.begin(), allowed.end(),
                         [this](int a, int b) { return m_entries.at(a).score > m_entries.at(b).score; });
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
        QVector<FuzzyMatchResult> found = FuzzyMatcher::search(query, candidates);
        // O uso desempata (e empurra um pouco): um casamento claramente melhor continua ganhando.
        for (FuzzyMatchResult &match : found) {
            match.score += int(std::min(m_entries.at(allowed.at(match.originalIndex)).score, 6.0) * 4);
        }
        std::stable_sort(found.begin(), found.end(),
                         [](const FuzzyMatchResult &a, const FuzzyMatchResult &b) { return a.score > b.score; });
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
        item->setData(kChipTextRole, entry.chipText);
        item->setData(kChipColorRole, entry.chipColor);
        item->setData(kChipIconRole, entry.chipIcon);
        item->setToolTip(entry.path.isEmpty() ? entry.name : entry.path + QStringLiteral(" / ") + entry.name);
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    const int rows = std::min(m_list->count(), kMaxRows);
    m_list->setFixedHeight(rows * kRowHeight + 2 * kListPad);
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
    if (watched == m_list->viewport()) {
        if (m_actionState.enabled && handleViewportEvent(event)) {
            return true;
        }
    } else if (watched == m_search && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const int count = m_list->count();
        if (count > 0 && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
            const int step = key->key() == Qt::Key_Down ? 1 : -1;
            m_list->setCurrentRow((m_list->currentRow() + step + count) % count);
            return true;
        }
        if (m_actionState.enabled && count > 0) {
            // Tab percorre as ações da linha (nenhuma -> 1ª -> ... -> nenhuma); Enter executa a marcada; Ctrl+E edita.
            if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
                cycleAction(key->key() == Qt::Key_Tab ? 1 : -1);
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && m_actionState.activeIndex >= 0) {
                triggerAction(m_list->currentItem(), m_actionState.activeIndex);
                return true;
            }
            if (key->key() == Qt::Key_E && key->modifiers() == Qt::ControlModifier) {
                triggerCurrentAction(QStringLiteral("edit"));
                return true;
            }
        }
    }
    return QFrame::eventFilter(watched, event);
}

void QuickRunPopup::setInlineActionsEnabled(bool enabled)
{
    m_actionState.enabled = enabled;
    m_list->viewport()->update();
}

bool QuickRunPopup::actionAt(const QPoint &viewportPos, QListWidgetItem **item, int *index) const
{
    QListWidgetItem *hit = m_list->itemAt(viewportPos);
    if (!hit) {
        return false;
    }
    const auto kind = static_cast<Kind>(hit->data(kKindRole).toInt());
    const QVector<ActionDef> actions = actionsFor(kind);
    const QVector<QRect> boxes = actionRects(m_list->visualItemRect(hit).adjusted(0, 1, 0, -1), int(actions.size()));
    for (int i = 0; i < boxes.size(); ++i) {
        if (boxes.at(i).contains(viewportPos)) {
            *item = hit;
            *index = i;
            return true;
        }
    }
    return false;
}

// Mouse sobre os ícones da linha: realce, dica com o nome e clique (consumido, para não virar a ação padrão).
bool QuickRunPopup::handleViewportEvent(QEvent *event)
{
    QWidget *viewport = m_list->viewport();
    auto setHover = [&](int row, int idx) {
        if (m_actionState.hoverRow != row || m_actionState.hoverIndex != idx) {
            m_actionState.hoverRow = row;
            m_actionState.hoverIndex = idx;
            viewport->update();
        }
        viewport->setCursor(idx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    };
    QListWidgetItem *item = nullptr;
    int index = -1;
    switch (event->type()) {
    case QEvent::MouseMove: {
        const QPoint pos = static_cast<QMouseEvent *>(event)->position().toPoint();
        if (actionAt(pos, &item, &index)) {
            setHover(m_list->row(item), index);
        } else {
            setHover(-1, -1);
        }
        return false;
    }
    case QEvent::Leave:
        setHover(-1, -1);
        return false;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || !actionAt(mouse->position().toPoint(), &item, &index)) {
            return false;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            triggerAction(item, index);
        }
        return true;
    }
    case QEvent::ToolTip: {
        const auto *help = static_cast<QHelpEvent *>(event);
        if (!actionAt(help->pos(), &item, &index)) {
            return false;
        }
        const auto kind = static_cast<Kind>(item->data(kKindRole).toInt());
        QToolTip::showText(help->globalPos(), utils::tr(QString::fromLatin1(actionsFor(kind).at(index).labelKey)), viewport);
        return true;
    }
    default:
        return false;
    }
}

void QuickRunPopup::cycleAction(int step)
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item) {
        return;
    }
    const int count = int(actionsFor(static_cast<Kind>(item->data(kKindRole).toInt())).size());
    if (count == 0) {
        return;
    }
    // Posições: -1 (nenhuma, Enter = ação padrão) e 0..count-1.
    int next = m_actionState.activeIndex + step;
    if (next >= count) {
        next = -1;
    } else if (next < -1) {
        next = count - 1;
    }
    m_actionState.activeIndex = next;
    m_list->viewport()->update();
}

void QuickRunPopup::triggerAction(QListWidgetItem *item, int index)
{
    if (!item) {
        return;
    }
    const auto kind = static_cast<Kind>(item->data(kKindRole).toInt());
    const QVector<ActionDef> actions = actionsFor(kind);
    if (index < 0 || index >= actions.size()) {
        return;
    }
    const QString id = item->data(kIdRole).toString();
    const QString actionId = QString::fromLatin1(actions.at(index).id);
    hide();
    emit entryActionRequested(kind, id, actionId);
}

bool QuickRunPopup::triggerCurrentAction(const QString &actionId)
{
    QListWidgetItem *item = m_list->currentItem();
    if (!m_actionState.enabled || !item) {
        return false;
    }
    const QVector<ActionDef> actions = actionsFor(static_cast<Kind>(item->data(kKindRole).toInt()));
    for (int i = 0; i < actions.size(); ++i) {
        if (actionId == QLatin1String(actions.at(i).id)) {
            triggerAction(item, i);
            return true;
        }
    }
    return false;
}

void QuickRunPopup::paintEvent(QPaintEvent *)
{
    // Pintado aqui (e não pelo stylesheet) porque a janela é translúcida: o fundo e a borda seguem o raio do tema.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor(tk::borderColor()));
    painter.setBrush(QColor(tk::surface2()));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), tk::radiusLg(), tk::radiusLg());
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
    layoutChips(); // a largura já foi definida pelo open*(): recalcula a quebra de linha dos chips
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
