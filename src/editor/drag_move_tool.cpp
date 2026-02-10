#include "fox/editor/drag_move_tool.h"
#include "optimized/platform_windows.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace fox::editor
{
    namespace
    {
        constexpr float kPickRadiusBase = 0.75f;
        constexpr float kRayEpsilon = 1e-5f;
        constexpr float kRotateSpeedRadPerPixel = 0.01f;

        vec4 transform_point(const matrix& m, const vec4& p) noexcept
        {
            vec4 out = m * p;
            if (std::fabs(out.w) > kRayEpsilon)
                out.divideW();
            out.w = 1.f;
            return out;
        }
    }

    void drag_move_tool::set_enabled(bool enabled) noexcept
    {
        enabled_ = enabled;
        if (!enabled_)
            stop_drag();
    }

    bool drag_move_tool::enabled() const noexcept
    {
        return enabled_;
    }

    void drag_move_tool::set_selected_object(render_queue::object_id id) noexcept
    {
        selected_object_id_ = id;
    }

    void drag_move_tool::on_mouse_button(bool left_down, bool left_pressed, bool left_released) noexcept
    {
        left_down_ = left_down;
        left_pressed_ = left_pressed;
        left_released_ = left_released;
    }

    void drag_move_tool::on_mouse_move(int x, int y, int dx, int dy) noexcept
    {
        mouse_x_ = x;
        mouse_y_ = y;
        mouse_dx_ = dx;
        mouse_dy_ = dy;
    }

    void drag_move_tool::tick(const editor_frame_context& ctx)
    {
        if (!enabled_ || !ctx.rq)
        {
            stop_drag();
            prev_left_down_ = false;
            has_prev_mouse_ = false;
            return;
        }

        const int mouse_x = ctx.window ? ctx.window->mouse_x() : mouse_x_;
        const int mouse_y = ctx.window ? ctx.window->mouse_y() : mouse_y_;
        int mouse_dx = mouse_dx_;
        if (ctx.window)
        {
            if (has_prev_mouse_)
                mouse_dx = mouse_x - prev_mouse_x_;
            prev_mouse_x_ = mouse_x;
            has_prev_mouse_ = true;
        }
        const bool left_down = ctx.window ? ctx.window->mouse_pressed(mouse_button::left) : left_down_;
        const bool right_down = ctx.window ? ctx.window->mouse_pressed(mouse_button::right) : false;
        const bool left_pressed = left_down && !prev_left_down_;
        const bool left_released = !left_down && prev_left_down_;
        prev_left_down_ = left_down;

        if (ctx.imgui_wants_mouse)
        {
            if (left_released)
                stop_drag();
            return;
        }

        const render_queue::object_id rotate_id = dragging_ ? dragged_object_id_ : selected_object_id_;
        if (right_down && rotate_id != 0 && mouse_dx != 0)
        {
            editor_object_component* rotate_obj = nullptr;
            if (ctx.rq->try_get_editor_object(rotate_id, rotate_obj) && rotate_obj)
            {
                vec4 new_rot = rotate_obj->rotation;
                new_rot.y += static_cast<float>(mouse_dx) * kRotateSpeedRadPerPixel;
                ctx.rq->set_transform(rotate_id, rotate_obj->position, new_rot, rotate_obj->scale);
            }
        }

        if (!dragging_)
        {
            if (left_pressed)
                start_drag(ctx);
            return;
        }

        if (left_released || !left_down)
        {
            stop_drag();
            return;
        }

        editor_object_component* obj = nullptr;
        if (!ctx.rq->try_get_editor_object(dragged_object_id_, obj) || !obj)
        {
            stop_drag();
            return;
        }

        const ray mouse_ray = screen_to_world_ray(
            mouse_x,
            mouse_y,
            ctx.window ? static_cast<int>(ctx.window->width()) : ctx.viewport_w,
            ctx.window ? static_cast<int>(ctx.window->height()) : ctx.viewport_h,
            ctx.view,
            ctx.proj,
            ctx.camera_pos);

        vec4 hit{};
        if (!ray_plane_hit(mouse_ray, drag_plane_point_, drag_plane_normal_, hit))
            return;

        vec4 new_pos = hit + drag_offset_;
        new_pos.y = drag_plane_point_.y;
        new_pos.w = 1.f;

        ctx.rq->set_transform(dragged_object_id_, new_pos, obj->rotation, obj->scale);
    }

    bool drag_move_tool::start_drag(const editor_frame_context& ctx)
    {
        if (!ctx.rq)[[unlikely]]
            return false;

        const int mouse_x = ctx.window ? ctx.window->mouse_x() : mouse_x_;
        const int mouse_y = ctx.window ? ctx.window->mouse_y() : mouse_y_;

        const ray mouse_ray = screen_to_world_ray(
            mouse_x,
            mouse_y,
            ctx.window ? static_cast<int>(ctx.window->width()) : ctx.viewport_w,
            ctx.window ? static_cast<int>(ctx.window->height()) : ctx.viewport_h,
            ctx.view,
            ctx.proj,
            ctx.camera_pos);

        std::vector<render_queue::object_info> objects;
        ctx.rq->enumerate_objects(objects);

        float best_t = std::numeric_limits<float>::max();
        render_queue::object_id best_id = 0;
        float selected_t = std::numeric_limits<float>::max();
        render_queue::object_id selected_hit = 0;

        for (const render_queue::object_info& info : objects)
        {
            if (!info.visible)
                continue;

            editor_object_component* obj = nullptr;
            if (!ctx.rq->try_get_editor_object(info.id, obj) || !obj)
                continue;

            float radius = kPickRadiusBase;
            if (!ctx.rq->try_get_pick_radius(info.id, radius))
            {
                const float max_scale = (std::max)({ std::fabs(obj->scale.x), std::fabs(obj->scale.y), std::fabs(obj->scale.z), 0.1f });
                radius = kPickRadiusBase * max_scale;
            }

            float t = 0.f;
            if (!ray_sphere_hit(mouse_ray, obj->position, radius, t))
                continue;

            if (info.id == selected_object_id_)
            {
                if (t < selected_t)
                {
                    selected_t = t;
                    selected_hit = info.id;
                }
                continue;
            }

            if (t < best_t)
            {
                best_t = t;
                best_id = info.id;
            }
        }

        dragged_object_id_ = (selected_hit != 0) ? selected_hit : best_id;
        if (dragged_object_id_ == 0)
            return false;

        editor_object_component* grabbed_obj = nullptr;
        if (!ctx.rq->try_get_editor_object(dragged_object_id_, grabbed_obj) || !grabbed_obj)
        {
            dragged_object_id_ = 0;
            return false;
        }

        drag_plane_point_ = grabbed_obj->position;
        drag_plane_normal_ = vec4(0.f, 1.f, 0.f, 0.f);

        vec4 plane_hit{};
        if (!ray_plane_hit(mouse_ray, drag_plane_point_, drag_plane_normal_, plane_hit))
        {
            dragged_object_id_ = 0;
            return false;
        }

        drag_offset_ = grabbed_obj->position - plane_hit;
        drag_offset_.w = 0.f;
        dragging_ = true;
        return true;
    }

    void drag_move_tool::stop_drag() noexcept
    {
        dragging_ = false;
        dragged_object_id_ = 0;
    }

    bool drag_move_tool::invert_matrix(const matrix& m, matrix& out) noexcept
    {
        float a[16];
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                a[row * 4 + col] = m(row, col);

        float inv[16];

        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

        const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (std::fabs(det) <= kRayEpsilon)
            return false;

        const float inv_det = 1.f / det;
        for (int i = 0; i < 16; ++i)
            inv[i] *= inv_det;

        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                out(row, col) = inv[row * 4 + col];

        return true;
    }

    drag_move_tool::ray drag_move_tool::screen_to_world_ray(
        int mouse_x, int mouse_y, int viewport_w, int viewport_h,
        const matrix& view, const matrix& proj,
        const vec4& fallback_origin) noexcept
    {
        ray out{};
        out.origin = fallback_origin;
        out.origin.w = 1.f;

        if (viewport_w <= 0 || viewport_h <= 0)
            return out;

        matrix inv_vp{};
        if (!invert_matrix(proj * view, inv_vp))
            return out;

        const float nx = (2.f * static_cast<float>(mouse_x) / static_cast<float>(viewport_w)) - 1.f;
        const float ny = 1.f - (2.f * static_cast<float>(mouse_y) / static_cast<float>(viewport_h));

        const vec4 near_clip(nx, ny, 0.f, 1.f);
        const vec4 far_clip(nx, ny, 1.f, 1.f);

        const vec4 near_world = transform_point(inv_vp, near_clip);
        const vec4 far_world = transform_point(inv_vp, far_clip);

        vec4 dir = far_world - near_world;
        const float len = dir.length();
        if (len > kRayEpsilon)
            dir *= (1.f / len);
        else
            dir = vec4(0.f, 0.f, -1.f, 0.f);

        dir.w = 0.f;
        out.origin = near_world;
        out.origin.w = 1.f;
        out.dir = dir;
        return out;
    }

    bool drag_move_tool::ray_sphere_hit(const ray& r, const vec4& center, float radius, float& t_out) noexcept
    {
        const vec4 oc = r.origin - center;
        const float a = vec4::dot(r.dir, r.dir);
        const float b = 2.f * vec4::dot(oc, r.dir);
        const float c = vec4::dot(oc, oc) - radius * radius;

        const float discriminant = b * b - 4.f * a * c;
        if (discriminant < 0.f)
            return false;

        const float sqrt_d = std::sqrt(discriminant);
        const float inv_2a = 1.f / (2.f * a);
        float t0 = (-b - sqrt_d) * inv_2a;
        float t1 = (-b + sqrt_d) * inv_2a;
        if (t0 > t1)
            std::swap(t0, t1);

        if (t1 < 0.f)
            return false;

        t_out = (t0 >= 0.f) ? t0 : t1;
        return true;
    }

    bool drag_move_tool::ray_plane_hit(const ray& r, const vec4& plane_point, const vec4& plane_normal, vec4& out_hit) noexcept
    {
        const float denom = vec4::dot(plane_normal, r.dir);
        if (std::fabs(denom) <= kRayEpsilon)
            return false;

        const vec4 p0l0 = plane_point - r.origin;
        const float t = vec4::dot(p0l0, plane_normal) / denom;
        if (t < 0.f)
            return false;

        out_hit = r.origin + (r.dir * t);
        out_hit.w = 1.f;
        return true;
    }
}
