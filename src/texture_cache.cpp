#include "texture_cache.h"

#include <cstdio>
#include <cstring>
#include <cmath>

#include <windows.h>
#include <wincodec.h>
#include <combaseapi.h>
#include <shlwapi.h>

namespace
{
    struct com_init_guard
    {
        com_init_guard()  { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
        ~com_init_guard() { CoUninitialize(); }
    };

    bool decode_wic(IWICImagingFactory* factory,
                    IWICBitmapSource* source,
                    TextureRGBA8& out)
    {
        UINT w = 0, h = 0;
        if (FAILED(source->GetSize(&w, &h)) || w == 0 || h == 0)
            return false;

        IWICFormatConverter* converter = nullptr;
        if (FAILED(factory->CreateFormatConverter(&converter)))
            return false;

        HRESULT hr = converter->Initialize(
            source,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);

        if (FAILED(hr))
        {
            converter->Release();
            return false;
        }

        auto* pixels = new std::uint32_t[(std::size_t)w * (std::size_t)h];
        hr = converter->CopyPixels(
            nullptr,
            w * 4u,
            w * h * 4u,
            reinterpret_cast<BYTE*>(pixels));

        converter->Release();

        if (FAILED(hr))
        {
            delete[] pixels;
            return false;
        }

        out.pixels = pixels;
        out.width  = w;
        out.height = h;
        out.owned  = true;
        return true;
    }
}

texture_cache::texture_cache()
{
    generate_checkerboard();
}

texture_cache::~texture_cache() = default;

void texture_cache::generate_checkerboard() noexcept
{
    constexpr std::uint32_t W = 64;
    constexpr std::uint32_t H = 64;
    constexpr std::uint32_t CHECK = 8;

    auto* pixels = new std::uint32_t[W * H];

    constexpr std::uint32_t C0 = 0xFF00FF00 | 0xFF;
    constexpr std::uint32_t MAGENTA = (255u << 24) | (255u << 16) | (0u << 8) | 255u; // A=FF B=FF G=00 R=FF
    constexpr std::uint32_t DARK    = (255u << 24) | (40u << 16) | (40u << 8) | 40u;  // A=FF B=28 G=28 R=28

    for (std::uint32_t y = 0; y < H; ++y)
    {
        for (std::uint32_t x = 0; x < W; ++x)
        {
            const bool checker = ((x / CHECK) + (y / CHECK)) & 1;
            pixels[y * W + x] = checker ? MAGENTA : DARK;
        }
    }

    checkerboard_.pixels = pixels;
    checkerboard_.width  = W;
    checkerboard_.height = H;
    checkerboard_.owned  = true;
}

const TextureRGBA8* texture_cache::load_file(const std::string& path)
{
    if (path.empty()) return nullptr;

    auto it = cache_.find(path);
    if (it != cache_.end())
        return it->second.get();

    com_init_guard com;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));

    if (FAILED(hr) || !factory)
    {
        std::printf("texture_cache: failed to create WIC factory\n");
        return nullptr;
    }

    // Convert path to wide string
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(
        wpath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder);

    if (FAILED(hr) || !decoder)
    {
        factory->Release();
        std::printf("texture_cache: failed to decode file %s\n", path.c_str());
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);

    if (FAILED(hr) || !frame)
    {
        decoder->Release();
        factory->Release();
        return nullptr;
    }

    auto tex = std::make_unique<TextureRGBA8>();
    const bool ok = decode_wic(factory, frame, *tex);

    frame->Release();
    decoder->Release();
    factory->Release();

    if (!ok)
    {
        std::printf("texture_cache: failed to convert %s\n", path.c_str());
        return nullptr;
    }

    std::printf("texture_cache: loaded %s (%ux%u)\n",
                path.c_str(), tex->width, tex->height);

    const TextureRGBA8* ptr = tex.get();
    cache_.emplace(path, std::move(tex));
    return ptr;
}

const TextureRGBA8* texture_cache::load_memory(
    const std::string& key,
    const void* data,
    std::size_t size)
{
    if (!data || size == 0) return nullptr;

    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second.get();

    com_init_guard com;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));

    if (FAILED(hr) || !factory) return nullptr;

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream)
    {
        factory->Release();
        return nullptr;
    }

    hr = stream->InitializeFromMemory(
        static_cast<BYTE*>(const_cast<void*>(data)),
        static_cast<DWORD>(size));

    if (FAILED(hr))
    {
        stream->Release();
        factory->Release();
        return nullptr;
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(
        stream,
        nullptr,
        WICDecodeMetadataCacheOnDemand,
        &decoder);

    if (FAILED(hr) || !decoder)
    {
        stream->Release();
        factory->Release();
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);

    if (FAILED(hr) || !frame)
    {
        decoder->Release();
        stream->Release();
        factory->Release();
        return nullptr;
    }

    auto tex = std::make_unique<TextureRGBA8>();
    const bool ok = decode_wic(factory, frame, *tex);

    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();

    if (!ok) return nullptr;

    std::printf("texture_cache: loaded from memory key=%s (%ux%u)\n",
                key.c_str(), tex->width, tex->height);

    const TextureRGBA8* ptr = tex.get();
    cache_.emplace(key, std::move(tex));
    return ptr;
}

const TextureRGBA8* texture_cache::get(const std::string& key) const
{
    auto it = cache_.find(key);
    return (it != cache_.end()) ? it->second.get() : nullptr;
}
