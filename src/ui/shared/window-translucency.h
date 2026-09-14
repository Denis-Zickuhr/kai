#pragma once

namespace kai::ui {

// As janelas sem moldura do sistema (principal, destacada, diálogos) são
// TRANSLÚCIDAS para o raio dos cantos sair suave (o QSS pinta o arco com
// antialiasing; uma máscara de região é binária e fica serrilhada).
// Plataformas headless (testes/CI) não têm compositor de verdade e usam só a
// máscara; KAI_NO_TRANSLUCENT_WINDOW=1 força a máscara também (ex.: X11 sem
// compositor, onde a translucidez vira fundo preto). No Windows os cantos vêm
// do DWM (applyNativeWindowCorners), sem translucidez; no WSLg a transparência
// não é honrada (os cantos saíam retos/pretos), então também usa a máscara.
bool shouldUseTranslucentWindow();

} // namespace kai::ui
