#include "ui/shared/tab-strip-background.h"

#include "ui/shared/panel-metrics.h"
#include "utils/design-tokens.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>

namespace kai::ui {
namespace tk = kai::utils::tokens;

TabStripBackground::TabStripBackground(QWidget *host, QWidget *bar)
    : QWidget(host)
    , m_host(host)
    , m_bar(bar)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    host->installEventFilter(this);
    bar->installEventFilter(this);
    reposition();
}

void TabStripBackground::reposition()
{
    setGeometry(0, m_bar->y(), m_host->width(), m_bar->height());
    lower(); // atrás da barra de abas e do conteúdo
}

bool TabStripBackground::touchesFrameTop() const
{
    // A faixa só precisa de cantos de cima arredondados quando está colada no
    // topo da moldura do painel (sem barra de ações acima dela).
    for (const QWidget *ancestor = m_host; ancestor; ancestor = ancestor->parentWidget()) {
        if (ancestor->objectName() == QLatin1String("panelCard")) {
            return m_host->mapTo(ancestor, QPoint(0, m_bar->y())).y() <= panelFrameInset();
        }
    }
    return false;
}

bool TabStripBackground::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host || watched == m_bar) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::LayoutRequest:
        case QEvent::ChildAdded:
            reposition();
            update();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TabStripBackground::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        update();
    }
}

void TabStripBackground::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Levemente MAIS ESCURA que o fundo padrão (pedido do usuário): contraste
    // sutil com a lista, sem destoar do tema.
    const QColor color = QColor(tk::bg()).darker(112);
    if (!touchesFrameTop()) {
        painter.fillRect(rect(), color);
        return;
    }
    // Cantos de cima concêntricos à moldura; os de baixo ficam retos (o retângulo
    // arredondado é estendido para baixo e cortado pela área da faixa).
    const qreal radius = panelInnerRadius();
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0, 0, 0, radius), radius, radius);
    painter.setClipRect(rect());
    painter.fillPath(path, color);
}

} // namespace kai::ui
