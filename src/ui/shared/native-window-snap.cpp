#include "ui/shared/native-window-snap.h"

#include <QWidget>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <windowsx.h>
#endif

namespace kai::ui {

#if defined(Q_OS_WIN)
namespace {
// Espessura da borda de redimensionar (e do excesso de uma janela maximizada), no DPI da janela.
int frameThickness(HWND hwnd, bool vertical)
{
    const UINT dpi = ::GetDpiForWindow(hwnd);
    return ::GetSystemMetricsForDpi(vertical ? SM_CYSIZEFRAME : SM_CXSIZEFRAME, dpi)
        + ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}
} // namespace

bool enableNativeSnap(QWidget *window)
{
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return false;
    }
    const LONG_PTR wanted = WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & wanted) == wanted) {
        return true;
    }
    ::SetWindowLongPtrW(hwnd, GWL_STYLE, style | wanted);
    // Refaz o cálculo da área não cliente (que o handler abaixo zera: a moldura não é desenhada).
    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

bool handleNativeSnapMessage(QWidget *window, const QByteArray &eventType, void *message, qintptr *result,
                             const std::function<bool(const QPoint &)> &isCaptionAt)
{
    if (eventType != "windows_generic_MSG" || !message || window->isFullScreen()) {
        return false;
    }
    const MSG *msg = static_cast<const MSG *>(message);
    const HWND hwnd = msg->hwnd;

    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        // A janela inteira é área cliente (sem moldura nativa). Maximizada, o Windows estende a janela além do monitor
        // pela espessura da borda: devolve esse excesso para o conteúdo não ficar cortado.
        auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
        if (::IsZoomed(hwnd)) {
            const int fx = frameThickness(hwnd, false);
            const int fy = frameThickness(hwnd, true);
            params->rgrc[0].left += fx;
            params->rgrc[0].right -= fx;
            params->rgrc[0].top += fy;
            params->rgrc[0].bottom -= fy;
        }
        *result = 0;
        return true;
    }

    if (msg->message == WM_NCHITTEST) {
        RECT rect;
        if (!::GetWindowRect(hwnd, &rect)) {
            return false;
        }
        const int x = GET_X_LPARAM(msg->lParam);
        const int y = GET_Y_LPARAM(msg->lParam);
        if (!::IsZoomed(hwnd)) {
            const int bx = frameThickness(hwnd, false);
            const int by = frameThickness(hwnd, true);
            const bool left = x < rect.left + bx;
            const bool right = x >= rect.right - bx;
            const bool top = y < rect.top + by;
            const bool bottom = y >= rect.bottom - by;
            if (top && left) { *result = HTTOPLEFT; return true; }
            if (top && right) { *result = HTTOPRIGHT; return true; }
            if (bottom && left) { *result = HTBOTTOMLEFT; return true; }
            if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
            if (left) { *result = HTLEFT; return true; }
            if (right) { *result = HTRIGHT; return true; }
            if (top) { *result = HTTOP; return true; }
            if (bottom) { *result = HTBOTTOM; return true; }
        }
        const qreal dpr = window->devicePixelRatioF();
        const QPoint local(qRound((x - rect.left) / dpr), qRound((y - rect.top) / dpr));
        *result = (isCaptionAt && isCaptionAt(local)) ? HTCAPTION : HTCLIENT;
        return true;
    }
    return false;
}
#else
bool enableNativeSnap(QWidget *)
{
    return false;
}

bool handleNativeSnapMessage(QWidget *, const QByteArray &, void *, qintptr *,
                             const std::function<bool(const QPoint &)> &)
{
    return false;
}
#endif

} // namespace kai::ui
