#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace kai::ui {

// Provedor central de ícones Lucide (ISC), embarcados via Qt Resource (assets/icons/icons.qrc, prefixo
// ":/icons/lucide/"). O stroke="currentColor" dos SVGs é substituído pela cor pedida antes de renderizar com QSvgRenderer.
class LucideIcons {
public:
    // QIcon do ícone Lucide `name` recolorido para `color`, em `size` px lógicos (o DPR é aplicado internamente).
    // Cacheado por (name, color, size). QIcon nulo se o ícone não existir, para fallback do chamador.
    static QIcon icon(const QString &name, const QColor &color, int size = 24);

    // true se `name` corresponde a um SVG embarcado no resource.
    static bool has(const QString &name);
};

} // namespace kai::ui
