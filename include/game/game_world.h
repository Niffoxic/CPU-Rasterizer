#pragma once

#include "optimized/optimized_renderer.h"
#include "texture_cache.h"
#include "camera.h"
#include "static_mesh.h"
#include "dynamic_mesh.h"
#include "level_builder_ui.h"
#include "render_queue.h"
#include "fox/scene_io.h"
#include "fox/editor/drag_move_tool.h"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fox
{
    struct game_world_config
    {
        std::uint32_t w = 1024u;
        std::uint32_t h = 768u;
        const char* title = "FoxGame";
        bool offline = false;
        std::uint32_t clear_rgba = 0;
    };

    class game_world
    {
    public:
        explicit game_world(const game_world_config& config);
        void init();
        void run();

    private:
        [[nodiscard]] bool is_mouse_safe_for_editing() const noexcept;
        void update_light_cycle(float dt) noexcept;
        void update_free_camera(float dt) noexcept;
        void update_timing(std::chrono::steady_clock::time_point now) noexcept;

        [[nodiscard]] scene_io::scene_post_processing_settings post_processing_settings() const noexcept;
        void apply_post_processing_settings(const scene_io::scene_post_processing_settings& settings) noexcept;

        game_world_config config_{};
        optimized_renderer_rt renderer_;

        Light default_light_{};
        vec4 light_dir_{ 0.f, 1.f, 1.f, 0.f };

        texture_cache tex_cache_{};
        std::unordered_map<std::string, std::unique_ptr<static_mesh>> static_mesh_cache_{};
        std::unordered_map<std::string, std::unique_ptr<dynamic_mesh>> dynamic_mesh_cache_{};

        camera camera_{};

        bool free_camera_enabled_ = true;
        bool fps_mouse_enabled_ = false;
        bool space_was_down_ = false;
        bool free_cam_toggle_was_down_ = false;
        float camera_mouse_sensitivity_ = 0.003f;
        vec4 camera_pos_{ 0.f, 8.f, 18.f, 1.f };

        bool light_cycle_enabled_ = true;
        float light_cycle_speed_ = 0.2f;
        bool light_color_override_ = false;

        float normalize_size_ = 10.f;

        std::chrono::steady_clock::time_point start_time_{};
        std::chrono::steady_clock::time_point last_time_{};
        float delta_time_s_ = 0.f;
        float elapsed_time_s_ = 0.f;
        float fps_ = 0.f;
        double fps_accum_s_ = 0.0;
        std::uint32_t fps_frames_ = 0u;
        bool initialized_ = false;

        std::unique_ptr<render_queue> render_queue_{};
        std::unique_ptr<scene_io> scene_io_{};

        level_builder_ui level_editor_{};
        editor::drag_move_tool drag_move_tool_{};
        Transform* camera_target_transform_ = nullptr;
        int prev_mouse_x_ = 0;
        int prev_mouse_y_ = 0;
        bool has_prev_mouse_ = false;
        bool prev_left_mouse_down_ = false;
    };
}
