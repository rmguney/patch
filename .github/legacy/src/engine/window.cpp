#include "window.h"
#include <cstdlib>

namespace patch {

static Window* g_window = nullptr;

static LRESULT CALLBACK window_proc_static(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (!g_window) {
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
    return g_window->handle_message(hwnd, msg, wparam, lparam);
}

Window::Window(int32_t, int32_t, const char* title)
    : hwnd_(nullptr)
    , hinstance_(GetModuleHandleA(nullptr))
    , width_(0)
    , height_(0)
    , resized_(false)
    , should_close_(false)
    , focused_(false)
    , mouse_{0.0f, 0.0f, false, false}
    , keys_{false, false, false, false, false, false, false} {

    POINT origin{0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(MONITORINFO);
    GetMonitorInfoA(monitor, &info);
    int screen_width = info.rcMonitor.right - info.rcMonitor.left;
    int screen_height = info.rcMonitor.bottom - info.rcMonitor.top;
    width_ = screen_width;
    height_ = screen_height;

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc_static;
    wc.hInstance = hinstance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = "PatchWindowClass";

    RegisterClassExA(&wc);

    hwnd_ = CreateWindowExA(
        WS_EX_APPWINDOW,
        "PatchWindowClass",
        title,
        WS_POPUP,
        info.rcMonitor.left,
        info.rcMonitor.top,
        screen_width,
        screen_height,
        nullptr,
        nullptr,
        hinstance_,
        nullptr
    );

    g_window = this;
}

void Window::show() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

Window::~Window() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UnregisterClassA("PatchWindowClass", hinstance_);
    g_window = nullptr;
}

void Window::poll_events() {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);

        if (msg.message == WM_QUIT) {
            should_close_ = true;
        }
    }
}

VkSurfaceKHR Window::create_surface(VkInstance instance) const {
    VkWin32SurfaceCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.hwnd = hwnd_;
    create_info.hinstance = hinstance_;

    VkSurfaceKHR surface;
    if (vkCreateWin32SurfaceKHR(instance, &create_info, nullptr, &surface) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return surface;
}

LRESULT Window::handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            should_close_ = true;
            if (hwnd_) {
                DestroyWindow(hwnd_);
                hwnd_ = nullptr;
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SETFOCUS:
            focused_ = true;
            return 0;

        case WM_KILLFOCUS:
            focused_ = false;
            mouse_.left_down = false;
            mouse_.right_down = false;
            ReleaseCapture();
            return 0;

        case WM_ACTIVATEAPP:
            focused_ = wparam ? true : false;
            if (!focused_) {
                mouse_.left_down = false;
                mouse_.right_down = false;
                ReleaseCapture();
            }
            return 0;

        case WM_SIZE:
            width_ = LOWORD(lparam);
            height_ = HIWORD(lparam);
            resized_ = true;
            return 0;

        case WM_MOUSEMOVE:
            mouse_.x = static_cast<float>(LOWORD(lparam));
            mouse_.y = static_cast<float>(HIWORD(lparam));
            return 0;

        case WM_LBUTTONDOWN:
            mouse_.left_down = true;
            SetCapture(hwnd);
            return 0;

        case WM_LBUTTONUP:
            mouse_.left_down = false;
            ReleaseCapture();
            return 0;

        case WM_RBUTTONDOWN:
            mouse_.right_down = true;
            return 0;

        case WM_RBUTTONUP:
            mouse_.right_down = false;
            return 0;

        case WM_KEYDOWN:
            update_key(wparam, true);
            return 0;

        case WM_KEYUP:
            update_key(wparam, false);
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
}

void Window::update_key(WPARAM key, bool down) {
    switch (key) {
        case 'W': keys_.w = down; break;
        case 'A': keys_.a = down; break;
        case 'S': keys_.s = down; break;
        case 'D': keys_.d = down; break;
        case 'R': keys_.r = down; break;
        case VK_SPACE: keys_.space = down; break;
        case VK_SHIFT: keys_.shift = down; break;
    }
}

}
