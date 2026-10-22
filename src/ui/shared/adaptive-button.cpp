#include "ui/shared/adaptive-button.h"

#include <QStyleOptionButton>
#include <QStylePainter>

namespace kai::ui {

AdaptiveButton::AdaptiveButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QSize AdaptiveButton::minimumSizeHint() const
{
    const QSize full = sizeHint();
    if (icon().isNull()) {
        return full;
    }
    QStyleOptionButton opt;
    initStyleOption(&opt);
    opt.text.clear();
    QSize iconOnly = style()->sizeFromContents(QStyle::CT_PushButton, &opt, opt.iconSize, this);
    return iconOnly.expandedTo(QSize(0, full.height())).boundedTo(full);
}

void AdaptiveButton::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionButton opt;
    initStyleOption(&opt);
    if (m_collapsed) {
        opt.text.clear();
    }
    painter.drawControl(QStyle::CE_PushButton, opt);
}

void AdaptiveButton::resizeEvent(QResizeEvent *event)
{
    QPushButton::resizeEvent(event);
    updateCollapsed();
}

void AdaptiveButton::updateCollapsed()
{
    const bool collapse = !icon().isNull() && !text().trimmed().isEmpty() && width() < sizeHint().width();
    if (collapse == m_collapsed) {
        return;
    }
    m_collapsed = collapse;
    if (collapse) {
        m_ownTip = toolTip();
        if (m_ownTip.isEmpty()) {
            setToolTip(text().trimmed());
        }
    } else if (m_ownTip.isEmpty()) {
        setToolTip(QString());
    }
    update();
}

} // namespace kai::ui
