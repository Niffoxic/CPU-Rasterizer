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
#ifndef FOXRASTERIZER_RENDERER_H
#define FOXRASTERIZER_RENDERER_H

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdint>
#include "base.h"
#include "fmath.h"
#include "zbuffer.h"

class Renderer {
    float fov = 90.0f * M_PI / 180.0f; // Field of view in radians (converted from degrees)
    float aspect = 4.0f / 3.0f;        // Aspect ratio of the canvas (width/height)
    float n = 0.1f;                    // Near clipping plane distance
    float f = 100.0f;                  // Far clipping plane distance
public:
    Zbuffer<float> zbuffer;                  // Z-buffer for depth management
    GamesEngineeringBase::Window canvas;     // Canvas for rendering the scene
    matrix perspective;                      // Perspective projection matrix

    // Constructor initializes the canvas, Z-buffer, and perspective projection matrix.
    Renderer() {
        canvas.create(1024, 768, "Raster");  // Create a canvas with specified dimensions and title
        zbuffer.create(1024, 768);           // Initialize the Z-buffer with the same dimensions
        perspective = matrix::makePerspective(fov, aspect, n, f); // Set up the perspective matrix
    }

    // Clears the canvas and resets the Z-buffer.
    void clear() {
        canvas.clear();  // Clear the canvas (sets all pixels to the background color)
        zbuffer.clear(); // Reset the Z-buffer to the farthest depth
    }

    // Presents the current canvas frame to the display.
    void present() {
        canvas.present(); // Display the rendered frame
    }
};

#endif // FOXRASTERIZER_RENDERER_H
