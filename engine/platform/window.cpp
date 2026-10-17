#include "window.h"

#include <windowsx.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace patch
{

    static Window *g_window = nullptr;

    static constexpr DWORD WINDOWED_STYLE = WS_OVERLAPPEDWINDOW;
    static constexpr DWORD FULLSCREEN_STYLE = WS_POPUP;
    static constexpr DWORD FRAME_EX_STYLE = WS_EX_APPWINDOW;
    static constexpr int32_t DEFAULT_WINDOWED_WIDTH = 1280;
    static constexpr int32_t DEFAULT_WINDOWED_HEIGHT = 720;
    /* Keeps frames coming inside a modal move / size loop while the mouse rests */
    static constexpr UINT_PTR MODAL_FRAME_TIMER = 1;
    /* WM_KEYDOWN / WM_SYSKEYDOWN lParam: Alt held, key already down (auto-repeat) */
    static constexpr LPARAM KEY_ALT_DOWN = LPARAM{1} << 29;
    static constexpr LPARAM KEY_REPEAT = LPARAM{1} << 30;

    static LRESULT CALLBACK window_proc_static(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        if (!g_window)
        {
            return DefWindowProcA(hwnd, msg, wparam, lparam);
        }
        return g_window->handle_message(hwnd, msg, wparam, lparam);
    }

    static MONITORINFO monitor_info(HMONITOR monitor)
    {
        MONITORINFO info{};
        info.cbSize = sizeof(MONITORINFO);
        GetMonitorInfoA(monitor, &info);
        return info;
    }

    static BOOL CALLBACK collect_secondary_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM monitors)
    {
        if (!(monitor_info(monitor).dwFlags & MONITORINFOF_PRIMARY))
            /* NOLINTNEXTLINE(performance-no-int-to-ptr): EnumDisplayMonitors hands the pointer back as an LPARAM */
            reinterpret_cast<std::vector<HMONITOR> *>(monitors)->push_back(monitor);
        return TRUE;
    }

    /* PATCH_MONITOR (see window.h). The enumeration order of the monitors is unspecified, so
     * the others are ordered by position: the same setup always gives the same numbers. */
    static HMONITOR select_monitor()
    {
        const HMONITOR primary = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        char value[16];
        const DWORD length = GetEnvironmentVariableA("PATCH_MONITOR", value, sizeof(value));
        if (length == 0 || length >= sizeof(value))
            return primary;
        char *end = nullptr;
        const long index = strtol(value, &end, 10);
        if (end == value || *end != '\0' || index <= 0)
            return primary;

        std::vector<HMONITOR> others;
        EnumDisplayMonitors(nullptr, nullptr, collect_secondary_monitor, reinterpret_cast<LPARAM>(&others));
        std::sort(others.begin(), others.end(), [](HMONITOR a, HMONITOR b) {
            const RECT ra = monitor_info(a).rcMonitor;
            const RECT rb = monitor_info(b).rcMonitor;
            return ra.left != rb.left ? ra.left < rb.left : ra.top < rb.top;
        });
        if (static_cast<size_t>(index) > others.size())
        {
            fprintf(stderr, "PATCH_MONITOR=%ld: there are %zu other monitors, using the primary\n", index, others.size());
            return primary;
        }
        return others[static_cast<size_t>(index) - 1];
    }

    Window::Window(const char *title, WindowMode mode, int32_t width, int32_t height)
        : hwnd_(nullptr), hinstance_(GetModuleHandleA(nullptr)), width_(0), height_(0), resized_(false), should_close_(false), mouse_{}, keys_{},
          test_(mode == WindowMode::Test), fullscreen_(mode == WindowMode::Fullscreen),
          windowed_width_(width > 0 ? width : DEFAULT_WINDOWED_WIDTH),
          windowed_height_(height > 0 ? height : DEFAULT_WINDOWED_HEIGHT),
          has_windowed_placement_(false), windowed_placement_{},
          app_fiber_(nullptr), message_fiber_(nullptr), converted_thread_(false), in_modal_loop_(false)
    {
        /* Before any window exists. A DPI-unaware window on a scaled display is drawn at the
         * logical size and stretched by the compositor; this way the swapchain covers the
         * physical pixels. Fails harmlessly when a manifest already set the awareness. */
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        app_fiber_ = ConvertThreadToFiber(nullptr);
        if (app_fiber_)
            converted_thread_ = true;
        else if (GetLastError() == ERROR_ALREADY_FIBER)
            app_fiber_ = GetCurrentFiber();
        if (app_fiber_)
            message_fiber_ = CreateFiber(0, message_fiber_main, this);

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = window_proc_static;
        wc.hInstance = hinstance_;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = "PatchWindowClass";

        RegisterClassExA(&wc);

        /* Full monitor resolution for fullscreen and test runs (benchmarks stay comparable);
         * test windows are frameless, so their client area is all of it. WS_EX_NOACTIVATE
         * keeps a click on a test window from taking the keyboard focus (and its taskbar button
         * away). */
        HMONITOR monitor = select_monitor();
        const RECT screen = monitor_info(monitor).rcMonitor;
        hwnd_ = CreateWindowExA(test_ ? WS_EX_NOACTIVATE : FRAME_EX_STYLE, "PatchWindowClass", title,
                                test_ || fullscreen_ ? FULLSCREEN_STYLE : WINDOWED_STYLE,
                                screen.left, screen.top, screen.right - screen.left, screen.bottom - screen.top,
                                nullptr, nullptr, hinstance_, nullptr);

        g_window = this;

        /* The frame's outer size depends on the DPI of the monitor it is on, so it is sized
         * once it exists */
        if (hwnd_ && mode == WindowMode::Windowed)
            place_windowed_default(monitor);

        RECT client{};
        if (hwnd_ && GetClientRect(hwnd_, &client))
        {
            width_ = client.right;
            height_ = client.bottom;
        }
    }

    /* The frame at its first-open client size, centred in the monitor's work area and kept inside it */
    void Window::place_windowed_default(HMONITOR monitor)
    {
        const RECT work = monitor_info(monitor).rcWork;
        const int32_t work_width = work.right - work.left;
        const int32_t work_height = work.bottom - work.top;

        RECT frame{0, 0, windowed_width_, windowed_height_};
        AdjustWindowRectExForDpi(&frame, WINDOWED_STYLE, FALSE, FRAME_EX_STYLE, GetDpiForWindow(hwnd_));
        const int32_t frame_width = std::min<int32_t>(frame.right - frame.left, work_width);
        const int32_t frame_height = std::min<int32_t>(frame.bottom - frame.top, work_height);
        SetWindowPos(hwnd_, nullptr, work.left + (work_width - frame_width) / 2, work.top + (work_height - frame_height) / 2,
                     frame_width, frame_height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Window::set_client_size(int32_t width, int32_t height)
    {
        RECT frame{0, 0, width, height};
        AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrA(hwnd_, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrA(hwnd_, GWL_EXSTYLE)), GetDpiForWindow(hwnd_));
        SetWindowPos(hwnd_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* Covers the monitor the window is on */
    void Window::fit_to_monitor()
    {
        const RECT screen = monitor_info(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST)).rcMonitor;
        SetWindowPos(hwnd_, HWND_TOP, screen.left, screen.top, screen.right - screen.left, screen.bottom - screen.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }

    /* The frame's placement (position, size, maximized) is kept while fullscreen and restored after */
    void Window::set_fullscreen(bool fullscreen)
    {
        if (test_ || !hwnd_ || fullscreen == fullscreen_)
            return;
        fullscreen_ = fullscreen;

        const LONG_PTR style = GetWindowLongPtrA(hwnd_, GWL_STYLE) & ~static_cast<LONG_PTR>(WINDOWED_STYLE | FULLSCREEN_STYLE);
        if (fullscreen)
        {
            windowed_placement_.length = sizeof(WINDOWPLACEMENT);
            has_windowed_placement_ = GetWindowPlacement(hwnd_, &windowed_placement_) != FALSE;
            SetWindowLongPtrA(hwnd_, GWL_STYLE, style | FULLSCREEN_STYLE);
            fit_to_monitor();
        }
        else
        {
            SetWindowLongPtrA(hwnd_, GWL_STYLE, style | WINDOWED_STYLE);
            if (has_windowed_placement_)
                SetWindowPlacement(hwnd_, &windowed_placement_);
            else
                place_windowed_default(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST)); /* started fullscreen */
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    }

    void Window::show()
    {
        if (!hwnd_)
            return;
        /* A test window appears without activation. Unlike a first ShowWindow, SetWindowPos
         * also ignores the show state of the launcher's STARTUPINFO (the test runners pass
         * SW_HIDE), so every scripted run renders to a visible window. */
        if (test_)
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        else
            ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }

    Window::~Window()
    {
        if (hwnd_)
        {
            /* Closed mid-drag: the message fiber is suspended inside the modal loop. Cancel the
             * loop and let the fiber run until it has left it (the loop's timer keeps it moving),
             * so it is not deleted with the system's loop on its stack. */
            if (in_modal_loop_)
            {
                SendMessageA(hwnd_, WM_CANCELMODE, 0, 0);
                while (in_modal_loop_)
                    SwitchToFiber(message_fiber_);
            }
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        UnregisterClassA("PatchWindowClass", hinstance_);
        g_window = nullptr;

        if (message_fiber_)
            DeleteFiber(message_fiber_);
        if (converted_thread_)
            ConvertFiberToThread();
    }

    void Window::dispatch_messages()
    {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);

            if (msg.message == WM_QUIT)
            {
                should_close_ = true;
            }
        }
    }

    /* Message fiber: handles the queue, hands control back to the app, and repeats on the
     * app's next poll_events(). A fiber's function must never return. */
    void CALLBACK Window::message_fiber_main(void *window)
    {
        Window *self = static_cast<Window *>(window);
        for (;;)
        {
            self->dispatch_messages();
            SwitchToFiber(self->app_fiber_);
        }
    }

    void Window::poll_events()
    {
        /* Without a message fiber (its creation failed) a modal loop holds the app until it ends */
        if (message_fiber_)
            SwitchToFiber(message_fiber_);
        else
            dispatch_messages();
    }

    /*
     * Called on the message fiber inside a modal move / size loop: the app runs one iteration of
     * its loop, and its next poll_events() continues the modal loop from here. Messages that the
     * app's own calls send (SetWindowPos, DestroyWindow) are handled on the app's fiber, which
     * must not switch.
     */
    void Window::resume_app()
    {
        if (message_fiber_ && GetCurrentFiber() == message_fiber_)
            SwitchToFiber(app_fiber_);
    }

    VkSurfaceKHR Window::create_surface(VkInstance instance) const
    {
        VkWin32SurfaceCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        create_info.hwnd = hwnd_;
        create_info.hinstance = hinstance_;

        VkSurfaceKHR surface;
        if (vkCreateWin32SurfaceKHR(instance, &create_info, nullptr, &surface) != VK_SUCCESS)
        {
            return VK_NULL_HANDLE;
        }
        return surface;
    }

    /* Focus lost: no key-up / button-up messages will arrive, so nothing may stay pressed */
    void Window::release_input()
    {
        mouse_.left_down = false;
        mouse_.right_down = false;
        keys_ = {};
        ReleaseCapture();
    }

    LRESULT Window::handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            /* Only flag it: the app leaves its loop and the destructor destroys the window
             * once the renderer (and its swapchain on this window) is gone */
            should_close_ = true;
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KILLFOCUS:
            release_input();
            return 0;

        case WM_ACTIVATEAPP:
            if (!wparam)
                release_input();
            return 0;

        case WM_SIZE:
            width_ = LOWORD(lparam);
            height_ = HIWORD(lparam);
            resized_ = true;
            /* Mid-drag: render this size now rather than when the mouse button is released */
            if (in_modal_loop_)
                resume_app();
            return 0;

        case WM_MOUSEMOVE:
            /* Signed: with the mouse captured, positions left of or above the window
             * (other monitors) are negative */
            mouse_.x = static_cast<float>(GET_X_LPARAM(lparam));
            mouse_.y = static_cast<float>(GET_Y_LPARAM(lparam));
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            mouse_.left_down = msg == WM_LBUTTONDOWN;
            update_capture();
            return 0;

        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            mouse_.right_down = msg == WM_RBUTTONDOWN;
            update_capture();
            return 0;

        case WM_CAPTURECHANGED:
            /* Taken by another window: the releases of the buttons still down go there */
            /* NOLINTNEXTLINE(performance-no-int-to-ptr): WM_CAPTURECHANGED passes the new capture window in lparam */
            if (reinterpret_cast<HWND>(lparam) != hwnd)
            {
                mouse_.left_down = false;
                mouse_.right_down = false;
            }
            return 0;

        case WM_KEYDOWN:
            if (wparam == VK_F11 && !(lparam & KEY_REPEAT))
                set_fullscreen(!fullscreen_);
            update_key(wparam, true);
            return 0;

        case WM_KEYUP:
            update_key(wparam, false);
            return 0;

        /* Keys pressed or released while Alt is down (and F10) arrive here: they are tracked like
         * any other, and Windows still sees them (Alt+F4, Alt+Space) */
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            update_key(wparam, msg == WM_SYSKEYDOWN);
            /* Alt+Enter: fullscreen / windowed, like F11 (WM_KEYDOWN) */
            if (msg == WM_SYSKEYDOWN && wparam == VK_RETURN && (lparam & KEY_ALT_DOWN))
            {
                if (!(lparam & KEY_REPEAT))
                    set_fullscreen(!fullscreen_);
                return 0;
            }
            return DefWindowProcA(hwnd, msg, wparam, lparam);

        case WM_SYSCHAR:
            /* Alt+Enter is no menu mnemonic: DefWindowProc would beep */
            if (wparam == VK_RETURN)
                return 0;
            return DefWindowProcA(hwnd, msg, wparam, lparam);

        case WM_SYSCOMMAND:
            /* Alt or F10 alone would enter the system menu's modal loop and hold the app until
             * the next key; the menu still opens with Alt+Space or a click on the frame icon */
            if ((wparam & 0xFFF0) == SC_KEYMENU && lparam == 0)
                return 0;
            return DefWindowProcA(hwnd, msg, wparam, lparam);

        case WM_ENTERSIZEMOVE:
            in_modal_loop_ = true;
            SetTimer(hwnd, MODAL_FRAME_TIMER, USER_TIMER_MINIMUM, nullptr);
            return 0;

        case WM_EXITSIZEMOVE:
            KillTimer(hwnd, MODAL_FRAME_TIMER);
            in_modal_loop_ = false;
            return 0;

        case WM_TIMER:
            if (wparam == MODAL_FRAME_TIMER)
                resume_app();
            return 0;

        case WM_DPICHANGED:
            /* Moved to a monitor with another scale, or the scale changed: the frame takes the
             * size Windows suggests for the new DPI; fullscreen keeps covering its monitor */
            if (fullscreen_)
            {
                fit_to_monitor();
            }
            else
            {
                /* NOLINTNEXTLINE(performance-no-int-to-ptr): WM_DPICHANGED passes the suggested rect in lparam */
                const RECT *suggested = reinterpret_cast<const RECT *>(lparam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                             suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;

        case WM_DISPLAYCHANGE:
            /* The monitor's resolution changed: fullscreen follows it */
            if (fullscreen_)
                fit_to_monitor();
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wparam, lparam);
        }
    }

    void Window::update_key(WPARAM key, bool down)
    {
        switch (key)
        {
        case 'W':
            keys_.w = down;
            break;
        case 'A':
            keys_.a = down;
            break;
        case 'S':
            keys_.s = down;
            break;
        case 'D':
            keys_.d = down;
            break;
        case VK_SPACE:
            keys_.space = down;
            break;
        case VK_SHIFT:
            keys_.shift = down;
            break;
        case VK_ESCAPE:
            keys_.escape = down;
            break;
        case VK_F2:
            keys_.f2 = down;
            break;
        case VK_F3:
            keys_.f3 = down;
            break;
        case VK_F4:
            keys_.f4 = down;
            break;
        case VK_F5:
            keys_.f5 = down;
            break;
        default:
            break; /* Keys the app does not use */
        }
    }

    void Window::set_cursor_visible(bool visible)
    {
        /* ShowCursor keeps a display counter; drive it to exactly shown / hidden */
        if (visible)
        {
            while (ShowCursor(TRUE) < 0)
            {
            }
        }
        else
        {
            while (ShowCursor(FALSE) >= 0)
            {
            }
        }
    }

    /* Captured while any button is down, released with the last one */
    void Window::update_capture()
    {
        const bool any_down = mouse_.left_down || mouse_.right_down;
        if (any_down && GetCapture() != hwnd_)
            SetCapture(hwnd_);
        else if (!any_down && GetCapture() == hwnd_)
            ReleaseCapture();
    }

}
