#pragma once

#include <QPixmap>

class QPixmap;
class QToolButton;
class QWidget;

namespace kai::ui {

// Botões minimizar / maximizar-restaurar / fechar das janelas sem moldura do
// app (a barra do topo da janela principal e a barra de título das janelas
// destacadas): mesmos ícones Lucide, tamanho e hover.
enum class WindowControl { Minimize, Maximize, Close };

QToolButton *makeWindowControlButton(QWidget *parent, WindowControl kind);
// Recolore o ícone com o tema atual. `maximized` só importa para WindowControl::Maximize
// (mostra "restaurar" quando a janela já está maximizada).
void refreshWindowControlIcon(QToolButton *button, WindowControl kind, bool maximized = false);

// Logo do app (assets/logo/kai.png) para a barra de título das janelas sem
// moldura; nulo se o arquivo não existir (quem chama cai no texto "Kai").
QPixmap loadAppLogoPixmap();

} // namespace kai::ui
