#pragma once

#include "game/game_components.h"
#include "game/render_queue.h"
#include "fox/scene_paths.h"
#include "json_loader.h"
#include "optimized/optimized_renderer.h"

#include <cstdint>
#include <functional>
#include <string>

namespace fox
{
    struct scene_save_desc
    {
        std::string path{};
        std::string scene_name = "scene";
        bool include_globals = true;
    };

    struct scene_load_desc
    {
        std::string path{};
        bool clear_existing = true;
    };

    class scene_io
    {
    public:
        struct scene_post_processing_settings
        {
            optimized_renderer_core::post_process_settings post_process{};
            optimized_renderer_core::rainy_effect_settings rainy_effect{};
            optimized_renderer_core::advanced_effects_settings advanced_effects{};
        };

        scene_io(fecs::world& world, render_queue& queue);

        void set_post_processing_callbacks(
            std::function<void(scene_post_processing_settings&)> read_callback,
            std::function<void(const scene_post_processing_settings&)> write_callback);

        bool save_scene(const scene_save_desc& desc);
        bool load_scene(const scene_load_desc& desc);

        [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

    private:
        struct scene_object_record
        {
            std::uint64_t id = 0;
            std::string name{};
            std::string model{};
            bool is_dynamic = false;
            bool visible = true;
            vec4 position{ 0.f, 0.f, 0.f, 1.f };
            vec4 rotation{ 0.f, 0.f, 0.f, 0.f };
            vec4 scale{ 1.f, 1.f, 1.f, 0.f };
            dynamic_anim_state anim{};
        };

        void set_error(const std::string& message);
        void clear_error();
        void clear_existing_editor_objects();

        void write_globals(JsonLoader& scene, bool include_globals);
        void read_globals(const JsonLoader& scene);
        void write_post_processing(JsonLoader& globals);
        void read_post_processing(const JsonLoader& globals);
        static void write_colour(JsonLoader& node, const colour& value);
        static void read_colour(const JsonLoader& node, colour& value);
        static void write_vec4_xy(JsonLoader& node, const vec4& value);
        static void read_vec4_xy(const JsonLoader& node, vec4& value);

        bool parse_scene_object(const JsonLoader& node, scene_object_record& out) const;
        std::uint64_t parse_u64(const JsonLoader& node, std::uint64_t fallback) const;

        fecs::world& world_;
        render_queue& queue_;
        std::function<void(scene_post_processing_settings&)> read_post_processing_callback_{};
        std::function<void(const scene_post_processing_settings&)> write_post_processing_callback_{};
        std::string last_error_{};
    };
}
