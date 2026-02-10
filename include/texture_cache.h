#ifndef FOXRASTERIZER_TEXTURE_CACHE_H
#define FOXRASTERIZER_TEXTURE_CACHE_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct TextureRGBA8
{
    std::uint32_t* pixels = nullptr;
    std::uint32_t  width  = 0;
    std::uint32_t  height = 0;
    bool           owned  = false;

    TextureRGBA8() = default;
    ~TextureRGBA8() noexcept { destroy(); }

    TextureRGBA8(const TextureRGBA8&) = delete;
    TextureRGBA8& operator=(const TextureRGBA8&) = delete;

    TextureRGBA8(TextureRGBA8&& o) noexcept
        : pixels(o.pixels), width(o.width), height(o.height), owned(o.owned)
    {
        o.pixels = nullptr; o.width = 0; o.height = 0; o.owned = false;
    }

    TextureRGBA8& operator=(TextureRGBA8&& o) noexcept
    {
        if (this != &o)
        {
            destroy();
            pixels = o.pixels; width = o.width; height = o.height; owned = o.owned;
            o.pixels = nullptr; o.width = 0; o.height = 0; o.owned = false;
        }
        return *this;
    }

    void destroy() noexcept
    {
        if (owned && pixels) delete[] pixels;
        pixels = nullptr; width = 0; height = 0; owned = false;
    }

    [[nodiscard]] bool valid() const noexcept { return pixels && width > 0 && height > 0; }

    [[nodiscard]] std::uint32_t sample_nearest(float u, float v) const noexcept
    {
        u = u - std::floor(u);
        v = v - std::floor(v);
        if (u < 0.f) u += 1.f;
        if (v < 0.f) v += 1.f;

        const std::uint32_t tx = static_cast<std::uint32_t>(u * (float)width ) % width;
        const std::uint32_t ty = static_cast<std::uint32_t>(v * (float)height) % height;
        return pixels[(std::size_t)ty * (std::size_t)width + (std::size_t)tx];
    }
};

class texture_cache
{
public:
    texture_cache();
    ~texture_cache();

    texture_cache(const texture_cache&) = delete;
    texture_cache& operator=(const texture_cache&) = delete;

    const TextureRGBA8* load_file(const std::string& path);
    const TextureRGBA8* load_memory(const std::string& key,
                                     const void* data,
                                     std::size_t size);

    const TextureRGBA8* get(const std::string& key) const;
    const TextureRGBA8* checkerboard() const noexcept { return &checkerboard_; }
    [[nodiscard]] std::size_t size() const noexcept { return cache_.size(); }

private:
    void generate_checkerboard() noexcept;

    std::unordered_map<std::string, std::unique_ptr<TextureRGBA8>> cache_;
    TextureRGBA8 checkerboard_;
};

#endif // FOXRASTERIZER_TEXTURE_CACHE_H
