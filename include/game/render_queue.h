#pragma once

#include "game/game_components.h"
#include "optimized/optimized_renderer.h"
#include "texture_cache.h"
#include "game/static_mesh.h"
#include "game/dynamic_mesh.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fox
{
    class render_queue
    {
    public:
        using object_id = std::uint64_t;

        struct object_info
        {
            object_id id = 0;
            std::string name{};
            bool is_dynamic = false;
            bool visible = true;
            std::string asset_path{};
        };

        struct static_mesh_desc
        {
            std::string path{};
            vec4 position{ 0.f, 0.f, 0.f, 1.f };
            vec4 rotation{ 0.f, 0.f, 0.f, 0.f };
            vec4 scale{ 1.f, 1.f, 1.f, 0.f };
            bool visible = true;
            std::string name{};
            colour colour_tint{ 0.45f, 0.5f, 0.55f };
            float ka = 0.75f;
            float kd = 0.75f;
            object_id forced_id = 0;
        };

        struct dynamic_mesh_desc
        {
            std::string path{};
            vec4 position{ 0.f, 0.f, 0.f, 1.f };
            vec4 rotation{ 0.f, 0.f, 0.f, 0.f };
            vec4 scale{ 1.f, 1.f, 1.f, 0.f };
            bool visible = true;
            std::string name{};
            colour colour_tint{ 0.35f, 0.45f, 0.85f };
            float ka = 0.75f;
            float kd = 0.75f;
            bool anim_enabled = true;
            bool anim_paused = false;
            std::size_t anim_index = 0;
            float playback_speed = 1.0f;
            float time_offset = 0.0f;
            float anim_time = 0.0f;
            object_id forced_id = 0;
        };

        render_queue(
            fecs::world& world,
            std::unordered_map<std::string, std::unique_ptr<static_mesh>>& static_cache,
            std::unordered_map<std::string, std::unique_ptr<dynamic_mesh>>& dynamic_cache,
            texture_cache* tex_cache,
            float normalize_size);

        static void register_components(fecs::world& world);

        object_id add_static_mesh(const static_mesh_desc& desc);
        object_id add_dynamic_mesh(const dynamic_mesh_desc& desc);
        void remove(object_id id);
        [[nodiscard]] bool exists(object_id id) const;

        [[nodiscard]] Transform get_transform(object_id id) const;
        bool try_get_transform(object_id id, Transform*& out);
        bool try_get_static(object_id id, static_mesh_component*& out);
        bool try_get_dynamic(object_id id, dynamic_mesh_component*& out);
        bool try_get_editor_object(object_id id, editor_object_component*& out);
        void enumerate_objects(std::vector<object_info>& out);
        bool try_get_pick_radius(object_id id, float& out_radius) const;
        void set_transform(object_id id, const vec4& pos, const vec4& rot, const vec4& scale);
        void set_visible(object_id id, bool visible);

        void set_animation_enabled(object_id id, bool enabled);
        void set_animation_paused(object_id id, bool paused);
        void set_animation_index(object_id id, std::size_t index);
        void set_playback_speed(object_id id, float speed);
        void set_time_offset(object_id id, float offset);
        void set_anim_time(object_id id, float anim_time);
        void reset_anim_time(object_id id);
        [[nodiscard]] dynamic_anim_state get_anim_state(object_id id) const;
        void tick_dynamic_animations(float dt) noexcept;

        [[nodiscard]] std::vector<fecs::entity> entities(object_id id) const;
        [[nodiscard]] object_id next_object_id() const noexcept { return next_object_id_; }
        void set_next_object_id(const object_id next_id) noexcept { next_object_id_ = next_id; }

    private:
        static matrix make_scale_xyz(float sx, float sy, float sz) noexcept;
        matrix build_normalized_world(
            const vec4& pos,
            const vec4& rot,
            const vec4& scale,
            const vec4& bounds_min,
            const vec4& bounds_max) const noexcept;
        static float clamp_scale_min(float v) noexcept;

        static_mesh* get_static_mesh(const std::string& path);
        dynamic_mesh* get_dynamic_mesh(const std::string& path);

        object_id resolve_id(object_id forced_id);
        static std::string make_default_name(const std::string& path, object_id id);
        void apply_world_to_entities(object_id id, const matrix& base_world) const;

        fecs::world& world_;
        std::unordered_map<std::string, std::unique_ptr<static_mesh>>& static_cache_;
        std::unordered_map<std::string, std::unique_ptr<dynamic_mesh>>& dynamic_cache_;
        texture_cache* tex_cache_ = nullptr;
        float normalize_size_ = 10.f;
        object_id next_object_id_ = 1;
    };
}
