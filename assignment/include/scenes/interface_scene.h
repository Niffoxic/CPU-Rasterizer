#ifndef FOXRASTERIZER_INTERFACE_SCENE_H
#define FOXRASTERIZER_INTERFACE_SCENE_H

#include "optimized/optimized_renderer.h"

#include <chrono>

#ifdef USE_SIMD
#include <immintrin.h>
inline void simd_add_scalar(float& value, float delta) noexcept
{
    __m128 v = _mm_load_ss(&value);
    __m128 d = _mm_load_ss(&delta);
    v = _mm_add_ss(v, d);
    _mm_store_ss(&value, v);
}
#else
inline void simd_add_scalar(float& value, float delta) noexcept
{
    value += delta;
}
#endif

static inline Transform* get_transform_ptr(fecs::world& w, fecs::entity e) noexcept
{
    const auto loc = w.get_location(e);
    auto& tbl = w.storage().get_table(loc.tid);
    return tbl.get_array<Transform>(w.registry()) + loc.row;
}

struct scene_common_state
{
    fox::platform_window* window = nullptr;
    fecs::world* world = nullptr;
    bool use_fps = false;
    int cycle_edges = 0;
    double last_cycle_ms = 0.0;
    using hclock = std::chrono::high_resolution_clock;
    hclock::time_point cycle_start = hclock::now();
    mutable matrix cam_cached{};
    float ka = 0.75f;
    float kd = 0.75f;
};

__interface interface_scene
{
    scene_common_state& common() noexcept;
    const scene_common_state& common() const noexcept;

    void build(optimized_renderer_core& r);
    bool tick_cycle_completed(fox::platform_window* windows) noexcept;

    [[nodiscard]] matrix camera_matrix() const noexcept;
    [[nodiscard]] bool cycle_complete_once() const noexcept;
};

#endif // FOXRASTERIZER_INTERFACE_SCENE_H
