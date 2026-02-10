#pragma once

#include "game/render_queue.h"

namespace fox
{
    class platform_window;
}

namespace fox::editor
{
    struct editor_frame_context
    {
        int viewport_w = 0;
        int viewport_h = 0;
        int mouse_x = 0;
        int mouse_y = 0;
        matrix view = matrix::makeIdentity();
        matrix proj = matrix::makeIdentity();
        vec4 camera_pos{ 0.f, 0.f, 0.f, 1.f };
        bool imgui_wants_mouse = false;
        platform_window* window = nullptr;
        render_queue* rq = nullptr;
    };

    class drag_move_tool
    {
    public:
        void set_enabled(bool enabled) noexcept;
        [[nodiscard]] bool enabled() const noexcept;

        void set_selected_object(render_queue::object_id id) noexcept;

        void on_mouse_button(bool left_down, bool left_pressed, bool left_released) noexcept;
        void on_mouse_move(int x, int y, int dx, int dy) noexcept;

        void tick(const editor_frame_context& ctx);

    private:
        struct ray
        {
            vec4 origin{ 0.f, 0.f, 0.f, 1.f };
            vec4 dir{ 0.f, 0.f, -1.f, 0.f };
        };

        bool start_drag(const editor_frame_context& ctx);
        void stop_drag() noexcept;

        [[nodiscard]] static bool invert_matrix(const matrix& m, matrix& out) noexcept;
        [[nodiscard]] static ray screen_to_world_ray(int mouse_x, int mouse_y, int viewport_w, int viewport_h,
                                                     const matrix& view, const matrix& proj,
                                                     const vec4& fallback_origin) noexcept;
        [[nodiscard]] static bool ray_sphere_hit(const ray& r, const vec4& center, float radius, float& t_out) noexcept;
        [[nodiscard]] static bool ray_plane_hit(const ray& r, const vec4& plane_point,
            const vec4& plane_normal, vec4& out_hit) noexcept;

        bool enabled_ = false;
        bool dragging_ = false;
        render_queue::object_id selected_object_id_ = 0;
        render_queue::object_id dragged_object_id_ = 0;
        bool left_down_ = false;
        bool left_pressed_ = false;
        bool left_released_ = false;
        int mouse_x_ = 0;
        int mouse_y_ = 0;
        int mouse_dx_ = 0;
        int mouse_dy_ = 0;
        vec4 drag_plane_point_{ 0.f, 0.f, 0.f, 1.f };
        vec4 drag_plane_normal_{ 0.f, 1.f, 0.f, 0.f };
        vec4 drag_offset_{ 0.f, 0.f, 0.f, 0.f };
        bool prev_left_down_ = false;
        int prev_mouse_x_ = 0;
        bool has_prev_mouse_ = false;
    };
}
