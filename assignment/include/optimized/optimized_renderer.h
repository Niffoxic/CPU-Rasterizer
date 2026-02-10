#ifndef FOXRASTERIZER_OPTIMIZED_RENDERER_H
#define FOXRASTERIZER_OPTIMIZED_RENDERER_H

#include "gfx_dx11.h"
#include "platform_windows.h"
#include "fecs.h"
#include "base/mesh.h"
#include "base/light.h"

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
    std::uint32_t tri_count = 0;

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
            tri_count = o.tri_count;
            o.positions = nullptr;
            o.normals = nullptr;
            o.tri_count = 0;
        }
        return *this;
    }

    void destroy() noexcept
    {
        if (positions) _aligned_free(positions);
        if (normals)   _aligned_free(normals);
        positions = nullptr;
        normals = nullptr;
        tri_count = 0;
    }

    ~MeshAssetPN() noexcept { destroy(); }

    void allocate(std::uint32_t count) noexcept
    {
        destroy();
        tri_count = count;
        const std::size_t verts = (std::size_t)count * 3u;
        positions = static_cast<vec4*>(_aligned_malloc(sizeof(vec4) * verts, ALIGN_BYTES));
        normals   = static_cast<vec4*>(_aligned_malloc(sizeof(vec4) * verts, ALIGN_BYTES));
    }
};

struct alignas(64) MeshRefPN
{
    const vec4*   positions = nullptr;
    const vec4*   normals   = nullptr;
    std::uint32_t tri_count = 0;
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

static inline MeshAssetPN build_asset_from_indexed_mesh(const Mesh& m) noexcept
{
    MeshAssetPN a{};
    const std::uint32_t tri_count = (std::uint32_t)m.triangles.size();
    a.allocate(tri_count);

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
    float kd) noexcept
{
    fecs::entity e = w.create_entity();

    MeshRefPN r{ asset.positions, asset.normals, asset.tri_count };
    Transform tr{ world_mtx };
    Material  mat{};
    mat.col = col;
    mat.ka  = ka;
    mat.kd  = kd;

    w.add_component<MeshRefPN>(e, r);
    w.add_component<Transform>(e, tr);
    w.add_component<Material>(e, mat);

    return e;
}

struct SVtx
{
    float  x, y, z, w;
    vec4   n;
    colour c;
};

static inline SVtx make_svtx(
    const vec4& hp,
    const matrix& world,
    const vec4& nrm_os,
    float fw,
    float fh,
    const colour& c) noexcept
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
    return o;
}

class optimized_renderer_core
{
public:
    optimized_renderer_core(std::uint32_t w = 1024, std::uint32_t h = 768, const char* name = "FoxRasterizer");
    ~optimized_renderer_core();
    void set_offline_rendering(bool v) noexcept;
    optimized_renderer_core(const optimized_renderer_core&) = delete;
    optimized_renderer_core& operator=(const optimized_renderer_core&) = delete;

    bool begin_cpu_frame(std::uint32_t clear_rgba) noexcept;
    void draw_world(const matrix& cam, const Light& L, const vec4& light_dir) noexcept;
    void pin_draw_query_once() noexcept;
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
        MeshRefPN* meshes       = nullptr;
        Transform* transforms   = nullptr;
        Material*  materials    = nullptr;
        std::size_t n           = 0;
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

protected:
    fox::cpu_frame cur_frame{};

private:
    void bind_targets_from_frame(const fox::cpu_frame& f) noexcept;
    void clear_color_rgba(std::uint32_t rgba) const noexcept;
    void refresh_render_cache() noexcept;

private:
    static constexpr int kWorkerCount = 5;
    static constexpr int kTotalSlices = kWorkerCount + 1;

    struct draw_job_shared
    {
        matrix vp{};
        vec4   light_dir{};
        float  fw = 0.f;
        float  fh = 0.f;
        std::uint32_t W = 0;
        std::uint32_t H = 0;
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

    fecs::render_cache<MeshRefPN, Transform, Material> render_cache_{};
    std::uint64_t render_cache_version_{ std::numeric_limits<std::uint64_t>::max() };

private:
    void init_persistent_workers() noexcept;
    void shutdown_persistent_workers() noexcept;

    void compute_slice_ranges(std::uint32_t H) noexcept;

    void worker_loop(int worker_index) noexcept;

    void draw_world_slice(int y0, int y1) const noexcept;
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
