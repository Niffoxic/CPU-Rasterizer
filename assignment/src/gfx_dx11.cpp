#include "optimized/gfx_dx11.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <cstdio>

#ifdef USE_SIMD
#include <immintrin.h>
#endif

using Microsoft::WRL::ComPtr;

namespace
{
    static inline void DebugFail(const char* where, HRESULT hr) noexcept
    {
        char b[256];
        std::snprintf(b, sizeof(b), "%s failed hr=0x%08X\n", where, (unsigned)hr);
        OutputDebugStringA(b);
    }

#ifdef USE_SIMD
    static inline void copy_row_simd(std::uint8_t* dst, const std::uint8_t* src, std::size_t bytes) noexcept
    {
        std::size_t i = 0;
#if defined(__AVX2__)
        const bool use_nt = bytes >= 512;
        if (use_nt)
        {
            const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(dst);
            const std::size_t align = (32u - (addr & 31u)) & 31u;
            for (; i < align && i < bytes; ++i) dst[i] = src[i];
            for (; i + 32 <= bytes; i += 32)
            {
                const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
                _mm256_stream_si256(reinterpret_cast<__m256i*>(dst + i), v);
            }
            for (; i < bytes; ++i) dst[i] = src[i];
            _mm_sfence();
            return;
        }
        for (; i + 32 <= bytes; i += 32)
        {
            const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), v);
        }
#else
        for (; i + 16 <= bytes; i += 16)
        {
            const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), v);
        }
#endif
        for (; i < bytes; ++i) dst[i] = src[i];
    }
#endif

    static inline std::uint32_t align_up_u32(std::uint32_t v, std::uint32_t a) noexcept
    {
        return (v + (a - 1u)) & ~(a - 1u);
    }

    static inline std::uint64_t qpc_now() noexcept
    {
        LARGE_INTEGER t{};
        QueryPerformanceCounter(&t);
        return (std::uint64_t)t.QuadPart;
    }

    static inline std::uint64_t qpc_freq() noexcept
    {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return (std::uint64_t)f.QuadPart;
    }
}

class fox::gfx_dx11::pimpl
{
public:
    HWND hwnd = nullptr;
    std::uint32_t w = 0;
    std::uint32_t h = 0;

    ComPtr<ID3D11Device>        dev;
    ComPtr<ID3D11DeviceContext> ctx;

    ComPtr<IDXGIFactory1>   factory1;
    ComPtr<IDXGIFactory2>   factory2;
    ComPtr<IDXGISwapChain1> sc1;
    ComPtr<IDXGISwapChain3> sc3;

    std::vector<ComPtr<ID3D11RenderTargetView>> rtvs;
    D3D11_VIEWPORT vp{};

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader>  ps;
    ComPtr<ID3D11SamplerState> samp;

    std::uint32_t ring_size = 0;
    std::vector<ComPtr<ID3D11Texture2D>>          ring_tex;
    std::vector<ComPtr<ID3D11ShaderResourceView>> ring_srv;

    std::uint32_t gpu_pitch_bytes = 0;

    struct slot_t
    {
        std::vector<std::uint32_t> color;
        std::uint32_t color_pitch_bytes  = 0;
        std::uint32_t color_pitch_pixels = 0;

        float*        z = nullptr;
        std::uint32_t  z_pitch = 0;

        std::uint32_t generation = 1;

        enum class state_t : std::uint8_t { free, acquired, queued, presenting, clearing } state = state_t::free;
    };

    std::vector<slot_t> slots;

    std::thread present_worker;

    static constexpr int CLEAR_WORKERS = 5;
    std::thread clear_workers[CLEAR_WORKERS];

    std::mutex mtx;
    std::condition_variable cv_present;
    std::condition_variable cv_clear;
    std::condition_variable cv_free;
    std::condition_variable cv_flush;

    bool shutting_down = false;

    enum class latest_kind : std::uint8_t { none, slot, recorded };
    latest_kind latest_k = latest_kind::none;

    bool latest_slot_valid = false;
    std::uint32_t latest_slot = 0;
    std::uint32_t latest_gen  = 0;

    bool latest_rec_valid = false;
    std::uint32_t latest_rec_index = 0;

    std::deque<std::uint32_t> clear_q;

    struct bb_clear_cmd { std::uint32_t rgba = 0; };
    std::deque<bb_clear_cmd> bb_q;

    std::uint64_t next_marker = 1;
    std::uint64_t flush_marker_requested = 0;
    std::uint64_t flush_marker_done = 0;

    std::atomic<std::uint64_t> try_fail_count{0};
    std::uint64_t qpc_last_fail_log = 0;
    std::uint64_t qpc_f = 0;

    bool recording = false;
    bool playing   = false;
    bool looping   = false;
    std::uint32_t play_cursor = 0;

    std::uint32_t rec_pitch_bytes  = 0;
    std::uint32_t rec_pitch_pixels = 0;
    std::vector<std::vector<std::uint32_t>> rec_frames;

    void create_device()
    {
        const D3D_FEATURE_LEVEL fl_req[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL fl_out{};

        const HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, fl_req, 1, D3D11_SDK_VERSION,
            dev.GetAddressOf(), &fl_out, ctx.GetAddressOf());

        if (FAILED(hr)) { DebugFail("D3D11CreateDevice", hr); std::abort(); }

        ComPtr<IDXGIDevice>  dxgiDev;
        ComPtr<IDXGIAdapter> dxgiAd;

        dev.As(&dxgiDev);
        dxgiDev->GetAdapter(dxgiAd.GetAddressOf());

        dxgiAd->GetParent(__uuidof(IDXGIFactory2),
            reinterpret_cast<void**>(factory2.GetAddressOf()));

        if (!factory2)
        {
            dxgiAd->GetParent(__uuidof(IDXGIFactory1),
                reinterpret_cast<void**>(factory1.GetAddressOf()));
        }
        else
        {
            factory2.As(&factory1);
        }

        if (factory2) factory2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        else          factory1->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }

    void create_swapchain()
    {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width       = w;
        desc.Height      = h;
        desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc  = { 1, 0 };
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 3;
        desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling     = DXGI_SCALING_STRETCH;

        const HRESULT hr = factory2->CreateSwapChainForHwnd(
            dev.Get(), hwnd, &desc, nullptr, nullptr, sc1.GetAddressOf());

        if (FAILED(hr)) { DebugFail("CreateSwapChainForHwnd", hr); std::abort(); }

        sc1.As(&sc3);
    }

    void create_rtvs()
    {
        DXGI_SWAP_CHAIN_DESC1 d{};
        sc1->GetDesc1(&d);

        rtvs.resize(d.BufferCount);
        for (UINT i = 0; i < d.BufferCount; ++i)
        {
            ComPtr<ID3D11Texture2D> bb;
            sc1->GetBuffer(i, IID_PPV_ARGS(bb.GetAddressOf()));
            dev->CreateRenderTargetView(bb.Get(), nullptr, rtvs[i].GetAddressOf());
        }
    }

    void update_viewport() noexcept
    {
        vp.Width    = (float)w;
        vp.Height   = (float)h;
        vp.MinDepth = 0.f;
        vp.MaxDepth = 1.f;
        vp.TopLeftX = 0.f;
        vp.TopLeftY = 0.f;
    }

    void compile_shaders()
    {
        const char* vs_src =
            "struct O{float4 p:SV_Position;float2 u:TEX;};"
            "O main(uint i:SV_VertexId){"
            "float2 t=float2((i<<1)&2,i&2);"
            "O o;o.p=float4(t*float2(2,-2)+float2(-1,1),0,1);"
            "o.u=t;return o;}";

        const char* ps_src =
            "Texture2D t:register(t0);SamplerState s:register(s0);"
            "float4 main(float4 p:SV_Position,float2 u:TEX):SV_Target"
            "{return t.SampleLevel(s,u,0);}";

        ComPtr<ID3DBlob> vsb, psb, err;

        HRESULT hr = D3DCompile(vs_src, std::strlen(vs_src), nullptr, nullptr, nullptr,
            "main", "vs_5_0", 0, 0, vsb.GetAddressOf(), err.GetAddressOf());
        if (FAILED(hr)) { DebugFail("D3DCompile(vs)", hr); std::abort(); }

        err.Reset();
        hr = D3DCompile(ps_src, std::strlen(ps_src), nullptr, nullptr, nullptr,
            "main", "ps_5_0", 0, 0, psb.GetAddressOf(), err.GetAddressOf());
        if (FAILED(hr)) { DebugFail("D3DCompile(ps)", hr); std::abort(); }

        hr = dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vs.GetAddressOf());
        if (FAILED(hr)) { DebugFail("CreateVertexShader", hr); std::abort(); }

        hr = dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, ps.GetAddressOf());
        if (FAILED(hr)) { DebugFail("CreatePixelShader", hr); std::abort(); }

        D3D11_SAMPLER_DESC sd{};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        hr = dev->CreateSamplerState(&sd, samp.GetAddressOf());
        if (FAILED(hr)) { DebugFail("CreateSamplerState", hr); std::abort(); }
    }

    void set_pipeline() noexcept
    {
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ID3D11SamplerState* s0 = samp.Get();
        ctx->PSSetSamplers(0, 1, &s0);
    }

    void create_ring(std::uint32_t count)
    {
        if (count == 0) count = 2;
        ring_size = count;

        ring_tex.resize(ring_size);
        ring_srv.resize(ring_size);

        D3D11_TEXTURE2D_DESC td{};
        td.Width          = w;
        td.Height         = h;
        td.MipLevels      = 1;
        td.ArraySize      = 1;
        td.Format         = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc     = { 1, 0 };
        td.Usage          = D3D11_USAGE_DYNAMIC;
        td.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        for (std::uint32_t i = 0; i < ring_size; ++i)
        {
            HRESULT hr = dev->CreateTexture2D(&td, nullptr, ring_tex[i].GetAddressOf());
            if (FAILED(hr)) { DebugFail("CreateTexture2D(ring)", hr); std::abort(); }
            hr = dev->CreateShaderResourceView(ring_tex[i].Get(), nullptr, ring_srv[i].GetAddressOf());
            if (FAILED(hr)) { DebugFail("CreateSRV(ring)", hr); std::abort(); }
        }

        D3D11_MAPPED_SUBRESOURCE map{};
        HRESULT hr = ctx->Map(ring_tex[0].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        if (FAILED(hr)) { DebugFail("Map(ring_tex[0])", hr); std::abort(); }
        gpu_pitch_bytes = (std::uint32_t)map.RowPitch;
        ctx->Unmap(ring_tex[0].Get(), 0);

        const std::uint32_t pitch_pixels = gpu_pitch_bytes / 4u;

        slots.resize(ring_size);

        const std::uint32_t z_align_floats = 16u;
        const std::uint32_t z_pitch = align_up_u32(w, z_align_floats);

        for (std::uint32_t i = 0; i < ring_size; ++i)
        {
            auto& s = slots[i];

            s.color_pitch_bytes  = gpu_pitch_bytes;
            s.color_pitch_pixels = pitch_pixels;
            s.color.resize((std::size_t)pitch_pixels * (std::size_t)h);
            std::memset(s.color.data(), 0, s.color.size() * sizeof(std::uint32_t));

            s.z_pitch = z_pitch;
            const std::size_t zcount = (std::size_t)z_pitch * (std::size_t)h;
            s.z = static_cast<float*>(_aligned_malloc(zcount * sizeof(float), 64));
            if (!s.z) std::abort();
            std::memset(s.z, 0, zcount * sizeof(float));

            s.generation = 1;
            s.state = slot_t::state_t::free;
        }

        rec_pitch_bytes  = gpu_pitch_bytes;
        rec_pitch_pixels = pitch_pixels;
    }

    void bind_backbuffer() noexcept
    {
        const UINT idx = sc3->GetCurrentBackBufferIndex();
        ID3D11RenderTargetView* rtv = rtvs[idx].Get();
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
    }

    void clear_backbuffer_rgba8_now(std::uint32_t rgba) noexcept
    {
        const float r = float((rgba      ) & 0xFF) / 255.0f;
        const float g = float((rgba >>  8) & 0xFF) / 255.0f;
        const float b = float((rgba >> 16) & 0xFF) / 255.0f;
        const float a = float((rgba >> 24) & 0xFF) / 255.0f;
        const float c[4] = { r, g, b, a };

        const UINT idx = sc3->GetCurrentBackBufferIndex();
        ID3D11RenderTargetView* rtv = rtvs[idx].Get();
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, c);
    }

    void upload_bytes_to_tex(std::uint32_t slot, const std::uint8_t* src_base, std::uint32_t src_pitch_bytes) noexcept
    {
        ID3D11Texture2D* tex = ring_tex[slot].Get();
        if (!tex || !src_base || src_pitch_bytes == 0) return;

        D3D11_MAPPED_SUBRESOURCE map{};
        const HRESULT hr = ctx->Map(tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        if (FAILED(hr)) return;

        const std::uint32_t copy_bytes = (std::min)(src_pitch_bytes, (std::uint32_t)map.RowPitch);

        const std::uint8_t* src = src_base;
        std::uint8_t* dst = (std::uint8_t*)map.pData;

        for (std::uint32_t y = 0; y < h; ++y)
        {
#ifdef USE_SIMD
            copy_row_simd(dst, src, copy_bytes);
#else
            std::memcpy(dst, src, copy_bytes);
#endif
            src += src_pitch_bytes;
            dst += (std::uint32_t)map.RowPitch;
        }

        ctx->Unmap(tex, 0);
    }

    void upload_slot_to_tex(std::uint32_t slot) noexcept
    {
        auto& s = slots[slot];
        upload_bytes_to_tex(slot,
            reinterpret_cast<const std::uint8_t*>(s.color.data()),
            s.color_pitch_bytes);
    }

    void present_tex_slot_now(std::uint32_t slot) noexcept
    {
        set_pipeline();
        bind_backbuffer();
        ctx->RSSetViewports(1, &vp);

        ID3D11ShaderResourceView* srv = ring_srv[slot].Get();
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->Draw(3, 0);

        ID3D11ShaderResourceView* null_srv = nullptr;
        ctx->PSSetShaderResources(0, 1, &null_srv);

        sc1->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
    }

    void clear_slot_cpu(std::uint32_t slot) noexcept
    {
        auto& s = slots[slot];
        if (!s.color.empty()) std::memset(s.color.data(), 0, s.color.size() * sizeof(std::uint32_t));
        if (s.z)
        {
            const std::size_t zcount = (std::size_t)s.z_pitch * (std::size_t)h;
            std::memset(s.z, 0, zcount * sizeof(float));
        }
    }

    void copy_frame_to_recording(const cpu_frame& f) noexcept
    {
        if (!f.valid()) return;
        if (rec_pitch_bytes == 0 || rec_pitch_pixels == 0) return;

        std::vector<std::uint32_t> dst;
        dst.resize((std::size_t)rec_pitch_pixels * (std::size_t)h);

        const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(f.color);
        std::uint8_t* out = reinterpret_cast<std::uint8_t*>(dst.data());

        const std::uint32_t copy_bytes = (std::min)(f.color_pitch_bytes, rec_pitch_bytes);

        for (std::uint32_t y = 0; y < h; ++y)
        {
            std::memcpy(out, src, copy_bytes);
            if (rec_pitch_bytes > copy_bytes)
                std::memset(out + copy_bytes, 0, (std::size_t)(rec_pitch_bytes - copy_bytes));
            src += f.color_pitch_bytes;
            out += rec_pitch_bytes;
        }

        rec_frames.emplace_back(std::move(dst));
    }

    void log_try_fail_if_needed_locked() noexcept
    {
        const std::uint64_t now = qpc_now();
        if (!qpc_f) qpc_f = qpc_freq();
        if (!qpc_last_fail_log) qpc_last_fail_log = now;

        const double dt = static_cast<double>(now - qpc_last_fail_log) / static_cast<double>(qpc_f);
        if (dt < 0.5) return;
        qpc_last_fail_log = now;

        std::uint32_t free_n = 0, acq_n = 0, queued_n = 0, pres_n = 0, clr_n = 0;
        for (auto& s : slots)
        {
            switch (s.state)
            {
                case slot_t::state_t::free:       ++free_n; break;
                case slot_t::state_t::acquired:   ++acq_n; break;
                case slot_t::state_t::queued:     ++queued_n; break;
                case slot_t::state_t::presenting: ++pres_n; break;
                case slot_t::state_t::clearing:   ++clr_n; break;
            }
        }
    }

    void clear_loop_worker() noexcept
    {
        for (;;)
        {
            std::uint32_t slot = 0xFFFFFFFFu;

            {
                std::unique_lock<std::mutex> lk(mtx);
                cv_clear.wait(lk, [&] { return shutting_down || !clear_q.empty(); });

                if (shutting_down && clear_q.empty())
                    return;

                slot = clear_q.front();
                clear_q.pop_front();
            }

            if (slot < slots.size())
            {
                clear_slot_cpu(slot);

                {
                    std::lock_guard<std::mutex> lk(mtx);
                    auto& s = slots[slot];
                    if (s.state == slot_t::state_t::clearing)
                    {
                        s.state = slot_t::state_t::free;
                        cv_free.notify_one();
                    }
                }
            }
        }
    }

    void drop_latest_slot_if_queued_locked() noexcept
    {
        if (!latest_slot_valid) return;

        const std::uint32_t old_slot = latest_slot;
        const std::uint32_t old_gen  = latest_gen;

        latest_slot_valid = false;

        if (old_slot >= slots.size())
            return;

        auto& os = slots[old_slot];
        if (os.state == slot_t::state_t::queued && os.generation == old_gen)
        {
            os.state = slot_t::state_t::clearing;
            ++os.generation;
            clear_q.push_back(old_slot);
            cv_clear.notify_one();
        }
    }

    void present_loop() noexcept
    {
        std::uint32_t upload_slot = 0;

        for (;;)
        {
            bool do_slot = false;
            std::uint32_t slot = 0xFFFFFFFFu;
            std::uint32_t gen  = 0;

            bool do_rec = false;
            std::uint32_t rec_idx = 0;

            {
                std::unique_lock<std::mutex> lk(mtx);
                cv_present.wait(lk, [&] {
                    return shutting_down
                        || latest_slot_valid
                        || latest_rec_valid
                        || !bb_q.empty()
                        || (flush_marker_requested != 0);
                });

                if (shutting_down)
                    return;

                while (!bb_q.empty())
                {
                    const auto cmd = bb_q.front();
                    bb_q.pop_front();
                    lk.unlock();
                    clear_backbuffer_rgba8_now(cmd.rgba);
                    lk.lock();
                }

                if (flush_marker_requested != 0)
                {
                    bool busy = latest_slot_valid || latest_rec_valid || !clear_q.empty();
                    if (!busy)
                    {
                        for (auto& s : slots)
                        {
                            if (s.state == slot_t::state_t::queued || s.state == slot_t::state_t::presenting)
                            {
                                busy = true;
                                break;
                            }
                        }
                    }

                    if (!busy)
                    {
                        flush_marker_done = flush_marker_requested;
                        flush_marker_requested = 0;
                        cv_flush.notify_all();
                    }
                }

                if (latest_rec_valid)
                {
                    do_rec = true;
                    rec_idx = latest_rec_index;
                    latest_rec_valid = false;
                    drop_latest_slot_if_queued_locked();
                }
                else if (latest_slot_valid)
                {
                    do_slot = true;
                    slot = latest_slot;
                    gen  = latest_gen;
                    latest_slot_valid = false;

                    if (slot < slots.size())
                    {
                        auto& s = slots[slot];
                        if (s.state == slot_t::state_t::queued && s.generation == gen)
                        {
                            s.state = slot_t::state_t::presenting;
                        }
                        else
                        {
                            if (s.state == slot_t::state_t::queued || s.state == slot_t::state_t::presenting)
                            {
                                s.state = slot_t::state_t::clearing;
                                clear_q.push_back(slot);
                                cv_clear.notify_one();
                            }
                            do_slot = false;
                            slot = 0xFFFFFFFFu;
                        }
                    }
                    else
                    {
                        do_slot = false;
                        slot = 0xFFFFFFFFu;
                    }
                }
                else
                {
                    continue;
                }
            }

            if (do_rec)
            {
                const std::uint8_t* src = nullptr;
                std::uint32_t src_pitch = 0;

                {
                    std::lock_guard<std::mutex> lk(mtx);
                    if (rec_idx < rec_frames.size())
                    {
                        src = reinterpret_cast<const std::uint8_t*>(rec_frames[rec_idx].data());
                        src_pitch = rec_pitch_bytes;
                    }
                }

                if (src && src_pitch)
                {
                    upload_slot = (upload_slot + 1u) % (ring_size ? ring_size : 1u);
                    upload_bytes_to_tex(upload_slot, src, src_pitch);
                    present_tex_slot_now(upload_slot);
                }
            }
            else if (do_slot && slot != 0xFFFFFFFFu)
            {
                upload_slot_to_tex(slot);
                present_tex_slot_now(slot);

                {
                    std::lock_guard<std::mutex> lk(mtx);
                    if (slot < slots.size())
                    {
                        auto& s = slots[slot];
                        if (s.state == slot_t::state_t::presenting)
                        {
                            s.state = slot_t::state_t::clearing;
                            ++s.generation;
                            clear_q.push_back(slot);
                            cv_clear.notify_one();
                        }
                    }
                }
            }
        }
    }

    void start_threads()
    {
        shutting_down = false;

        for (int i = 0; i < CLEAR_WORKERS; ++i)
            clear_workers[i] = std::thread([this] { clear_loop_worker(); });

        present_worker = std::thread([this] { present_loop(); });
    }

    void stop_threads()
    {
        {
            std::lock_guard<std::mutex> lk(mtx);
            shutting_down = true;
        }

        cv_present.notify_all();
        cv_clear.notify_all();
        cv_free.notify_all();
        cv_flush.notify_all();

        if (present_worker.joinable()) present_worker.join();

        for (int i = 0; i < CLEAR_WORKERS; ++i)
            if (clear_workers[i].joinable())
                clear_workers[i].join();
    }

    bool has_free_slot_nolock() const noexcept
    {
        for (const auto& s : slots)
            if (s.state == slot_t::state_t::free) return true;
        return false;
    }

    cpu_frame acquire_slot_blocking() noexcept
    {
        std::unique_lock<std::mutex> lk(mtx);

        cv_free.wait(lk, [&] {
            return shutting_down || has_free_slot_nolock();
        });

        if (shutting_down)
            return cpu_frame{};

        for (std::uint32_t i = 0; i < (std::uint32_t)slots.size(); ++i)
        {
            auto& s = slots[i];
            if (s.state == slot_t::state_t::free)
            {
                s.state = slot_t::state_t::acquired;

                cpu_frame f{};
                f.w = w;
                f.h = h;
                f.color_pitch_bytes  = s.color_pitch_bytes;
                f.color_pitch_pixels = s.color_pitch_pixels;
                f.color = s.color.data();
                f.z_pitch = s.z_pitch;
                f.z = s.z;
                f.slot = i;
                f.generation = s.generation;
                return f;
            }
        }

        return cpu_frame{};
    }

    cpu_frame acquire_slot_try() noexcept
    {
        std::lock_guard<std::mutex> lk(mtx);

        if (shutting_down)
            return cpu_frame{};

        for (std::uint32_t i = 0; i < (std::uint32_t)slots.size(); ++i)
        {
            auto& s = slots[i];
            if (s.state == slot_t::state_t::free)
            {
                s.state = slot_t::state_t::acquired;

                cpu_frame f{};
                f.w = w;
                f.h = h;
                f.color_pitch_bytes  = s.color_pitch_bytes;
                f.color_pitch_pixels = s.color_pitch_pixels;
                f.color = s.color.data();
                f.z_pitch = s.z_pitch;
                f.z = s.z;
                f.slot = i;
                f.generation = s.generation;
                return f;
            }
        }
        return cpu_frame{};
    }

    void enqueue_present_latest(std::uint32_t slot, std::uint32_t gen) noexcept
    {
        std::lock_guard<std::mutex> lk(mtx);

        if (shutting_down) return;
        if (slot >= slots.size()) return;

        auto& s = slots[slot];
        if (s.state != slot_t::state_t::acquired) return;
        if (s.generation != gen) return;

        s.state = slot_t::state_t::queued;

        if (latest_slot_valid)
        {
            const std::uint32_t old_slot = latest_slot;
            const std::uint32_t old_gen  = latest_gen;

            if (old_slot < slots.size())
            {
                auto& os = slots[old_slot];
                if (os.state == slot_t::state_t::queued && os.generation == old_gen)
                {
                    os.state = slot_t::state_t::clearing;
                    ++os.generation;
                    clear_q.push_back(old_slot);
                    cv_clear.notify_one();
                }
            }
        }

        latest_slot_valid = true;
        latest_slot  = slot;
        latest_gen   = gen;

        latest_rec_valid = false;

        cv_present.notify_one();
    }

    void enqueue_recorded_latest(std::uint32_t rec_index) noexcept
    {
        std::lock_guard<std::mutex> lk(mtx);

        if (shutting_down) return;
        if (rec_index >= rec_frames.size()) return;

        latest_rec_valid = true;
        latest_rec_index = rec_index;

        drop_latest_slot_if_queued_locked();

        cv_present.notify_one();
    }

    void enqueue_backbuffer_clear(std::uint32_t rgba) noexcept
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (shutting_down) return;
        bb_q.push_back(bb_clear_cmd{ rgba });
        cv_present.notify_one();
    }

    void flush_blocking() noexcept
    {
        std::uint64_t marker = 0;
        {
            std::lock_guard<std::mutex> lk(mtx);
            marker = next_marker++;
            flush_marker_requested = marker;
        }

        cv_present.notify_one();

        std::unique_lock<std::mutex> lk(mtx);
        cv_flush.wait(lk, [&] {
            return shutting_down || (flush_marker_done >= marker && flush_marker_requested == 0);
        });
    }

    void mark_slot_clearing_and_enqueue(std::uint32_t slot) noexcept
    {
        if (slot >= slots.size()) return;
        auto& s = slots[slot];
        if (s.state == slot_t::state_t::clearing) return;
        if (s.state == slot_t::state_t::acquired || s.state == slot_t::state_t::queued || s.state == slot_t::state_t::presenting)
        {
            s.state = slot_t::state_t::clearing;
            ++s.generation;
            clear_q.push_back(slot);
            cv_clear.notify_one();
        }
    }

    void record_begin_locked() noexcept
    {
        rec_frames.clear();
        rec_frames.shrink_to_fit();
        recording = true;
        playing = false;
        looping = false;
        play_cursor = 0;
        latest_rec_valid = false;
    }

    void record_end_locked() noexcept
    {
        recording = false;
        play_cursor = 0;
    }

    void playback_start_locked(bool loop) noexcept
    {
        if (rec_frames.empty())
        {
            playing = false;
            looping = false;
            play_cursor = 0;
            latest_rec_valid = false;
            return;
        }
        playing = true;
        looping = loop;
        play_cursor = 0;
        latest_rec_valid = false;
    }

    void playback_stop_locked() noexcept
    {
        playing = false;
        looping = false;
        latest_rec_valid = false;
    }

    bool playback_present_next_nolock() noexcept
    {
        if (rec_frames.empty())
            return false;

        if (play_cursor >= (std::uint32_t)rec_frames.size())
        {
            if (!looping)
            {
                playing = false;
                return false;
            }
            play_cursor = 0;
        }

        const std::uint32_t idx = play_cursor++;
        enqueue_recorded_latest(idx);
        return true;
    }

    void destroy_all() noexcept
    {
        stop_threads();

        for (auto& s : slots)
        {
            if (s.z) { _aligned_free(s.z); s.z = nullptr; }
            s.color.clear();
            s.color.shrink_to_fit();
        }

        slots.clear();
        ring_srv.clear();
        ring_tex.clear();
        rtvs.clear();

        samp.Reset();
        ps.Reset();
        vs.Reset();

        sc3.Reset();
        sc1.Reset();
        factory2.Reset();
        factory1.Reset();
        ctx.Reset();
        dev.Reset();

        hwnd = nullptr;
        w = h = 0;
        ring_size = 0;
        gpu_pitch_bytes = 0;

        {
            std::lock_guard<std::mutex> lk(mtx);
            latest_slot_valid = false;
            latest_rec_valid = false;
            clear_q.clear();
            bb_q.clear();
            flush_marker_requested = 0;
            flush_marker_done = 0;
            next_marker = 1;
            shutting_down = false;

            recording = false;
            playing = false;
            looping = false;
            play_cursor = 0;
            rec_pitch_bytes = 0;
            rec_pitch_pixels = 0;
            rec_frames.clear();
        }
    }
};

namespace fox
{
    gfx_dx11::gfx_dx11() : p_(std::make_unique<pimpl>()) {}
    gfx_dx11::~gfx_dx11() { destroy(); }

    gfx_dx11::gfx_dx11(gfx_dx11&&) noexcept = default;
    gfx_dx11& gfx_dx11::operator=(gfx_dx11&&) noexcept = default;

    void gfx_dx11::create(const create_dx11_params& params)
    {
        if (!p_) p_ = std::make_unique<pimpl>();
        auto& s = *p_;

        s.hwnd = (HWND)params.hwnd;
        s.w = params.width;
        s.h = params.height;

        s.create_device();
        s.create_swapchain();
        s.create_rtvs();
        s.update_viewport();
        s.compile_shaders();
        s.set_pipeline();

        s.create_ring(params.ring_size);

        for (std::uint32_t i = 0; i < s.ring_size; ++i)
            s.clear_slot_cpu(i);

        s.start_threads();
    }

    void gfx_dx11::destroy()
    {
        if (!p_) return;
        p_->destroy_all();
        p_.reset();
    }

    std::uint32_t gfx_dx11::width() const noexcept  { return p_ ? p_->w : 0; }
    std::uint32_t gfx_dx11::height() const noexcept { return p_ ? p_->h : 0; }

    cpu_frame gfx_dx11::begin_frame() noexcept
    {
        if (!p_) return cpu_frame{};
        return p_->acquire_slot_blocking();
    }

    cpu_frame gfx_dx11::try_begin_frame() noexcept
    {
        if (!p_) return cpu_frame{};
        return p_->acquire_slot_try();
    }

    void gfx_dx11::present(const cpu_frame& frame) noexcept
    {
        if (!p_) return;
        if (!frame.valid()) return;
        p_->enqueue_present_latest(frame.slot, frame.generation);
    }

    void gfx_dx11::flush() noexcept
    {
        if (!p_) return;
        p_->flush_blocking();
    }

    void gfx_dx11::clear_backbuffer_rgba8(std::uint32_t rgba) noexcept
    {
        if (!p_) return;
        p_->enqueue_backbuffer_clear(rgba);
    }

    std::uint32_t gfx_dx11::pending_frames() const noexcept
    {
        if (!p_) return 0;

        std::lock_guard<std::mutex> lk(p_->mtx);

        std::uint32_t pending = 0;
        pending += (std::uint32_t)p_->bb_q.size();
        pending += (std::uint32_t)p_->clear_q.size();
        if (p_->latest_slot_valid) ++pending;
        if (p_->latest_rec_valid) ++pending;

        for (auto& s : p_->slots)
        {
            if (s.state == pimpl::slot_t::state_t::queued ||
                s.state == pimpl::slot_t::state_t::presenting ||
                s.state == pimpl::slot_t::state_t::clearing)
                ++pending;
        }
        return pending;
    }

    void gfx_dx11::record_begin() noexcept
    {
        if (!p_) return;
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->record_begin_locked();
    }

    void gfx_dx11::record_submit(const cpu_frame& frame) noexcept
    {
        if (!p_) return;
        if (!frame.valid()) return;

        {
            std::lock_guard<std::mutex> lk(p_->mtx);
            if (!p_->recording) return;
            p_->copy_frame_to_recording(frame);
            p_->mark_slot_clearing_and_enqueue(frame.slot);
        }

        p_->cv_present.notify_one();
        p_->cv_clear.notify_all();
        p_->cv_free.notify_one();
    }

    void gfx_dx11::record_end() noexcept
    {
        if (!p_) return;
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->record_end_locked();
    }

    std::uint32_t gfx_dx11::record_frame_count() const noexcept
    {
        if (!p_) return 0;
        std::lock_guard<std::mutex> lk(p_->mtx);
        return (std::uint32_t)p_->rec_frames.size();
    }

    void gfx_dx11::playback_start(bool loop) noexcept
    {
        if (!p_) return;
        {
            std::lock_guard<std::mutex> lk(p_->mtx);
            p_->playback_start_locked(loop);
        }
        p_->cv_present.notify_one();
    }

    void gfx_dx11::playback_stop() noexcept
    {
        if (!p_) return;
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->playback_stop_locked();
    }

    bool gfx_dx11::playback_present_next() noexcept
    {
        if (!p_) return false;

        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(p_->mtx);
            if (!p_->playing) return false;
            ok = p_->playback_present_next_nolock();
        }

        if (ok) p_->cv_present.notify_one();
        return ok;
    }

    void gfx_dx11::playback_rewind() noexcept
    {
        if (!p_) return;
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->play_cursor = 0;
        p_->latest_rec_valid = false;
    }

    playback_state gfx_dx11::get_playback_state() const noexcept
    {
        playback_state st{};
        if (!p_) return st;
        std::lock_guard<std::mutex> lk(p_->mtx);
        st.recording = p_->recording;
        st.playing   = p_->playing;
        st.looping   = p_->looping;
        st.frame_count = (std::uint32_t)p_->rec_frames.size();
        st.cursor = p_->play_cursor;
        return st;
    }
}
