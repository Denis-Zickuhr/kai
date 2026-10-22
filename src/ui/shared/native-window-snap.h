#pragma once

#include <QByteArray>
#include <QPoint>
#include <QtGlobal>
#include <functional>

class QWidget;

namespace kai::ui {

// Windows: dá a uma janela SEM moldura (a principal, com a barra de título própria) o comportamento nativo de janela:
// encostar nas bordas/cantos da tela (snap), Win+setas, arrastar da barra para restaurar, redimensionar pelas bordas e
// duplo clique para maximizar. Faz isso devolvendo ao sistema as bordas (WS_THICKFRAME) e respondendo ao teste de
// posição do mouse (WM_NCHITTEST), sem desenhar moldura alguma. Fora do Windows não faz nada (retorna false).
//
// `enableNativeSnap` aplica os estilos à janela nativa (já criada); chamar de novo é inofensivo.
bool enableNativeSnap(QWidget *window);

// Chamar de QWidget::nativeEvent. `isCaptionAt` recebe um ponto em coordenadas da janela (lógicas) e diz se ele é a área
// de arrastar da barra de título (vazia, sem botão nem menu). Devolve true quando tratou a mensagem.
bool handleNativeSnapMessage(QWidget *window, const QByteArray &eventType, void *message, qintptr *result,
                             const std::function<bool(const QPoint &)> &isCaptionAt);

} // namespace kai::ui
