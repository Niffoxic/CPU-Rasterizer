#include "optimized/platform_windows.h"

#include <windows.h>

#include <cassert>
#include <cstring>
#include <string>

#include "imgui.h"
#include "optimized/imgui_hook.h"

namespace
{
    static inline int GET_X(const LPARAM lp) noexcept { return (int)static_cast<short>(LOWORD(lp)); }
    static inline int GET_Y(const LPARAM lp) noexcept { return (int)static_cast<short>(HIWORD(lp)); }

    static inline void set_icon_hwnd(const HWND hwnd, HICON _small, HICON big) noexcept
    {
        if (_small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(_small));
        if (big)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   reinterpret_cast<LPARAM>(big));
    }

    static inline HICON load_icon_from_res(const HINSTANCE inst, const std::uint16_t res_id, const int cx, const int cy) noexcept
    {
        if (!res_id) return nullptr;
        return static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(res_id), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    }

    static inline HICON load_icon_from_file(const std::wstring& path, const int cx, const int cy) noexcept
    {
        if (path.empty()) return nullptr;
        return static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE | LR_DEFAULTCOLOR));
    }

    static inline void safe_clip_cursor_release() noexcept { ClipCursor(nullptr); }

    static inline void show_cursor_set_visible(const bool visible) noexcept
    {
        if (visible) { while (ShowCursor(TRUE) < 0) {} }
        else         { while (ShowCursor(FALSE) >= 0) {} }
    }
}

class fox::platform_window::pimpl
{
public:
    HWND hwnd = nullptr;
    HINSTANCE hinst = nullptr;

    std::wstring class_name;
    std::wstring title;

    std::uint32_t w = 0;
    std::uint32_t h = 0;
    bool fullscreen = false;
    bool resizable  = true;

    bool keys[256]{};
    int mx = 0;
    int my = 0;
    int wheel = 0;
    fox::mouse_button_state buttons[3]{
        fox::mouse_button_state::up,
        fox::mouse_button_state::up,
        fox::mouse_button_state::up
    };

    bool cursor_hidden = false;
    bool cursor_clipped = false;
    bool close_requested = false;

    HICON icon_small = nullptr;
    HICON icon_big   = nullptr;
    bool  icon_small_owned = false;
    bool  icon_big_owned   = false;

    void age_buttons_once_per_frame() noexcept
    {
        for (int i = 0; i < 3; ++i)
            if (buttons[i] == fox::mouse_button_state::down)
                buttons[i] = fox::mouse_button_state::pressed;
    }

    static void pump_messages(pimpl& self) noexcept
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                self.close_requested = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        self.age_buttons_once_per_frame();
    }

    static LRESULT CALLBACK WndProc(HWND hwnd_, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* self = reinterpret_cast<pimpl*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd = hwnd_;
        }

        auto* self = reinterpret_cast<pimpl*>(GetWindowLongPtrW(hwnd_, GWLP_USERDATA));
        if (!self)
            return DefWindowProcW(hwnd_, msg, wp, lp);

        return self->real_wndproc(hwnd_, msg, wp, lp);
    }

    LRESULT real_wndproc(HWND hwnd_, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (imgui_hook::instance().message_pump(hwnd_, msg, wp, lp)) return 0;

        switch (msg)
        {
        case WM_CLOSE:
        case WM_DESTROY:
            close_requested = true;
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            keys[(unsigned)wp & 0xFFu] = true;
            return 0;

        case WM_KEYUP:
            keys[(unsigned)wp & 0xFFu] = false;
            return 0;

        case WM_MOUSEMOVE:
            mx = GET_X(lp);
            my = GET_Y(lp);
            return 0;

        case WM_MOUSEWHEEL:
            mx = GET_X(lp);
            my = GET_Y(lp);
            wheel += GET_WHEEL_DELTA_WPARAM(wp);
            return 0;

        case WM_LBUTTONDOWN:
            mx = GET_X(lp); my = GET_Y(lp);
            buttons[(int)fox::mouse_button::left] = fox::mouse_button_state::down;
            return 0;
        case WM_LBUTTONUP:
            mx = GET_X(lp); my = GET_Y(lp);
            buttons[(int)fox::mouse_button::left] = fox::mouse_button_state::up;
            return 0;

        case WM_MBUTTONDOWN:
            mx = GET_X(lp); my = GET_Y(lp);
            buttons[(int)fox::mouse_button::middle] = fox::mouse_button_state::down;
            return 0;
        case WM_MBUTTONUP:
            mx = GET_X(lp); my = GET_Y(lp);
            buttons[(int)fox::mouse_button::middle] = fox::mouse_button_state::up;
            return 0;

        case WM_RBUTTONDOWN:
            mx = GET_X(lp); my = GET_Y(lp);
            buttons[(int)fox::mouse_button::right] = fox::mouse_button_state::down;
            return 0;
        case WM_RBUTTONUP:
        {
            mx = GET_X(lp); my = GET_Y(lp);
            buttons[(int)fox::mouse_button::right] = fox::mouse_button_state::up;
            return 0;
        }
        case WM_SIZE:
        {
            if (wp == SIZE_MINIMIZED)
                return 0;

            const UINT wd = LOWORD(lp);
            const UINT hd = HIWORD(lp);
            imgui_hook::instance().on_resize(wd, hd);

            if (cursor_clipped)
                clip_cursor_to_client(true);

            return 0;
        }
        case WM_DPICHANGED:
        {
            const UINT dpi = HIWORD(wp);
            imgui_hook::instance().on_dpi_changed(dpi);

            const auto* const prc = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr,
                prc->left, prc->top,
                prc->right - prc->left,
                prc->bottom - prc->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
        }
    }

    void clip_cursor_to_client(bool enabled) noexcept
    {
        cursor_clipped = enabled;
        if (!enabled)
        {
            safe_clip_cursor_release();
            return;
        }

        if (!hwnd) return;

        RECT rect{};
        GetClientRect(hwnd, &rect);

        POINT ul{ rect.left, rect.top };
        POINT lr{ rect.right, rect.bottom };

        MapWindowPoints(hwnd, nullptr, &ul, 1);
        MapWindowPoints(hwnd, nullptr, &lr, 1);

        rect.left   = ul.x;
        rect.top    = ul.y;
        rect.right  = lr.x;
        rect.bottom = lr.y;

        ClipCursor(&rect);
    }

    void hide_cursor(bool hidden) noexcept
    {
        cursor_hidden = hidden;
        show_cursor_set_visible(!hidden);
    }

    void clear_owned_icons() noexcept
    {
        if (icon_small_owned && icon_small) { DestroyIcon(icon_small); }
        if (icon_big_owned   && icon_big)   { DestroyIcon(icon_big); }
        icon_small = icon_big = nullptr;
        icon_small_owned = icon_big_owned = false;
    }

    void apply_icons(const fox::window_icons& icons)
    {
        clear_owned_icons();

        const int sm_cx = GetSystemMetrics(SM_CXSMICON);
        const int sm_cy = GetSystemMetrics(SM_CYSMICON);
        const int lg_cx = GetSystemMetrics(SM_CXICON);
        const int lg_cy = GetSystemMetrics(SM_CYICON);

        HICON _small = nullptr;
        HICON big   = nullptr;

        if (icons.small_icon_res_id) _small = load_icon_from_res(hinst, icons.small_icon_res_id, sm_cx, sm_cy);
        if (icons.big_icon_res_id)   big   = load_icon_from_res(hinst, icons.big_icon_res_id,   lg_cx, lg_cy);

        if (!_small && !icons.small_icon_file.empty())
        {
            _small = load_icon_from_file(icons.small_icon_file, sm_cx, sm_cy);
            icon_small_owned = (_small != nullptr);
        }
        if (!big && !icons.big_icon_file.empty())
        {
            big = load_icon_from_file(icons.big_icon_file, lg_cx, lg_cy);
            icon_big_owned = (big != nullptr);
        }

        icon_small = _small;
        icon_big   = big;

        if (hwnd)
            set_icon_hwnd(hwnd, icon_small, icon_big);
    }

    void shutdown()
    {
        clip_cursor_to_client(false);
        hide_cursor(false);
        clear_owned_icons();

        if (hwnd)
        {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }
};

namespace fox
{
    platform_window::platform_window()
        : p_(std::make_unique<pimpl>())
    {}

    platform_window::~platform_window()
    {
        if (p_) p_->shutdown();
    }

    platform_window::platform_window(platform_window&& o) noexcept
        : p_(std::move(o.p_))
    {}

    platform_window& platform_window::operator=(platform_window&& o) noexcept
    {
        if (this != &o) p_ = std::move(o.p_);
        return *this;
    }

    void platform_window::create(const create_window_params& params) const
    {
        assert(p_);
        auto& s = *p_;

        s.shutdown();

        s.hinst = GetModuleHandleW(nullptr);
        s.fullscreen = params.fullscreen;
        s.resizable  = params.resizable;

        s.title = std::wstring(params.window_name.begin(), params.window_name.end());
        s.class_name = L"FoxRasterizerWindowClass";

        DWORD style = 0;
        if (params.fullscreen)
        {
            style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP;
            s.w = static_cast<std::uint32_t>(GetSystemMetrics(SM_CXSCREEN));
            s.h = static_cast<std::uint32_t>(GetSystemMetrics(SM_CYSCREEN));
        }
        else
        {
            style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
            if (params.resizable)
                style |= WS_MAXIMIZEBOX | WS_SIZEBOX;

            s.w = params.width;
            s.h = params.height;
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = pimpl::WndProc;
        wc.hInstance = s.hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = s.class_name.c_str();

        const ATOM a = RegisterClassExW(&wc);
        assert(a != 0);

        RECT wr{ 0, 0, static_cast<LONG>(s.w), static_cast<LONG>(s.h) };
        AdjustWindowRect(&wr, style, FALSE);

        s.hwnd = CreateWindowExW(
            WS_EX_APPWINDOW,
            s.class_name.c_str(),
            s.title.c_str(),
            style,
            params.window_x,
            params.window_y,
            wr.right - wr.left,
            wr.bottom - wr.top,
            nullptr,
            nullptr,
            s.hinst,
            p_.get());

        assert(s.hwnd);

        ShowWindow(s.hwnd, SW_SHOW);
        SetForegroundWindow(s.hwnd);
        SetFocus(s.hwnd);

        {
            RECT rc{};
            GetClientRect(s.hwnd, &rc);
            s.w = (std::uint32_t)(rc.right - rc.left);
            s.h = (std::uint32_t)(rc.bottom - rc.top);
        }

        s.apply_icons(params.icons);

        std::memset(s.keys, 0, sizeof(s.keys));
        s.mx = s.my = s.wheel = 0;
        s.buttons[0] = s.buttons[1] = s.buttons[2] = mouse_button_state::up;
    }

    void platform_window::destroy()
    {
        assert(p_);
        p_->shutdown();
    }

    void platform_window::poll_messages()
    {
        assert(p_);
        pimpl::pump_messages(*p_);
    }

    void* platform_window::native_hwnd() const noexcept
    {
        return p_ ? (void*)p_->hwnd : nullptr;
    }

    std::uint32_t platform_window::width() const noexcept  { return p_ ? p_->w : 0; }
    std::uint32_t platform_window::height() const noexcept { return p_ ? p_->h : 0; }

    void platform_window::set_window_title(const char* title)
    {
        assert(p_);
        if (!p_->hwnd) return;
        if (!title) title = "";
        const std::wstring w(title, title + std::strlen(title));
        SetWindowTextW(p_->hwnd, w.c_str());
    }

    void platform_window::set_icons(const window_icons& icons)
    {
        assert(p_);
        p_->apply_icons(icons);
    }

    void platform_window::clip_cursor_to_client(bool enabled)
    {
        assert(p_);
        p_->clip_cursor_to_client(enabled);
    }

    void platform_window::hide_cursor(bool hidden)
    {
        assert(p_);
        p_->hide_cursor(hidden);
    }

    bool platform_window::key_down(int vk) const noexcept
    {
        if (!p_) return false;
        const unsigned k = (unsigned)vk & 0xFFu;
        return p_->keys[k];
    }

    void platform_window::set_key_down(int vk, bool down) const noexcept
    {
        if (!p_) return;
        const unsigned k = static_cast<unsigned>(vk) & 0xFFu;
        p_->keys[k] = down;
    }

    bool platform_window::close_requested() const noexcept
    {
        return p_ && p_->close_requested;
    }

    int platform_window::mouse_x() const noexcept { return p_ ? p_->mx : 0; }
    int platform_window::mouse_y() const noexcept { return p_ ? p_->my : 0; }
    int platform_window::mouse_wheel() const noexcept { return p_ ? p_->wheel : 0; }

    void platform_window::reset_mouse_wheel() const noexcept
    {
        if (!p_) return;
        p_->wheel = 0;
    }

    mouse_button_state platform_window::mouse_state(mouse_button b) const noexcept
    {
        if (!p_) return mouse_button_state::up;
        return p_->buttons[(int)b];
    }

    bool platform_window::mouse_pressed(mouse_button b) const noexcept
    {
        if (!p_) return false;
        const auto st = p_->buttons[(int)b];
        return (st == mouse_button_state::down) || (st == mouse_button_state::pressed);
    }
}
