#ifndef FOXRASTERIZER_OPTIMIZED_RENDERER_H
#define FOXRASTERIZER_OPTIMIZED_RENDERER_H

#include "gfx_dx11.h"
#include "platform_windows.h"
#include "fecs.h"
#include "mesh.h"
#include "light.h"

#include <cstdint>
#include <vector>
#include <cstddef>
#include <utility>
#include <limits>
#include <cmath>
#include <cstring>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#ifdef USE_SIMD
#include <immintrin.h>
#endif

struct FramebufferRGBA8
{
    std::uint32_t  w     = 0;
    std::uint32_t  h     = 0;
    std::uint32_t  pitch_bytes  = 0;
    std::uint32_t  pitch_pixels = 0;
    std::uint32_t* data  = nullptr;

    void bind(std::uint32_t width, std::uint32_t height,
              std::uint32_t pitch_bytes_in, std::uint32_t pitch_pixels_in,
              std::uint32_t* ptr) noexcept
    {
        w = width;
        h = height;
        pitch_bytes  = pitch_bytes_in;
        pitch_pixels = pitch_pixels_in;
        data = ptr;
    }

    [[nodiscard]] std::uint32_t* pixel_ptr(std::uint32_t x, std::uint32_t y) noexcept
    {
        return data + (std::size_t)y * (std::size_t)pitch_pixels + (std::size_t)x;
    }
};

struct ZBufferF32
{
    std::uint32_t w     = 0;
    std::uint32_t h     = 0;
    std::uint32_t pitch = 0;
    float*        data  = nullptr;
};

struct MeshAssetPN
{
    vec4*         positions = nullptr;
    vec4*         normals   = nullptr;
    float*        uvs       = nullptr; // 2 floats per vertex (u,v), 6 per triangle
    std::uint32_t tri_count = 0;
    bool          has_uvs   = false;

    static constexpr std::size_t ALIGN_BYTES = 64;

    MeshAssetPN() = default;
    MeshAssetPN(const MeshAssetPN&) = delete;
    MeshAssetPN& operator=(const MeshAssetPN&) = delete;

    MeshAssetPN(MeshAssetPN&& o) noexcept { *this = std::move(o); }
    MeshAssetPN& operator=(MeshAssetPN&& o) noexcept
    {
        if (this != &o)
        {
            destroy();
            positions = o.positions;
            normals = o.normals;
            uvs = o.uvs;
            tri_count = o.tri_count;
            has_uvs = o.has_uvs;
            o.positions = nullptr;
            o.normals = nullptr;
            o.uvs = nullptr;
            o.tri_count = 0;
            o.has_uvs = false;
        }
        return *this;
    }

    void destroy() noexcept
    {
        if (positions) _aligned_free(positions);
        if (normals)   _aligned_free(normals);
        if (uvs)       _aligned_free(uvs);
        positions = nullptr;
        normals = nullptr;
        uvs = nullptr;
        tri_count = 0;
        has_uvs = false;
    }

    ~MeshAssetPN() noexcept { destroy(); }

    void allocate(std::uint32_t count, bool alloc_uvs = false) noexcept
    {
        destroy();
        tri_count = count;
        has_uvs = alloc_uvs;
        const std::size_t verts = (std::size_t)count * 3u;
        positions = static_cast<vec4*>(_aligned_malloc(sizeof(vec4) * verts, ALIGN_BYTES));
        normals   = static_cast<vec4*>(_aligned_malloc(sizeof(vec4) * verts, ALIGN_BYTES));
        if (alloc_uvs)
        {
            uvs = static_cast<float*>(_aligned_malloc(sizeof(float) * verts * 2u, ALIGN_BYTES));
            std::memset(uvs, 0, sizeof(float) * verts * 2u);
        }
    }
};

struct alignas(64) MeshRefPN
{
    const vec4*   positions = nullptr;
    const vec4*   normals   = nullptr;
    const float*  uvs       = nullptr; // 2 floats per vertex, null if no UVs
    std::uint32_t tri_count = 0;
    bool          has_uvs   = false;
};

struct alignas(64) Transform
{
    matrix world{};
};

struct alignas(64) Material
{
    colour col{};
    float  ka = 0.75f;
    float  kd = 0.75f;
};

// Texture reference component - points to cached CPU texture data
struct alignas(64) TextureRef
{
    const std::uint32_t* pixels = nullptr;
    std::uint32_t tex_w = 0;
    std::uint32_t tex_h = 0;

    [[nodiscard]] bool valid() const noexcept { return pixels && tex_w > 0 && tex_h > 0; }

    [[nodiscard]] std::uint32_t sample_nearest(float u, float v) const noexcept
    {
        u = u - std::floor(u);
        v = v - std::floor(v);
        if (u < 0.f) u += 1.f;
        if (v < 0.f) v += 1.f;
        const std::uint32_t tx = static_cast<std::uint32_t>(u * (float)tex_w) % tex_w;
        const std::uint32_t ty = static_cast<std::uint32_t>(v * (float)tex_h) % tex_h;
        return pixels[(std::size_t)ty * (std::size_t)tex_w + (std::size_t)tx];
    }
};

static inline MeshAssetPN build_asset_from_indexed_mesh(const Mesh& m) noexcept
{
    MeshAssetPN a{};
    const std::uint32_t tri_count = (std::uint32_t)m.triangles.size();
    a.allocate(tri_count, false);

    for (std::uint32_t t = 0; t < tri_count; ++t)
    {
        const triIndices& ind = m.triangles[t];

        const Vertex& A = m.vertices[ind.v0];
        const Vertex& B = m.vertices[ind.v1];
        const Vertex& C = m.vertices[ind.v2];

        const std::size_t base = (std::size_t)t * 3u;

        a.positions[base + 0] = A.p;
        a.positions[base + 1] = B.p;
        a.positions[base + 2] = C.p;

        a.normals[base + 0] = A.normal;
        a.normals[base + 1] = B.normal;
        a.normals[base + 2] = C.normal;

        a.positions[base + 0][3] = 1.f;
        a.positions[base + 1][3] = 1.f;
        a.positions[base + 2][3] = 1.f;

        a.normals[base + 0][3] = 0.f;
        a.normals[base + 1][3] = 0.f;
        a.normals[base + 2][3] = 0.f;
    }

    return a;
}

static inline fecs::entity spawn_instance(
    fecs::world& w,
    const MeshAssetPN& asset,
    const matrix& world_mtx,
    const colour& col,
    float ka,
    float kd,
    const TextureRef& tex = {}) noexcept
{
    fecs::entity e = w.create_entity();

    MeshRefPN r{};
    r.positions = asset.positions;
    r.normals   = asset.normals;
    r.uvs       = asset.uvs;
    r.tri_count = asset.tri_count;
    r.has_uvs   = asset.has_uvs;

    Transform tr{ world_mtx };
    Material  mat{};
    mat.col = col;
    mat.ka  = ka;
    mat.kd  = kd;

    w.add_component<MeshRefPN>(e, r);
    w.add_component<Transform>(e, tr);
    w.add_component<Material>(e, mat);
    w.add_component<TextureRef>(e, tex);

    return e;
}

struct SVtx
{
    float  x, y, z, w;
    vec4   n;
    colour c;
    float  u, v; // texture coordinates
};

static inline SVtx make_svtx(
    const vec4& hp,
    const matrix& world,
    const vec4& nrm_os,
    float fw,
    float fh,
    const colour& c,
    float tex_u = 0.f,
    float tex_v = 0.f) noexcept
{
    SVtx o{};

    const float ww = (hp[3] != 0.f) ? hp[3] : 1.f;

#ifdef USE_SIMD
    const __m128 hpv  = _mm_load_ps(hp.data());
    const __m128 invw = _mm_set1_ps(1.f / ww);
    const __m128 ndc_v = _mm_mul_ps(hpv, invw);
    alignas(16) float ndc_vals[4];
    _mm_store_ps(ndc_vals, ndc_v);
    const float ndcx = ndc_vals[0];
    const float ndcy = ndc_vals[1];
    const float ndcz = ndc_vals[2];
#else
    const float ndcx = hp[0] / ww;
    const float ndcy = hp[1] / ww;
    const float ndcz = hp[2] / ww;
#endif

    o.z = 1.0f - ndcz;
    o.w = hp[3];

#ifdef USE_SIMD
    __m128 ndc_xy = _mm_set_ps(0.f, 0.f, ndcy, ndcx);
    __m128 add    = _mm_add_ps(ndc_xy, _mm_set1_ps(1.f));
    __m128 half   = _mm_mul_ps(add, _mm_set1_ps(0.5f));
    __m128 scale  = _mm_set_ps(0.f, 0.f, fh, fw);
    __m128 screen = _mm_mul_ps(half, scale);
    alignas(16) float screen_vals[4];
    _mm_store_ps(screen_vals, screen);
    o.x = screen_vals[0];
    o.y = fh - screen_vals[1];
#else
    o.x = (ndcx + 1.f) * 0.5f * fw;
    o.y = (ndcy + 1.f) * 0.5f * fh;
    o.y = fh - o.y;
#endif

    o.n = world * nrm_os;
    o.n.normalise();
    o.c = c;
    o.u = tex_u;
    o.v = tex_v;
    return o;
}

class optimized_renderer_core
{
public:
    optimized_renderer_core(std::uint32_t w = 1024, std::uint32_t h = 768, const char* name = "FoxRasterizer");
    ~optimized_renderer_core();
    void set_offline_rendering(bool v) noexcept;
    struct post_process_settings
    {
        bool  enabled = true;
        bool  exposure_enabled = true;
        float exposure = 1.05f;
        bool  contrast_enabled = true;
        float contrast = 1.05f;
        bool  saturation_enabled = true;
        float saturation = 1.08f;
        bool  vignette_enabled = true;
        float vignette_strength = 0.25f;
        float vignette_power = 1.4f;
    };

    struct rainy_effect_settings
    {
        bool  enabled = false;
        float intensity = 0.35f;
        float streak_density = 0.025f;
        float streak_length = 0.2f;
        float streak_speed = 1.4f;
        float streak_probability = 0.45f;
        float depth_weight = 0.8f;
        float depth_bias = 0.1f;
        float wind = 0.15f;
        float darken = 0.35f;
        colour tint = colour(0.6f, 0.7f, 0.8f);
    };

    struct advanced_effects_settings
    {
        bool  enabled = true;
        bool  bloom_enabled = true;
        float bloom_threshold = 0.75f;
        float bloom_intensity = 0.3f;
        bool  film_grain_enabled = false;
        float film_grain_strength = 0.03f;
        float film_grain_speed = 1.0f;
        bool  motion_blur_enabled = false;
        float motion_blur_strength = 0.2f;
        bool  fog_enabled = false;
        colour fog_colour = colour(0.65f, 0.7f, 0.8f);
        float fog_start = 0.35f;
        float fog_end = 0.95f;
        bool  ssr_enabled = false;
        float ssr_strength = 0.2f;
        bool  depth_of_field_enabled = false;
        float dof_focus = 0.4f;
        float dof_range = 0.25f;
        bool  god_rays_enabled = false;
        float god_rays_strength = 0.2f;
        vec4  god_rays_screen_pos = vec4(0.5f, 0.2f, 0.f, 0.f);
    };

    void apply_post_process(const post_process_settings& settings) noexcept;
    void apply_rainy_effect(const rainy_effect_settings& settings, float time_s) noexcept;
    void apply_advanced_effects(const advanced_effects_settings& settings, float time_s) noexcept;
    optimized_renderer_core(const optimized_renderer_core&) = delete;
    optimized_renderer_core& operator=(const optimized_renderer_core&) = delete;

    bool begin_cpu_frame(std::uint32_t clear_rgba) noexcept;
    void draw_world(const matrix& cam, const Light& L, const vec4& light_dir) noexcept;
    void pin_draw_query_once() noexcept;

    bool textures_enabled = true;
    bool flip_v           = true;

private:
    bool m_offline = false;

    struct offline_targets_t
    {
        std::uint32_t w = 0, h = 0;
        std::uint32_t pitch_pixels = 0;
        std::uint32_t pitch_bytes = 0;
        std::uint32_t z_pitch = 0;
        std::vector<std::uint32_t> color;
        std::vector<float> z;
    } m_offline_targets;

    void ensure_offline_targets(std::uint32_t w, std::uint32_t h) noexcept;
public:
    struct draw_pinned_block
    {
        MeshRefPN*   meshes       = nullptr;
        Transform*   transforms   = nullptr;
        Material*    materials    = nullptr;
        TextureRef*  textures     = nullptr;
        std::size_t  n            = 0;
    };

    std::vector<draw_pinned_block> pinned_draw_blocks{};
    bool pinned_draw_ready = false;

    FramebufferRGBA8 framebuffer{};
    ZBufferF32       zbuffer{};

    fecs::world          world{};
    fox::platform_window windows{};
    fox::gfx_dx11        canvas{};

    matrix      perspective{};
    MeshAssetPN cube_asset{};

    post_process_settings post_process{};
    rainy_effect_settings rainy_effect{};
    advanced_effects_settings advanced_effects{};

protected:
    fox::cpu_frame cur_frame{};

private:
    enum class job_type : std::uint8_t
    {
        draw,
        post_process,
        rainy_effect,
        advanced_effects
    };

    job_type m_job_type = job_type::draw;
    void bind_targets_from_frame(const fox::cpu_frame& f) noexcept;
    void clear_color_rgba(std::uint32_t rgba) const noexcept;
    void refresh_render_cache() noexcept;

private:
    static constexpr int kWorkerCount = 8;  // TODO: Tune it
    static constexpr int kTotalSlices = kWorkerCount + 1;

    struct draw_job_shared
    {
        matrix vp{};
        vec4   light_dir{};
        float  fw = 0.f;
        float  fh = 0.f;
        std::uint32_t W = 0;
        std::uint32_t H = 0;
        bool textures_on = true;
        bool flip_v_on   = false;
        post_process_settings post_settings{};
        rainy_effect_settings rain_settings{};
        advanced_effects_settings advanced_settings{};
        float time_s = 0.f;
    };

    struct worker_range
    {
        int y0 = 0;
        int y1 = 0;
    };

    std::thread m_workers[kWorkerCount]{};

    std::mutex              m_job_mtx{};
    std::condition_variable m_job_cv{};
    std::condition_variable m_done_cv{};

    std::atomic<bool> m_shutdown{ false };

    std::uint64_t m_job_gen = 0;
    int           m_done_count = 0;

    draw_job_shared m_job{};
    worker_range    m_ranges[kTotalSlices]{};

    fecs::render_cache<MeshRefPN, Transform, Material, TextureRef> render_cache_{};
    std::uint64_t render_cache_version_{ std::numeric_limits<std::uint64_t>::max() };

private:
    void init_persistent_workers() noexcept;
    void shutdown_persistent_workers() noexcept;

    void compute_slice_ranges(std::uint32_t H) noexcept;

    void worker_loop(int worker_index) noexcept;

    void draw_world_slice(int y0, int y1) const noexcept;
    void post_process_slice(int y0, int y1) const noexcept;
    void rainy_effect_slice(int y0, int y1) const noexcept;
    void advanced_effects_slice(int y0, int y1) const noexcept;
};

class optimized_renderer_rt : public optimized_renderer_core
{
public:
    using optimized_renderer_core::optimized_renderer_core;

    inline void present() noexcept
    {
        if (cur_frame.valid())
        {
            canvas.present(cur_frame);
            cur_frame = {};
            framebuffer = {};
            zbuffer = {};
        }
    }
};

class optimized_renderer_offline : public optimized_renderer_core
{
public:
    using optimized_renderer_core::optimized_renderer_core;

    inline void start_recording() noexcept
    {
        canvas.record_begin();
    }

    inline void stop_recording() noexcept
    {
        canvas.record_end();
    }

    inline std::uint32_t recorded_frame_count() const noexcept
    {
        return canvas.record_frame_count();
    }

    inline void record_frame_only() noexcept
    {
        if (cur_frame.valid())
        {
            canvas.record_submit(cur_frame);
            cur_frame = {};
            framebuffer = {};
            zbuffer = {};
        }
    }

    inline void playback_start(bool loop = true) noexcept
    {
        canvas.playback_start(loop);
    }

    inline void playback_stop() noexcept
    {
        canvas.playback_stop();
    }

    inline void playback_rewind() noexcept
    {
        canvas.playback_rewind();
    }

    inline bool playback_present_next() noexcept
    {
        return canvas.playback_present_next();
    }

    inline fox::playback_state playback_state() const noexcept
    {
        return canvas.get_playback_state();
    }
};

#endif // FOXRASTERIZER_OPTIMIZED_RENDERER_H
