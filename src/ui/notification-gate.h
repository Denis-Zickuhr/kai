#pragma once

namespace kai::ui {

// Decide se uma notificação de evento deve aparecer, dados os toggles
// relevantes. Lógica PURA (sem QWidget/QSystemTrayIcon), extraída pra ser
// testável sem precisar instanciar janela/bandeja de verdade — mesmo
// espírito de name-uniqueness.h.
//
// Regra: precisa ter bandeja disponível, notificações habilitadas no
// geral E o toggle daquele evento específico ligado; e (se a janela
// estiver em foco) só mostra se `notifyEvenWhenFocused` estiver ligado —
// senão o badge vermelho que já aparece na árvore com o Kai aberto seria
// redundante com um toast por cima.
inline bool shouldShowNotification(bool trayAvailable, bool notificationsEnabled,
                                    bool eventToggleOn, bool notifyEvenWhenFocused,
                                    bool windowIsActive)
{
    if (!trayAvailable || !notificationsEnabled || !eventToggleOn) {
        return false;
    }
    return notifyEvenWhenFocused || !windowIsActive;
}

} // namespace kai::ui
