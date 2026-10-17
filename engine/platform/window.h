#ifndef PATCH_ENGINE_WINDOW_H
#define PATCH_ENGINE_WINDOW_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <cstdint>

namespace patch
{

    struct MouseState
    {
        float x;
        float y;
        bool left_down;
        bool right_down;
    };

    struct KeyState
    {
        bool w;
        bool a;
        bool s;
        bool d;
        bool space;
        bool shift;
        bool escape;
        bool f2; /* Debug overlay toggle */
        bool f3; /* Export debug info */
        bool f4; /* Terrain debug mode next */
        bool f5; /* Terrain debug mode previous */
    };

    enum class WindowMode : uint8_t
    {
        Fullscreen, /* borderless, covering its monitor */
        Windowed,   /* resizable frame */
        Test        /* frameless, covering its monitor, never activated (scripted test runs) */
    };

    /*
     * Win32 window, per-monitor DPI aware: sizes and mouse positions are physical pixels. F11 or
     * Alt+Enter switch between borderless fullscreen on the monitor the window is on and the
     * resizable frame, which comes back at its previous placement.
     *
     * The window opens on the monitor PATCH_MONITOR names: 0 (or unset) the primary, 1.. the
     * others from left to right, then top to bottom; a number past the last is the primary.
     * A Test window neither takes the keyboard focus when it appears nor when it is clicked,
     * so scripted runs can go on beside someone working on the machine. The window holds the
     * mouse capture while a button is down, so its release arrives even outside the window.
     *
     * While the user moves or sizes the frame, Windows runs a modal loop inside DispatchMessage
     * that would hold the app's loop until the mouse button is released. The message pump runs
     * on its own fiber instead: poll_events() switches to it, and inside the modal loop each
     * WM_SIZE and a timer tick switch back, so the app keeps rendering, at the new size, during
     * the drag.
     */
    class Window
    {
    public:
        /* width / height: windowed client size, 0 for the default */
        Window(const char *title, WindowMode mode, int32_t width = 0, int32_t height = 0);
        ~Window();

        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(Window &&) = delete;

        void show();
        /* Handles the queued messages; inside a modal move / size loop, returns at its next
         * WM_SIZE or timer tick instead */
        void poll_events();
        VkSurfaceKHR create_surface(VkInstance instance) const;

        /* Client area in pixels; the frame (if any) is added around it */
        void set_client_size(int32_t width, int32_t height);

        bool consume_resize()
        {
            bool was_resized = resized_;
            resized_ = false;
            return was_resized;
        }

        bool should_close() const { return should_close_; }
        void request_close() { should_close_ = true; }

        int32_t width() const { return width_; }
        int32_t height() const { return height_; }

        const MouseState &mouse() const { return mouse_; }
        const KeyState &keys() const { return keys_; }

        void set_cursor_visible(bool visible);

        LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    private:
        static void CALLBACK message_fiber_main(void *window);
        void dispatch_messages();
        void update_key(WPARAM key, bool down);
        void update_capture();
        void release_input();
        void resume_app();
        void set_fullscreen(bool fullscreen);
        void place_windowed_default(HMONITOR monitor);
        void fit_to_monitor();

        HWND hwnd_;
        HINSTANCE hinstance_;
        int32_t width_;
        int32_t height_;
        bool resized_;
        bool should_close_;
        MouseState mouse_;
        KeyState keys_;

        bool test_;
        bool fullscreen_;
        int32_t windowed_width_;  /* client size the frame first opens at */
        int32_t windowed_height_;
        bool has_windowed_placement_;
        WINDOWPLACEMENT windowed_placement_; /* the frame's placement when fullscreen was entered */

        void *app_fiber_;
        void *message_fiber_;
        bool converted_thread_; /* the app fiber was made here, not by the caller */
        bool in_modal_loop_;    /* the message fiber is inside a modal move / size loop */
    };

}

#endif
