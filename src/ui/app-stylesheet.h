#pragma once

#include <QString>

class QWidget;

namespace kai::ui {

// ============================================================================
// CAMADA VISUAL MODERNA
// ----------------------------------------------------------------------------
// O QSS do tema é gerado no kai-core (ThemeManager::buildQss), que não pode
// depender de QtGui. Esta camada é aplicada DEPOIS dele e, por ter a mesma
// especificidade, sobrescreve o que precisa — sem reescrever o core.
//
// Tudo aqui sai dos design tokens (utils/design-tokens.h): raio, espaçamento,
// escala tipográfica, elevação. Assim uma repaginação é uma mudança de token,
// não uma caçada a 45 setStyleSheet espalhados.
// ============================================================================

// QSS moderno completo, derivado dos tokens ativos.
QString buildModernStylesheet();

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
