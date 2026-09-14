#include "ui/shared/status-line.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QToolButton>

namespace kai::ui {
namespace tk = utils::tokens;

StatusLine::StatusLine(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("statusLine"));

    auto *layout = new QHBoxLayout(this);
    // Mesmas margens laterais do conteúdo (16px), para alinhar com os painéis.
    layout->setContentsMargins(16, 0, 16, tk::space(2));

    // Ícone e texto separados: no QToolButton o ícone encostava no texto.
    layout->setSpacing(tk::space(1));
    m_runningIcon = new QLabel(this);
    m_runningIcon->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_runningIcon, 0, Qt::AlignVCenter);

    m_runningButton = new QToolButton(this);
    m_runningButton->setObjectName(QStringLiteral("statusLineRunning"));
    m_runningButton->setAutoRaise(true);
    m_runningButton->setCursor(Qt::PointingHandCursor);
    m_runningButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_runningButton->setToolTip(utils::tr(QStringLiteral("statusline.running.tooltip")));
    connect(m_runningButton, &QToolButton::clicked, this, &StatusLine::runningClicked);
    layout->addWidget(m_runningButton, 0, Qt::AlignVCenter);

    // Notificações: junto dos comandos em execução. Caixa própria (ícone +
    // texto) em vez de QToolButton, para o destaque cobrir os dois e o ícone
    // não encostar no texto.
    layout->addSpacing(tk::space(3));
    m_notificationsBox = new QWidget(this);
    m_notificationsBox->setObjectName(QStringLiteral("statusLineNotifications"));
    m_notificationsBox->setAttribute(Qt::WA_StyledBackground, true);
    m_notificationsBox->setCursor(Qt::PointingHandCursor);
    m_notificationsBox->installEventFilter(this);
    auto *notificationsLayout = new QHBoxLayout(m_notificationsBox);
    notificationsLayout->setContentsMargins(tk::space(2), 1, tk::space(2), 1);
    notificationsLayout->setSpacing(tk::space(1));
    m_notificationsIcon = new QLabel(m_notificationsBox);
    m_notificationsIcon->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    notificationsLayout->addWidget(m_notificationsIcon, 0, Qt::AlignVCenter);
    m_notificationsText = new QLabel(m_notificationsBox);
    m_notificationsText->setObjectName(QStringLiteral("statusLineNotificationsText"));
    m_notificationsText->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    notificationsLayout->addWidget(m_notificationsText, 0, Qt::AlignVCenter);
    layout->addWidget(m_notificationsBox, 0, Qt::AlignVCenter);

    layout->addStretch(1);

    m_resultIcon = new QLabel(this);
    m_resultIcon->setVisible(false);
    layout->addWidget(m_resultIcon, 0, Qt::AlignVCenter);

    m_resultText = new QLabel(this);
    m_resultText->setObjectName(QStringLiteral("statusLineResult"));
    m_resultText->setVisible(false);
    layout->addWidget(m_resultText, 0, Qt::AlignVCenter);

    applyTheme();
    refreshTexts();
}

void StatusLine::setRunningCount(int count)
{
    count = qMax(0, count);
    if (count == m_runningCount) {
        return;
    }
    m_runningCount = count;
    refreshTexts();
}

void StatusLine::setUnreadNotifications(int count)
{
    count = qMax(0, count);
    if (count == m_unreadNotifications) {
        return;
    }
    m_unreadNotifications = count;
    refreshTexts();
}

bool StatusLine::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_notificationsBox && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && m_notificationsBox->rect().contains(mouse->position().toPoint())) {
            emit notificationsRequested();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void StatusLine::setLastResult(const QString &name, bool success, const QDateTime &when)
{
    m_lastName = name;
    m_lastSuccess = success;
    m_lastWhen = when;
    refreshTexts();
}

void StatusLine::applyTheme()
{
    const int pt = qMax(7, tk::fontSizePt() - 1);
    setStyleSheet(QStringLiteral(
        "QWidget#statusLine { background: transparent; border: none; }"
        "QToolButton#statusLineRunning { background: transparent; border: none; padding: 1px 2px;"
        " color: %1; font-size: %2pt; }"
        "QToolButton#statusLineRunning:hover { color: %3; }"
        "QLabel#statusLineResult { background: transparent; color: %1; font-size: %2pt; }"
        // Notificações: discreto quando tudo lido; com não lidas vira um pill
        // na cor primária (raio pelos tokens de canto).
        "QWidget#statusLineNotifications { background: transparent; border: none; border-radius: %4px; }"
        "QLabel#statusLineNotificationsText { background: transparent; color: %1; font-size: %2pt; }"
        "QWidget#statusLineNotifications:hover QLabel#statusLineNotificationsText { color: %3; }"
        "QWidget#statusLineNotifications[unread=\"true\"] { background-color: %5; }"
        "QWidget#statusLineNotifications[unread=\"true\"] QLabel#statusLineNotificationsText"
        " { color: %6; font-weight: 700; }")
        .arg(tk::mutedFg()).arg(pt).arg(tk::fg()).arg(tk::radiusSm())
        .arg(tk::buttonColor(), tk::buttonFg()));
    refreshTexts();
}

void StatusLine::refreshTexts()
{
    const bool running = m_runningCount > 0;
    m_runningButton->setText(running
        ? utils::tr(QStringLiteral("statusline.running.count")).arg(m_runningCount)
        : utils::tr(QStringLiteral("statusline.running.none")));
    m_runningIcon->setPixmap(LucideIcons::icon(
        running ? QStringLiteral("loader") : QStringLiteral("activity"),
        QColor(running ? tk::infoFg() : tk::mutedFg()), 12).pixmap(12, 12));

    const bool unread = m_unreadNotifications > 0;
    m_notificationsText->setText(!unread
        ? utils::tr(QStringLiteral("statusline.notifications.none"))
        : m_unreadNotifications == 1
            ? utils::tr(QStringLiteral("statusline.notifications.unread_one"))
            : utils::tr(QStringLiteral("statusline.notifications.unread_other")).arg(m_unreadNotifications));
    m_notificationsIcon->setPixmap(LucideIcons::icon(QStringLiteral("bell"),
        unread ? QColor(tk::buttonFg()) : QColor(tk::mutedFg()), 12).pixmap(12, 12));
    const QString notificationsTip = unread
        ? utils::tr(QStringLiteral("statusline.notifications.tooltip_unread")).arg(m_unreadNotifications)
        : utils::tr(QStringLiteral("statusline.notifications.tooltip"));
    m_notificationsBox->setToolTip(notificationsTip);
    if (m_notificationsBox->property("unread").toBool() != unread) {
        m_notificationsBox->setProperty("unread", unread);
        m_notificationsBox->style()->unpolish(m_notificationsBox);
        m_notificationsBox->style()->polish(m_notificationsBox);
    }

    const bool hasResult = !m_lastName.isEmpty();
    m_resultIcon->setVisible(hasResult);
    m_resultText->setVisible(hasResult);
    if (hasResult) {
        m_resultIcon->setPixmap(LucideIcons::icon(
            m_lastSuccess ? QStringLiteral("circle-check") : QStringLiteral("circle-x"),
            QColor(m_lastSuccess ? tk::successFg() : tk::errorFg()), 12).pixmap(12, 12));
        m_resultText->setText(QStringLiteral("%1 · %2").arg(m_lastName, m_lastWhen.time().toString(QStringLiteral("HH:mm:ss"))));
        const QString tip = utils::tr(m_lastSuccess ? QStringLiteral("statusline.last.success")
                                                    : QStringLiteral("statusline.last.failed"));
        m_resultIcon->setToolTip(tip);
        m_resultText->setToolTip(tip);
    }
}

} // namespace kai::ui
