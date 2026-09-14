#pragma once

#include <QPixmap>

class QPixmap;
class QToolButton;
class QWidget;

namespace kai::ui {

// Botões minimizar / maximizar-restaurar / fechar das janelas sem moldura (barra do topo e janelas destacadas).
enum class WindowControl { Minimize, Maximize, Close };

QToolButton *makeWindowControlButton(QWidget *parent, WindowControl kind);
// Recolore o ícone com o tema; `maximized` só importa para Maximize (mostra "restaurar").
void refreshWindowControlIcon(QToolButton *button, WindowControl kind, bool maximized = false);

// Logo do app (assets/logo/kai.png) para a barra de título das janelas sem
// moldura; nulo se o arquivo não existir (quem chama cai no texto "Kai").
QPixmap loadAppLogoPixmap();

} // namespace kai::ui
