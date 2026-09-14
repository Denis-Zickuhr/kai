#include "ui/shared/app-window-frame.h"

#include "ui/shared/native-window-corners.h"
#include "ui/shared/window-control-buttons.h"
#include "utils/asset-paths.h"
#include "utils/design-tokens.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPixmap>
#include <QRegion>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace kai::ui {

namespace tk = kai::utils::tokens;

namespace {
// Faixa da borda (em px) onde o clique inicia o redimensionamento nativo.
constexpr int kResizeMargin = 6;

} // namespace

// A barra de título: arrastar move a janela; clique duplo maximiza/restaura.
class AppWindowFrame::TitleBar : public QWidget {
public:
    explicit TitleBar(AppWindowFrame *frame)
        : QWidget(frame)
        , m_frame(frame)
    {
        setObjectName(QStringLiteral("appWindowTitleBar"));
        setAttribute(Qt::WA_StyledBackground, true);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !m_frame->isMaximized()) {
            if (QWindow *handle = m_frame->windowHandle()) {
                handle->startSystemMove();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_frame->toggleMaximized();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

private:
    AppWindowFrame *m_frame;
};

AppWindowFrame::AppWindowFrame(QWidget *parent)
    : QWidget(parent)
{
    m_systemFrame = systemFrameByDefault();
    setWindowFlags(frameFlags());
    setObjectName(QStringLiteral("rootContainer"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMouseTracking(true);
    setMinimumSize(480, 320);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_titleBar = new TitleBar(this);
    auto *bar = new QHBoxLayout(m_titleBar);
    bar->setContentsMargins(6, 2, 6, 0);
    bar->setSpacing(6);
    m_logo = new QLabel(m_titleBar);
    const QPixmap logo = loadAppLogoPixmap();
    if (!logo.isNull()) {
        m_logo->setPixmap(logo.scaledToHeight(20, Qt::SmoothTransformation));
    } else {
        m_logo->setText(QStringLiteral("Kai"));
        m_logo->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 11pt;"));
    }
    m_title = new QLabel(m_titleBar);
    m_title->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    m_minimize = makeWindowControlButton(m_titleBar, WindowControl::Minimize);
    m_maximize = makeWindowControlButton(m_titleBar, WindowControl::Maximize);
    m_close = makeWindowControlButton(m_titleBar, WindowControl::Close);
    bar->addWidget(m_logo);
    bar->addWidget(m_title);
    bar->addStretch(1);
    bar->addWidget(m_minimize, 0, Qt::AlignVCenter);
    bar->addWidget(m_maximize, 0, Qt::AlignVCenter);
    bar->addWidget(m_close, 0, Qt::AlignVCenter);
    root->addWidget(m_titleBar);
    if (m_systemFrame) {
        m_titleBar->hide(); // a barra de título é a nativa
    }

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("appWindowContent"));
    m_content->setStyleSheet(QStringLiteral("QWidget#appWindowContent { background: transparent; }"));
    root->addWidget(m_content, 1);

    connect(m_minimize, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(m_maximize, &QToolButton::clicked, this, &AppWindowFrame::toggleMaximized);
    connect(m_close, &QToolButton::clicked, this, &QWidget::close);
    syncTitle();
}

AppWindowFrame::~AppWindowFrame()
{
    clearResizeCursor();
}

bool AppWindowFrame::systemFrameByDefault()
{
#if defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

// Os hints são explícitos: o FramelessWindowHint faz o Qt tratar os flags como "customizados" e não aplicar os padrões (no
// Windows a janela nascia sem WS_MINIMIZEBOX/WS_SYSMENU e o clique na barra de tarefas era ignorado).
Qt::WindowFlags AppWindowFrame::frameFlags() const
{
    Qt::WindowFlags flags = Qt::Window | Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint
                            | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint;
    flags |= m_systemFrame ? Qt::WindowTitleHint : Qt::FramelessWindowHint;
    return flags;
}

void AppWindowFrame::setUseSystemFrame(bool on)
{
    if (m_systemFrame == on) {
        return;
    }
    m_systemFrame = on;
    const bool wasVisible = isVisible();
    const Qt::WindowFlags onTop = windowFlags() & Qt::WindowStaysOnTopHint;
    setWindowFlags(frameFlags() | onTop); // mudar os hints esconde a janela
    m_titleBar->setVisible(!on);
    if (on) {
        qApp->removeEventFilter(this); // as bordas passam a ser do sistema
        clearResizeCursor();
    }
    m_nativeCornerStyle = -1;
    if (wasVisible) {
        show();
    }
    updateShape();
}

QString AppWindowFrame::titleText() const
{
    return m_title->text();
}

void AppWindowFrame::syncTitle()
{
    m_title->setText(windowTitle());
}

void AppWindowFrame::toggleMaximized()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void AppWindowFrame::refreshAppearance()
{
    refreshWindowControlIcon(m_minimize, WindowControl::Minimize);
    refreshWindowControlIcon(m_maximize, WindowControl::Maximize, isMaximized());
    refreshWindowControlIcon(m_close, WindowControl::Close);
    m_title->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    m_nativeCornerStyle = -1; // reaplica os cantos nativos (estilo e cor da borda)
    updateShape();
}

// Mesma regra da janela principal: maximizada (ou com cantos nativos do DWM) fica reta; senão o contêiner pinta o raio
// do token e uma máscara recorta onde a transparência da janela não é honrada.
void AppWindowFrame::updateShape()
{
    const bool fillsScreen = isMaximized() || isFullScreen();
    const int radius = tk::radiusMd();

#if defined(Q_OS_WIN)
    const int cornerStyle = tk::effects().cornerStyle;
    if (isVisible() && m_nativeCornerStyle != cornerStyle) {
        if (m_systemFrame) {
            applyNativeTitleBarTheme(this);
        } else {
            m_nativeCorners = applyNativeWindowCorners(this, cornerStyle);
        }
        m_nativeCornerStyle = cornerStyle;
    }
#endif

    // Moldura do sistema: cantos, borda e sombra são dela; o contêiner só pinta o fundo, reto.
    const bool flat = m_systemFrame || fillsScreen || m_nativeCorners;
    if (property("kaiMaximized").toBool() != flat) {
        setProperty("kaiMaximized", flat);
        style()->unpolish(this);
        style()->polish(this);
    }
    if (flat || radius <= 0) {
        clearMask();
        return;
    }
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), radius, radius);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

bool AppWindowFrame::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::WindowTitleChange:
        syncTitle();
        break;
    case QEvent::WindowStateChange:
        refreshWindowControlIcon(m_maximize, WindowControl::Maximize, isMaximized());
        updateShape();
        break;
    case QEvent::WindowDeactivate:
    case QEvent::Leave:
        // O cursor de borda é do app inteiro (override): perder a ativação ou o ponteiro sair da janela com ele ligado o
        // deixava preso em qualquer outra janela do Kai.
        clearResizeCursor();
        break;
    default:
        break;
    }
    return QWidget::event(event);
}

void AppWindowFrame::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateShape();
}

void AppWindowFrame::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Filtro global: os filhos cobrem as bordas, então só vemos o clique aqui. Com a moldura do sistema as bordas são dela.
    if (!m_systemFrame) {
        qApp->installEventFilter(this);
    }
    updateShape();
}

void AppWindowFrame::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    qApp->removeEventFilter(this);
    clearResizeCursor();
}

Qt::Edges AppWindowFrame::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    if (pos.x() < 0 || pos.x() >= width() || pos.y() < 0 || pos.y() >= height()) {
        return edges;
    }
    if (pos.x() <= kResizeMargin) edges |= Qt::LeftEdge;
    if (pos.x() >= width() - kResizeMargin) edges |= Qt::RightEdge;
    if (pos.y() <= kResizeMargin) edges |= Qt::TopEdge;
    if (pos.y() >= height() - kResizeMargin) edges |= Qt::BottomEdge;
    return edges;
}

void AppWindowFrame::clearResizeCursor()
{
    if (m_resizeCursorActive) {
        QApplication::restoreOverrideCursor();
        m_resizeCursorActive = false;
    }
}

bool AppWindowFrame::eventFilter(QObject *watched, QEvent *event)
{
    if (m_systemFrame) {
        return QWidget::eventFilter(watched, event);
    }
    if (m_resizeCursorActive && event->type() == QEvent::MouseMove) {
        // O ponteiro foi para um widget de outra janela (ou esta não está mais ativa): o cursor de borda não vale mais.
        const auto *under = qobject_cast<QWidget *>(watched);
        if (!under || under->window() != this || !isActiveWindow()) {
            clearResizeCursor();
        }
    }
    if (isMaximized() || isFullScreen() || !isActiveWindow()
        || (event->type() != QEvent::MouseMove && event->type() != QEvent::MouseButtonPress)) {
        return QWidget::eventFilter(watched, event);
    }
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != this) {
        return QWidget::eventFilter(watched, event);
    }
    auto *mouse = static_cast<QMouseEvent *>(event);
    const Qt::Edges edges = edgesAt(mapFromGlobal(mouse->globalPosition().toPoint()));
    if (edges == Qt::Edges()) {
        if (event->type() == QEvent::MouseMove) {
            clearResizeCursor();
        }
        return QWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseMove && mouse->buttons() == Qt::NoButton) {
        Qt::CursorShape shape = Qt::ArrowCursor;
        if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) {
            shape = Qt::SizeFDiagCursor;
        } else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)) {
            shape = Qt::SizeBDiagCursor;
        } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
            shape = Qt::SizeHorCursor;
        } else {
            shape = Qt::SizeVerCursor;
        }
        clearResizeCursor();
        QApplication::setOverrideCursor(shape);
        m_resizeCursorActive = true;
    } else if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
        if (QWindow *handle = windowHandle()) {
            clearResizeCursor();
            handle->startSystemResize(edges);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace kai::ui
