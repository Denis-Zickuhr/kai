#pragma once

#include "utils/design-tokens.h"

#include <QtGlobal>

namespace kai::ui {

// Recuo interno das molduras dos painéis principais ("panelCard": lista de
// comandos e Saída). Os filhos são retângulos quadrados: se o canto deles cai
// DENTRO do arco da borda arredondada, eles pintam por cima dele e o arco
// "quebra". O recuo é 1px de borda + ~30% do raio (o canto do filho fica dentro
// do círculo do arco) + 1px de respiro; mínimo de 2px com cantos retos.
inline int panelFrameInset()
{
    const int radius = kai::utils::tokens::radiusMd();
    return qMax(2, (radius * 3 + 9) / 10 + 1);
}

// Raio CONCÊNTRICO para o conteúdo que encosta no canto da moldura.
inline int panelInnerRadius()
{
    return qMax(0, kai::utils::tokens::radiusMd() - panelFrameInset());
}

} // namespace kai::ui
