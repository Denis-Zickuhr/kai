#include "ui/features/output/http-wait-view.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QTransform>

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/duration-format.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
constexpr int kIconSize = 32;
}

HttpWaitView::HttpWaitView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("httpWaitView"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    layout->setSpacing(tk::space(2));
    layout->addStretch(1);

    m_icon = new QLabel(this);
    m_icon->setFixedSize(kIconSize, kIconSize);
    layout->addWidget(m_icon, 0, Qt::AlignHCenter);

    m_title = new QLabel(utils::tr(QStringLiteral("output.http.waiting")), this);
    m_title->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_title, 0, Qt::AlignHCenter);

    m_elapsed = new QLabel(this);
    m_elapsed->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_elapsed, 0, Qt::AlignHCenter);

    m_chip = new QLabel(this);
    layout->addWidget(m_chip, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    m_timer = new QTimer(this);
    m_timer->setInterval(125);
    connect(m_timer, &QTimer::timeout, this, &HttpWaitView::tick);

    hide();
}

void HttpWaitView::start(const QDateTime &startedAt, int timeoutSec)
{
    m_startedAt = startedAt.isValid() ? startedAt : QDateTime::currentDateTime();
    m_timeoutSec = timeoutSec;
    m_active = true;
    applyTheme();
    tick();
    m_timer->start();
    show();
}

void HttpWaitView::stop()
{
    m_active = false;
    m_timer->stop();
    hide();
}

void HttpWaitView::tick()
{
    ++m_frame;
    const qint64 elapsedMs = m_startedAt.msecsTo(QDateTime::currentDateTime());
    m_elapsed->setText(utils::formatShortDuration(qMax<qint64>(0, elapsedMs)));

    // O ícone gira em passos de 45 graus (sem animação própria).
    const QPixmap base = LucideIcons::icon(QStringLiteral("loader"), QColor(m_timeoutSec > 0 ? tk::infoFg() : tk::warningFg()), kIconSize)
                             .pixmap(kIconSize, kIconSize);
    m_icon->setPixmap(base.transformed(QTransform().rotate((m_frame % 8) * 45.0), Qt::SmoothTransformation)
                          .scaled(kIconSize, kIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if (m_timeoutSec > 0) {
        const qint64 remainingSec = qMax<qint64>(0, m_timeoutSec - elapsedMs / 1000);
        m_chip->setText(utils::tr(QStringLiteral("output.http.timeout_left")).arg(remainingSec));
    } else {
        m_chip->setText(utils::tr(QStringLiteral("output.http.no_timeout")));
    }
}

void HttpWaitView::setBottomRadius(int radius)
{
    m_bottomRadius = radius;
    applyTheme();
}

void HttpWaitView::applyTheme()
{
    const QString tone = m_timeoutSec > 0 ? tk::infoFg() : tk::warningFg();
    setStyleSheet(QStringLiteral(
        "QWidget#httpWaitView { background-color: %1; border: none; border-top-left-radius: 0px; border-top-right-radius: 0px;"
        " border-bottom-left-radius: %2px; border-bottom-right-radius: %2px; }"
        "QWidget#httpWaitView QLabel { background: transparent; border: none; }")
        .arg(tk::terminalBg()).arg(m_bottomRadius));
    m_title->setStyleSheet(QStringLiteral("color: %1; font-weight: 600; font-size: %2pt;")
                               .arg(tk::fg()).arg(tk::fontSizeSmallPt() + 3));
    m_elapsed->setStyleSheet(QStringLiteral("color: %1; font-family: %2; font-size: %3pt;")
                                 .arg(tk::mutedFg(), tk::monoFamily()).arg(tk::fontSizeSmallPt() + 3));
    QColor chipBg(tone);
    chipBg.setAlphaF(0.18f);
    m_chip->setStyleSheet(QStringLiteral(
        "color: %1; background-color: rgba(%2, %3, %4, %5); border: 1px solid %1; border-radius: %6px;"
        " padding: %7px %8px; font-weight: 600;")
        .arg(tone).arg(chipBg.red()).arg(chipBg.green()).arg(chipBg.blue()).arg(chipBg.alpha())
        .arg(tk::radiusSm()).arg(tk::space(1)).arg(tk::space(2)));
}

} // namespace kai::ui
