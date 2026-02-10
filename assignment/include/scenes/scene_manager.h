#ifndef FOXRASTERIZER_SCENE_MANAGER_H
#define FOXRASTERIZER_SCENE_MANAGER_H

#include "scenes/interface_scene.h"
#include "optimized/optimized_renderer.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <windows.h>
#include <algorithm>
#include <type_traits>

struct fps_counter
{
    using clock = std::chrono::steady_clock;
    clock::time_point last = clock::now();
    double accum_ms = 0.0;
    std::uint32_t frames = 0;

    double tick() noexcept
    {
        const auto now = clock::now();
        const double dt_ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        accum_ms += dt_ms;
        ++frames;
        if (accum_ms >= 1000.0)
        {
            const double fps = (double)frames * (1000.0 / accum_ms);
            accum_ms = 0.0;
            frames = 0;
            return fps;
        }
        return 0.0;
    }
};

static inline Light make_default_light() noexcept
{
    return {
        vec4(0.f, 1.f, 1.f, 0.f),
        colour(1.f, 1.f, 1.f),
        colour(0.2f, 0.2f, 0.2f)
    };
}

template<class T>
concept scene_has_light_dir = requires(const T s) { s.current_light_dir(); };

template<class TScene>
static inline void run_realtime_scene(const char* tag, const char* window_name)
{
    optimized_renderer_rt r(1024, 768, window_name);

    const Light L = make_default_light();

    // fallback
    vec4 light_dir(0.f, 1.f, 1.f, 0.f);
    light_dir.normalise();

    static_assert(std::is_base_of_v<interface_scene, TScene>);
    TScene scene;
    scene.common().window = &r.windows;
    scene.common().world = &r.world;
    scene.build(r);

    using clock = std::chrono::steady_clock;
    clock::time_point cycle_t0 = clock::now();
    std::uint64_t cycle_presented_frames = 0;
    std::uint32_t cycle_failed_begins = 0;

    bool last_use_fps = false;

    while (true)
    {
        FOX_FRAME();
        r.windows.poll_messages();
        if (r.windows.key_down(VK_ESCAPE))
            break;

        if (r.windows.key_down('F'))
            scene.common().use_fps = !scene.common().use_fps;

        if (scene.common().use_fps != last_use_fps)
        {
            last_use_fps = scene.common().use_fps;
            std::printf("%s USE_FPS: %s\n", tag, scene.common().use_fps ? "ON" : "OFF");
        }

        if (!r.begin_cpu_frame(0xFF000000u))
        {
            ++cycle_failed_begins;
            continue;
        }

        ++cycle_presented_frames;

        const bool cycle_done = scene.tick_cycle_completed(&r.windows);
        const matrix camera = scene.camera_matrix();

        // pull rotating light dir from scene if provided
        if constexpr (scene_has_light_dir<TScene>)
        {
            light_dir = scene.current_light_dir();
        }

        r.draw_world(camera, L, light_dir);
        r.present();

        if (cycle_done)
        {
            const auto cycle_t1 = clock::now();
            const double cycle_ms =
                std::chrono::duration<double, std::milli>(cycle_t1 - cycle_t0).count();

            const double avg_fps = (cycle_ms > 0.0)
                ? (double)cycle_presented_frames * (1000.0 / cycle_ms)
                : 0.0;

            std::printf(
                "%s CYCLE_MS: %.3f | AVG_FPS: %.2f | FAILED_BEGIN: %u\n",
                tag,
                scene.common().last_cycle_ms,
                avg_fps,
                static_cast<unsigned>(cycle_failed_begins)
            );

            cycle_t0 = cycle_t1;
            cycle_presented_frames = 0;
            cycle_failed_begins = 0;
        }
    }

    r.canvas.flush();
}

static inline std::size_t ceil_div_size(std::size_t a, std::size_t b) noexcept
{
    return (b == 0) ? 0 : (a + b - 1) / b;
}

template<class TScene>
static inline void run_offload_scene(
    const char* tag,
    const char* window_name,
    std::size_t record_step,
    std::size_t keep_frames_exact = 0
)
{
    if (record_step == 0) record_step = 1;

    optimized_renderer_rt r(1024, 768, window_name);

    const Light L = make_default_light();

    vec4 light_dir(0.f, 1.f, 1.f, 0.f);
    light_dir.normalise();

    static_assert(std::is_base_of_v<interface_scene, TScene>);
    TScene scene;
    scene.common().window = &r.windows;
    scene.common().world = &r.world;
    scene.build(r);

    r.set_offline_rendering(true);

    std::vector<std::vector<std::uint32_t>> recorded{};
    recorded.reserve((keep_frames_exact > 0) ? keep_frames_exact : 4096);

    using clock = std::chrono::steady_clock;
    const auto record_t0 = clock::now();

    std::uint64_t sim_frames = 0;
    std::uint64_t kept_frames = 0;
    std::uint32_t record_failed_begins = 0;
    bool recorded_done = false;

    while (!recorded_done)
    {
        FOX_FRAME();
        r.windows.poll_messages();
        if (r.windows.key_down(VK_ESCAPE))
            break;

        if (!r.begin_cpu_frame(0xFF000000u))
        {
            ++record_failed_begins;
            continue;
        }

        const bool cycle_done = scene.tick_cycle_completed(&r.windows);
        const matrix camera = scene.camera_matrix();

        if constexpr (scene_has_light_dir<TScene>)
        {
            light_dir = scene.current_light_dir();
        }

        r.draw_world(camera, L, light_dir);

        ++sim_frames;

        if ((sim_frames % record_step) == 0)
        {
            const std::size_t pitch_pixels = (std::size_t)r.framebuffer.pitch_pixels;
            const std::size_t h            = (std::size_t)r.framebuffer.h;
            const std::size_t pixels       = pitch_pixels * h;

            recorded.emplace_back(pixels);
            std::memcpy(recorded.back().data(),
                        r.framebuffer.data,
                        pixels * sizeof(std::uint32_t));

            ++kept_frames;

            if (keep_frames_exact > 0 && kept_frames >= keep_frames_exact)
                recorded_done = true;
        }

        if (keep_frames_exact == 0 && cycle_done && sim_frames > 0)
            recorded_done = true;
    }

    const auto record_t1 = clock::now();
    const double record_ms  = std::chrono::duration<double, std::milli>(record_t1 - record_t0).count();
    const double sim_fps    = (record_ms > 0.0) ? (double)sim_frames * (1000.0 / record_ms) : 0.0;
    const double kept_fps   = (record_ms > 0.0) ? (double)kept_frames * (1000.0 / record_ms) : 0.0;

    std::printf(
        "%s OFFLOAD_RECORD_OFFLINE_MS: %.3f | SIM_FRAMES: %llu | KEPT: %zu | STEP: %zu | SIM_FPS: %.2f | KEPT_FPS: %.2f | FAILED_BEGIN: %u\n",
        tag,
        record_ms,
        (unsigned long long)sim_frames,
        recorded.size(),
        record_step,
        sim_fps,
        kept_fps,
        (unsigned)record_failed_begins
    );

    if (recorded.empty())
    {
        r.set_offline_rendering(false);
        r.canvas.flush();
        return;
    }

    r.set_offline_rendering(false);

    fps_counter fps{};
    std::size_t idx = 0;

    using clock2 = std::chrono::steady_clock;
    clock2::time_point loop_t0 = clock2::now();

    while (true)
    {
        FOX_FRAME();
        r.windows.poll_messages();
        if (r.windows.key_down(VK_ESCAPE))
            break;

        if (!r.begin_cpu_frame(0))
            continue;

        const std::vector<std::uint32_t>& src = recorded[idx];

        const std::size_t pitch_pixels = (std::size_t)r.framebuffer.pitch_pixels;
        const std::size_t h            = (std::size_t)r.framebuffer.h;
        const std::size_t pixels       = pitch_pixels * h;

        const std::size_t copy_pixels = (std::min)(pixels, src.size());
        std::memcpy(r.framebuffer.data, src.data(), copy_pixels * sizeof(std::uint32_t));

        r.present();

        idx = (idx + 1) % recorded.size();

        if (idx == 0)
        {
            const auto loop_t1 = clock2::now();
            const double loop_ms = std::chrono::duration<double, std::milli>(loop_t1 - loop_t0).count();
            std::printf("%s CYCLE MS: %.3f | FRAMES: %zu\n", tag, loop_ms, recorded.size());
            loop_t0 = loop_t1;
        }
    }

    r.canvas.flush();
}

#endif // FOXRASTERIZER_SCENE_MANAGER_H
