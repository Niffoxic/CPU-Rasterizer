#pragma once

#include "game/game_components.h"
#include "game/render_queue.h"
#include "texture_cache.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fox
{
    struct world_debug_state
    {
        float fps = 0.0f;
        float delta_time_s = 0.0f;
        bool free_camera_enabled = true;
        bool fps_mouse_enabled = false;
        float camera_mouse_sensitivity = 0.003f;
        bool light_cycle_enabled = true;
        float light_cycle_speed = 0.2f;
        bool light_color_override = false;
        Light light{};
        vec4 camera_pos{ 0.f, 8.f, 18.f, 1.f };
        std::size_t cached_texture_count = 0;
    };

    struct world_render_settings
    {
        bool textures_enabled = true;
        bool flip_v = true;
        optimized_renderer_core::post_process_settings post_process{};
        optimized_renderer_core::rainy_effect_settings rainy_effect{};
        optimized_renderer_core::advanced_effects_settings advanced_effects{};
    };

    struct world_editor_callbacks
    {
        std::function<void(world_debug_state&)> read_debug_state{};
        std::function<void(const world_debug_state&)> write_debug_state{};
        std::function<void()> snap_camera_to_player{};
        std::function<void(world_render_settings&)> read_render_settings{};
        std::function<void(const world_render_settings&)> write_render_settings{};
    };

    class level_builder_ui
    {
    public:
        level_builder_ui() = default;

        void init(fecs::world* world,
                  std::unordered_map<std::string, std::unique_ptr<static_mesh>>* static_cache,
                  texture_cache* tex_cache,
                  float normalize_size);
        void set_render_queue(render_queue* queue);
        void set_save_scene_callback(const std::function<void()>& callback);

        // Draw the imgui level editor panel
        void draw_ui();
        void init_world_editor(const world_editor_callbacks& callbacks);
        void setup_imgui_views();
        void draw_runtime_ui();

        [[nodiscard]] render_queue::object_id selected_runtime_object_id() const noexcept { return selected_runtime_object_id_; }
        [[nodiscard]] bool move_tool_enabled() const noexcept { return runtime_move_tool_enabled_; }
        [[nodiscard]] bool imgui_wants_mouse() const noexcept { return runtime_imgui_wants_mouse_; }

        [[nodiscard]] std::size_t object_count() const;

    private:
        void draw_create_view();
        void draw_edit_save_view();
        void refresh_asset_lists();

        fecs::world* world_ = nullptr;
        std::unordered_map<std::string, std::unique_ptr<static_mesh>>* static_cache_ = nullptr;
        texture_cache* tex_cache_ = nullptr;
        float normalize_size_ = 10.f;

        world_editor_callbacks world_callbacks_{};
        render_queue* render_queue_ = nullptr;
        render_queue::object_id selected_runtime_object_id_ = 0;
        std::string runtime_new_model_path_{};
        std::string runtime_new_name_{};
        bool runtime_new_dynamic_ = false;
        vec4 runtime_new_pos_{ 0.f, 0.f, 0.f, 1.f };
        vec4 runtime_new_rot_{ 0.f, 0.f, 0.f, 0.f };
        vec4 runtime_new_scale_{ 1.f, 1.f, 1.f, 0.f };
        bool runtime_new_anim_enabled_ = true;
        std::size_t runtime_new_anim_index_ = 0;
        float runtime_new_playback_speed_ = 1.0f;
        float runtime_new_time_offset_ = 0.0f;
        float runtime_new_anim_time_ = 0.0f;
        bool runtime_new_anim_paused_ = false;
        float runtime_translate_snap_ = 0.0f;
        float runtime_rotate_snap_deg_ = 0.0f;
        float runtime_scale_snap_ = 0.0f;
        bool runtime_move_tool_enabled_ = false;
        std::vector<std::string> runtime_static_assets_{};
        std::vector<std::string> runtime_dynamic_assets_{};
        int runtime_static_asset_index_ = -1;
        int runtime_dynamic_asset_index_ = -1;
        std::vector<render_queue::object_info> runtime_objects_{};
        bool runtime_imgui_wants_mouse_ = false;

        world_debug_state debug_state_{};
        world_render_settings render_state_{};

        std::function<void()> save_scene_callback_{};
    };
}
