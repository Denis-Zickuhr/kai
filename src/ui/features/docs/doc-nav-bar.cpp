#include "ui/features/docs/doc-nav-bar.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {
constexpr int kButton = 28;      // lado da área clicável de cada botão
constexpr int kIcon = 16;
constexpr int kGap = 2;
constexpr int kMargin = 8;
constexpr int kZoomLabelWidth = 46;
constexpr int kSeparator = 14;   // largura do "›" entre breadcrumbs
} // namespace

DocNavBar::DocNavBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("docNavBar"));
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(m_barHeight);
}

void DocNavBar::setBarHeight(int height)
{
    m_barHeight = std::max(28, height);
    setFixedHeight(m_barHeight);
    layoutItems();
    update();
}

void DocNavBar::setCanGoBack(bool enabled)
{
    m_canBack = enabled;
    update();
}

void DocNavBar::setCanGoForward(bool enabled)
{
    m_canForward = enabled;
    update();
}

void DocNavBar::setTreeActive(bool active)
{
    m_treeActive = active;
    update();
}

void DocNavBar::setZoomPercent(int percent)
{
    m_zoom = percent;
    update();
}

void DocNavBar::setCrumbs(const QVector<DocCrumb> &crumbs)
{
    m_crumbs = crumbs;
    layoutItems();
    update();
}

QRect DocNavBar::rectOf(Item item) const
{
    switch (item) {
    case Item::Back: return m_back;
    case Item::Forward: return m_forward;
    case Item::Tree: return m_tree;
    case Item::ZoomOut: return m_zoomOut;
    case Item::ZoomLabel: return m_zoomLabel;
    case Item::ZoomIn: return m_zoomIn;
    default: break;
    }
    return QRect();
}

QRect DocNavBar::crumbRect(int index) const
{
    return index >= 0 && index < m_visibleCrumbs.size() ? m_visibleCrumbs.at(index).rect : QRect();
}

void DocNavBar::layoutItems()
{
    const int top = (m_barHeight - 1 - kButton) / 2;
    int x = kMargin;
    m_tree = QRect(x, top, kButton, kButton);
    x += kButton + kMargin;
    m_back = QRect(x, top, kButton, kButton);
    x += kButton + kGap;
    m_forward = QRect(x, top, kButton, kButton);
    x += kButton + kMargin;
    const int crumbsLeft = x;

    // Grupo da direita: [-] [100%] [+].
    int right = width() - kMargin;
    m_zoomIn = QRect(right - kButton, top, kButton, kButton);
    right -= kButton + kGap;
    m_zoomLabel = QRect(right - kZoomLabelWidth, top, kZoomLabelWidth, kButton);
    right -= kZoomLabelWidth + kGap;
    m_zoomOut = QRect(right - kButton, top, kButton, kButton);
    right -= kButton + kMargin;

    // Breadcrumbs: cabem em [crumbsLeft, right]; se não couberem, os primeiros viram "…" (o último nunca some).
    // O último breadcrumb é desenhado em negrito: medir com a fonte normal o cortava no fim.
    const QFontMetrics metrics(font());
    QFont boldFont = font();
    boldFont.setBold(true);
    const QFontMetrics boldMetrics(boldFont);
    const int available = std::max(0, right - crumbsLeft);
    QVector<DocCrumb> crumbs = m_crumbs;
    auto totalWidth = [&](const QVector<DocCrumb> &list) {
        int w = 0;
        for (int i = 0; i < list.size(); ++i) {
            const QFontMetrics &m = i + 1 == list.size() ? boldMetrics : metrics;
            w += m.horizontalAdvance(list.at(i).label) + 14 + (i > 0 ? kSeparator : 0);
        }
        return w;
    };
    while (crumbs.size() > 2 && totalWidth(crumbs) > available) {
        crumbs.remove(1); // tira o segundo; o "…" entra no lugar do primeiro cortado
        crumbs[0] = DocCrumb{QStringLiteral("…"), QString()};
    }
    m_visibleCrumbs.clear();
    int cx = crumbsLeft;
    const int chipHeight = 24;
    const int chipTop = (m_barHeight - 1 - chipHeight) / 2;
    for (int i = 0; i < crumbs.size(); ++i) {
        if (i > 0) {
            cx += kSeparator;
        }
        QString text = crumbs.at(i).label;
        const QFontMetrics &textMetrics = i + 1 == crumbs.size() ? boldMetrics : metrics;
        int w = textMetrics.horizontalAdvance(text) + 14;
        const int room = std::max(30, right - cx);
        if (w > room) { // último/único que ainda não cabe: corta com reticências
            text = textMetrics.elidedText(text, Qt::ElideMiddle, room - 14);
            w = room;
        }
        m_visibleCrumbs.append({text, crumbs.at(i).target, QRect(cx, chipTop, w, chipHeight)});
        cx += w;
    }
}

void DocNavBar::resizeEvent(QResizeEvent *)
{
    layoutItems();
}

bool DocNavBar::isEnabled(Item item) const
{
    switch (item) {
    case Item::Back: return m_canBack;
    case Item::Forward: return m_canForward;
    case Item::ZoomOut: return m_zoom > 60;
    case Item::ZoomIn: return m_zoom < 250;
    default: return true;
    }
}

DocNavBar::Hit DocNavBar::hitAt(const QPoint &pos) const
{
    const std::pair<Item, QRect> items[] = {{Item::Tree, m_tree}, {Item::Back, m_back},       {Item::Forward, m_forward},
                                            {Item::ZoomOut, m_zoomOut}, {Item::ZoomLabel, m_zoomLabel}, {Item::ZoomIn, m_zoomIn}};
    for (const auto &entry : items) {
        if (entry.second.contains(pos)) {
            return Hit{entry.first, -1};
        }
    }
    for (int i = 0; i < m_visibleCrumbs.size(); ++i) {
        if (m_visibleCrumbs.at(i).rect.contains(pos) && !m_visibleCrumbs.at(i).target.isEmpty()) {
            return Hit{Item::Crumb, i};
        }
    }
    return Hit{};
}

void DocNavBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(tk::bg()));
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int radius = tk::radiusSm();
    const QColor fg(tk::fg());
    const QColor muted(tk::mutedFg());
    const QColor accent(tk::accent());

    auto drawButton = [&](Item item, const QRect &rect, const QString &icon, bool active = false) {
        const bool enabled = isEnabled(item);
        const bool hovered = enabled && m_hover.item == item;
        if (hovered || active) {
            painter.setPen(Qt::NoPen);
            QColor fill(hovered ? tk::hoverBg() : tk::selBg());
            if (!hovered) {
                fill.setAlpha(150);
            }
            painter.setBrush(fill);
            painter.drawRoundedRect(QRectF(rect), radius, radius);
        }
        QColor color = !enabled ? muted : (hovered || active ? fg : muted);
        if (!enabled) {
            color.setAlpha(90);
        }
        const QRect box(rect.left() + (rect.width() - kIcon) / 2, rect.top() + (rect.height() - kIcon) / 2, kIcon, kIcon);
        LucideIcons::icon(icon, color, kIcon).paint(&painter, box);
    };
    drawButton(Item::Back, m_back, QStringLiteral("arrow-left"));
    drawButton(Item::Forward, m_forward, QStringLiteral("arrow-right"));
    drawButton(Item::Tree, m_tree, QStringLiteral("list-tree"), m_treeActive);
    drawButton(Item::ZoomOut, m_zoomOut, QStringLiteral("minus"));
    drawButton(Item::ZoomIn, m_zoomIn, QStringLiteral("plus"));
    // O percentual: clicar volta a 100%.
    if (m_hover.item == Item::ZoomLabel) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(tk::hoverBg()));
        painter.drawRoundedRect(QRectF(m_zoomLabel), radius, radius);
    }
    painter.setPen(m_hover.item == Item::ZoomLabel ? fg : muted);
    painter.drawText(m_zoomLabel, Qt::AlignCenter, QStringLiteral("%1%").arg(m_zoom));

    // Breadcrumbs.
    for (int i = 0; i < m_visibleCrumbs.size(); ++i) {
        const VisibleCrumb &crumb = m_visibleCrumbs.at(i);
        const bool last = i + 1 == m_visibleCrumbs.size();
        const bool clickable = !crumb.target.isEmpty();
        const bool hovered = m_hover.item == Item::Crumb && m_hover.crumb == i;
        if (i > 0) {
            const QRect sep(crumb.rect.left() - kSeparator, crumb.rect.top(), kSeparator, crumb.rect.height());
            QColor dim = muted;
            dim.setAlpha(150);
            LucideIcons::icon(QStringLiteral("chevron-right"), dim, 12)
                .paint(&painter, QRect(sep.left() + (sep.width() - 12) / 2, sep.top() + (sep.height() - 12) / 2, 12, 12));
        }
        if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(tk::hoverBg()));
            painter.drawRoundedRect(QRectF(crumb.rect), radius, radius);
        }
        QFont crumbFont = font();
        crumbFont.setBold(last);
        painter.setFont(crumbFont);
        painter.setPen(last ? fg : (clickable ? (hovered ? accent : muted) : muted));
        painter.drawText(crumb.rect, Qt::AlignCenter, crumb.text);
    }
    painter.setFont(font());
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QColor(tk::borderColor()));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void DocNavBar::mousePressEvent(QMouseEvent *event)
{
    m_pressed = hitAt(event->position().toPoint());
    if (m_pressed.item == Item::None) {
        QWidget::mousePressEvent(event);
    }
}

void DocNavBar::mouseReleaseEvent(QMouseEvent *event)
{
    const Hit hit = hitAt(event->position().toPoint());
    const Hit pressed = m_pressed;
    m_pressed = Hit{};
    if (hit.item == Item::None || !(hit == pressed) || event->button() != Qt::LeftButton || !isEnabled(hit.item)) {
        return;
    }
    switch (hit.item) {
    case Item::Back: emit backRequested(); break;
    case Item::Forward: emit forwardRequested(); break;
    case Item::Tree: emit treeToggleRequested(); break;
    case Item::ZoomIn: emit zoomInRequested(); break;
    case Item::ZoomOut: emit zoomOutRequested(); break;
    case Item::ZoomLabel: emit zoomResetRequested(); break;
    case Item::Crumb: emit crumbActivated(m_visibleCrumbs.at(hit.crumb).target); break;
    case Item::None: break;
    }
}

void DocNavBar::mouseMoveEvent(QMouseEvent *event)
{
    const Hit hit = hitAt(event->position().toPoint());
    if (!(hit == m_hover)) {
        m_hover = hit;
        setCursor(hit.item != Item::None && isEnabled(hit.item) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void DocNavBar::leaveEvent(QEvent *event)
{
    if (m_hover.item != Item::None) {
        m_hover = Hit{};
        update();
    }
    QWidget::leaveEvent(event);
}

QString DocNavBar::tooltipFor(const Hit &hit) const
{
    switch (hit.item) {
    case Item::Back: return utils::tr(QStringLiteral("doc.nav.back"));
    case Item::Forward: return utils::tr(QStringLiteral("doc.nav.forward"));
    case Item::Tree: return utils::tr(QStringLiteral("doc.nav.tree"));
    case Item::ZoomOut: return utils::tr(QStringLiteral("doc.nav.zoom_out"));
    case Item::ZoomIn: return utils::tr(QStringLiteral("doc.nav.zoom_in"));
    case Item::ZoomLabel: return utils::tr(QStringLiteral("doc.nav.zoom_reset"));
    case Item::Crumb: return m_visibleCrumbs.at(hit.crumb).target;
    case Item::None: break;
    }
    return QString();
}

bool DocNavBar::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const QString text = tooltipFor(hitAt(help->pos()));
        if (text.isEmpty()) {
            QToolTip::hideText();
        } else {
            QToolTip::showText(help->globalPos(), text, this);
        }
        return true;
    }
    return QWidget::event(event);
}

} // namespace kai::ui
