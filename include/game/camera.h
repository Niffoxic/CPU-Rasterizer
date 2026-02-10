#pragma once

#include "fmath.h"
#include "optimized/platform_windows.h"

namespace fox
{
    class camera
    {
    public:
        void init(const vec4& offset,
                  float offset_speed,
                  float min_offset_z,
                  float max_offset_z) noexcept;

        void update(float dt,
                    platform_window* wnd,
                    const matrix& target_world) noexcept;
        void update_free_fly(float dt,
                             platform_window* wnd,
                             float mouse_dx,
                             float mouse_dy,
                             bool fps_mouse_enabled) noexcept;

        void set_position(const vec4& position) noexcept;

        [[nodiscard]] matrix view_matrix() const noexcept { return view_; }
        [[nodiscard]] const vec4& position() const noexcept { return position_; }
        [[nodiscard]] const vec4& offset() const noexcept { return offset_; }

    private:
        vec4  position_{ 0.f, 8.f, 18.f, 1.f };
        vec4  offset_{ 0.f, 8.f, 18.f, 0.f };
        float offset_speed_ = 30.f;
        float min_offset_z_ = 6.f;
        float max_offset_z_ = 80.f;
        matrix view_{};
        float yaw_ = 3.14f;
        float pitch_ = 0.f;
    };
}
