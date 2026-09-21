#include "ui/shared/panel-frame.h"

#include "utils/design-tokens.h"

#include <QEvent>
#include <QPainter>

namespace kai::ui {
namespace tk = kai::utils::tokens;

PanelFrame::PanelFrame(QWidget *host)
    : QWidget(host)
    , m_host(host)
{
    setObjectName(QStringLiteral("panelFrameOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // Cinto e suspensório: mesmo que algum QSS global mire QWidget, esta camada
    // NUNCA pinta fundo (só o anel da borda, no paintEvent).
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    host->installEventFilter(this);
    followHost();
}

void PanelFrame::followHost()
{
    setGeometry(m_host->rect());
    // Sempre por cima dos filhos (inclusive os adicionados depois).
    if (m_host->children().isEmpty() || m_host->children().last() != this) {
        raise();
    }
}

bool PanelFrame::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::LayoutRequest:
        case QEvent::ChildAdded:
            followHost();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PanelFrame::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // Tema/cantos/estilo mudaram: os tokens são lidos de novo ao repintar.
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        update();
    }
}

void PanelFrame::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(tk::borderColor()));
    pen.setWidth(1);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const qreal radius = tk::radiusMd();
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

} // namespace kai::ui
