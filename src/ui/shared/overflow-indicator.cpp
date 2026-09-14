#include "ui/shared/overflow-indicator.h"

#include "utils/design-tokens.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTabBar>
#include <QToolButton>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {
constexpr int kThickness = 6;
constexpr int kMinThumb = 28;
constexpr qreal kTrackAlpha = 0.15;
constexpr qreal kThumbAlpha = 0.6;
constexpr qreal kThumbHoverAlpha = 0.85;
} // namespace

OverflowIndicator::OverflowIndicator(QWidget *host, Metrics metrics, ScrollTo scrollTo)
    : QWidget(host)
    , m_host(host)
    , m_metrics(std::move(metrics))
    , m_scrollTo(std::move(scrollTo))
{
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(false);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    host->installEventFilter(this);
    reposition();
    hide(); // só aparece quando há overflow (ver refresh)
}

int OverflowIndicator::thickness()
{
    return kThickness;
}

OverflowIndicator::Metrics OverflowIndicator::forTabBar(QTabBar *bar)
{
    return [bar](int &offset, int &viewport, int &content) {
        const int count = bar->count();
        if (count <= 0) {
            return false;
        }
        // tabRect() já vem em coordenadas do widget, com a rolagem aplicada:
        // a primeira aba fica em x = -deslocamento quando rolada.
        const int left = bar->tabRect(0).left();
        content = bar->tabRect(count - 1).right() - left + 1;
        viewport = bar->width();
        const auto buttons = bar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
        for (const QToolButton *button : buttons) {
            if (button->isVisible()) {
                viewport -= button->width();
            }
        }
        offset = qMax(0, -left);
        return true;
    };
}

OverflowIndicator::ScrollTo OverflowIndicator::scrollToForTabBar(QTabBar *bar)
{
    // O QTabBar não expõe o deslocamento; a rolagem pública são os dois botões
    // (esquerda e direita, nessa ordem de criação), que andam uma aba por
    // clique. Cada evento dá no máximo UM passo, na direção do alvo.
    return [bar](int target) {
        const auto buttons = bar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
        if (buttons.size() < 2 || bar->count() <= 0) {
            return;
        }
        QToolButton *left = buttons.at(0);
        QToolButton *right = buttons.at(1);
        const int offset = qMax(0, -bar->tabRect(0).left());
        const int half = qMax(1, bar->tabRect(0).width() / 2);
        if (target > offset + half && right->isEnabled()) {
            right->click();
        } else if (target < offset - half && left->isEnabled()) {
            left->click();
        }
    };
}

OverflowIndicator::Metrics OverflowIndicator::forScrollBar(QScrollBar *bar)
{
    return [bar](int &offset, int &viewport, int &content) {
        // Mesmo com a barra escondida (ScrollBarAlwaysOff) o intervalo é mantido.
        viewport = bar->pageStep();
        content = bar->maximum() - bar->minimum() + bar->pageStep();
        offset = bar->value() - bar->minimum();
        return true;
    };
}

OverflowIndicator::ScrollTo OverflowIndicator::scrollToForScrollBar(QScrollBar *bar)
{
    return [bar](int offset) { bar->setValue(bar->minimum() + offset); };
}

void OverflowIndicator::setInsets(int left, int right)
{
    m_insetLeft = left;
    m_insetRight = right;
    reposition();
}

void OverflowIndicator::setBottomInset(int inset)
{
    m_bottomInset = inset;
    reposition();
}

void OverflowIndicator::setAnchor(QWidget *anchor)
{
    m_anchor = anchor;
    if (anchor) {
        anchor->installEventFilter(this);
    }
    reposition();
}

bool OverflowIndicator::overflowing() const
{
    int offset = 0;
    int viewport = 0;
    int content = 0;
    return m_metrics && m_metrics(offset, viewport, content) && viewport > 0 && content > viewport;
}

void OverflowIndicator::setSuppressed(bool suppressed)
{
    if (m_suppressed == suppressed) {
        return;
    }
    m_suppressed = suppressed;
    refresh();
}

void OverflowIndicator::refresh()
{
    reposition();
    const bool want = !m_suppressed && overflowing();
    if (isHidden() == want) {
        setVisible(want);
        if (want) {
            raise();
        }
    }
    update();
}

void OverflowIndicator::reposition()
{
    if (m_anchor) {
        // Abaixo da âncora, com a largura dela (coordenadas do hospedeiro).
        setGeometry(m_anchor->x() + m_insetLeft, m_anchor->y() + m_anchor->height() + m_bottomInset,
                    qMax(0, m_anchor->width() - m_insetLeft - m_insetRight), kThickness);
        return;
    }
    const int y = qMax(0, m_host->height() - m_bottomInset - kThickness);
    setGeometry(m_insetLeft, y, qMax(0, m_host->width() - m_insetLeft - m_insetRight), kThickness);
}

bool OverflowIndicator::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host || watched == m_anchor) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::LayoutRequest:
        case QEvent::ChildAdded:
        case QEvent::Paint:
            // Paint: a rolagem das abas repinta o hospedeiro e a alça acompanha.
            refresh();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool OverflowIndicator::thumbLayout(ThumbLayout &out) const
{
    int offset = 0;
    int viewport = 0;
    int content = 0;
    if (!m_metrics || !m_metrics(offset, viewport, content) || viewport <= 0 || content <= viewport) {
        return false;
    }
    const int total = width();
    out.width = qBound(qMin(kMinThumb, total),
                       static_cast<int>(static_cast<qint64>(total) * viewport / content), total);
    out.maxOffset = content - viewport;
    out.x = out.maxOffset > 0
        ? static_cast<int>(static_cast<qint64>(total - out.width) * qBound(0, offset, out.maxOffset) / out.maxOffset)
        : 0;
    return true;
}

void OverflowIndicator::paintEvent(QPaintEvent *)
{
    ThumbLayout thumb;
    if (!thumbLayout(thumb)) {
        return;
    }

    // Raio pelo token de cantos, limitado à metade da espessura da faixa.
    const qreal radius = qMin(tk::radiusSm(), height() / 2);

    QColor track(tk::accent());
    track.setAlphaF(kTrackAlpha);
    QColor thumbColor(tk::accent());
    thumbColor.setAlphaF(m_dragging ? kThumbHoverAlpha : kThumbAlpha);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(track);
    painter.drawRoundedRect(QRectF(rect()), radius, radius);
    painter.setBrush(thumbColor);
    painter.drawRoundedRect(QRectF(thumb.x, 0, thumb.width, height()), radius, radius);
}

void OverflowIndicator::dragTo(int x)
{
    ThumbLayout thumb;
    if (!m_scrollTo || !thumbLayout(thumb)) {
        return;
    }
    const int span = width() - thumb.width;
    if (span <= 0) {
        return;
    }
    const int wantX = qBound(0, x - m_grabOffset, span);
    m_scrollTo(static_cast<int>(static_cast<qint64>(wantX) * thumb.maxOffset / span));
    update();
}

void OverflowIndicator::mousePressEvent(QMouseEvent *event)
{
    ThumbLayout thumb;
    if (event->button() != Qt::LeftButton || !thumbLayout(thumb)) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int x = event->position().toPoint().x();
    m_dragging = true;
    if (x >= thumb.x && x < thumb.x + thumb.width) {
        m_grabOffset = x - thumb.x; // agarrou a alça: mantém o ponto pego
    } else {
        m_grabOffset = thumb.width / 2; // clicou na trilha: centraliza a alça ali
        dragTo(x);
    }
    update();
}

void OverflowIndicator::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        dragTo(event->position().toPoint().x());
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void OverflowIndicator::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        update();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

} // namespace kai::ui
