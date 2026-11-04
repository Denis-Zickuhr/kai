#pragma once

namespace kai::ui {

// Decide se uma notificação deve aparecer. Lógica pura (testável sem janela/bandeja). Exige bandeja disponível,
// notificações ligadas no geral e o toggle do evento; com a janela em foco só mostra se `notifyEvenWhenFocused`
// estiver ligado (o badge da árvore já avisa).
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
