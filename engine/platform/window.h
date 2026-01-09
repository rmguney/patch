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
        float wheel_delta;
        bool left_down;
        bool right_down;
    };

    struct KeyState
    {
        bool w;
        bool a;
        bool s;
        bool d;
        bool r;
        bool space;
        bool shift;
        bool escape;
        bool f3; /* DEBUG currently inactive */
        bool f4; /* DEBUG currently inactive */
        bool f5; /* DEBUG: Export debug info */
        bool f6; /* DEBUG: Toggle terrain debug mode */
        bool f7; /* DEBUG: Toggle free camera mode */
    };

    class Window
    {
    public:
        Window(int32_t width, int32_t height, const char *title);
        ~Window();

        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(Window &&) = delete;

        void show();
        void poll_events();
        VkSurfaceKHR create_surface(VkInstance instance) const;

        bool consume_resize()
        {
            bool was_resized = resized_;
            resized_ = false;
            return was_resized;
        }

        bool should_close() const { return should_close_; }
        void request_close()
        {
            should_close_ = true;
            if (hwnd_)
            {
                PostMessageA(hwnd_, WM_CLOSE, 0, 0);
            }
        }

        int32_t width() const { return width_; }
        int32_t height() const { return height_; }
        float aspect_ratio() const
        {
            if (height_ <= 0)
                return 1.0f;
            return static_cast<float>(width_) / static_cast<float>(height_);
        }

        const MouseState &mouse() const { return mouse_; }
        const KeyState &keys() const { return keys_; }
        bool key_pressed(int key_code) const;

        bool has_focus() const { return focused_; }

        void set_cursor_visible(bool visible);
        void set_mouse_capture(bool capture);
        void set_cursor_position(float x, float y);

        HWND handle() const { return hwnd_; }
        HINSTANCE instance() const { return hinstance_; }

        LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    private:
        void update_key(WPARAM key, bool down);

        HWND hwnd_;
        HINSTANCE hinstance_;
        int32_t width_;
        int32_t height_;
        bool resized_;
        bool should_close_;
        bool focused_;
        MouseState mouse_;
        KeyState keys_;
    };

}

#endif
