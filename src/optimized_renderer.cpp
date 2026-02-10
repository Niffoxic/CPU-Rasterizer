#include "optimized/optimized_renderer.h"

#include <algorithm>
#include <limits>
#include <cmath>
#include <cstring>

#ifdef USE_SIMD
#include <immintrin.h>
#endif

#ifdef USE_SIMD
static inline bool tri_outside_frustum_simd(const vec4& hp0, const vec4& hp1, const vec4& hp2) noexcept
{
    const __m128 x = _mm_set_ps(hp2[0], hp2[0], hp1[0], hp0[0]);
    const __m128 y = _mm_set_ps(hp2[1], hp2[1], hp1[1], hp0[1]);
    const __m128 z = _mm_set_ps(hp2[2], hp2[2], hp1[2], hp0[2]);
    const __m128 w = _mm_set_ps(hp2[3], hp2[3], hp1[3], hp0[3]);

    const __m128 neg_w = _mm_sub_ps(_mm_setzero_ps(), w);
    const int left_mask   = _mm_movemask_ps(_mm_cmplt_ps(x, neg_w));
    const int right_mask  = _mm_movemask_ps(_mm_cmpgt_ps(x, w));
    const int bottom_mask = _mm_movemask_ps(_mm_cmplt_ps(y, neg_w));
    const int top_mask    = _mm_movemask_ps(_mm_cmpgt_ps(y, w));
    const int near_mask   = _mm_movemask_ps(_mm_cmplt_ps(z, _mm_setzero_ps()));
    const int far_mask    = _mm_movemask_ps(_mm_cmpgt_ps(z, w));

    constexpr int tri_mask = 0x7;
    return ((left_mask & tri_mask) == tri_mask) ||
           ((right_mask & tri_mask) == tri_mask) ||
           ((bottom_mask & tri_mask) == tri_mask) ||
           ((top_mask & tri_mask) == tri_mask) ||
           ((near_mask & tri_mask) == tri_mask) ||
           ((far_mask & tri_mask) == tri_mask);
}
#endif

static inline float edge_fn(float ax, float ay, float bx, float by, float px, float py) noexcept
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static inline float clamp01(float v) noexcept
{
    return (v < 0.f) ? 0.f : (v > 1.f ? 1.f : v);
}

static inline float fract(float v) noexcept
{
    return v - std::floor(v);
}

static inline float hash11(float x) noexcept
{
    return fract(std::sin(x * 12.9898f) * 43758.5453f);
}

static inline std::uint32_t pack_rgba8_from_colour(const colour& c) noexcept
{
    const std::uint8_t r = (std::uint8_t)(clamp01(c.r) * 255.f);
    const std::uint8_t g = (std::uint8_t)(clamp01(c.g) * 255.f);
    const std::uint8_t b = (std::uint8_t)(clamp01(c.b) * 255.f);
    const std::uint8_t a = 255u;

    return (std::uint32_t(a) << 24) |
           (std::uint32_t(b) << 16) |
           (std::uint32_t(g) <<  8) |
           (std::uint32_t(r) <<  0);
}

static inline std::uint32_t modulate_texture(std::uint32_t tex_rgba, float intensity) noexcept
{
    const float r_f = (float)((tex_rgba >>  0) & 0xFFu) * intensity;
    const float g_f = (float)((tex_rgba >>  8) & 0xFFu) * intensity;
    const float b_f = (float)((tex_rgba >> 16) & 0xFFu) * intensity;

    const std::uint8_t r = (std::uint8_t)(std::min)(r_f, 255.f);
    const std::uint8_t g = (std::uint8_t)(std::min)(g_f, 255.f);
    const std::uint8_t b = (std::uint8_t)(std::min)(b_f, 255.f);

    return (255u << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(r);
}

static inline void unpack_rgba8(std::uint32_t rgba, float& r, float& g, float& b) noexcept
{
    r = ((rgba >> 0) & 0xFFu) / 255.f;
    g = ((rgba >> 8) & 0xFFu) / 255.f;
    b = ((rgba >> 16) & 0xFFu) / 255.f;
}

static inline std::uint32_t pack_rgba8_from_rgb(float r, float g, float b) noexcept
{
    const std::uint8_t rr = (std::uint8_t)(clamp01(r) * 255.f);
    const std::uint8_t gg = (std::uint8_t)(clamp01(g) * 255.f);
    const std::uint8_t bb = (std::uint8_t)(clamp01(b) * 255.f);
    return (0xFFu << 24) | (std::uint32_t(bb) << 16) | (std::uint32_t(gg) << 8) | std::uint32_t(rr);
}

optimized_renderer_core::optimized_renderer_core(std::uint32_t w, std::uint32_t h, const char* name)
{
    fox::create_window_params wp{};
    wp.width = w;
    wp.height = h;
    wp.window_name = name;
    windows.create(wp);

    fox::create_dx11_params dx{};
    dx.width = w;
    dx.height = h;
    dx.hwnd = windows.native_hwnd();
    dx.ring_size = 3; // TODO: Tune it later
    canvas.create(dx);

    perspective = matrix::makePerspective(90.f * fox_math::pi_f / 180.f, (float)w / (float)h, 0.1f, 100.f);

    world.register_component<MeshRefPN>();
    world.register_component<Transform>();
    world.register_component<Material>();
    world.register_component<TextureRef>();

    cube_asset = build_asset_from_indexed_mesh(Mesh::makeCube(1.f));

    init_persistent_workers();
}

optimized_renderer_core::~optimized_renderer_core()
{
    shutdown_persistent_workers();
    canvas.flush();
}

void optimized_renderer_core::set_offline_rendering(bool v) noexcept
{
    m_offline = v;

    if (m_offline)
    {
        ensure_offline_targets(framebuffer.w ? framebuffer.w : 1024u,
                               framebuffer.h ? framebuffer.h : 768u);
    }
}

void optimized_renderer_core::apply_post_process(const post_process_settings& settings) noexcept
{
    if (!settings.enabled) return;
    if (!framebuffer.data || framebuffer.w == 0 || framebuffer.h == 0) return;
    if (!settings.exposure_enabled && !settings.contrast_enabled &&
        !settings.saturation_enabled && !settings.vignette_enabled)
        return;

    compute_slice_ranges(framebuffer.h);

    {
        std::lock_guard<std::mutex> lg(m_job_mtx);
        m_job.W = framebuffer.w;
        m_job.H = framebuffer.h;
        m_job.fw = settings.exposure;
        m_job.fh = settings.vignette_power;
        m_job.post_settings = settings;
        m_job_type = job_type::post_process;
        m_done_count = 0;
        ++m_job_gen;
    }
    m_job_cv.notify_all();

    const worker_range main_r = m_ranges[kTotalSlices - 1];
    if (main_r.y0 <= main_r.y1)
        post_process_slice(main_r.y0, main_r.y1);

    {
        std::unique_lock<std::mutex> lk(m_job_mtx);
        m_done_cv.wait(lk, [&]() { return m_done_count == kWorkerCount; });
    }
}

void optimized_renderer_core::apply_rainy_effect(const rainy_effect_settings& settings, float time_s) noexcept
{
    if (!settings.enabled) return;
    if (!framebuffer.data || framebuffer.w == 0 || framebuffer.h == 0) return;
    if (!zbuffer.data || zbuffer.w == 0 || zbuffer.h == 0) return;
    if (settings.intensity <= 0.f || settings.streak_probability <= 0.f) return;

    compute_slice_ranges(framebuffer.h);

    {
        std::lock_guard<std::mutex> lg(m_job_mtx);
        m_job.W = framebuffer.w;
        m_job.H = framebuffer.h;
        m_job.rain_settings = settings;
        m_job.time_s = time_s;
        m_job_type = job_type::rainy_effect;
        m_done_count = 0;
        ++m_job_gen;
    }
    m_job_cv.notify_all();

    const worker_range main_r = m_ranges[kTotalSlices - 1];
    if (main_r.y0 <= main_r.y1)
        rainy_effect_slice(main_r.y0, main_r.y1);

    {
        std::unique_lock<std::mutex> lk(m_job_mtx);
        m_done_cv.wait(lk, [&]() { return m_done_count == kWorkerCount; });
    }
}

void optimized_renderer_core::apply_advanced_effects(const advanced_effects_settings& settings, float time_s) noexcept
{
    if (!settings.enabled) return;
    if (!framebuffer.data || framebuffer.w == 0 || framebuffer.h == 0) return;

    const bool any_enabled = settings.bloom_enabled || settings.film_grain_enabled ||
                             settings.motion_blur_enabled || settings.fog_enabled ||
                             settings.ssr_enabled || settings.depth_of_field_enabled ||
                             settings.god_rays_enabled;
    if (!any_enabled) return;

    if ((settings.fog_enabled || settings.depth_of_field_enabled || settings.ssr_enabled) &&
        (!zbuffer.data || zbuffer.w == 0 || zbuffer.h == 0))
        return;

    compute_slice_ranges(framebuffer.h);

    {
        std::lock_guard<std::mutex> lg(m_job_mtx);
        m_job.W = framebuffer.w;
        m_job.H = framebuffer.h;
        m_job.advanced_settings = settings;
        m_job.time_s = time_s;
        m_job_type = job_type::advanced_effects;
        m_done_count = 0;
        ++m_job_gen;
    }
    m_job_cv.notify_all();

    const worker_range main_r = m_ranges[kTotalSlices - 1];
    if (main_r.y0 <= main_r.y1)
        advanced_effects_slice(main_r.y0, main_r.y1);

    {
        std::unique_lock<std::mutex> lk(m_job_mtx);
        m_done_cv.wait(lk, [&]() { return m_done_count == kWorkerCount; });
    }
}

void optimized_renderer_core::init_persistent_workers() noexcept
{
    for (int i = 0; i < kWorkerCount; ++i)
        m_workers[i] = std::thread([this, i]() { worker_loop(i); });
}

void optimized_renderer_core::shutdown_persistent_workers() noexcept
{
    m_shutdown.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lg(m_job_mtx);
        ++m_job_gen;
    }
    m_job_cv.notify_all();

    for (int i = 0; i < kWorkerCount; ++i)
    {
        if (m_workers[i].joinable())
            m_workers[i].join();
    }
}

void optimized_renderer_core::worker_loop(int worker_index) noexcept
{
    std::uint64_t seen_gen = 0;
    job_type local_type = job_type::draw;

    for (;;)
    {
        {
            std::unique_lock<std::mutex> lk(m_job_mtx);
            m_job_cv.wait(lk, [&]() {
                return m_shutdown.load(std::memory_order_acquire) || (m_job_gen != seen_gen);
            });

            if (m_shutdown.load(std::memory_order_acquire))
                return;

            seen_gen = m_job_gen;
            local_type = m_job_type;
        }

        const worker_range r = m_ranges[worker_index];
        if (r.y0 <= r.y1)
        {
            if (local_type == job_type::draw)
                draw_world_slice(r.y0, r.y1);
            else if (local_type == job_type::post_process)
                post_process_slice(r.y0, r.y1);
            else if (local_type == job_type::rainy_effect)
                rainy_effect_slice(r.y0, r.y1);
            else
                advanced_effects_slice(r.y0, r.y1);
        }

        {
            std::lock_guard<std::mutex> lg(m_job_mtx);
            ++m_done_count;
            if (m_done_count == kWorkerCount)
                m_done_cv.notify_one();
        }
    }
}

void optimized_renderer_core::compute_slice_ranges(std::uint32_t H) noexcept
{
    const int total = kTotalSlices;
    const int h = (int)H;

    const int base = h / total;
    const int rem  = h % total;

    constexpr int kRowAlign = 4;
    int y = 0;
    for (int s = 0; s < total; ++s)
    {
        const int slice_h = base + (s < rem ? 1 : 0);
        const int y0 = y;
        int y1 = y + slice_h - 1;
        if (s == total - 1)
        {
            y1 = h - 1;
        }
        else
        {
            const int aligned_end = ((y1 + 1 + (kRowAlign - 1)) / kRowAlign) * kRowAlign - 1;
            if (aligned_end < h - 1) y1 = aligned_end;
        }
        m_ranges[s] = worker_range{ y0, y1 };
        y = y1 + 1;
    }
}

void optimized_renderer_core::bind_targets_from_frame(const fox::cpu_frame& f) noexcept
{
    const std::uint32_t pitch_pixels = (f.color_pitch_bytes >> 2);
    framebuffer.bind(f.w, f.h, f.color_pitch_bytes, pitch_pixels, reinterpret_cast<std::uint32_t*>(f.color));

    zbuffer.w = f.w;
    zbuffer.h = f.h;
    zbuffer.pitch = f.z_pitch;
    zbuffer.data = f.z;
}

void optimized_renderer_core::clear_color_rgba(std::uint32_t rgba) const noexcept
{
    if (!framebuffer.data || framebuffer.h == 0 || framebuffer.pitch_bytes == 0 || framebuffer.pitch_pixels == 0) return;

    if (rgba == 0)
    {
        const std::size_t bytes = (std::size_t)framebuffer.pitch_bytes * (std::size_t)framebuffer.h;
        std::memset(framebuffer.data, 0, bytes);
        return;
    }

    const std::uint32_t W = framebuffer.w;
    const std::uint32_t H = framebuffer.h;

#ifdef USE_SIMD
    const __m128i rgba4 = _mm_set1_epi32(static_cast<int>(rgba));
    for (std::uint32_t y = 0; y < H; ++y)
    {
        std::uint32_t* row = framebuffer.data + (std::size_t)y * (std::size_t)framebuffer.pitch_pixels;
        std::uint32_t x = 0;
        const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(row);
        if (addr & 0xF)
        {
            const std::uint32_t align = (16u - (addr & 0xFu)) >> 2;
            const std::uint32_t pre = (std::min)(align, W);
            for (; x < pre; ++x) row[x] = rgba;
        }
        for (; x + 4 <= W; x += 4)
            _mm_stream_si128(reinterpret_cast<__m128i*>(row + x), rgba4);
        for (; x < W; ++x)
            row[x] = rgba;
    }
    _mm_sfence();
#else
    for (std::uint32_t y = 0; y < H; ++y)
    {
        std::uint32_t* row = framebuffer.data + (std::size_t)y * (std::size_t)framebuffer.pitch_pixels;
        std::fill_n(row, W, rgba);
    }
#endif
}

bool optimized_renderer_core::begin_cpu_frame(std::uint32_t clear_rgba) noexcept
{
    if (m_offline)
    {
        ensure_offline_targets(1024u, 768u);

        framebuffer.bind(
            m_offline_targets.w,
            m_offline_targets.h,
            m_offline_targets.pitch_bytes,
            m_offline_targets.pitch_pixels,
            m_offline_targets.color.data()
        );

        zbuffer.w     = m_offline_targets.w;
        zbuffer.h     = m_offline_targets.h;
        zbuffer.pitch = m_offline_targets.z_pitch;
        zbuffer.data  = m_offline_targets.z.data();

        if (clear_rgba != 0)
            clear_color_rgba(clear_rgba);

        if (zbuffer.data)
        {
            const std::size_t bytes = (std::size_t)zbuffer.pitch * (std::size_t)zbuffer.h * sizeof(float);
            std::memset(zbuffer.data, 0, bytes);
        }

        return true;
    }

    cur_frame = canvas.try_begin_frame();
    if (!cur_frame.valid())
        return false;

    bind_targets_from_frame(cur_frame);

    if (clear_rgba != 0)
        clear_color_rgba(clear_rgba);

    return true;
}

void optimized_renderer_core::draw_world(const matrix& cam, const Light&, const vec4& light_dir_in) noexcept
{
    if (!framebuffer.data || !zbuffer.data) return;

    refresh_render_cache();
    if (!pinned_draw_ready || pinned_draw_blocks.empty()) return;

    const std::uint32_t W = framebuffer.w;
    const std::uint32_t H = framebuffer.h;

    compute_slice_ranges(H);

    {
        std::lock_guard<std::mutex> lg(m_job_mtx);
        m_job.W = W;
        m_job.H = H;
        m_job.fw = static_cast<float>(W);
        m_job.fh = static_cast<float>(H);
        m_job.textures_on = textures_enabled;
        m_job.flip_v_on   = flip_v;
        m_job.vp = perspective * cam;
        m_job.light_dir = light_dir_in;
        m_job_type = job_type::draw;
        m_done_count = 0;
        ++m_job_gen;
    }
    m_job_cv.notify_all();

    const worker_range main_r = m_ranges[kTotalSlices - 1];
    if (main_r.y0 <= main_r.y1)
        draw_world_slice(main_r.y0, main_r.y1);

    {
        std::unique_lock<std::mutex> lk(m_job_mtx);
        m_done_cv.wait(lk, [&]() { return m_done_count == kWorkerCount; });
    }
}

void optimized_renderer_core::post_process_slice(int y0, int y1) const noexcept
{
    if (!framebuffer.data || framebuffer.w == 0 || framebuffer.h == 0) return;

    const post_process_settings settings = m_job.post_settings;
    if (!settings.enabled)
        return;
    if (!settings.exposure_enabled && !settings.contrast_enabled &&
        !settings.saturation_enabled && !settings.vignette_enabled)
        return;

    const float inv_w = 1.f / static_cast<float>(framebuffer.w);
    const float inv_h = 1.f / static_cast<float>(framebuffer.h);
    const float half_w = 0.5f * static_cast<float>(framebuffer.w);
    const float half_h = 0.5f * static_cast<float>(framebuffer.h);

    const std::uint32_t W = framebuffer.w;

    for (int y = y0; y <= y1; ++y)
    {
        std::uint32_t* row = framebuffer.data + (std::size_t)y * (std::size_t)framebuffer.pitch_pixels;
        const float fy = (static_cast<float>(y) - half_h) * inv_h;
#ifdef USE_SIMD
        const __m128 inv255 = _mm_set1_ps(1.f / 255.f);
        const __m128 zero = _mm_set1_ps(0.f);
        const __m128 one = _mm_set1_ps(1.f);
        const __m128 exposure_v = _mm_set1_ps(settings.exposure);
        const __m128 contrast_v = _mm_set1_ps(settings.contrast);
        const __m128 saturation_v = _mm_set1_ps(settings.saturation);
        const __m128 half_v = _mm_set1_ps(0.5f);
        const __m128 strength_v = _mm_set1_ps(settings.vignette_strength);
        const __m128 fy_v = _mm_set1_ps(fy);
        const __m128 inv_w_v = _mm_set1_ps(inv_w);
        const __m128 half_w_v = _mm_set1_ps(half_w);
        const __m128 lum_r = _mm_set1_ps(0.2126f);
        const __m128 lum_g = _mm_set1_ps(0.7152f);
        const __m128 lum_b = _mm_set1_ps(0.0722f);
        const __m128 step = _mm_set_ps(3.f, 2.f, 1.f, 0.f);

        std::uint32_t x = 0;
        for (; x + 4 <= W; x += 4)
        {
            const __m128i rgba = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + x));
            const __m128i r_i = _mm_and_si128(rgba, _mm_set1_epi32(0xFF));
            const __m128i g_i = _mm_and_si128(_mm_srli_epi32(rgba, 8), _mm_set1_epi32(0xFF));
            const __m128i b_i = _mm_and_si128(_mm_srli_epi32(rgba, 16), _mm_set1_epi32(0xFF));

            __m128 r = _mm_mul_ps(_mm_cvtepi32_ps(r_i), inv255);
            __m128 g = _mm_mul_ps(_mm_cvtepi32_ps(g_i), inv255);
            __m128 b = _mm_mul_ps(_mm_cvtepi32_ps(b_i), inv255);

            if (settings.exposure_enabled)
            {
                r = _mm_mul_ps(r, exposure_v);
                g = _mm_mul_ps(g, exposure_v);
                b = _mm_mul_ps(b, exposure_v);
            }

            if (settings.contrast_enabled)
            {
                r = _mm_add_ps(_mm_mul_ps(_mm_sub_ps(r, half_v), contrast_v), half_v);
                g = _mm_add_ps(_mm_mul_ps(_mm_sub_ps(g, half_v), contrast_v), half_v);
                b = _mm_add_ps(_mm_mul_ps(_mm_sub_ps(b, half_v), contrast_v), half_v);
            }

            if (settings.saturation_enabled)
            {
                const __m128 lum = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r, lum_r), _mm_mul_ps(g, lum_g)), _mm_mul_ps(b, lum_b));
                r = _mm_add_ps(lum, _mm_mul_ps(_mm_sub_ps(r, lum), saturation_v));
                g = _mm_add_ps(lum, _mm_mul_ps(_mm_sub_ps(g, lum), saturation_v));
                b = _mm_add_ps(lum, _mm_mul_ps(_mm_sub_ps(b, lum), saturation_v));
            }

            if (settings.vignette_enabled)
            {
                const __m128 x_v = _mm_add_ps(_mm_set1_ps(static_cast<float>(x)), step);
                const __m128 fx = _mm_mul_ps(_mm_sub_ps(x_v, half_w_v), inv_w_v);
                const __m128 dist2 = _mm_add_ps(_mm_mul_ps(fx, fx), _mm_mul_ps(fy_v, fy_v));
                alignas(16) float dist_vals[4];
                _mm_store_ps(dist_vals, dist2);
                float vignette_vals[4];
                for (int lane = 0; lane < 4; ++lane)
                {
                    const float vignette = 1.f - settings.vignette_strength * std::pow(dist_vals[lane], settings.vignette_power);
                    vignette_vals[lane] = vignette;
                }
                const __m128 vignette_v = _mm_loadu_ps(vignette_vals);
                r = _mm_mul_ps(r, vignette_v);
                g = _mm_mul_ps(g, vignette_v);
                b = _mm_mul_ps(b, vignette_v);
            }

            r = _mm_min_ps(_mm_max_ps(r, zero), one);
            g = _mm_min_ps(_mm_max_ps(g, zero), one);
            b = _mm_min_ps(_mm_max_ps(b, zero), one);

            const __m128i r_out = _mm_cvtps_epi32(_mm_mul_ps(r, _mm_set1_ps(255.f)));
            const __m128i g_out = _mm_cvtps_epi32(_mm_mul_ps(g, _mm_set1_ps(255.f)));
            const __m128i b_out = _mm_cvtps_epi32(_mm_mul_ps(b, _mm_set1_ps(255.f)));

            const __m128i packed = _mm_or_si128(
                _mm_or_si128(_mm_slli_epi32(b_out, 16), _mm_slli_epi32(g_out, 8)),
                _mm_or_si128(r_out, _mm_set1_epi32(0xFF000000u))
            );
            _mm_storeu_si128(reinterpret_cast<__m128i*>(row + x), packed);
        }

        for (; x < W; ++x)
#else
        for (std::uint32_t x = 0; x < W; ++x)
#endif
        {
            float r, g, b;
            unpack_rgba8(row[x], r, g, b);

            if (settings.exposure_enabled)
            {
                r *= settings.exposure;
                g *= settings.exposure;
                b *= settings.exposure;
            }

            if (settings.contrast_enabled)
            {
                r = (r - 0.5f) * settings.contrast + 0.5f;
                g = (g - 0.5f) * settings.contrast + 0.5f;
                b = (b - 0.5f) * settings.contrast + 0.5f;
            }

            if (settings.saturation_enabled)
            {
                const float lum = r * 0.2126f + g * 0.7152f + b * 0.0722f;
                r = lum + (r - lum) * settings.saturation;
                g = lum + (g - lum) * settings.saturation;
                b = lum + (b - lum) * settings.saturation;
            }

            if (settings.vignette_enabled)
            {
                const float fx = (static_cast<float>(x) - half_w) * inv_w;
                const float dist2 = fx * fx + fy * fy;
                const float vignette = 1.f - settings.vignette_strength * std::pow(dist2, settings.vignette_power);
                r *= vignette;
                g *= vignette;
                b *= vignette;
            }

            row[x] = pack_rgba8_from_rgb(r, g, b);
        }
    }
}

void optimized_renderer_core::rainy_effect_slice(int y0, int y1) const noexcept
{
    if (!framebuffer.data || framebuffer.w == 0 || framebuffer.h == 0) return;
    if (!zbuffer.data || zbuffer.w == 0 || zbuffer.h == 0) return;

    const rainy_effect_settings settings = m_job.rain_settings;
    if (!settings.enabled || settings.intensity <= 0.f) return;

    const std::uint32_t W = framebuffer.w;
    const float time_s = m_job.time_s;
    const float drop_speed = settings.streak_speed;
    const float drop_density = settings.streak_density;
    const float drop_length = (settings.streak_length > 0.001f) ? settings.streak_length : 0.001f;
    const float drop_probability = clamp01(settings.streak_probability);
    const float depth_weight = settings.depth_weight;
    const float depth_bias = settings.depth_bias;
    const float tint_r = settings.tint.r;
    const float tint_g = settings.tint.g;
    const float tint_b = settings.tint.b;

    for (int y = y0; y <= y1; ++y)
    {
        std::uint32_t* row = framebuffer.data + (std::size_t)y * (std::size_t)framebuffer.pitch_pixels;
        const float* zrow = zbuffer.data + (std::size_t)y * (std::size_t)zbuffer.pitch;
        const float fy = static_cast<float>(framebuffer.h - 1u - static_cast<std::uint32_t>(y));

        for (std::uint32_t x = 0; x < W; ++x)
        {
            const float column_seed = hash11(static_cast<float>(x) * 0.271f);
            if (column_seed > drop_probability)
                continue;

            const float drift = settings.wind * time_s;
            const float phase = fract((fy * drop_density) - (time_s * drop_speed) + column_seed * 10.f + drift);
            if (phase >= drop_length)
                continue;

            const float z = zrow[x];
            const float depth_factor = clamp01(depth_bias + (1.f - z) * depth_weight);
            const float streak = 1.f - (phase / drop_length);
            float drop = streak * settings.intensity * depth_factor;
            if (drop <= 0.f)
                continue;

            const float jitter = 0.75f + 0.25f * std::sin((static_cast<float>(x) * 0.15f) + time_s);
            drop *= jitter;

            float r, g, b;
            unpack_rgba8(row[x], r, g, b);

            const float darken = 1.f - drop * settings.darken;
            r = r * darken + tint_r * drop;
            g = g * darken + tint_g * drop;
            b = b * darken + tint_b * drop;

            row[x] = pack_rgba8_from_rgb(r, g, b);
        }
    }
}

void optimized_renderer_core::advanced_effects_slice(int y0, int y1) const noexcept
{
    if (!framebuffer.data || framebuffer.w == 0 || framebuffer.h == 0) return;

    const advanced_effects_settings settings = m_job.advanced_settings;
    if (!settings.enabled)
        return;

    const std::uint32_t W = framebuffer.w;
    const std::uint32_t H = framebuffer.h;
    const float time_s = m_job.time_s;
    const bool use_depth = (settings.fog_enabled || settings.depth_of_field_enabled || settings.ssr_enabled);

    std::vector<std::uint32_t> row_copy(W);
    std::vector<std::uint32_t> blur_copy(W);

    for (int y = y0; y <= y1; ++y)
    {
        std::uint32_t* row = framebuffer.data + (std::size_t)y * (std::size_t)framebuffer.pitch_pixels;
        const float* zrow = use_depth ? (zbuffer.data + (std::size_t)y * (std::size_t)zbuffer.pitch) : nullptr;

        std::copy_n(row, W, row_copy.begin());

        if (settings.motion_blur_enabled || settings.depth_of_field_enabled)
        {
            std::copy_n(row_copy.begin(), W, blur_copy.begin());
        }

        for (std::uint32_t x = 0; x < W; ++x)
        {
            float r, g, b;
            unpack_rgba8(row_copy[x], r, g, b);

            const float lum = r * 0.2126f + g * 0.7152f + b * 0.0722f;
            if (settings.bloom_enabled)
            {
                const float boost = (std::max)(0.f, lum - settings.bloom_threshold) * settings.bloom_intensity;
                r += boost;
                g += boost;
                b += boost;
            }

            float depth = 0.f;
            if (use_depth && zrow)
                depth = clamp01(zrow[x]);

            if (settings.fog_enabled)
            {
                const float fog_range = (std::max)(0.001f, settings.fog_end - settings.fog_start);
                const float fog_t = clamp01((depth - settings.fog_start) / fog_range);
                r = r + (settings.fog_colour.r - r) * fog_t;
                g = g + (settings.fog_colour.g - g) * fog_t;
                b = b + (settings.fog_colour.b - b) * fog_t;
            }

            if (settings.ssr_enabled)
            {
                const std::uint32_t mirror_y = (H - 1u) - static_cast<std::uint32_t>(y);
                const std::uint32_t* mirror_row = framebuffer.data + (std::size_t)mirror_y * (std::size_t)framebuffer.pitch_pixels;
                float rr, rg, rb;
                unpack_rgba8(mirror_row[x], rr, rg, rb);
                const float reflect = settings.ssr_strength * (1.f - depth);
                r = r + (rr - r) * reflect;
                g = g + (rg - g) * reflect;
                b = b + (rb - b) * reflect;
            }

            if (settings.depth_of_field_enabled)
            {
                const float dof_range = (std::max)(0.001f, settings.dof_range);
                const float blur_t = clamp01(std::fabs(depth - settings.dof_focus) / dof_range);
                if (blur_t > 0.f)
                {
                    const std::uint32_t x0 = (x > 0) ? x - 1u : x;
                    const std::uint32_t x1 = (x + 1u < W) ? x + 1u : x;
                    float br0, bg0, bb0;
                    float br1, bg1, bb1;
                    unpack_rgba8(blur_copy[x0], br0, bg0, bb0);
                    unpack_rgba8(blur_copy[x1], br1, bg1, bb1);
                    const float blur_r = (br0 + br1) * 0.5f;
                    const float blur_g = (bg0 + bg1) * 0.5f;
                    const float blur_b = (bb0 + bb1) * 0.5f;
                    r = r + (blur_r - r) * blur_t;
                    g = g + (blur_g - g) * blur_t;
                    b = b + (blur_b - b) * blur_t;
                }
            }

            if (settings.god_rays_enabled)
            {
                const float fx = static_cast<float>(x) / (std::max)(1.f, static_cast<float>(W - 1u));
                const float fy = static_cast<float>(y) / (std::max)(1.f, static_cast<float>(H - 1u));
                const float dx = fx - settings.god_rays_screen_pos.x;
                const float dy = fy - settings.god_rays_screen_pos.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float shaft = clamp01(1.f - dist * 1.5f) * settings.god_rays_strength;
                r += shaft;
                g += shaft;
                b += shaft;
            }

            if (settings.motion_blur_enabled)
            {
                const std::uint32_t x0 = (x > 0) ? x - 1u : x;
                const std::uint32_t x1 = (x + 1u < W) ? x + 1u : x;
                float mr0, mg0, mb0;
                float mr1, mg1, mb1;
                unpack_rgba8(blur_copy[x0], mr0, mg0, mb0);
                unpack_rgba8(blur_copy[x1], mr1, mg1, mb1);
                const float blur_r = (mr0 + mr1) * 0.5f;
                const float blur_g = (mg0 + mg1) * 0.5f;
                const float blur_b = (mb0 + mb1) * 0.5f;
                const float mb = clamp01(settings.motion_blur_strength);
                r = r + (blur_r - r) * mb;
                g = g + (blur_g - g) * mb;
                b = b + (blur_b - b) * mb;
            }

            if (settings.film_grain_enabled)
            {
                const float grain = (hash11(static_cast<float>(x) * 0.17f + static_cast<float>(y) * 0.29f + time_s * settings.film_grain_speed) - 0.5f)
                    * settings.film_grain_strength;
                r += grain;
                g += grain;
                b += grain;
            }

            row[x] = pack_rgba8_from_rgb(r, g, b);
        }
    }
}

void optimized_renderer_core::draw_world_slice(int y0, int y1) const noexcept
{
    const std::uint32_t W = m_job.W;
    const std::uint32_t H = m_job.H;
    (void)H;

    const float fw = m_job.fw;
    const float fh = m_job.fh;

    const matrix vp = m_job.vp;
    const vec4 light_dir_in = m_job.light_dir;
    const bool tex_on    = m_job.textures_on;
    const bool flip_v_on = m_job.flip_v_on;

    const std::uint32_t pitch_pixels = framebuffer.pitch_pixels;

    for (const auto& block : render_cache_.blocks())
    {
        MeshRefPN*  meshes     = std::get<0>(block.arrays);
        Transform*  transforms = std::get<1>(block.arrays);
        Material*   materials  = std::get<2>(block.arrays);
        TextureRef* textures   = std::get<3>(block.arrays);
        const std::size_t n    = block.n;

        for (std::size_t ei = 0; ei < n; ++ei)
        {
            const MeshRefPN&  mesh = meshes[ei];
            const Transform&  tr   = transforms[ei];
            const Material&   mat  = materials[ei];
            const TextureRef& tex  = textures[ei];

            if (!mesh.positions || !mesh.normals || mesh.tri_count == 0) continue;

            const bool use_tex = tex_on && tex.valid() && mesh.has_uvs && mesh.uvs;

            const matrix p = vp * tr.world;

            for (std::uint32_t ti = 0; ti < mesh.tri_count; ++ti)
            {
                const std::size_t base = (std::size_t)ti * 3u;
                const vec4 hp0 = p * mesh.positions[base + 0];
                const vec4 hp1 = p * mesh.positions[base + 1];
                const vec4 hp2 = p * mesh.positions[base + 2];

                if (hp0[3] <= 0.0001f || hp1[3] <= 0.0001f || hp2[3] <= 0.0001f) continue;

#ifdef USE_SIMD
                if (tri_outside_frustum_simd(hp0, hp1, hp2)) continue;
#else
                if (hp0[0] < -hp0[3] && hp1[0] < -hp1[3] && hp2[0] < -hp2[3]) continue;
                if (hp0[0] >  hp0[3] && hp1[0] >  hp1[3] && hp2[0] >  hp2[3]) continue;
                if (hp0[1] < -hp0[3] && hp1[1] < -hp1[3] && hp2[1] < -hp2[3]) continue;
                if (hp0[1] >  hp0[3] && hp1[1] >  hp1[3] && hp2[1] >  hp2[3]) continue;
                if (hp0[2] < 0.f && hp1[2] < 0.f && hp2[2] < 0.f) continue;
                if (hp0[2] > hp0[3] && hp1[2] > hp1[3] && hp2[2] > hp2[3]) continue;
#endif

                // Read UVs if texturing
                float tu0 = 0.f, tv0 = 0.f;
                float tu1 = 0.f, tv1 = 0.f;
                float tu2 = 0.f, tv2 = 0.f;
                if (use_tex)
                {
                    const std::size_t uv_base = base * 2u;
                    tu0 = mesh.uvs[uv_base + 0]; tv0 = mesh.uvs[uv_base + 1];
                    tu1 = mesh.uvs[uv_base + 2]; tv1 = mesh.uvs[uv_base + 3];
                    tu2 = mesh.uvs[uv_base + 4]; tv2 = mesh.uvs[uv_base + 5];
                    if (flip_v_on)
                    {
                        tv0 = 1.f - tv0;
                        tv1 = 1.f - tv1;
                        tv2 = 1.f - tv2;
                    }
                }

                const SVtx v0 = make_svtx(hp0, tr.world, mesh.normals[base + 0], fw, fh, mat.col, tu0, tv0);
                const SVtx v1 = make_svtx(hp1, tr.world, mesh.normals[base + 1], fw, fh, mat.col, tu1, tv1);
                const SVtx v2 = make_svtx(hp2, tr.world, mesh.normals[base + 2], fw, fh, mat.col, tu2, tv2);

                float area = edge_fn(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
                if (area == 0.f) continue;

                const float sign = (area < 0.f) ? -1.f : 1.f;
                const float inv_area = 1.f / (area * sign);

                const float minx_f = (std::min)({ v0.x, v1.x, v2.x });
                const float maxx_f = (std::max)({ v0.x, v1.x, v2.x });
                const float miny_f = (std::min)({ v0.y, v1.y, v2.y });
                const float maxy_f = (std::max)({ v0.y, v1.y, v2.y });

                int minx = (int)std::floor(minx_f);
                int maxx = (int)std::ceil (maxx_f);
                int miny = (int)std::floor(miny_f);
                int maxy = (int)std::ceil (maxy_f);

                if (maxx < 0 || maxy < y0 || minx >= (int)W || miny > y1) continue;

                if (minx < 0) minx = 0;
                if (maxx >= (int)W) maxx = (int)W - 1;

                if (miny < y0) miny = y0;
                if (maxy > y1) maxy = y1;

                if (minx > maxx || miny > maxy) continue;

#ifdef USE_SIMD
                vec4 nrm{};
                __m128 n0_v = _mm_load_ps(v0.n.data());
                __m128 n1_v = _mm_load_ps(v1.n.data());
                __m128 n2_v = _mm_load_ps(v2.n.data());
                __m128 nsum = _mm_add_ps(_mm_add_ps(n0_v, n1_v), n2_v);
                _mm_store_ps(nrm.data(), nsum);
#else
                vec4 nrm = v0.n + v1.n + v2.n;
#endif
                nrm.normalise();

                float ndotl = vec4::dot(nrm, light_dir_in);
                if (ndotl < 0.f) ndotl = 0.f;

                const float intensity = mat.ka + mat.kd * ndotl;

                // Edge function setup
                const float e0_a = (v2.y - v1.y) * sign;
                const float e0_b = (v1.x - v2.x) * sign;
                const float e0_c = (v2.x * v1.y - v2.y * v1.x) * sign;

                const float e1_a = (v0.y - v2.y) * sign;
                const float e1_b = (v2.x - v0.x) * sign;
                const float e1_c = (v0.x * v2.y - v0.y * v2.x) * sign;

                const float e2_a = (v1.y - v0.y) * sign;
                const float e2_b = (v0.x - v1.x) * sign;
                const float e2_c = (v1.x * v0.y - v1.y * v0.x) * sign;

                const float start_x = (float)minx + 0.5f;
                const float start_y = (float)miny + 0.5f;

                float w0_row = e0_a * start_x + e0_b * start_y + e0_c;
                float w1_row = e1_a * start_x + e1_b * start_y + e1_c;
                float w2_row = e2_a * start_x + e2_b * start_y + e2_c;

                const float dzdx = (e0_a * v0.z + e1_a * v1.z + e2_a * v2.z) * inv_area;
                const float dzdy = (e0_b * v0.z + e1_b * v1.z + e2_b * v2.z) * inv_area;
                float z_row = (w0_row * v0.z + w1_row * v1.z + w2_row * v2.z) * inv_area;

                // For textured rendering perspective correct UV interpolation
                float d_invw_dx = 0.f, d_invw_dy = 0.f;
                float d_uow_dx  = 0.f, d_uow_dy  = 0.f;
                float d_vow_dx  = 0.f, d_vow_dy  = 0.f;
                float invw_row  = 0.f;
                float uow_row   = 0.f;
                float vow_row   = 0.f;

                if (use_tex)
                {
                    const float inv_w0 = 1.f / v0.w;
                    const float inv_w1 = 1.f / v1.w;
                    const float inv_w2 = 1.f / v2.w;

                    const float u0w = v0.u * inv_w0;
                    const float v0w_t = v0.v * inv_w0;
                    const float u1w = v1.u * inv_w1;
                    const float v1w_t = v1.v * inv_w1;
                    const float u2w = v2.u * inv_w2;
                    const float v2w_t = v2.v * inv_w2;

                    d_invw_dx = (e0_a * inv_w0 + e1_a * inv_w1 + e2_a * inv_w2) * inv_area;
                    d_invw_dy = (e0_b * inv_w0 + e1_b * inv_w1 + e2_b * inv_w2) * inv_area;

                    d_uow_dx = (e0_a * u0w + e1_a * u1w + e2_a * u2w) * inv_area;
                    d_uow_dy = (e0_b * u0w + e1_b * u1w + e2_b * u2w) * inv_area;

                    d_vow_dx = (e0_a * v0w_t + e1_a * v1w_t + e2_a * v2w_t) * inv_area;
                    d_vow_dy = (e0_b * v0w_t + e1_b * v1w_t + e2_b * v2w_t) * inv_area;

                    invw_row = (w0_row * inv_w0 + w1_row * inv_w1 + w2_row * inv_w2) * inv_area;
                    uow_row  = (w0_row * u0w    + w1_row * u1w    + w2_row * u2w)    * inv_area;
                    vow_row  = (w0_row * v0w_t  + w1_row * v1w_t  + w2_row * v2w_t)  * inv_area;
                }

                // Non-textured: pre compute the flat RGBA
                std::uint32_t flat_rgba = 0;
                if (!use_tex)
                {
                    colour lit = mat.col;
                    lit.r *= intensity;
                    lit.g *= intensity;
                    lit.b *= intensity;
                    flat_rgba = pack_rgba8_from_colour(lit);
                }

                for (int y = miny; y <= maxy; ++y)
                {
                    float* zptr = zbuffer.data + (std::size_t)y * (std::size_t)zbuffer.pitch + (std::size_t)minx;
                    std::uint32_t* cptr = framebuffer.data + (std::size_t)y * (std::size_t)pitch_pixels + (std::size_t)minx;

                    float w0 = w0_row;
                    float w1 = w1_row;
                    float w2 = w2_row;
                    float z  = z_row;

                    float invw_px = invw_row;
                    float uow_px  = uow_row;
                    float vow_px  = vow_row;

                    if (!use_tex)
                    {
                        // no texture flat color per triangle
#ifdef USE_SIMD
                        const __m128 step   = _mm_set_ps(3.f, 2.f, 1.f, 0.f);
                        const __m128 e0a_v  = _mm_set1_ps(e0_a);
                        const __m128 e1a_v  = _mm_set1_ps(e1_a);
                        const __m128 e2a_v  = _mm_set1_ps(e2_a);
                        const __m128 dzdx_v = _mm_set1_ps(dzdx);

                        int x = minx;
                        for (; x <= maxx - 3; x += 4)
                        {
                            __m128 w0v = _mm_add_ps(_mm_set1_ps(w0), _mm_mul_ps(e0a_v, step));
                            __m128 w1v = _mm_add_ps(_mm_set1_ps(w1), _mm_mul_ps(e1a_v, step));
                            __m128 w2v = _mm_add_ps(_mm_set1_ps(w2), _mm_mul_ps(e2a_v, step));

                            __m128 inside = _mm_and_ps(_mm_cmpge_ps(w0v, _mm_setzero_ps()),
                                                       _mm_and_ps(_mm_cmpge_ps(w1v, _mm_setzero_ps()),
                                                                  _mm_cmpge_ps(w2v, _mm_setzero_ps())));

                            const int inside_mask = _mm_movemask_ps(inside);
                            if (inside_mask)
                            {
                                __m128 zv   = _mm_add_ps(_mm_set1_ps(z), _mm_mul_ps(dzdx_v, step));
                                __m128 zbuf = _mm_load_ps(zptr);

                                __m128 zpass = _mm_cmpgt_ps(zv, zbuf);
                                __m128 final_mask = _mm_and_ps(inside, zpass);

                                const int write_mask = _mm_movemask_ps(final_mask);
                                if (write_mask)
                                {
                                    alignas(16) float zvals[4];
                                    _mm_store_ps(zvals, zv);

                                    for (int lane = 0; lane < 4; ++lane)
                                    {
                                        if (write_mask & (1 << lane))
                                        {
                                            zptr[lane] = zvals[lane];
                                            cptr[lane] = flat_rgba;
                                        }
                                    }
                                }
                            }

                            w0 += e0_a * 4.f;
                            w1 += e1_a * 4.f;
                            w2 += e2_a * 4.f;
                            z  += dzdx * 4.f;
                            cptr += 4;
                            zptr += 4;
                        }

                        for (; x <= maxx; ++x)
#else
                        for (int x = minx; x <= maxx; ++x)
#endif
                        {
                            if (w0 >= 0.f && w1 >= 0.f && w2 >= 0.f)
                            {
                                if (z > *zptr)
                                {
                                    *zptr = z;
                                    *cptr = flat_rgba;
                                }
                            }

                            w0 += e0_a;
                            w1 += e1_a;
                            w2 += e2_a;
                            z  += dzdx;
                            ++cptr;
                            ++zptr;
                        }
                    }
                    else
                    {
                        // Textured path per pixel perspective correct UV sampling
                        for (int x = minx; x <= maxx; ++x)
                        {
                            if (w0 >= 0.f && w1 >= 0.f && w2 >= 0.f)
                            {
                                if (z > *zptr)
                                {
                                    const float rcp_invw = (invw_px > 0.0001f) ? (1.f / invw_px) : 1.f;
                                    const float uu = uow_px * rcp_invw;
                                    const float vv = vow_px * rcp_invw;

                                    const std::uint32_t tex_color = tex.sample_nearest(uu, vv);

                                    *zptr = z;
                                    *cptr = modulate_texture(tex_color, intensity);
                                }
                            }

                            w0 += e0_a;
                            w1 += e1_a;
                            w2 += e2_a;
                            z  += dzdx;
                            invw_px += d_invw_dx;
                            uow_px  += d_uow_dx;
                            vow_px  += d_vow_dx;
                            ++cptr;
                            ++zptr;
                        }
                    }

                    w0_row += e0_b;
                    w1_row += e1_b;
                    w2_row += e2_b;
                    z_row  += dzdy;

                    if (use_tex)
                    {
                        invw_row += d_invw_dy;
                        uow_row  += d_uow_dy;
                        vow_row  += d_vow_dy;
                    }
                }
            }
        }
    }
}

void optimized_renderer_core::pin_draw_query_once() noexcept
{
    pinned_draw_blocks.clear();

    render_cache_.refresh(world);
    pinned_draw_blocks.reserve(render_cache_.blocks().size());
    for (const auto& b : render_cache_.blocks())
    {
        auto [m, t, mat, tex] = b.arrays;
        draw_pinned_block out{};
        out.meshes     = m;
        out.transforms = t;
        out.materials  = mat;
        out.textures   = tex;
        out.n = b.n;
        pinned_draw_blocks.emplace_back(out);
    }

    pinned_draw_ready = true;
}

void optimized_renderer_core::ensure_offline_targets(std::uint32_t w, std::uint32_t h) noexcept
{
    if (w == 0 || h == 0) return;

    if (m_offline_targets.w == w && m_offline_targets.h == h &&
        !m_offline_targets.color.empty() && !m_offline_targets.z.empty())
        return;

    m_offline_targets.w = w;
    m_offline_targets.h = h;
    m_offline_targets.pitch_pixels = w;
    m_offline_targets.pitch_bytes  = w * 4u;
    m_offline_targets.z_pitch      = w;

    m_offline_targets.color.resize((std::size_t)w * (std::size_t)h);
    m_offline_targets.z.resize((std::size_t)w * (std::size_t)h);

    std::memset(m_offline_targets.color.data(), 0, m_offline_targets.color.size() * sizeof(std::uint32_t));
    std::memset(m_offline_targets.z.data(),     0, m_offline_targets.z.size() * sizeof(float));
}

void optimized_renderer_core::refresh_render_cache() noexcept
{
    render_cache_.refresh(world);
    const std::uint64_t version = render_cache_.version();
    if (!pinned_draw_ready || render_cache_version_ != version)
    {
        pin_draw_query_once();
        render_cache_version_ = version;
    }
}
