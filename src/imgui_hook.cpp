#include "optimized/imgui_hook.h"

#include <windows.h>
#include <algorithm>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

extern LRESULT CALLBACK ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

namespace fox
{
    static float clamp_scale(float s) noexcept
    {
        if (s < 0.75f) return 0.75f;
        if (s > 3.0f)  return 3.0f;
        return s;
    }

    void imgui_hook::rebuild_fonts_(float scale) noexcept
    {
        if (!ImGui::GetCurrentContext())
            return;

        scale = clamp_scale(scale);
        dpi_scale_ = scale;

        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);
        style.ScaleAllSizes(dpi_scale_);
        ImGui::GetStyle() = style;

        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        const float font_px = base_font_px_ * dpi_scale_;

        io.FontDefault = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Medium.ttf", font_px);

        if (!io.FontDefault)
            io.FontDefault = io.Fonts->AddFontDefault();

        ImGui_ImplDX11_InvalidateDeviceObjects();
        ImGui_ImplDX11_CreateDeviceObjects();
    }

    imgui_hook& imgui_hook::instance() noexcept
    {
        static imgui_hook g;
        return g;
    }

    void imgui_hook::init(void* hwnd, ID3D11Device* dev, ID3D11DeviceContext* ctx) noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);

        if (initialized_)
            return;

        hwnd_ = hwnd;
        dev_  = dev;
        ctx_  = ctx;

        if (!hwnd_ || !dev_ || !ctx_)
            return;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init((HWND)hwnd_);
        ImGui_ImplDX11_Init(dev_, ctx_);

        UINT dpi = 96;
        if (auto hw = static_cast<HWND>(hwnd_))
        {
            if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll"))
            {
                using get_dpi_for_window_fn = UINT(WINAPI*)(HWND);
                if (auto pGetDpiForWindow = reinterpret_cast<get_dpi_for_window_fn>(::GetProcAddress(user32, "GetDpiForWindow")))
                    dpi = pGetDpiForWindow(hw);
            }
        }

        const float scale = static_cast<float>(dpi) / 96.0f;
        rebuild_fonts_(scale);

        // Initial display size
        refresh_display_size_from_hwnd();

        enabled_     = true;
        initialized_ = true;
    }

    void imgui_hook::shutdown() noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);

        if (!initialized_)
            return;

        if (ImGui::GetCurrentContext())
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }

        views_.clear();
        hwnd_ = nullptr;
        dev_  = nullptr;
        ctx_  = nullptr;

        last_w_ = 0;
        last_h_ = 0;

        enabled_     = false;
        initialized_ = false;
    }

    void imgui_hook::set_enabled(bool e) noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);
        enabled_ = e;
    }

    bool imgui_hook::enabled() const noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return enabled_;
    }

    bool imgui_hook::initialized() const noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return initialized_;
    }

    bool imgui_hook::message_pump(void* hwnd, std::uint32_t msg, std::uint64_t wp, std::int64_t lp) noexcept
    {
        if (!enabled_ || !initialized_)
            return false;

        return ImGui_ImplWin32_WndProcHandler(
            (HWND)hwnd,
            (UINT)msg,
            (WPARAM)wp,
            (LPARAM)lp
        ) != 0;
    }

    void imgui_hook::add_view(std::string name, view_fn fn)
    {
        std::lock_guard<std::mutex> lk(mtx_);

        for (auto& v : views_)
        {
            if (v.name == name)
            {
                v.fn   = std::move(fn);
                v.open = true;
                return;
            }
        }

        view v{};
        v.name = std::move(name);
        v.fn   = std::move(fn);
        v.open = true;
        views_.push_back(std::move(v));
    }

    void imgui_hook::clear_views() noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);
        views_.clear();
    }

    void imgui_hook::apply_display_size_(std::uint32_t w, std::uint32_t h) noexcept
    {
        if (!ImGui::GetCurrentContext())
            return;

        w = (std::max)(w, 1u);
        h = (std::max)(h, 1u);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);

        last_w_ = w;
        last_h_ = h;
    }

    void imgui_hook::on_resize(std::uint32_t client_w, std::uint32_t client_h) noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);

        if (!initialized_)
            return;

        apply_display_size_(client_w, client_h);
    }

    void imgui_hook::on_dpi_changed(std::uint32_t dpi, float scale) noexcept
    {
        if (!initialized_ || !ImGui::GetCurrentContext())
            return;

        if (scale <= 0.0f)
            scale = (float)dpi / 96.0f;

        rebuild_fonts_(scale);
        refresh_display_size_from_hwnd();
    }

    void imgui_hook::refresh_display_size_from_hwnd() noexcept
    {
        if (!initialized_ || !hwnd_)
            return;

        RECT rc{};
        if (!::GetClientRect((HWND)hwnd_, &rc))
            return;

        const std::uint32_t w = (rc.right > rc.left) ? (std::uint32_t)(rc.right - rc.left) : 0u;
        const std::uint32_t h = (rc.bottom > rc.top) ? (std::uint32_t)(rc.bottom - rc.top) : 0u;

        apply_display_size_(w, h);
    }

    std::uint32_t imgui_hook::last_client_w() const noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return last_w_;
    }

    std::uint32_t imgui_hook::last_client_h() const noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return last_h_;
    }

    void imgui_hook::begin_frame(float dt_seconds) noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);

        if (!enabled_ || !initialized_)
            return;

        ImGuiIO& io = ImGui::GetIO();
        if (dt_seconds > 0.0f) io.DeltaTime = dt_seconds;
        else if (io.DeltaTime <= 0.0f) io.DeltaTime = 1.0f / 60.0f;

        if (last_w_ == 0 || last_h_ == 0)
            refresh_display_size_from_hwnd();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (!views_.empty())
        {
            ImGui::Begin("UI", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            for (auto& v : views_)
                ImGui::Checkbox(v.name.c_str(), &v.open);
            ImGui::End();
        }

        for (auto& v : views_)
        {
            if (!v.open || !v.fn)
                continue;

            ImGui::Begin(v.name.c_str(), &v.open);
            v.fn();
            ImGui::End();
        }

        ImGui::Render();
    }

    void imgui_hook::render() noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);

        if (!enabled_ || !initialized_)
            return;

        ImDrawData* dd = ImGui::GetDrawData();
        if (!dd)
            return;

        ImGui_ImplDX11_RenderDrawData(dd);
    }
}
