#pragma once

class QWidget;

namespace kai::ui {

// Windows 11: pede ao DWM os cantos NATIVOS da janela (suaves, iguais aos das
// janelas padrão) e pinta a borda de 1px com a cor de borda do tema. `cornerStyle`
// segue a preferência do usuário (0 = reto). Devolve false onde o DWM não aceita
// (Windows 10) e em outros sistemas — aí vale a máscara de região.
bool applyNativeWindowCorners(QWidget *window, int cornerStyle);

// Janela com a moldura do sistema (Windows): pinta a barra de título nativa com as cores do tema (escura ou clara, cor
// da barra e do texto) para ela não destoar do app. Só vale com a janela já criada; devolve false onde o DWM não aceita.
bool applyNativeTitleBarTheme(QWidget *window);

} // namespace kai::ui
