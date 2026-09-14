#pragma once

#include "utils/design-tokens.h"

#include <QString>

namespace kai::ui {

// Altura padrão de uma barra de abas/ações: a MESMA do cabeçalho da Saída e
// das barras de ação, para tudo alinhar (o MainWindow pode passar a altura
// real medida, ver MainWindow::syncPanelBarHeights).
inline int standardTabBarHeight()
{
    namespace tk = kai::utils::tokens;
    return tk::controlHeight() + tk::space(2);
}

// Estilo ÚNICO das abas planas (estilo tool window da JetBrains): só texto,
// sublinhado na cor de destaque na aba selecionada, mesma altura, mesmo recuo
// horizontal e mesma fonte em qualquer barra (pastas raiz, abas da Saída).
// A altura do ::tab é a da barra menos o sublinhado, para ele encostar na base.
// Raio 0 de propósito: uma aba com sublinhado não desenha canto arredondado.
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
