#pragma once

#include <QString>

class QWidget;

namespace kai::ui {

// Camada visual moderna aplicada DEPOIS do QSS do tema do kai-core (ThemeManager::buildQss, sem dependência de QtGui);
// mesma especificidade, então sobrescreve o que precisa. Tudo sai dos design tokens (utils/design-tokens.h): raio,
// espaçamento, escala tipográfica, elevação.

// QSS moderno completo, derivado dos tokens ativos.
QString buildModernStylesheet();

// A folha completa de um tema: o QSS do núcleo (`theme.qss`, que ainda traz `@RADIUS_MD@`/`@RADIUS_SM@` no lugar dos raios) com
// os cantos da preferência do usuário (tokens) no lugar, mais a camada moderna. É o que vai para `setStyleSheet`.
QString themeStyleSheet(const QString &themeQss);

// Aplica sombra (elevação) num painel/diálogo, se o efeito estiver ligado.
// Nível 1 = painel embutido, 2 = popover, 3 = diálogo modal.
void applyElevation(QWidget *widget, int level = 1);

// Prepara a janela para translucidez/blur conforme os efeitos ativos.
// Windows 11: usa o backdrop NATIVO (Mica/Acrylic) via DwmSetWindowAttribute.
// KDE/KWin: usa a propriedade de blur do compositor.
// Onde não há suporte, degrada para um fundo levemente translúcido.
void applyWindowBackdrop(QWidget *window);

// Fade-in suave ao exibir um widget (diálogos, overlays), se animações
// estiverem ligadas. Sem efeito quando desligado (custo zero).
void animateFadeIn(QWidget *widget, int durationMs = 140);

} // namespace kai::ui
