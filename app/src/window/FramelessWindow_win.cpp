#include "FramelessWindow_win.h"

#include "MainWindow.h"
#include "TitleBar.h"
#include "theme/Theme.h"

#include <QWidget>
#include <QWindow>

#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

namespace plynth::app::win {

namespace {

// Physical pixel size of the resize border for the window's current display scale.
int borderThickness(const MainWindow& window)
{
    return qRound(Theme::instance().resizeBorder() * window.devicePixelRatioF());
}

// Tells whether the window is currently maximized according to Windows.
bool isMaximizedNow(HWND hwnd)
{
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    return GetWindowPlacement(hwnd, &placement) && placement.showCmd == SW_SHOWMAXIMIZED;
}

// Works out what is under the mouse and returns the matching Windows hit test code.
LRESULT hitTest(MainWindow& window, TitleBar& titleBar, HWND hwnd, LPARAM lParam)
{
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    const int x = GET_X_LPARAM(lParam);
    const int y = GET_Y_LPARAM(lParam);

    if (!isMaximizedNow(hwnd)) {
        const int b = borderThickness(window);
        const bool left = x < rect.left + b;
        const bool right = x >= rect.right - b;
        const bool top = y < rect.top + b;
        const bool bottom = y >= rect.bottom - b;
        if (top && left)
            return HTTOPLEFT;
        if (top && right)
            return HTTOPRIGHT;
        if (bottom && left)
            return HTBOTTOMLEFT;
        if (bottom && right)
            return HTBOTTOMRIGHT;
        if (left)
            return HTLEFT;
        if (right)
            return HTRIGHT;
        if (top)
            return HTTOP;
        if (bottom)
            return HTBOTTOM;
    }

    const qreal ratio = window.devicePixelRatioF();
    const QPoint inWindow(qRound((x - rect.left) / ratio), qRound((y - rect.top) / ratio));
    if (titleBar.geometry().contains(inWindow)) {
        switch (titleBar.hitTest(inWindow - titleBar.pos())) {
        case TitleBar::Hit::Caption:
            return HTCAPTION;
        case TitleBar::Hit::MaximizeButton:
            return HTMAXBUTTON;
        case TitleBar::Hit::Client:
            return HTCLIENT;
        }
    }
    return HTCLIENT;
}

// Asks Windows to tell us when the mouse leaves the non-client area.
void trackNcLeave(HWND hwnd)
{
    TRACKMOUSEEVENT track{};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE | TME_NONCLIENT;
    track.hwndTrack = hwnd;
    TrackMouseEvent(&track);
}

}

// Adds the standard window styles back so Windows provides snap, shadow and animations.
void enableNativeFrame(QWidget& window)
{
    const HWND hwnd = reinterpret_cast<HWND>(window.winId());
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME | WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    const MARGINS margins = {0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// Handles the window messages that make the custom title bar behave like a system one.
bool handleNativeEvent(MainWindow& window, TitleBar& titleBar, void* message, qintptr* result)
{
    MSG* msg = static_cast<MSG*>(message);
    switch (msg->message) {
    case WM_NCCALCSIZE: {
        if (!msg->wParam)
            return false;
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
        if (isMaximizedNow(msg->hwnd)) {
            const UINT dpi = GetDpiForWindow(msg->hwnd);
            const int frame = GetSystemMetricsForDpi(SM_CXFRAME, dpi)
                              + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            params->rgrc[0].left += frame;
            params->rgrc[0].right -= frame;
            params->rgrc[0].top += frame;
            params->rgrc[0].bottom -= frame;
        }
        *result = 0;
        return true;
    }
    case WM_NCHITTEST:
        *result = hitTest(window, titleBar, msg->hwnd, msg->lParam);
        return true;
    case WM_NCMOUSEMOVE:
        if (msg->wParam == HTMAXBUTTON) {
            titleBar.maximizeButton()->setHovered(true);
            trackNcLeave(msg->hwnd);
        } else {
            titleBar.maximizeButton()->setHovered(false);
        }
        return false;
    case WM_NCMOUSELEAVE:
        titleBar.maximizeButton()->setHovered(false);
        titleBar.maximizeButton()->setPressedLook(false);
        return false;
    case WM_NCLBUTTONDOWN:
        if (msg->wParam == HTMAXBUTTON) {
            titleBar.maximizeButton()->setPressedLook(true);
            *result = 0;
            return true;
        }
        return false;
    case WM_NCLBUTTONUP:
        if (msg->wParam == HTMAXBUTTON) {
            titleBar.maximizeButton()->setPressedLook(false);
            titleBar.maximizeToggleRequested();
            *result = 0;
            return true;
        }
        return false;
    default:
        return false;
    }
}

}
