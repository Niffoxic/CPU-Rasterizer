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
#ifndef FOXRASTERIZER_FGEN_H
#define FOXRASTERIZER_FGEN_H

#include <random>

class RandomNumberGenerator {
public:
	// Delete copy constructor and assignment operator
	RandomNumberGenerator(const RandomNumberGenerator&) = delete;
	RandomNumberGenerator& operator=(const RandomNumberGenerator&) = delete;

	// Get the singleton instance
	static RandomNumberGenerator& getInstance() {
		static RandomNumberGenerator instance;
		return instance;
	}

	// Generate a random integer within a range
	int getRandomInt(int min, int max) {
		std::uniform_int_distribution<int> distribution(min, max);
		return distribution(rng);
	}

	// Generate a random integer within a range
	float getRandomFloat(float min, float max) {
		std::uniform_real_distribution<float> distribution(min, max);
		return distribution(rng);
	}

private:
	// Private constructor for Singleton
	RandomNumberGenerator() : rng(std::random_device{}()) {}

	// Mersenne Twister random number generator
	std::mt19937 rng;
};

#endif //FOXRASTERIZER_FGEN_H