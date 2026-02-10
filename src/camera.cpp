#include "game/camera.h"
#include <algorithm>
#include <windows.h>

namespace fox
{
    void camera::init(const vec4& offset,
                      const float offset_speed,
                      const float min_offset_z,
                      const float max_offset_z) noexcept
    {
        offset_ = offset;
        offset_.w = 0.f;
        offset_speed_ = offset_speed;
        min_offset_z_ = min_offset_z;
        max_offset_z_ = max_offset_z;

        offset_.z = std::clamp(offset_.z, min_offset_z_, max_offset_z_);

        position_ = offset_;
        position_.w = 1.f;

        yaw_   = fox_math::pi_f;
        pitch_ = 0.0f;

        const float cos_pitch = std::cos(pitch_);
        const vec4 forward(
            std::sin(yaw_) * cos_pitch,
            std::sin(pitch_),
            std::cos(yaw_) * cos_pitch,
            0.f
        );

        const vec4 up(0.f, 1.f, 0.f, 0.f);

        view_ = fox_math::look_at(position_, position_ + forward, up);
    }


    void camera::update(float dt,
                        fox::platform_window* wnd,
                        const matrix& target_world) noexcept
    {
        if (dt <= 0.f)
            dt = 0.f;

        float dx = 0.f;
        float dy = 0.f;
        float dz = 0.f;

        if (wnd)
        {
            if (wnd->key_down('A')) dx -= 1.f;
            if (wnd->key_down('D')) dx += 1.f;
            if (wnd->key_down('W')) dy += 1.f;
            if (wnd->key_down('S')) dy -= 1.f;
            if (wnd->key_down(VK_UP))    dz += 1.f;
            if (wnd->key_down(VK_DOWN))  dz -= 1.f;
        }

        float speed = offset_speed_;
        if (wnd && (wnd->key_down(VK_SHIFT) || wnd->key_down(VK_LSHIFT) || wnd->key_down(VK_RSHIFT)))
            speed *= 3.0f;

        offset_.x += dx * speed * dt;
        offset_.y += dy * speed * dt;
        offset_.z += dz * speed * dt;
        offset_.z = std::clamp(offset_.z, min_offset_z_, max_offset_z_);

        const vec4 target(target_world(0, 3), target_world(1, 3), target_world(2, 3), 1.f);
        position_ = target + offset_;
        position_.w = 1.f;

        view_ = fox_math::look_at(position_, target, vec4(0.f, 1.f, 0.f, 0.f));
    }

    void camera::update_free_fly(float dt,
                                 fox::platform_window* wnd,
                                 float mouse_dx,
                                 float mouse_dy,
                                 bool fps_mouse_enabled) noexcept
    {
        if (dt <= 0.f)
            dt = 0.f;

        if (fps_mouse_enabled)
        {
            yaw_ -= mouse_dx;
            pitch_ -= mouse_dy;
            constexpr float pitch_limit = 1.55f;
            pitch_ = std::clamp(pitch_, -pitch_limit, pitch_limit);
        }

        float move_x = 0.f;
        float move_y = 0.f;
        float move_z = 0.f;

        if (wnd)
        {
            if (wnd->key_down('W')) move_z += 1.f;
            if (wnd->key_down('S')) move_z -= 1.f;
            if (wnd->key_down('D')) move_x -= 1.f;
            if (wnd->key_down('A')) move_x += 1.f;
            if (wnd->key_down('Q')) move_y -= 1.f;
            if (wnd->key_down('E')) move_y += 1.f;
        }

        float speed = offset_speed_;
        if (wnd && (wnd->key_down(VK_SHIFT) || wnd->key_down(VK_LSHIFT) || wnd->key_down(VK_RSHIFT)))
            speed *= 3.0f;

        const float cos_pitch = std::cos(pitch_);
        const vec4 forward(
            std::sin(yaw_) * cos_pitch,
            std::sin(pitch_),
            std::cos(yaw_) * cos_pitch,
            0.f);

        const vec4 right(
            std::cos(yaw_),
            0.f,
            -std::sin(yaw_),
            0.f);

        const vec4 up(0.f, 1.f, 0.f, 0.f);

        vec4 move = forward * move_z + right * move_x + up * move_y;
        if (move.length() > 0.0001f)
        {
            move.normalise();
            position_ += move * speed * dt;
            position_.w = 1.f;
        }

        view_ = fox_math::look_at(position_, position_ + forward, up);
    }

    void camera::set_position(const vec4& position) noexcept
    {
        position_ = position;
        position_.w = 1.f;
    }
}
