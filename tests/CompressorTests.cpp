// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Compressor unity-below-threshold and gain-reduction tests

#include "dsp/Compressor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using howl::AudioBlock;
using howl::dsp::Compressor;

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

TEST_CASE("Compressor passes a signal below threshold at exact unity gain", "[compressor]") {
    std::vector<float> samples = generateSine(0.1f, 4096);
    std::vector<float> input = samples;
    float* channels[1] = { samples.data() };
    AudioBlock block { channels, 1, 4096 };

    Compressor compressor;
    compressor.prepare(kSampleRate, 512);
    compressor.setParameter(Compressor::kThreshold, normalizedLinear(-10.0f, -60.0f, 0.0f));
    compressor.setParameter(Compressor::kMakeup, normalizedLinear(0.0f, 0.0f, 24.0f));
    compressor.process(block);

    for (std::size_t i = 0; i < samples.size(); ++i) {
        REQUIRE(samples[i] == Catch::Approx(input[i]).margin(1e-6));
    }
}

TEST_CASE("Compressor reduces a full-scale signal above threshold by the expected amount", "[compressor]") {
    std::vector<float> samples = generateSine(1.0f, 8192);
    float* channels[1] = { samples.data() };
    AudioBlock block { channels, 1, 8192 };

    Compressor compressor;
    compressor.prepare(kSampleRate, 512);
    compressor.setParameter(Compressor::kThreshold, normalizedLinear(-20.0f, -60.0f, 0.0f));
    compressor.setParameter(Compressor::kRatio, std::log(4.0f / 1.0f) / std::log(20.0f / 1.0f));
    compressor.process(block);

    float peak = 0.0f;
    for (std::size_t i = samples.size() - 1024; i < samples.size(); ++i) {
        peak = std::max(peak, std::abs(samples[i]));
    }

    REQUIRE(peak == Catch::Approx(0.178f).margin(0.05f));
}
