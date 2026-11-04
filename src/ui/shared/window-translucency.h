#pragma once

namespace kai::ui {

// Janelas sem moldura (principal, destacada, diálogos) são translúcidas para o raio dos cantos sair suave (o QSS
// pinta o arco com antialiasing; a máscara de região é serrilhada). Usam só a máscara: plataformas headless
// (sem compositor), KAI_NO_TRANSLUCENT_WINDOW=1 (ex.: X11 sem compositor, onde vira fundo preto) e WSLg (não honra
// alfa). No Windows os cantos vêm do DWM (applyNativeWindowCorners), sem translucidez.
bool shouldUseTranslucentWindow();

} // namespace kai::ui
