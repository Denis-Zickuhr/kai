#include "ui/features/command-editor/folder-actions-view.h"

#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFont>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>

#include <algorithm>
#include <iterator>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
// Id (de hover/clique) do símbolo de expansão de uma pasta e do ícone de um grupo.
QString expanderIdFor(const QString &folderId)
{
    return QStringLiteral("<expander>") + folderId;
}

QString groupIdFor(const QString &folderId, const QString &group)
{
    return QStringLiteral("<group>") + folderId + QLatin1Char('|') + group.toLower();
}

constexpr auto kDefaultGroupIcon = "layers";

// Uma posição da linha: um ícone de ação, o ícone de um grupo ou o símbolo de expansão.
struct Slot {
    enum class Kind { Inline, Group, Expander } kind = Kind::Inline;
    FolderActionIcon icon;               // Inline
    QString group;                       // Group
    QString groupIcon;                   // Group
    QVector<FolderActionIcon> members;   // Group / Expander: o que o menu lista
};

// Ordem na linha: ícones comuns, depois um por grupo (na ordem em que aparecem), por fim o
// expansor (se sobrou alguma de expansão).
QVector<Slot> slotsFor(const QVector<FolderActionIcon> &icons)
{
    QVector<Slot> inlines;
    QVector<Slot> groups;
    Slot expander;
    expander.kind = Slot::Kind::Expander;
    for (const FolderActionIcon &icon : icons) {
        if (!icon.group.isEmpty()) {
            auto it = std::find_if(groups.begin(), groups.end(),
                                   [&icon](const Slot &g) { return g.group.compare(icon.group, Qt::CaseInsensitive) == 0; });
            if (it == groups.end()) {
                Slot slot;
                slot.kind = Slot::Kind::Group;
                slot.group = icon.group;
                groups.append(slot);
                it = std::prev(groups.end());
            }
            if (it->groupIcon.isEmpty()) {
                it->groupIcon = icon.groupIcon;
            }
            it->members.append(icon);
        } else if (icon.expansion) {
            expander.members.append(icon);
        } else {
            Slot slot;
            slot.icon = icon;
            inlines.append(slot);
        }
    }
    QVector<Slot> result = inlines + groups;
    if (!expander.members.isEmpty()) {
        result.append(expander);
    }
    return result;
}

// Ícone do pool do Lucide, imagem do usuário ou, na falta, o `fallback`.
QIcon iconFor(const QString &name, const QString &fallback, const QColor &color, int size)
{
    if (!name.isEmpty() && LucideIcons::has(name)) {
        return LucideIcons::icon(name, color, size);
    }
    if (const QIcon custom = IconPickerWidget::iconForName(name); !custom.isNull()) {
        return custom;
    }
    return LucideIcons::icon(fallback, color, size);
}

bool anyRunning(const FolderActionsView::Provider &provider, const QVector<FolderActionIcon> &members)
{
    return provider.isRunning
        && std::any_of(members.cbegin(), members.cend(),
                       [&provider](const FolderActionIcon &a) { return provider.isRunning(a.virtualId); });
}

bool anyFocused(const QString &focused, const QVector<FolderActionIcon> &members)
{
    return !focused.isEmpty()
        && std::any_of(members.cbegin(), members.cend(),
                       [&focused](const FolderActionIcon &a) { return a.virtualId == focused; });
}
} // namespace

FolderActionsView::FolderActionsView(QTreeWidget *tree, Provider provider)
    : QStyledItemDelegate(tree)
    , m_tree(tree)
    , m_provider(std::move(provider))
{
    tree->setItemDelegateForColumn(kActionsColumn, this);
    tree->viewport()->installEventFilter(this);
}

int FolderActionsView::slotCount(const QVector<FolderActionIcon> &icons)
{
    return static_cast<int>(slotsFor(icons).size());
}

int FolderActionsView::columnWidthFor(int count)
{
    return count <= 0 ? 0 : count * kBox + (count - 1) * kGap + 2 * kMargin;
}

QVector<QRect> FolderActionsView::iconRects(const QRect &cell, int count)
{
    QVector<QRect> rects;
    if (count <= 0 || cell.width() <= 0) {
        return rects;
    }
    // Numa linha mais baixa que a caixa, o ícone encolhe: nunca vaza pra linha vizinha
    // (e o centro continua sendo o da linha).
    const int box = std::max(1, std::min(kBox, cell.height()));
    const int fit = std::max(0, (cell.width() - 2 * kMargin + kGap) / (box + kGap));
    const int visible = std::min(count, fit);
    int x = cell.left() + kMargin;
    const int y = cell.top() + (cell.height() - box) / 2;
    for (int i = 0; i < visible; ++i) {
        rects.append(QRect(x, y, box, box));
        x += box + kGap;
    }
    return rects;
}

QRect FolderActionsView::cellRectFor(const QTreeWidgetItem *item) const
{
    // Altura/posição vertical da linha; horizontal = a coluna de ações.
    QRect rect = m_tree->visualItemRect(item);
    rect.setLeft(m_tree->columnViewportPosition(kActionsColumn));
    rect.setWidth(m_tree->columnWidth(kActionsColumn));
    return rect;
}

FolderActionsView::Hit FolderActionsView::hitTest(const QPoint &viewportPos) const
{
    Hit hit;
    QTreeWidgetItem *item = m_tree->itemAt(viewportPos);
    if (!item || !m_provider.folderIdOf) {
        return hit;
    }
    const QString folderId = m_provider.folderIdOf(item);
    if (folderId.isEmpty()) {
        return hit;
    }
    const QVector<FolderActionIcon> icons = m_provider.iconsFor(folderId);
    const QVector<Slot> row = slotsFor(icons);
    const QVector<QRect> rects = iconRects(cellRectFor(item), static_cast<int>(row.size()));
    for (int i = 0; i < rects.size(); ++i) {
        if (!rects.at(i).contains(viewportPos)) {
            continue;
        }
        const Slot &slot = row.at(i);
        hit.valid = true;
        hit.rect = rects.at(i);
        switch (slot.kind) {
        case Slot::Kind::Inline:
            hit.icon = slot.icon;
            break;
        case Slot::Kind::Group:
            hit.group = slot.group;
            hit.icon.folderId = folderId;
            hit.icon.virtualId = groupIdFor(folderId, slot.group);
            break;
        case Slot::Kind::Expander:
            hit.expander = true;
            hit.icon.folderId = folderId;
            hit.icon.virtualId = expanderIdFor(folderId);
            break;
        }
        return hit;
    }
    return hit;
}

void FolderActionsView::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QTreeWidgetItem *item = index.column() == kActionsColumn ? m_tree->itemFromIndex(index) : nullptr;
    const QString folderId = (item && m_provider.folderIdOf) ? m_provider.folderIdOf(item) : QString();
    const QVector<FolderActionIcon> icons = folderId.isEmpty() ? QVector<FolderActionIcon>()
                                                               : m_provider.iconsFor(folderId);
    if (icons.isEmpty()) {
        // Linha de comando (play + tempo) ou pasta sem ação: o desenho padrão da coluna.
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const QVector<Slot> row = slotsFor(icons);
    const QVector<QRect> rects = iconRects(option.rect, static_cast<int>(row.size()));

    const QString focused = m_provider.focusedId ? m_provider.focusedId() : QString();
    for (int i = 0; i < rects.size(); ++i) {
        const Slot &slot = row.at(i);
        if (slot.kind == Slot::Kind::Inline) {
            const FolderActionIcon &icon = slot.icon;
            paintIcon(painter, rects.at(i), icon,
                      m_provider.isRunning && m_provider.isRunning(icon.virtualId),
                      !focused.isEmpty() && icon.virtualId == focused,
                      icon.virtualId == m_hoverId,
                      m_provider.hasOutput && m_provider.hasOutput(icon.virtualId));
            continue;
        }
        // O ícone do grupo / o expansor acompanham as ações que escondem: verde se alguma roda,
        // em destaque se a focada (a que mostra a saída) é uma delas.
        const bool isGroup = slot.kind == Slot::Kind::Group;
        paintMenuSlot(painter, rects.at(i), isGroup ? slot.groupIcon : QStringLiteral("chevron-down"),
                      isGroup ? QString::fromLatin1(kDefaultGroupIcon) : QStringLiteral("chevron-down"),
                      anyRunning(m_provider, slot.members), anyFocused(focused, slot.members),
                      (isGroup ? groupIdFor(folderId, slot.group) : expanderIdFor(folderId)) == m_hoverId);
    }
}

void FolderActionsView::paintMenuSlot(QPainter *painter, const QRect &box, const QString &iconName,
                                      const QString &fallbackName, bool anyRunning, bool focused, bool hovered) const
{
    const QColor stateColor(anyRunning ? tk::successFg() : tk::accent());
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (focused || hovered) {
        QColor fill = stateColor;
        fill.setAlpha(focused ? 64 : 30);
        painter->setPen(focused ? QPen(stateColor, 1) : QPen(Qt::NoPen));
        painter->setBrush(fill);
        painter->drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), tk::radiusSm(), tk::radiusSm());
    }
    const int iconSize = std::min({16, box.width(), box.height()});
    const QRect inner(box.left() + (box.width() - iconSize) / 2, box.top() + (box.height() - iconSize) / 2,
                      iconSize, iconSize);
    iconFor(iconName, fallbackName, stateColor, iconSize).paint(painter, inner);
    painter->restore();
}

QString FolderActionsView::tooltipFor(const FolderActionIcon &icon) const
{
    const bool running = m_provider.isRunning && m_provider.isRunning(icon.virtualId);
    const bool focused = m_provider.focusedId && m_provider.focusedId() == icon.virtualId;
    const bool saved = m_provider.hasOutput && m_provider.hasOutput(icon.virtualId);
    // O que o PRÓXIMO clique faz: rodando/não carregado com saída = carrega; carregado = roda de novo.
    const char *key = running ? "tree.action.tooltip.running"
                    : focused ? "tree.action.tooltip.rerun"
                    : saved   ? "tree.action.tooltip.load"
                              : "tree.action.tooltip.idle";
    return utils::tr(QString::fromLatin1(key)).arg(icon.name);
}

void FolderActionsView::showSlotMenu(const QString &folderId, const QString &group, const QRect &anchor)
{
    QVector<FolderActionIcon> hidden;
    for (const Slot &slot : slotsFor(m_provider.iconsFor(folderId))) {
        const bool wanted = group.isEmpty() ? slot.kind == Slot::Kind::Expander
                                            : slot.kind == Slot::Kind::Group && slot.group == group;
        if (wanted) {
            hidden = slot.members;
        }
    }
    if (hidden.isEmpty()) {
        return;
    }
    const QString focused = m_provider.focusedId ? m_provider.focusedId() : QString();
    QMenu menu(m_tree);
    menu.setToolTipsVisible(true);
    m_menu = &menu;
    menu.installEventFilter(this);
    for (const FolderActionIcon &icon : hidden) {
        const bool running = m_provider.isRunning && m_provider.isRunning(icon.virtualId);
        const QColor stateColor(running ? tk::successFg() : tk::accent());
        QAction *action = menu.addAction(iconFor(icon.iconName, QStringLiteral("square-terminal"), stateColor, 16),
                                         icon.name);
        action->setData(QStringList{icon.commandId, icon.folderId});
        action->setToolTip(tooltipFor(icon));
        if (!focused.isEmpty() && icon.virtualId == focused) {
            QFont bold = action->font();
            bold.setBold(true);
            action->setFont(bold);
        }
    }
    const QPoint where = m_tree->viewport()->mapToGlobal(anchor.bottomLeft());
    QAction *chosen = menu.exec(where);
    m_menu.clear();
    if (chosen) {
        const QStringList ids = chosen->data().toStringList();
        emit activated(ids.value(0), ids.value(1));
    }
}

FolderActionsView::HoverGlyph FolderActionsView::hoverGlyph(bool running, bool focused, bool hasOutput)
{
    if (running || (hasOutput && !focused)) {
        return HoverGlyph::Eye;
    }
    return focused ? HoverGlyph::Play : HoverGlyph::None;
}

void FolderActionsView::paintIcon(QPainter *painter, const QRect &box, const FolderActionIcon &icon,
                                  bool running, bool focused, bool hovered, bool hasOutput) const
{
    const QColor stateColor(running ? tk::successFg() : tk::accent());
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (focused || hovered) {
        QColor fill = stateColor;
        fill.setAlpha(focused ? 64 : 30);
        painter->setPen(focused ? QPen(stateColor, 1) : QPen(Qt::NoPen));
        painter->setBrush(fill);
        painter->drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), tk::radiusSm(), tk::radiusSm());
    }
    constexpr int kIconSize = 16;
    // Offset exato (QRect::center() arredonda pra esquerda/cima e deixava o ícone torto na caixa).
    const int iconSize = std::min({kIconSize, box.width(), box.height()});
    const QRect inner(box.left() + (box.width() - iconSize) / 2, box.top() + (box.height() - iconSize) / 2, iconSize, iconSize);
    const HoverGlyph glyph = hovered ? hoverGlyph(running, focused, hasOutput) : HoverGlyph::None;
    if (glyph == HoverGlyph::Eye) {
        // O próximo clique só MOSTRA a saída: o olho avisa que não vai executar nada.
        LucideIcons::icon(QStringLiteral("eye"), stateColor, iconSize).paint(painter, inner);
    } else if (glyph == HoverGlyph::Play) {
        // Já carregado e parado: o próximo clique executa de novo — o "play" avisa isso.
        LucideIcons::icon(QStringLiteral("play"), stateColor, iconSize).paint(painter, inner);
    } else if (!icon.iconName.isEmpty() && LucideIcons::has(icon.iconName)) {
        LucideIcons::icon(icon.iconName, stateColor, iconSize).paint(painter, inner);
    } else {
        const QIcon custom = IconPickerWidget::iconForName(icon.iconName);
        if (!custom.isNull()) {
            // Imagem do usuário não se pinta: o estado vai numa barrinha embaixo.
            custom.paint(painter, inner);
            painter->setPen(QPen(stateColor, 2));
            painter->drawLine(box.left() + 4, box.bottom() - 1, box.right() - 4, box.bottom() - 1);
        } else {
            LucideIcons::icon(QStringLiteral("square-terminal"), stateColor, iconSize).paint(painter, inner);
        }
    }
    painter->restore();
}

bool FolderActionsView::eventFilter(QObject *watched, QEvent *event)
{
    if (m_menu && watched == m_menu.data()) {
        // Botão direito num item do menu de expansão: o mesmo menu de contexto do ícone
        // (fecha este menu primeiro; abre o outro fora do tratamento do evento).
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            QAction *action = mouse->button() == Qt::RightButton ? m_menu->actionAt(mouse->pos()) : nullptr;
            const QStringList ids = action ? action->data().toStringList() : QStringList();
            if (ids.size() == 2) {
                const QPoint globalPos = mouse->globalPosition().toPoint();
                m_menu->close();
                QTimer::singleShot(0, this, [this, ids, globalPos]() {
                    emit contextRequested(ids.at(0), ids.at(1), globalPos);
                });
                return true;
            }
        }
        return false;
    }
    if (watched != m_tree->viewport()) {
        return QStyledItemDelegate::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) {
            return false;
        }
        const Hit hit = hitTest(mouse->pos());
        if (hit.valid) {
            m_pressedId = hit.icon.virtualId;
            return true; // nem seleciona a linha, nem começa a arrastar, nem expande
        }
        if (QTreeWidgetItem *item = m_tree->itemAt(mouse->pos())) {
            const QString folderId = m_provider.folderIdOf ? m_provider.folderIdOf(item) : QString();
            if (!folderId.isEmpty()) {
                emit rowClicked(folderId); // sem consumir: a linha seleciona como sempre
            }
        }
        return false;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || m_pressedId.isEmpty()) {
            return false;
        }
        const QString pressed = std::exchange(m_pressedId, QString());
        const Hit hit = hitTest(mouse->pos());
        if (hit.valid && hit.icon.virtualId == pressed) {
            if (hit.expander || !hit.group.isEmpty()) {
                // Abre fora do tratamento deste clique, senão o menu já nasce com o release dele.
                const QString folderId = hit.icon.folderId;
                const QString group = hit.group;
                const QRect rect = hit.rect;
                QTimer::singleShot(0, this, [this, folderId, group, rect]() { showSlotMenu(folderId, group, rect); });
            } else {
                emit activated(hit.icon.commandId, hit.icon.folderId);
            }
        }
        return true;
    }
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const Hit hit = hitTest(mouse->pos());
        const QString hovered = hit.valid ? hit.icon.virtualId : QString();
        if (hovered != m_hoverId) {
            m_hoverId = hovered;
            m_tree->viewport()->setCursor(hit.valid ? Qt::PointingHandCursor : Qt::ArrowCursor);
            m_tree->viewport()->update();
        }
        return false;
    }
    case QEvent::Leave:
        if (!m_hoverId.isEmpty()) {
            m_hoverId.clear();
            m_tree->viewport()->setCursor(Qt::ArrowCursor);
            m_tree->viewport()->update();
        }
        return false;
    case QEvent::ToolTip: {
        auto *help = static_cast<QHelpEvent *>(event);
        const Hit hit = hitTest(help->pos());
        if (!hit.valid) {
            return false;
        }
        QString text;
        if (hit.expander || !hit.group.isEmpty()) {
            QStringList names;
            for (const Slot &slot : slotsFor(m_provider.iconsFor(hit.icon.folderId))) {
                if (hit.expander ? slot.kind == Slot::Kind::Expander
                                 : slot.kind == Slot::Kind::Group && slot.group == hit.group) {
                    for (const FolderActionIcon &a : slot.members) names << a.name;
                }
            }
            const QString list = names.join(QStringLiteral(", "));
            text = hit.expander ? utils::tr(QStringLiteral("tree.action.expander.tooltip")).arg(list)
                                : utils::tr(QStringLiteral("tree.action.group.tooltip")).arg(hit.group, list);
        } else {
            text = tooltipFor(hit.icon);
        }
        QToolTip::showText(help->globalPos(), text, m_tree->viewport(), hit.rect);
        return true;
    }
    case QEvent::ContextMenu: {
        auto *menu = static_cast<QContextMenuEvent *>(event);
        const Hit hit = hitTest(menu->pos());
        if (!hit.valid || hit.expander || !hit.group.isEmpty()) {
            return false;
        }
        emit contextRequested(hit.icon.commandId, hit.icon.folderId, menu->globalPos());
        return true;
    }
    default:
        return false;
    }
}

} // namespace kai::ui
