#include "ui/window_chrome.h"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace rerevved::tools
{
namespace
{

GLFWwindow* chromeWindow = nullptr;
bool        chromeActive = false;
// Physical client pixels, written by the UI each frame and read by the window
// procedure during the next event poll on the same thread.
float dragBottom      = 0.0F;
float dragButtonsLeft = 0.0F;

#ifdef _WIN32
WNDPROC glfwProcedure = nullptr;
// Half the 1600 x 900 canvas.
constexpr LONG kMinimumWidth = 800;

int frameThickness(HWND window)
{
    const UINT dpi = GetDpiForWindow(window);
    return GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

LRESULT CALLBACK chromeProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_NCCALCSIZE:
            if (wParam == TRUE)
            {
                // The client area covers the whole window. A maximized window
                // hangs past the monitor by its frame, so pull that back in.
                if (IsZoomed(window))
                {
                    auto&     params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    const int frame  = frameThickness(window);
                    params.rgrc[0].left += frame;
                    params.rgrc[0].top += frame;
                    params.rgrc[0].right -= frame;
                    params.rgrc[0].bottom -= frame;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST:
        {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(window, &point);
            RECT client{};
            GetClientRect(window, &client);
            if (!IsZoomed(window))
            {
                const int  edge   = frameThickness(window);
                const bool left   = point.x < edge;
                const bool right  = point.x >= client.right - edge;
                const bool top    = point.y < edge;
                const bool bottom = point.y >= client.bottom - edge;
                if (top && left)
                {
                    return HTTOPLEFT;
                }
                if (top && right)
                {
                    return HTTOPRIGHT;
                }
                if (bottom && left)
                {
                    return HTBOTTOMLEFT;
                }
                if (bottom && right)
                {
                    return HTBOTTOMRIGHT;
                }
                if (left)
                {
                    return HTLEFT;
                }
                if (right)
                {
                    return HTRIGHT;
                }
                if (top)
                {
                    return HTTOP;
                }
                if (bottom)
                {
                    return HTBOTTOM;
                }
            }
            if (static_cast<float>(point.y) < dragBottom && static_cast<float>(point.x) < dragButtonsLeft)
            {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_SIZING:
        {
            auto&      bounds = *reinterpret_cast<RECT*>(lParam);
            const bool across = wParam == WMSZ_LEFT || wParam == WMSZ_RIGHT;
            const bool upward = wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT;
            const bool tall   = wParam == WMSZ_TOP || wParam == WMSZ_BOTTOM;
            const LONG width  = tall ? (bounds.bottom - bounds.top) * 16 / 9 : bounds.right - bounds.left;
            const LONG height = width * 9 / 16;
            if (tall)
            {
                bounds.right = bounds.left + width;
            }
            else if (upward && !across)
            {
                bounds.top = bounds.bottom - height;
            }
            else
            {
                bounds.bottom = bounds.top + height;
            }
            if (wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
            {
                bounds.left = bounds.right - width;
            }
            return TRUE;
        }
        case WM_GETMINMAXINFO:
        {
            const LRESULT result  = CallWindowProcW(glfwProcedure, window, message, wParam, lParam);
            auto&         limits  = *reinterpret_cast<MINMAXINFO*>(lParam);
            limits.ptMinTrackSize = { kMinimumWidth, kMinimumWidth * 9 / 16 };
            return result;
        }
        default:
            break;
    }
    return CallWindowProcW(glfwProcedure, window, message, wParam, lParam);
}
#endif

} // namespace

bool EnableCustomChrome(GLFWwindow* window)
{
    chromeWindow = window;
#ifdef _WIN32
    HWND handle = glfwGetWin32Window(window);
    if (handle == nullptr)
    {
        return false;
    }
    int width  = 0;
    int height = 0;
    glfwGetWindowSize(window, &width, &height);
    glfwProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&chromeProcedure)));
    if (glfwProcedure == nullptr)
    {
        return false;
    }
    // A one pixel frame margin keeps the DWM shadow and rounded corners.
    const MARGINS margins{ 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea(handle, &margins);
    chromeActive = true;
    // The window is now all client area. Keep the client size it was created with.
    RECT bounds{};
    GetWindowRect(handle, &bounds);
    SetWindowPos(handle, nullptr, bounds.left, bounds.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
#else
    return false;
#endif
}

bool ChromeActive() noexcept
{
    return chromeActive;
}

void SetChromeDragArea(float bottom, float buttonsLeft, float scale) noexcept
{
    dragBottom      = bottom * scale;
    dragButtonsLeft = buttonsLeft * scale;
}

void MinimizeWindow()
{
    if (chromeWindow != nullptr)
    {
        glfwIconifyWindow(chromeWindow);
    }
}

void ToggleMaximizeWindow()
{
    if (chromeWindow == nullptr)
    {
        return;
    }
    if (glfwGetWindowAttrib(chromeWindow, GLFW_MAXIMIZED) != 0)
    {
        glfwRestoreWindow(chromeWindow);
    }
    else
    {
        glfwMaximizeWindow(chromeWindow);
    }
}

void RequestWindowClose()
{
    if (chromeWindow != nullptr)
    {
        glfwSetWindowShouldClose(chromeWindow, GLFW_TRUE);
    }
}

bool WindowMaximized()
{
    return chromeWindow != nullptr && glfwGetWindowAttrib(chromeWindow, GLFW_MAXIMIZED) != 0;
}

} // namespace rerevved::tools
