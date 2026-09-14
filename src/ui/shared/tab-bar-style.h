#pragma once

#include "utils/design-tokens.h"

#include <QString>

namespace kai::ui {

// Altura padrão de uma barra de abas/ações: a mesma do cabeçalho da Saída e das barras de ação (o MainWindow
// pode passar a altura medida, ver MainWindow::syncPanelBarHeights).
inline int standardTabBarHeight()
{
    namespace tk = kai::utils::tokens;
    return tk::controlHeight() + tk::space(2);
}

// Estilo único das abas planas (tool window da JetBrains): só texto, sublinhado no accent na aba selecionada,
// mesma altura/recuo/fonte em qualquer barra. A altura do ::tab é a da barra menos o sublinhado, para ele
// encostar na base. Raio 0 de propósito: aba com sublinhado não tem canto arredondado.
inline QString flatTabBarQss(int barHeight)
{
    namespace tk = kai::utils::tokens;
    constexpr int kUnderline = 2;
    return QStringLiteral(
        "QTabBar { background: transparent; border: none; margin: 0px; padding: 0px; }"
        "QTabBar::tab {"
        "   background: transparent;"
        "   color: %1;"
        "   border: none;"
        "   border-bottom: %2px solid transparent;"
        "   border-radius: 0px;"
        "   padding: 0px %3px;"
        "   margin: 0px;"
        "   height: %4px;"
        "   font-size: %7pt;"
        "   font-weight: 500;"
        "}"
        "QTabBar::tab:hover:!selected { color: %5; }"
        "QTabBar::tab:selected {"
        "   color: %5;"
        "   border-bottom: %2px solid %6;"
        "}")
        .arg(tk::tabInactiveFg())
        .arg(kUnderline)
        .arg(tk::space(3))
        .arg(barHeight - kUnderline)
        .arg(tk::fg())
        .arg(tk::accent())
        .arg(tk::fontSizePt());
}

} // namespace kai::ui
