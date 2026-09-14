#include "ui/features/output/output-tabs-bar.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/duration-format.h"
#include "utils/translation-manager.h"

#include <QAction>
#include <QActionGroup>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {
constexpr int kMargin = 6;        // respiro nas duas pontas da barra
constexpr int kTabGap = 2;        // espaço entre guias
constexpr int kPadX = 8;          // recuo interno da guia
constexpr int kInnerGap = 6;      // entre o ponto, o nome, o tempo...
constexpr int kDot = 8;           // diâmetro do ponto de status
constexpr int kUnreadDot = 6;     // marca de saída nova
constexpr int kClose = 16;        // lado da área do "×"
constexpr int kMaxTextWidth = 170;
constexpr int kVerticalInset = 3; // folga acima/abaixo da guia dentro da barra
constexpr int kOverflowWidth = 24;
constexpr int kToggleWidth = 26;   // chevron que recolhe a lista de comandos, no começo da barra
} // namespace

OutputTabsBar::OutputTabsBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("outputTabsBar"));
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(m_barHeight);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, [this]() { update(); });

    refreshStyle();
}

QString OutputTabsBar::statusText(OutputTabStatus status)
{
    switch (status) {
    case OutputTabStatus::Running: return utils::tr(QStringLiteral("output_tabs.status.running"));
    case OutputTabStatus::Waiting: return utils::tr(QStringLiteral("output_tabs.status.waiting"));
    case OutputTabStatus::Success: return utils::tr(QStringLiteral("output_tabs.status.success"));
    case OutputTabStatus::Failed:  return utils::tr(QStringLiteral("output_tabs.status.failed"));
    case OutputTabStatus::Skipped: return utils::tr(QStringLiteral("output_tabs.status.skipped"));
    case OutputTabStatus::Idle:    break;
    }
    return utils::tr(QStringLiteral("output_tabs.status.idle"));
}

QColor OutputTabsBar::dotColor(OutputTabStatus status) const
{
    switch (status) {
    case OutputTabStatus::Running: return QColor(tk::successFg());
    case OutputTabStatus::Waiting: return QColor(tk::warningFg());
    case OutputTabStatus::Failed:  return QColor(tk::errorFg());
    case OutputTabStatus::Skipped: return QColor(tk::infoFg());
    case OutputTabStatus::Success:
    case OutputTabStatus::Idle:    break;
    }
    return QColor(tk::mutedFg());
}

QStringList OutputTabsBar::ids() const
{
    QStringList list;
    for (const OutputTabInfo &tab : m_tabs) {
        list << tab.id;
    }
    return list;
}

bool OutputTabsBar::hasTab(const QString &id) const
{
    return indexOf(id) >= 0;
}

int OutputTabsBar::indexOf(const QString &id) const
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void OutputTabsBar::setTabs(const QVector<OutputTabInfo> &tabs)
{
    m_tabs = tabs;
    m_dragActive = false; // uma lista nova de fora desfaz um arrasto em andamento
    unsetCursor();
    const QDateTime now = QDateTime::currentDateTime();
    QSet<QString> present;
    for (const OutputTabInfo &tab : m_tabs) {
        present.insert(tab.id);
        const bool timed = tab.status == OutputTabStatus::Running || tab.status == OutputTabStatus::Waiting;
        if (timed && !m_started.contains(tab.id)) {
            m_started.insert(tab.id, now);
        } else if (!timed) {
            m_started.remove(tab.id);
        }
    }
    for (auto it = m_unread.begin(); it != m_unread.end();) {
        it = present.contains(it.key()) ? std::next(it) : m_unread.erase(it);
    }
    for (auto it = m_started.begin(); it != m_started.end();) {
        it = present.contains(it.key()) ? std::next(it) : m_started.erase(it);
    }
    m_hover = -1;
    updateVisibility();
    updateTimer();
    clampScroll();
    ensureCurrentVisible();
    update();
}

void OutputTabsBar::setCurrent(const QString &id)
{
    if (m_current == id && !m_unread.contains(id)) {
        return;
    }
    m_current = id;
    m_unread.remove(id);
    ensureCurrentVisible();
    update();
}

void OutputTabsBar::markActivity(const QString &id)
{
    if (id == m_current || indexOf(id) < 0 || m_unread.value(id)) {
        return;
    }
    m_unread.insert(id, true);
    update();
}

void OutputTabsBar::noteRunStarted(const QString &id)
{
    m_started.insert(id, QDateTime::currentDateTime());
    update();
}

void OutputTabsBar::setStripAllowed(bool allowed)
{
    m_allowed = allowed;
    updateVisibility();
}

void OutputTabsBar::updateVisibility()
{
    // Sempre presente (com a saída expandida): vazia, só mostra uma dica — a barra não aparece e some conforme os
    // comandos rodam, o que fazia o painel "pular" de altura.
    setVisible(m_allowed);
}

void OutputTabsBar::updateTimer()
{
    const bool needed = std::any_of(m_tabs.cbegin(), m_tabs.cend(), [](const OutputTabInfo &tab) {
        return tab.status == OutputTabStatus::Running || tab.status == OutputTabStatus::Waiting;
    });
    if (needed && !m_timer->isActive()) {
        m_timer->start();
    } else if (!needed) {
        m_timer->stop();
    }
}

void OutputTabsBar::setBarHeight(int height)
{
    m_barHeight = std::max(20, height);
    setFixedHeight(m_barHeight);
    update();
}

void OutputTabsBar::refreshStyle()
{
    update();
}

QString OutputTabsBar::elapsedText(const OutputTabInfo &tab) const
{
    if (tab.status != OutputTabStatus::Running && tab.status != OutputTabStatus::Waiting) {
        return QString();
    }
    const auto it = m_started.constFind(tab.id);
    if (it == m_started.constEnd()) {
        return QString();
    }
    return utils::formatShortDuration(it->msecsTo(QDateTime::currentDateTime()));
}

OutputTabsBar::Layout OutputTabsBar::computeLayout() const
{
    Layout layout;
    const QFontMetrics metrics(font());
    const int tabHeight = std::max(10, height() - 1 - 2 * kVerticalInset);
    int x = kMargin + leadingWidth() - m_scroll;
    for (const OutputTabInfo &tab : m_tabs) {
        const QString text = metrics.elidedText(tab.title, Qt::ElideRight, kMaxTextWidth);
        const QString elapsed = elapsedText(tab);
        int width = kPadX + kDot + kInnerGap + metrics.horizontalAdvance(text);
        if (!elapsed.isEmpty()) {
            width += kInnerGap + metrics.horizontalAdvance(elapsed);
        }
        if (m_unread.contains(tab.id)) {
            width += kInnerGap + kUnreadDot;
        }
        width += kInnerGap + kClose + kPadX / 2;
        const QRect rect(x, kVerticalInset, width, tabHeight);
        layout.tabs.append(rect);
        layout.closes.append(QRect(rect.right() - kPadX / 2 - kClose + 1, rect.top() + (tabHeight - kClose) / 2, kClose, kClose));
        x += width + kTabGap;
    }
    layout.contentWidth = x + m_scroll + kMargin - kTabGap;
    layout.viewportWidth = usableWidth() - (layout.contentWidth > usableWidth() ? kOverflowWidth : 0);
    return layout;
}

void OutputTabsBar::clampScroll()
{
    const Layout layout = computeLayout();
    const int maxScroll = std::max(0, layout.contentWidth - layout.viewportWidth);
    m_scroll = std::clamp(m_scroll, 0, maxScroll);
}

void OutputTabsBar::ensureCurrentVisible()
{
    const int index = indexOf(m_current);
    if (index < 0) {
        return;
    }
    const Layout layout = computeLayout();
    const QRect rect = layout.tabs.at(index);
    const int firstX = kMargin + leadingWidth();
    if (rect.left() < firstX) {
        m_scroll -= firstX - rect.left();
    } else if (rect.right() > layout.viewportWidth - kMargin) {
        m_scroll += rect.right() - (layout.viewportWidth - kMargin);
    }
    clampScroll();
}

int OutputTabsBar::leadingWidth() const
{
    if (!m_toggleVisible) {
        return 0;
    }
    // Chevron + "+" + documentos: três botões do mesmo tamanho, a partir de kMargin/2.
    return kMargin / 2 + 3 * (kToggleWidth - kMargin / 2);
}

int OutputTabsBar::usableWidth() const
{
    return width() - (m_outputToggleVisible ? kToggleWidth : 0);
}

void OutputTabsBar::setOutputToggle(bool visible, const QString &iconName)
{
    if (m_outputToggleVisible == visible && m_outputToggleIcon == iconName) {
        return;
    }
    m_outputToggleVisible = visible;
    m_outputToggleIcon = iconName;
    if (!visible) {
        m_hoverOutputToggle = false;
    }
    clampScroll();
    update();
}

QRect OutputTabsBar::outputToggleRect() const
{
    if (!m_outputToggleVisible) {
        return QRect();
    }
    return QRect(width() - kToggleWidth, kVerticalInset, kToggleWidth - kMargin / 2, std::max(10, height() - 1 - 2 * kVerticalInset));
}

void OutputTabsBar::setCommandsToggleVisible(bool visible)
{
    if (m_toggleVisible == visible) {
        return;
    }
    m_toggleVisible = visible;
    clampScroll();
    update();
}

void OutputTabsBar::setCommandsToggleState(bool collapsed, Qt::Edge commandsEdge)
{
    if (m_toggleCollapsed == collapsed && m_commandsEdge == commandsEdge) {
        return;
    }
    m_toggleCollapsed = collapsed;
    m_commandsEdge = commandsEdge;
    clampScroll();
    ensureCurrentVisible();
    update();
}

QRect OutputTabsBar::toggleRect() const
{
    if (!m_toggleVisible) {
        return QRect();
    }
    return QRect(kMargin / 2, kVerticalInset, kToggleWidth - kMargin / 2, std::max(10, height() - 1 - 2 * kVerticalInset));
}

QRect OutputTabsBar::quickRunRect() const
{
    if (!m_toggleVisible) {
        return QRect();
    }
    const QRect toggle = toggleRect();
    return QRect(toggle.right() + 1, toggle.top(), toggle.width(), toggle.height());
}

QRect OutputTabsBar::docsRect() const
{
    const QRect quickRun = quickRunRect();
    if (!quickRun.isValid()) {
        return QRect();
    }
    return QRect(quickRun.right() + 1, quickRun.top(), quickRun.width(), quickRun.height());
}

bool OutputTabsBar::isOverflowing() const
{
    return computeLayout().contentWidth > usableWidth();
}

QRect OutputTabsBar::overflowRect() const
{
    if (!isOverflowing()) {
        return QRect();
    }
    return QRect(usableWidth() - kOverflowWidth, kVerticalInset, kOverflowWidth - kMargin / 2, std::max(10, height() - 1 - 2 * kVerticalInset));
}

void OutputTabsBar::resizeEvent(QResizeEvent *)
{
    // Estreitou a janela: a guia em foco continua à vista.
    clampScroll();
    ensureCurrentVisible();
    update();
}

int OutputTabsBar::indexAt(const QPoint &pos) const
{
    const Layout layout = computeLayout();
    for (int i = 0; i < layout.tabs.size(); ++i) {
        if (layout.tabs.at(i).contains(pos) && pos.x() >= leadingWidth() && pos.x() < layout.viewportWidth) {
            return i;
        }
    }
    return -1;
}

QString OutputTabsBar::tabAt(const QPoint &pos) const
{
    const int index = indexAt(pos);
    return index < 0 ? QString() : m_tabs.at(index).id;
}

QRect OutputTabsBar::tabRect(const QString &id) const
{
    const int index = indexOf(id);
    return index < 0 ? QRect() : computeLayout().tabs.at(index);
}

QRect OutputTabsBar::closeRect(const QString &id) const
{
    const int index = indexOf(id);
    return index < 0 ? QRect() : computeLayout().closes.at(index);
}

void OutputTabsBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const Layout layout = computeLayout();
    const QFontMetrics metrics(font());
    const QColor fg(tk::fg());
    const QColor muted(tk::mutedFg());
    const int radius = tk::radiusSm();

    if (m_tabs.isEmpty()) {
        QColor hint = muted;
        hint.setAlpha(170);
        painter.setPen(hint);
        painter.drawText(QRect(kMargin + kPadX + leadingWidth(), 0, usableWidth() - 2 * kMargin - kPadX - leadingWidth(), height() - 1),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         metrics.elidedText(utils::tr(QStringLiteral("output_tabs.empty")), Qt::ElideRight,
                                            std::max(0, usableWidth() - 2 * kMargin - kPadX - leadingWidth())));
    }

    const QRect toggle = toggleRect();
    if (toggle.isValid()) {
        if (m_hoverToggle) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(tk::hoverBg()));
            painter.drawRoundedRect(QRectF(toggle), radius, radius);
        }
        // O chevron aponta para onde a lista vai (aberta) ou de onde ela volta (recolhida).
        QString icon;
        switch (m_commandsEdge) {
        case Qt::LeftEdge: icon = m_toggleCollapsed ? QStringLiteral("chevron-right") : QStringLiteral("chevron-left"); break;
        case Qt::RightEdge: icon = m_toggleCollapsed ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right"); break;
        default: icon = m_toggleCollapsed ? QStringLiteral("chevron-down") : QStringLiteral("chevron-up"); break;
        }
        constexpr int kToggleIcon = 14;
        const QRect iconRect(toggle.left() + (toggle.width() - kToggleIcon) / 2,
                             toggle.top() + (toggle.height() - kToggleIcon) / 2, kToggleIcon, kToggleIcon);
        LucideIcons::icon(icon, m_hoverToggle ? fg : muted, kToggleIcon).paint(&painter, iconRect);
    }

    const QRect quickRun = quickRunRect();
    if (quickRun.isValid()) {
        if (m_hoverQuickRun) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(tk::hoverBg()));
            painter.drawRoundedRect(QRectF(quickRun), radius, radius);
        }
        constexpr int kPlusIcon = 14;
        const QRect plusRect(quickRun.left() + (quickRun.width() - kPlusIcon) / 2,
                             quickRun.top() + (quickRun.height() - kPlusIcon) / 2, kPlusIcon, kPlusIcon);
        LucideIcons::icon(QStringLiteral("plus"), m_hoverQuickRun ? fg : muted, kPlusIcon).paint(&painter, plusRect);
    }

    const QRect outputToggle = outputToggleRect();
    if (outputToggle.isValid()) {
        if (m_hoverOutputToggle) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(tk::hoverBg()));
            painter.drawRoundedRect(QRectF(outputToggle), radius, radius);
        }
        constexpr int kOutputIcon = 14;
        const QRect iconRect(outputToggle.left() + (outputToggle.width() - kOutputIcon) / 2,
                             outputToggle.top() + (outputToggle.height() - kOutputIcon) / 2, kOutputIcon, kOutputIcon);
        LucideIcons::icon(m_outputToggleIcon, m_hoverOutputToggle ? fg : muted, kOutputIcon).paint(&painter, iconRect);
    }

    const QRect docs = docsRect();
    if (docs.isValid()) {
        if (m_hoverDocs) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(tk::hoverBg()));
            painter.drawRoundedRect(QRectF(docs), radius, radius);
        }
        constexpr int kDocIcon = 14;
        const QRect docRect(docs.left() + (docs.width() - kDocIcon) / 2, docs.top() + (docs.height() - kDocIcon) / 2,
                            kDocIcon, kDocIcon);
        LucideIcons::icon(QStringLiteral("file-text"), m_hoverDocs ? fg : muted, kDocIcon).paint(&painter, docRect);
    }

    painter.save();
    painter.setClipRect(QRect(leadingWidth(), 0, std::max(0, layout.viewportWidth - leadingWidth()), height()));
    // A guia arrastada é pintada por último (por cima das outras) e acompanha o mouse.
    QVector<int> paintOrder;
    const bool dragging = m_dragActive && m_pressed >= 0 && m_pressed < m_tabs.size();
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (!(dragging && i == m_pressed)) {
            paintOrder.append(i);
        }
    }
    if (dragging) {
        paintOrder.append(m_pressed);
    }
    for (const int i : std::as_const(paintOrder)) {
        const OutputTabInfo &tab = m_tabs.at(i);
        QRect rect = layout.tabs.at(i);
        QRect closeArea = layout.closes.at(i);
        if (dragging && i == m_pressed) {
            const int dx = m_dragX - rect.left();
            rect.translate(dx, 0);
            closeArea.translate(dx, 0);
        }
        if (rect.right() < 0 || rect.left() > layout.viewportWidth) {
            continue;
        }
        const bool current = tab.id == m_current;
        const bool hovered = i == m_hover;

        if (current || hovered) {
            QColor fill(current ? tk::selBg() : tk::hoverBg());
            if (current) {
                fill.setAlpha(150);
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawRoundedRect(QRectF(rect), radius, radius);
        }
        if (current) {
            painter.setPen(QPen(QColor(tk::accent()), 2, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(rect.left() + radius, rect.bottom(), rect.right() - radius, rect.bottom());
        }

        int x = rect.left() + kPadX;
        const int centerY = rect.center().y();
        painter.setPen(Qt::NoPen);
        painter.setBrush(dotColor(tab.status));
        painter.drawEllipse(QRectF(x, centerY - kDot / 2.0, kDot, kDot));
        x += kDot + kInnerGap;

        const QString text = metrics.elidedText(tab.title, Qt::ElideRight, kMaxTextWidth);
        painter.setPen(current || hovered ? fg : muted);
        painter.drawText(QRect(x, rect.top(), metrics.horizontalAdvance(text) + 2, rect.height()),
                         Qt::AlignVCenter | Qt::AlignLeft, text);
        x += metrics.horizontalAdvance(text);

        const QString elapsed = elapsedText(tab);
        if (!elapsed.isEmpty()) {
            x += kInnerGap;
            QColor dim = muted;
            dim.setAlpha(190);
            painter.setPen(dim);
            painter.drawText(QRect(x, rect.top(), metrics.horizontalAdvance(elapsed) + 2, rect.height()),
                             Qt::AlignVCenter | Qt::AlignLeft, elapsed);
            x += metrics.horizontalAdvance(elapsed);
        }
        if (m_unread.contains(tab.id)) {
            x += kInnerGap;
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(tk::accent()));
            painter.drawEllipse(QRectF(x, centerY - kUnreadDot / 2.0, kUnreadDot, kUnreadDot));
        }

        if (current || hovered) {
            const QRect close = closeArea;
            if (hovered && m_hoverClose) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(tk::hoverBg()));
                painter.drawRoundedRect(QRectF(close), radius, radius);
            }
            const QIcon icon = LucideIcons::icon(QStringLiteral("x"), hovered && m_hoverClose ? fg : muted, 12);
            icon.paint(&painter, close.adjusted(2, 2, -2, -2));
        }
    }
    painter.restore();

    // O botão com a lista de todas as guias, pintado como uma guia: mesmo topo, mesma altura, ícone no centro exato.
    const QRect overflow = overflowRect();
    if (overflow.isValid()) {
        if (m_hoverOverflow) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(tk::hoverBg()));
            painter.drawRoundedRect(QRectF(overflow), radius, radius);
        }
        constexpr int kIconSide = 14;
        const QRect iconRect(overflow.left() + (overflow.width() - kIconSide) / 2,
                             overflow.top() + (overflow.height() - kIconSide) / 2, kIconSide, kIconSide);
        LucideIcons::icon(QStringLiteral("chevron-down"), m_hoverOverflow ? fg : muted, kIconSide).paint(&painter, iconRect);
    }

    // Linha separadora embaixo, como a do container de ações.
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QColor(tk::borderColor()));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void OutputTabsBar::mousePressEvent(QMouseEvent *event)
{
    m_pressedQuickRun = quickRunRect().contains(event->position().toPoint());
    m_pressedDocs = docsRect().contains(event->position().toPoint());
    m_pressedOutputToggle = outputToggleRect().contains(event->position().toPoint());
    m_pressedToggle = toggleRect().contains(event->position().toPoint());
    m_pressedOverflow = overflowRect().contains(event->position().toPoint());
    m_pressed = indexAt(event->position().toPoint());
    m_dragActive = false;
    m_pressedClose = false;
    if (m_pressed >= 0) {
        const Layout layout = computeLayout();
        m_pressedClose = layout.closes.at(m_pressed).contains(event->position().toPoint());
        m_pressPos = event->position().toPoint();
        m_dragGrab = m_pressPos.x() - layout.tabs.at(m_pressed).left();
        m_dragOrigin = m_pressed;
    }
    if (m_pressed < 0 && !m_pressedOverflow && !m_pressedToggle && !m_pressedQuickRun && !m_pressedDocs
        && !m_pressedOutputToggle) {
        QWidget::mousePressEvent(event);
    }
}

void OutputTabsBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragActive) {
        // Soltou depois de arrastar: a ordem final vale (não é um clique na guia).
        m_dragActive = false;
        unsetCursor();
        const int finalIndex = m_pressed;
        m_pressed = -1;
        if (finalIndex >= 0 && finalIndex < m_tabs.size() && finalIndex != m_dragOrigin) {
            emit tabMoved(m_tabs.at(finalIndex).id, finalIndex);
        }
        update();
        return;
    }
    if (m_pressedOutputToggle) {
        m_pressedOutputToggle = false;
        if (event->button() == Qt::LeftButton && outputToggleRect().contains(event->position().toPoint())) {
            emit outputToggleRequested();
        }
        return;
    }
    if (m_pressedDocs) {
        m_pressedDocs = false;
        if (event->button() == Qt::LeftButton && docsRect().contains(event->position().toPoint())) {
            emit docsRequested();
        }
        return;
    }
    if (m_pressedQuickRun) {
        m_pressedQuickRun = false;
        if (event->button() == Qt::LeftButton && quickRunRect().contains(event->position().toPoint())) {
            emit quickRunRequested();
        }
        return;
    }
    if (m_pressedToggle) {
        m_pressedToggle = false;
        if (event->button() == Qt::LeftButton && toggleRect().contains(event->position().toPoint())) {
            emit commandsToggleRequested();
        }
        return;
    }
    if (m_pressedOverflow) {
        m_pressedOverflow = false;
        if (event->button() == Qt::LeftButton && overflowRect().contains(event->position().toPoint())) {
            showOverflowMenu();
        }
        return;
    }
    const int index = indexAt(event->position().toPoint());
    const int pressed = m_pressed;
    m_pressed = -1;
    if (index < 0 || index != pressed) {
        return;
    }
    const OutputTabInfo &tab = m_tabs.at(index);
    if (event->button() == Qt::MiddleButton) {
        emit tabCloseRequested(tab.id);
    } else if (event->button() == Qt::RightButton) {
        emit tabContextRequested(tab.id, event->globalPosition().toPoint());
    } else if (event->button() == Qt::LeftButton) {
        const bool onClose = computeLayout().closes.at(index).contains(event->position().toPoint());
        if (onClose) {
            emit tabCloseRequested(tab.id);
        } else {
            emit tabActivated(tab.id);
        }
    }
}

// Reordena as guias conforme o mouse passa pelo meio das vizinhas; a guia arrastada acompanha o mouse.
void OutputTabsBar::updateDrag(const QPoint &pos)
{
    const Layout first = computeLayout();
    // Perto das bordas, rola a faixa para dar espaço de soltar nas guias escondidas.
    if (pos.x() < leadingWidth() + 24) {
        m_scroll -= 10;
    } else if (pos.x() > first.viewportWidth - 24) {
        m_scroll += 10;
    }
    clampScroll();
    m_dragX = pos.x() - m_dragGrab;
    for (;;) {
        const Layout layout = computeLayout();
        const QRect mine = layout.tabs.at(m_pressed);
        const int center = m_dragX + mine.width() / 2;
        if (m_pressed > 0 && center < layout.tabs.at(m_pressed - 1).center().x()) {
            m_tabs.swapItemsAt(m_pressed, m_pressed - 1);
            --m_pressed;
        } else if (m_pressed + 1 < m_tabs.size() && center > layout.tabs.at(m_pressed + 1).center().x()) {
            m_tabs.swapItemsAt(m_pressed, m_pressed + 1);
            ++m_pressed;
        } else {
            break;
        }
    }
    update();
}

void OutputTabsBar::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (m_pressed >= 0 && !m_pressedClose && (event->buttons() & Qt::LeftButton)) {
        if (!m_dragActive && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance() && m_tabs.size() > 1) {
            m_dragActive = true;
            setCursor(Qt::ClosedHandCursor);
        }
        if (m_dragActive) {
            updateDrag(pos);
            return;
        }
    }
    const int index = indexAt(pos);
    const bool onClose = index >= 0 && computeLayout().closes.at(index).contains(pos);
    const bool onOverflow = overflowRect().contains(pos);
    const bool onToggle = toggleRect().contains(pos);
    const bool onQuickRun = quickRunRect().contains(pos);
    const bool onDocs = docsRect().contains(pos);
    const bool onOutputToggle = outputToggleRect().contains(pos);
    if (onOutputToggle != m_hoverOutputToggle) {
        m_hoverOutputToggle = onOutputToggle;
        update();
    }
    if (index != m_hover || onClose != m_hoverClose || onOverflow != m_hoverOverflow || onToggle != m_hoverToggle
        || onQuickRun != m_hoverQuickRun || onDocs != m_hoverDocs) {
        m_hoverDocs = onDocs;
        m_hoverQuickRun = onQuickRun;
        m_hover = index;
        m_hoverClose = onClose;
        m_hoverOverflow = onOverflow;
        m_hoverToggle = onToggle;
        setCursor(index >= 0 || onOverflow || onToggle || onQuickRun || onDocs || onOutputToggle ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void OutputTabsBar::leaveEvent(QEvent *event)
{
    if (m_hover != -1 || m_hoverOverflow || m_hoverToggle || m_hoverQuickRun || m_hoverDocs || m_hoverOutputToggle) {
        m_hover = -1;
        m_hoverOutputToggle = false;
        m_hoverDocs = false;
        m_hoverQuickRun = false;
        m_hoverToggle = false;
        m_hoverClose = false;
        m_hoverOverflow = false;
        update();
    }
    QWidget::leaveEvent(event);
}

void OutputTabsBar::wheelEvent(QWheelEvent *event)
{
    const QPoint delta = event->angleDelta();
    const int amount = delta.y() != 0 ? delta.y() : delta.x();
    if (amount == 0) {
        QWidget::wheelEvent(event);
        return;
    }
    const int before = m_scroll;
    m_scroll -= amount / 2;
    clampScroll();
    if (m_scroll != before) {
        update();
    }
    event->accept();
}

bool OutputTabsBar::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto *help = static_cast<QHelpEvent *>(event);
        if (toggleRect().contains(help->pos())) {
            QToolTip::showText(help->globalPos(),
                               utils::tr(m_toggleCollapsed ? QStringLiteral("output_tabs.commands.expand")
                                                           : QStringLiteral("output_tabs.commands.collapse")), this);
            return true;
        }
        if (quickRunRect().contains(help->pos())) {
            QToolTip::showText(help->globalPos(), utils::tr(QStringLiteral("output_tabs.quick_run.tooltip")), this);
            return true;
        }
        if (outputToggleRect().contains(help->pos())) {
            QToolTip::showText(help->globalPos(), utils::tr(QStringLiteral("terminal_drawer.toggle.tooltip")), this);
            return true;
        }
        if (docsRect().contains(help->pos())) {
            QToolTip::showText(help->globalPos(), utils::tr(QStringLiteral("output_tabs.docs.tooltip")), this);
            return true;
        }
        if (overflowRect().contains(help->pos())) {
            QToolTip::showText(help->globalPos(), utils::tr(QStringLiteral("output_tabs.overflow.tooltip")), this);
            return true;
        }
        const int index = indexAt(help->pos());
        if (index < 0) {
            QToolTip::hideText();
            return true;
        }
        const bool onClose = computeLayout().closes.at(index).contains(help->pos());
        QToolTip::showText(help->globalPos(),
                           onClose ? utils::tr(QStringLiteral("output_tabs.close.tooltip")) : m_tabs.at(index).tooltip, this);
        return true;
    }
    return QWidget::event(event);
}

void OutputTabsBar::showOverflowMenu()
{
    QMenu menu(this);
    auto *group = new QActionGroup(&menu);
    for (const OutputTabInfo &tab : m_tabs) {
        QPixmap dot(12, 12);
        dot.fill(Qt::transparent);
        {
            QPainter painter(&dot);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(dotColor(tab.status));
            painter.drawEllipse(QRectF(2, 2, 8, 8));
        }
        QAction *action = menu.addAction(QIcon(dot), tab.title);
        action->setCheckable(true);
        action->setChecked(tab.id == m_current);
        action->setData(tab.id);
        action->setToolTip(tab.tooltip);
        group->addAction(action);
    }
    if (QAction *chosen = menu.exec(mapToGlobal(QPoint(overflowRect().left(), height())))) {
        emit tabActivated(chosen->data().toString());
    }
}

} // namespace kai::ui
