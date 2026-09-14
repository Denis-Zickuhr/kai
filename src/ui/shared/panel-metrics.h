#pragma once

#include "utils/design-tokens.h"

#include <QtGlobal>

namespace kai::ui {

// Recuo interno das molduras dos painéis principais ("panelCard": lista de comandos e Saída). Filhos quadrados
// cujo canto cai dentro do arco da borda pintam por cima dele; o recuo é 1px de borda + ~30% do raio + 1px de
// respiro (mínimo 2px com cantos retos).
inline int panelFrameInset()
{
    const int radius = kai::utils::tokens::radiusMd();
    return qMax(2, (radius * 3 + 9) / 10 + 1);
}

inline int panelInnerRadius()
{
    return qMax(0, kai::utils::tokens::radiusMd() - panelFrameInset());
}

} // namespace kai::ui
