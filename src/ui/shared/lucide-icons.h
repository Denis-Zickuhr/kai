#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace kai::ui {

// Provedor central de ícones do Lucide (https://lucide.dev, licença ISC).
// Substitui os ícones handmade desenhados via QPainter espalhados pela UI
// (IconPickerWidget, ActionSidebar, TopUtilityBar, badges de status,
// botões de documento). Os SVGs são embarcados no binário via Qt Resource
// (assets/icons/icons.qrc, prefixo ":/icons/lucide/") — zero dependência
// de arquivos de sistema ou fontes de ícone, mantendo a portabilidade que
// motivou os ícones handmade originais.
//
// Todo SVG do Lucide usa stroke="currentColor"; a recoloração é feita
// substituindo esse token pela cor desejada ANTES de renderizar com
// QSvgRenderer. Assim, um mesmo ícone segue a cor do tema (accent) ou uma
// cor semântica fixa (verde/amarelo/vermelho) conforme o chamador.
class LucideIcons {
public:
    // Retorna um QIcon do ícone Lucide `name` (ex: "folder", "play",
    // "trash-2") recolorido para `color`, renderizado no tamanho `size`
    // (px lógicos; o device pixel ratio é aplicado internamente para
    // nitidez em telas HiDPI). Resultados são cacheados por
    // (name, color, size). Retorna um QIcon nulo se o ícone não existir
    // no resource, permitindo fallback seguro pelo chamador.
    static QIcon icon(const QString &name, const QColor &color, int size = 24);

    // true se `name` corresponde a um SVG embarcado no resource.
    static bool has(const QString &name);
};

} // namespace kai::ui
