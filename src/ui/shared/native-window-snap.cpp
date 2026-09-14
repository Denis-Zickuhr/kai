#include "ui/shared/native-window-snap.h"

#include <QRect>
#include <QScreen>
#include <QWidget>
#include <QWindow>

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
    // Tela cheia: o Qt tirou a moldura de propósito. Devolver WS_THICKFRAME/WS_CAPTION aqui encolhia a área cliente em
    // alguns pixels (a janela ficava "deslocada para dentro"); ao sair da tela cheia o Qt restaura os estilos dele.
    if (window->isFullScreen()) {
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
    if (eventType != "windows_generic_MSG" || !message) {
        return false;
    }
    const MSG *msg = static_cast<const MSG *>(message);
    const HWND hwnd = msg->hwnd;

    if (msg->message == WM_GETMINMAXINFO) {
        // O Qt trata a janela frameless como sem moldura e usa o retângulo inteiro dela como geometria. Maximizada, o
        // Windows estende esse retângulo além da área de trabalho pela espessura da borda (WS_THICKFRAME); o conteúdo
        // ficava maior que a área visível e era cortado à direita e embaixo. Aqui a janela maximizada é exatamente a
        // área de trabalho do monitor. Devolve false: o Qt ainda preenche os limites de redimensionamento.
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
            auto *mmi = reinterpret_cast<MINMAXINFO *>(msg->lParam);
            mmi->ptMaxPosition.x = info.rcWork.left - info.rcMonitor.left;
            mmi->ptMaxPosition.y = info.rcWork.top - info.rcMonitor.top;
            mmi->ptMaxSize.x = info.rcWork.right - info.rcWork.left;
            mmi->ptMaxSize.y = info.rcWork.bottom - info.rcWork.top;
        }
        return false;
    }

    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        // A janela inteira é área cliente (sem moldura nativa), maximizada ou não: o retângulo proposto já é o da janela
        // (a área de trabalho, ver WM_GETMINMAXINFO acima), e o Qt calcula a geometria a partir dele.
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
        if (!::IsZoomed(hwnd) && !window->isFullScreen()) { // tela cheia: sem bordas de redimensionar
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

void forceForegroundWindow(QWidget *window)
{
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }
    if (::IsIconic(hwnd)) {
        ::ShowWindow(hwnd, SW_RESTORE);
    }
    const HWND foregroundHwnd = ::GetForegroundWindow();
    const DWORD foregroundThreadId = foregroundHwnd
        ? ::GetWindowThreadProcessId(foregroundHwnd, nullptr) : 0;
    const DWORD currentThreadId = ::GetCurrentThreadId();
    const bool attached = foregroundThreadId != 0 && foregroundThreadId != currentThreadId
        && ::AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);
    ::SetForegroundWindow(hwnd);
    ::BringWindowToTop(hwnd);
    if (attached) {
        ::AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
    }
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

void forceForegroundWindow(QWidget *)
{
}
#endif

namespace {
QString rectText(const QRect &r)
{
    return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}
} // namespace

QString describeWindowGeometry(const QWidget *window)
{
    const QScreen *screen = window->screen();
    QString text = QStringLiteral("state=%1 qt.geometry=[%2] qt.frame=[%3] screen=[%4] available=[%5] dpr=%6")
        .arg(window->isFullScreen() ? QStringLiteral("fullscreen")
             : window->isMaximized() ? QStringLiteral("maximized") : QStringLiteral("normal"),
             rectText(window->geometry()), rectText(window->frameGeometry()),
             screen ? rectText(screen->geometry()) : QStringLiteral("?"),
             screen ? rectText(screen->availableGeometry()) : QStringLiteral("?"))
        .arg(window->devicePixelRatioF());
#if defined(Q_OS_WIN)
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    RECT outer{};
    RECT client{};
    if (hwnd && ::GetWindowRect(hwnd, &outer) && ::GetClientRect(hwnd, &client)) {
        POINT origin{0, 0};
        ::ClientToScreen(hwnd, &origin);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        ::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info);
        text += QStringLiteral(" | win.window=[%1,%2 %3x%4] win.client=[%5,%6 %7x%8] monitor=[%9,%10 %11x%12] work=[%13,%14 %15x%16] dpi=%17 zoomed=%18 style=0x%19")
            .arg(outer.left).arg(outer.top).arg(outer.right - outer.left).arg(outer.bottom - outer.top)
            .arg(origin.x).arg(origin.y).arg(client.right).arg(client.bottom)
            .arg(info.rcMonitor.left).arg(info.rcMonitor.top).arg(info.rcMonitor.right - info.rcMonitor.left)
            .arg(info.rcMonitor.bottom - info.rcMonitor.top)
            .arg(info.rcWork.left).arg(info.rcWork.top).arg(info.rcWork.right - info.rcWork.left)
            .arg(info.rcWork.bottom - info.rcWork.top)
            .arg(::GetDpiForWindow(hwnd)).arg(::IsZoomed(hwnd) ? 1 : 0)
            .arg(quint64(::GetWindowLongPtrW(hwnd, GWL_STYLE)), 0, 16);
    }
#endif
    return text;
}

} // namespace kai::ui
