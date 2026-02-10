#pragma once

#include "fmath.h"

#include <cstddef>
#include <cstdint>
#include <string>

class dynamic_mesh;
class static_mesh;
struct dynamic_mesh_instance;

namespace fox
{
    struct animation_controller_component
    {
        dynamic_mesh* mesh = nullptr;
        std::size_t current_anim = 0;
        float playback_speed = 1.0f;
        float time_offset = 0.0f;
    };

    struct animation_cycle_component
    {
        float timer = 0.f;
        float interval = 5.f;
        std::uint32_t rng_state = 1u;
    };

    struct animation_instance_component
    {
        dynamic_mesh_instance* instance = nullptr;
    };

    struct dynamic_anim_state
    {
        bool enabled = true;
        bool paused = false;
        std::size_t index = 0;
        float playback_speed = 1.0f;
        float time_offset = 0.0f;
        float anim_time = 0.0f;
    };

    struct static_mesh_component
    {
        static_mesh* mesh = nullptr;
        std::string path{};
        bool visible = true;
    };

    struct dynamic_mesh_component
    {
        dynamic_mesh* mesh = nullptr;
        std::string path{};
        bool visible = true;
        dynamic_anim_state anim{};
    };

    struct character_tag {};
    struct static_tag {};
    struct camera_target {};
  
    struct level_object_tag
    {
        std::size_t object_index = 0;
    };
    struct editor_tag {};

    struct editor_local_component
    {
        matrix local{};
    };

    struct editor_object_component
    {
        std::uint64_t object_id = 0;
        std::string name{};
        std::string model{};
        vec4 position{ 0.f, 0.f, 0.f, 1.f };
        vec4 rotation{ 0.f, 0.f, 0.f, 0.f };
        vec4 scale{ 1.f, 1.f, 1.f, 0.f };
        bool is_dynamic = false;
        bool anim_enabled = true;
        bool anim_paused = false;
        std::size_t anim_index = 0;
        float playback_speed = 1.0f;
        float time_offset = 0.0f;
        float anim_time = 0.0f;
    };
}
