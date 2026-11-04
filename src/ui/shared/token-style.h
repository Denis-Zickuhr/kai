#pragma once

#include <QString>

#include <functional>

class QWidget;

namespace kai::ui {

// Estilo local que depende dos tokens (raio da preferência de cantos, cores do tema): aplica `qss()` agora e de novo
// sempre que a aparência muda (refreshTokenStyles). Um setStyleSheet com o raio calculado na construção ficava com o
// canto antigo até reiniciar o app.
void bindTokenStyle(QWidget *widget, std::function<QString()> qss);

// Como bindTokenStyle, para campo de altura fixa que o layout pode espremer: `qss(radius)` recebe o raio da preferência de
// cantos já limitado pela altura REAL do campo (o Qt desenha canto reto se o raio passa da metade da altura), e é
// reaplicado quando a altura muda.
void bindFieldStyle(QWidget *widget, std::function<QString(int radius)> qss);

// Reaplica todos os estilos ligados por bindTokenStyle (chamado depois de trocar tema, cantos ou densidade).
void refreshTokenStyles();

} // namespace kai::ui
