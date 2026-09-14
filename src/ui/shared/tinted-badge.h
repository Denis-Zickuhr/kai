#pragma once

#include "ui/shared/lucide-icons.h"

#include <QColor>
#include <QPainter>
#include <QPixmap>
#include <QString>

namespace kai::ui {

// Selo redondo: círculo tingido (cor com pouca opacidade) com o ícone Lucide
// no centro. Usado nas listas (notificações, processos) para marcar o tipo ou
// o estado de cada linha.
inline QPixmap tintedBadgePixmap(const QString &iconName, const QColor &color, int diameter)
{
    const qreal dpr = 2.0;
    QPixmap pixmap(QSize(diameter, diameter) * dpr);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor fill = color;
    fill.setAlpha(40);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawEllipse(QRectF(0, 0, diameter, diameter));
    const int iconSize = diameter / 2 + 2;
    const QPixmap glyph = LucideIcons::icon(iconName, color, iconSize).pixmap(iconSize, iconSize);
    painter.drawPixmap((diameter - iconSize) / 2, (diameter - iconSize) / 2, glyph);
    return pixmap;
}

} // namespace kai::ui
