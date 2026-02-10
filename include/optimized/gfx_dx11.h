#ifndef FOXRASTERIZER_GFX_DX11_H
#define FOXRASTERIZER_GFX_DX11_H

#include <cstdint>
#include <memory>

#include "optimized/types.h"


namespace fox
{
    [[nodiscard]] static inline std::uint32_t pack_rgba8(
        std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255u) noexcept
    {
        return (static_cast<std::uint32_t>(a) << 24) |
               (static_cast<std::uint32_t>(b) << 16) |
               (static_cast<std::uint32_t>(g) <<  8) |
               (static_cast<std::uint32_t>(r) <<  0);
    }

    static inline void unpack_rgba8(
        std::uint32_t rgba, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) noexcept
    {
        r = static_cast<std::uint8_t>((rgba >> 0) & 0xFFu);
        g = static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
        b = static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
        a = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
    }

    [[nodiscard]] static inline std::uint8_t rgba8_r(std::uint32_t rgba) noexcept { return static_cast<std::uint8_t>((rgba >> 0) & 0xFFu); }
    [[nodiscard]] static inline std::uint8_t rgba8_g(std::uint32_t rgba) noexcept { return static_cast<std::uint8_t>((rgba >> 8) & 0xFFu); }
    [[nodiscard]] static inline std::uint8_t rgba8_b(std::uint32_t rgba) noexcept { return static_cast<std::uint8_t>((rgba >> 16) & 0xFFu); }
    [[nodiscard]] static inline std::uint8_t rgba8_a(std::uint32_t rgba) noexcept { return static_cast<std::uint8_t>((rgba >> 24) & 0xFFu); }

    struct cpu_frame
    {
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        std::uint32_t  color_pitch_bytes  = 0;
        std::uint32_t  color_pitch_pixels = 0;
        std::uint32_t* color              = nullptr;

        std::uint32_t z_pitch = 0; // elements per row
        float*        z = nullptr;

        std::uint32_t slot = 0;
        std::uint32_t generation = 0;

        [[nodiscard]] bool valid() const noexcept
        {
            return color && z && w && h;
        }
    };

    struct playback_state
    {
        bool          recording = false;
        bool          playing   = false;
        bool          looping   = false;
        std::uint32_t frame_count = 0;
        std::uint32_t cursor      = 0; // next frame index that would be presented
    };

    class gfx_dx11
    {
    public:
        gfx_dx11();
        ~gfx_dx11();

        gfx_dx11(const gfx_dx11&)            = delete;
        gfx_dx11& operator=(const gfx_dx11&) = delete;

        gfx_dx11(gfx_dx11&&) noexcept;
        gfx_dx11& operator=(gfx_dx11&&) noexcept;

        void create(const create_dx11_params& params);

        void destroy();

        cpu_frame begin_frame() noexcept;        // blocking acquire
        cpu_frame try_begin_frame() noexcept;    // non-blocking acquire

        void present(const cpu_frame& frame) noexcept;

        // Wait until any in flight work is drained
        void flush() noexcept;
        void clear_backbuffer_rgba8(std::uint32_t rgba) noexcept;

        [[nodiscard]] std::uint32_t width()  const noexcept;
        [[nodiscard]] std::uint32_t height() const noexcept;
        [[nodiscard]] std::uint32_t pending_frames() const noexcept;

        void record_begin() noexcept;
        void record_submit(const cpu_frame& frame) noexcept;
        void record_end() noexcept;

        [[nodiscard]] std::uint32_t record_frame_count() const noexcept;
        void playback_start(bool loop) noexcept;
        void playback_stop() noexcept;
        bool playback_present_next() noexcept;
        void playback_rewind() noexcept;
        [[nodiscard]] playback_state get_playback_state() const noexcept;

    private:
        class pimpl;
        std::unique_ptr<pimpl> p_;
    };

} // namespace fox

#endif // FOXRASTERIZER_GFX_DX11_H
