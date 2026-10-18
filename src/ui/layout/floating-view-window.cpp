#include "ui/layout/floating-view-window.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace kai::ui::layout {

namespace tk = kai::utils::tokens;

FloatingViewWindow::FloatingViewWindow(const QString &viewId, const QString &title, QWidget *parent)
    : AppWindowFrame(parent)
    , m_viewId(viewId)
{
    setObjectName(QStringLiteral("floatingViewWindow"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(title);
    setMinimumSize(280, 200);

    m_layout = new QVBoxLayout(contentWidget());
    m_layout->setContentsMargins(8, 2, 8, 8);
    m_layout->setSpacing(6);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->addStretch(1);
    m_pin = new QToolButton(contentWidget());
    m_pin->setObjectName(QStringLiteral("floatingPinButton"));
    m_pin->setAutoRaise(true);
    m_pin->setCheckable(true);
    m_pin->setCursor(Qt::PointingHandCursor);
    m_pin->setToolTip(utils::tr(QStringLiteral("layout.floating.pin")));
    m_dock = new QToolButton(contentWidget());
    m_dock->setObjectName(QStringLiteral("floatingDockButton"));
    m_dock->setAutoRaise(true);
    m_dock->setCursor(Qt::PointingHandCursor);
    m_dock->setToolTip(utils::tr(QStringLiteral("layout.floating.dock")));
    m_dock->setIcon(LucideIcons::icon(QStringLiteral("panel-bottom"), QColor(tk::mutedFg()), 15));
    bar->addWidget(m_pin);
    bar->addWidget(m_dock);
    m_layout->addLayout(bar);
    updatePinIcon();

    connect(m_dock, &QToolButton::clicked, this, [this]() { emit dockRequested(m_viewId); });
    connect(m_pin, &QToolButton::clicked, this, [this](bool checked) {
        setAlwaysOnTop(checked);
        emit alwaysOnTopChanged(m_viewId, checked);
    });
}

void FloatingViewWindow::updatePinIcon()
{
    m_pin->setChecked(m_alwaysOnTop);
    m_pin->setIcon(LucideIcons::icon(m_alwaysOnTop ? QStringLiteral("pin") : QStringLiteral("pin-off"),
                                     QColor(m_alwaysOnTop ? tk::accent() : tk::mutedFg()), 15));
}

void FloatingViewWindow::setViewTitle(const QString &title)
{
    setWindowTitle(title);
}

void FloatingViewWindow::setContent(QWidget *content)
{
    m_content = content;
    content->setParent(contentWidget());
    m_layout->addWidget(content, 1);
    content->show();
}

QWidget *FloatingViewWindow::takeContent(QWidget *newParent)
{
    QWidget *content = m_content;
    if (content) {
        m_layout->removeWidget(content);
        content->hide();
        content->setParent(newParent);
        m_content = nullptr;
    }
    return content;
}

void FloatingViewWindow::setAlwaysOnTop(bool onTop)
{
    if (m_alwaysOnTop == onTop) {
        return;
    }
    m_alwaysOnTop = onTop;
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
    if (wasVisible) {
        show(); // mudar os hints esconde a janela
    }
    updatePinIcon();
}

void FloatingViewWindow::closeEvent(QCloseEvent *event)
{
    // Quem decide é o host: fechar a janela fecha o painel (volta pelo menu Exibir).
    event->ignore();
    emit closeRequested(m_viewId);
}

void FloatingViewWindow::moveEvent(QMoveEvent *event)
{
    AppWindowFrame::moveEvent(event);
    emit geometryEdited(m_viewId, geometry());
}

void FloatingViewWindow::resizeEvent(QResizeEvent *event)
{
    AppWindowFrame::resizeEvent(event);
    emit geometryEdited(m_viewId, geometry());
}

} // namespace kai::ui::layout
