//
// Created by Niffoxic (Aka Harsh Dubey) on 1/30/2026.
//
// -----------------------------------------------------------------------------
// Project   : FoxRasterizer
// Purpose   : Academic and self-learning computer graphics project.
// Codebase  : CPU software rasterizer implementation in modern C++,
//             focused on the fundamentals of the rendering pipeline
//             (transforms, clipping, triangle setup, rasterization,
//             depth/stencil, shading, and optimization experiments).
// License   : MIT License
// -----------------------------------------------------------------------------
//
// Copyright (c) 2026 Niffoxic
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// -----------------------------------------------------------------------------
#ifndef FOXRASTERIZER_ZBUFFER_H
#define FOXRASTERIZER_ZBUFFER_H

#include <concepts>
#include <cassert>
#include <algorithm>
#include <cstddef>
#include <utility>

template<std::floating_point T>
class Zbuffer {
    T* buffer = nullptr;
    unsigned int width = 0, height = 0;

public:
    Zbuffer() = default;

    Zbuffer(unsigned int w, unsigned int h) {
        create(w, h);
    }

    // Non-copyable (prevents double-free).
    Zbuffer(const Zbuffer&) = delete;
    Zbuffer& operator=(const Zbuffer&) = delete;

    // Move constructor.
    Zbuffer(Zbuffer&& other) noexcept
        : buffer(std::exchange(other.buffer, nullptr))
        , width(std::exchange(other.width, 0))
        , height(std::exchange(other.height, 0)) {}

    // Move assignment.
    Zbuffer& operator=(Zbuffer&& other) noexcept {
        if (this != &other) {
            delete[] buffer;
            buffer = std::exchange(other.buffer, nullptr);
            width  = std::exchange(other.width, 0);
            height = std::exchange(other.height, 0);
        }
        return *this;
    }

    // Destructor.
    ~Zbuffer() {
        delete[] buffer;
        buffer = nullptr;
        width = height = 0;
    }

    // Allocate (or reallocate) buffer.
    // Note: delete[] nullptr is safe, so no need for if(buffer!=nullptr).
    void create(unsigned int w, unsigned int h) {
        width = w;
        height = h;

        // If someone passes 0, 0 we just free and keep empty.
        if (width == 0 || height == 0) {
            delete[] buffer;
            buffer = nullptr;
            return;
        }

        delete[] buffer;
        buffer = new T[static_cast<std::size_t>(width) * static_cast<std::size_t>(height)];
    }

    // Alias, if you like "resize" naming.
    void resize(unsigned int w, unsigned int h) {
        create(w, h);
    }

    // Dimensions.
    unsigned int getWidth()  const { return width; }
    unsigned int getHeight() const { return height; }

    // Raw accessors (useful for fast clears / debug dumps / SIMD later).
    T* data() { return buffer; }
    const T* data() const { return buffer; }

    std::size_t size() const {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }

    bool empty() const { return buffer == nullptr || width == 0 || height == 0; }

    // Indexing (x,y). Debug asserts to catch raster out-of-bounds early.
    T& operator()(unsigned int x, unsigned int y) {
        assert(buffer != nullptr);
        assert(x < width && y < height);
        return buffer[static_cast<std::size_t>(y) * width + x];
    }

    const T& operator()(unsigned int x, unsigned int y) const {
        assert(buffer != nullptr);
        assert(x < width && y < height);
        return buffer[static_cast<std::size_t>(y) * width + x];
    }

    // Clear all depths to "far" (default 1.0).
    void clear(T farDepth = T(1.0)) {
        if (empty()) return;
        std::fill_n(buffer, size(), farDepth);
    }
};

#endif // FOXRASTERIZER_ZBUFFER_H
