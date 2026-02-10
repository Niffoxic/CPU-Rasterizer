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
    dx.ring_size = 6; // TODO: Tune it
    canvas.create(dx);

    perspective = matrix::makePerspective(90.f * (float)M_PI / 180.f, (float)w / (float)h, 0.1f, 100.f);

    world.register_component<MeshRefPN>();
    world.register_component<Transform>();
    world.register_component<Material>();

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
        }

        const worker_range r = m_ranges[worker_index];
        if (r.y0 <= r.y1)
            draw_world_slice(r.y0, r.y1);

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

    m_job.W = W;
    m_job.H = H;
    m_job.fw = static_cast<float>(W);
    m_job.fh = static_cast<float>(H);
    m_job.vp = perspective * cam;
    m_job.light_dir = light_dir_in;

    {
        std::lock_guard<std::mutex> lg(m_job_mtx);
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

void optimized_renderer_core::draw_world_slice(int y0, int y1) const noexcept
{
    const std::uint32_t W = m_job.W;
    const std::uint32_t H = m_job.H;
    (void)H;

    const float fw = m_job.fw;
    const float fh = m_job.fh;

    const matrix vp = m_job.vp;
    const vec4 light_dir_in = m_job.light_dir;

    const std::uint32_t pitch_pixels = framebuffer.pitch_pixels;

    for (const auto& block : render_cache_.blocks())
    {
        MeshRefPN* meshes       = std::get<0>(block.arrays);
        Transform* transforms   = std::get<1>(block.arrays);
        Material*  materials    = std::get<2>(block.arrays);
        const std::size_t n     = block.n;

        for (std::size_t ei = 0; ei < n; ++ei)
        {
            const MeshRefPN& mesh = meshes[ei];
            const Transform& tr   = transforms[ei];
            const Material&  mat  = materials[ei];

            if (!mesh.positions || !mesh.normals || mesh.tri_count == 0) continue;

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

                const SVtx v0 = make_svtx(hp0, tr.world, mesh.normals[base + 0], fw, fh, mat.col);
                const SVtx v1 = make_svtx(hp1, tr.world, mesh.normals[base + 1], fw, fh, mat.col);
                const SVtx v2 = make_svtx(hp2, tr.world, mesh.normals[base + 2], fw, fh, mat.col);

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
                __m128 n0 = _mm_load_ps(v0.n.data());
                __m128 n1 = _mm_load_ps(v1.n.data());
                __m128 n2 = _mm_load_ps(v2.n.data());
                __m128 sum = _mm_add_ps(_mm_add_ps(n0, n1), n2);
                _mm_store_ps(nrm.data(), sum);
#else
                vec4 nrm = v0.n + v1.n + v2.n;
#endif
                nrm.normalise();

                float ndotl = vec4::dot(nrm, light_dir_in);
                if (ndotl < 0.f) ndotl = 0.f;

                const float intensity = mat.ka + mat.kd * ndotl;

                colour lit = mat.col;
                lit.r *= intensity;
                lit.g *= intensity;
                lit.b *= intensity;

                const std::uint32_t rgba = pack_rgba8_from_colour(lit);

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

                for (int y = miny; y <= maxy; ++y)
                {
                    float* zptr = zbuffer.data + (std::size_t)y * (std::size_t)zbuffer.pitch + (std::size_t)minx;
                    std::uint32_t* cptr = framebuffer.data + (std::size_t)y * (std::size_t)pitch_pixels + (std::size_t)minx;

                    float w0 = w0_row;
                    float w1 = w1_row;
                    float w2 = w2_row;
                    float z  = z_row;

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
                                        cptr[lane] = rgba;
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
                                *cptr = rgba;
                            }
                        }

                        w0 += e0_a;
                        w1 += e1_a;
                        w2 += e2_a;
                        z  += dzdx;
                        ++cptr;
                        ++zptr;
                    }

                    w0_row += e0_b;
                    w1_row += e1_b;
                    w2_row += e2_b;
                    z_row  += dzdy;
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
        auto [m, t, mat] = b.arrays;
        draw_pinned_block out{};
        out.meshes = m;
        out.transforms = t;
        out.materials = mat;
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
