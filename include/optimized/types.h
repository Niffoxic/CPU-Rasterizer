//
// Created by Niffoxic (Aka Harsh Dubey) on 1/31/2026.
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
#ifndef FOXRASTERIZER_FOX_PLATFORM_TYPES_H
#define FOXRASTERIZER_FOX_PLATFORM_TYPES_H

#include <cstdint>
#include <string>
#include "fmath.h"

#pragma once

#if TRACER
	#include <tracy/Tracy.hpp>
	#define FOX_ZONE() ZoneScoped
	#define FOX_ZONE_N(name) ZoneScopedN(name)
	#define FOX_FRAME() FrameMark
#else
	#define FOX_ZONE()
	#define FOX_ZONE_N(name)
	#define FOX_FRAME()
#endif

namespace fox
{
	enum class mouse_button : std::uint8_t
	{
		left   = 0,
		middle = 1,
		right  = 2,
	};

	enum class mouse_button_state : std::uint8_t
	{
		up       = 0,
		down     = 1,
		pressed  = 2,
	};

	// Icon configuration
	struct window_icons
	{
		std::uint16_t small_icon_res_id = 0;
		std::uint16_t big_icon_res_id   = 0;

		std::wstring small_icon_file;
		std::wstring big_icon_file;
	};

	struct create_window_params
	{
		std::uint32_t width  = 1280;
		std::uint32_t height = 720;

		int window_x = 100;
		int window_y = 100;

		bool fullscreen = false;
		bool resizable  = true;

		std::string window_name = "FoxRasterizer";

		window_icons icons{};
	};

	struct create_dx11_params
	{
		void*		  hwnd{ nullptr };
		std::uint32_t width = 1280;
		std::uint32_t height = 1080;
		std::uint32_t ring_size = 16;
	};
} // namespace fox

#endif //FOXRASTERIZER_FOX_PLATFORM_TYPES_H
