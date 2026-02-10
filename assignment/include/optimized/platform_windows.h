#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "optimized/types.h"

namespace fox
{
    class platform_window
    {
    public:
        platform_window();
        ~platform_window();

        platform_window(platform_window&&) noexcept;
        platform_window& operator=(platform_window&&) noexcept;

        platform_window(const platform_window&) = delete;
        platform_window& operator=(const platform_window&) = delete;

        void create(const create_window_params& params) const;
        void destroy();

        void poll_messages();

        void* native_hwnd() const noexcept;

        std::uint32_t width()  const noexcept;
        std::uint32_t height() const noexcept;

        void set_window_title(const char* title);
        void set_icons(const window_icons& icons);

        void clip_cursor_to_client(bool enabled);
        void hide_cursor(bool hidden);

        bool key_down(int vk) const noexcept;
        void set_key_down(int vk, bool down) const noexcept;

        int mouse_x() const noexcept;
        int mouse_y() const noexcept;
        int mouse_wheel() const noexcept;
        void reset_mouse_wheel() const noexcept;

        mouse_button_state mouse_state(mouse_button b) const noexcept;
        bool mouse_pressed(mouse_button b) const noexcept;

    private:
        class pimpl;
        std::unique_ptr<pimpl> p_;
    };
}
