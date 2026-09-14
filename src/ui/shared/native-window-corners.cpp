#include "ui/shared/native-window-corners.h"

#include "utils/design-tokens.h"

#include <QColor>
#include <QWidget>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

namespace kai::ui {

#if defined(Q_OS_WIN)
bool applyNativeWindowCorners(QWidget *window, int cornerStyle)
{
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return false;
    }
    // DWMWA_WINDOW_CORNER_PREFERENCE = 33: reto -> DWMWCP_DONOTROUND (1); senão
    // DWMWCP_ROUND (2, o único raio que o DWM oferece).
    const int preference = cornerStyle == 0 ? 1 : 2;
    const bool accepted = SUCCEEDED(::DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference)));
    if (accepted) {
        // DWMWA_BORDER_COLOR = 34
        const QColor border(utils::tokens::borderColor());
        const COLORREF color = RGB(border.red(), border.green(), border.blue());
        ::DwmSetWindowAttribute(hwnd, 34, &color, sizeof(color));
    }
    return accepted;
}
#else
bool applyNativeWindowCorners(QWidget *, int)
{
    return false;
}
#endif

} // namespace kai::ui
