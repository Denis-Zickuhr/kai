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

bool applyNativeTitleBarTheme(QWidget *window)
{
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return false;
    }
    const QColor caption(utils::tokens::altBg());
    const QColor text(utils::tokens::fg());
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20: a barra escura segue o brilho do tema.
    const BOOL dark = caption.lightness() < 128 ? TRUE : FALSE;
    const bool accepted = SUCCEEDED(::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)));
    // DWMWA_CAPTION_COLOR = 35 e DWMWA_TEXT_COLOR = 36 (Windows 11); no Windows 10 só vale o claro/escuro acima.
    const COLORREF captionColor = RGB(caption.red(), caption.green(), caption.blue());
    const COLORREF textColor = RGB(text.red(), text.green(), text.blue());
    ::DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
    ::DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
    return accepted;
}
#else
bool applyNativeWindowCorners(QWidget *, int)
{
    return false;
}

bool applyNativeTitleBarTheme(QWidget *)
{
    return false;
}
#endif

} // namespace kai::ui
