// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Limiter ceiling-clamp and below-ceiling unity tests

#include "dsp/Limiter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using howl::AudioBlock;
using howl::dsp::Limiter;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr float kPi = 3.14159265358979323846f;

// Generates numFrames samples of a 440 Hz sine at the given amplitude
std::vector<float> generateSine(float amplitude, int numFrames) {
    std::vector<float> samples(static_cast<std::size_t>(numFrames));

    for (int frame = 0; frame < numFrames; ++frame) {
        samples[static_cast<std::size_t>(frame)] =
            amplitude * std::sin(2.0f * kPi * 440.0f * static_cast<float>(frame) / static_cast<float>(kSampleRate));
    }

    return samples;
}

// Converts a real-unit value to the normalized 0..1 value the linear map expects
float normalizedLinear(float x, float min, float max) {
    return (x - min) / (max - min);
}

} // namespace

TEST_CASE("Limiter never exceeds its ceiling on a signal that overshoots it", "[limiter]") {
    std::vector<float> samples = generateSine(2.0f, 4096);
    float* channels[1] = { samples.data() };
    AudioBlock block { channels, 1, 4096 };

    Limiter limiter;
    limiter.prepare(kSampleRate, 512);
    limiter.setParameter(Limiter::kCeiling, normalizedLinear(-6.0f, -24.0f, 0.0f));
    limiter.process(block);

    const float ceilingLinear = std::pow(10.0f, -6.0f / 20.0f);

    for (float sample : samples) {
        REQUIRE(std::abs(sample) <= ceilingLinear + 1e-6f);
    }
}

TEST_CASE("Limiter passes a signal far below the ceiling at unity", "[limiter]") {
    std::vector<float> samples = generateSine(0.1f, 4096);
    std::vector<float> input = samples;
    float* channels[1] = { samples.data() };
    AudioBlock block { channels, 1, 4096 };

    Limiter limiter;
    limiter.prepare(kSampleRate, 512);
    limiter.process(block);

    for (std::size_t i = 0; i < samples.size(); ++i) {
        REQUIRE(samples[i] == Catch::Approx(input[i]).margin(1e-6));
    }
}
