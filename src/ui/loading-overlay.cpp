#include "ui/loading-overlay.h"
#include "utils/design-tokens.h"

#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>
#include <QApplication>

namespace kai::ui {

LoadingOverlay::LoadingOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false); // bloqueia cliques
    setAttribute(Qt::WA_StyledBackground, true);
    hide();

    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    // Espaço reservado no topo para o spinner (desenhado no paintEvent).
    layout->addStretch();
    m_messageLabel = new QLabel(this);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(utils::tokens::fg()));
    layout->addSpacing(48); // deixa espaço para o spinner acima da mensagem
    layout->addWidget(m_messageLabel);
    layout->addStretch();

    m_animationTimer = new QTimer(this);
    m_animationTimer->setInterval(60);
    connect(m_animationTimer, &QTimer::timeout, this, [this]() {
        m_angle = (m_angle + 30) % 360;
        update();
    });

    if (parent) {
        parent->installEventFilter(this);
    }
}

void LoadingOverlay::start(const QString &message)
{
    setMessage(message);
    reposition();
    raise();
    show();
    if (!m_animationTimer->isActive()) {
        m_animationTimer->start();
    }
}

void LoadingOverlay::stop()
{
    m_animationTimer->stop();
    hide();
}

void LoadingOverlay::setMessage(const QString &message)
{
    if (m_messageLabel) {
        m_messageLabel->setText(message.isEmpty()
            ? QStringLiteral("Carregando...") : message);
    }
}

void LoadingOverlay::reposition()
{
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
}

bool LoadingOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}

void LoadingOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Véu semitransparente.
    painter.fillRect(rect(), QColor(20, 21, 28, 180));

    // Spinner: 12 traços com opacidade decrescente, girando.
    const QPointF center(width() / 2.0, height() / 2.0 - 28.0);
    const qreal radius = 16.0;
    painter.translate(center);
    painter.rotate(m_angle);
    const int lines = 12;
    for (int i = 0; i < lines; ++i) {
        const qreal opacity = static_cast<qreal>(i + 1) / lines;
        QColor c(139, 233, 253); // ciano do tema
        c.setAlphaF(opacity);
        QPen pen(c);
        pen.setWidth(3);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(QPointF(0, radius * 0.55), QPointF(0, radius));
        painter.rotate(360.0 / lines);
    }
}

LoadingScope::LoadingScope(LoadingOverlay *overlay, const QString &message)
    : m_overlay(overlay)
{
    if (m_overlay) {
        m_overlay->start(message);
        // Processa eventos pendentes para o overlay pintar antes do
        // trabalho síncrono que virá em seguida.
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
}

LoadingScope::~LoadingScope()
{
    if (m_overlay) {
        m_overlay->stop();
    }
}

} // namespace kai::ui
