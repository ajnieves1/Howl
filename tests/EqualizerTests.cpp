// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Equalizer flatness, band-boost, band-selectivity, and stability tests

#include "dsp/Equalizer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using howl::AudioBlock;
using howl::dsp::Equalizer;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr float kPi = 3.14159265358979323846f;

// Generates numFrames samples of a full-scale sine at freqHz
std::vector<float> generateSine(float freqHz, int numFrames) {
    std::vector<float> samples(static_cast<std::size_t>(numFrames));

    for (int frame = 0; frame < numFrames; ++frame) {
        samples[static_cast<std::size_t>(frame)] =
            std::sin(2.0f * kPi * freqHz * static_cast<float>(frame) / static_cast<float>(kSampleRate));
    }

    return samples;
}

// Computes the root-mean-square of a buffer
float rms(const std::vector<float>& samples) {
    double sumSquares = 0.0;

    for (float sample : samples) {
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(samples.size())));
}

// Converts a real-unit frequency to the normalized 0..1 value the exponential map expects
float normalizedExponential(float x, float min, float max) {
    return std::log(x / min) / std::log(max / min);
}

// Converts a real-unit dB gain to the normalized 0..1 value the linear map expects
float normalizedLinear(float x, float min, float max) {
    return (x - min) / (max - min);
}

} // namespace

TEST_CASE("Equalizer at flat defaults is a near-passthrough on a 1 kHz sine", "[equalizer]") {
    std::vector<float> input = generateSine(1000.0f, 512);
    std::vector<float> output = input;
    float* channels[1] = { output.data() };
    AudioBlock block { channels, 1, 512 };

    Equalizer eq;
    eq.prepare(kSampleRate, 512);
    eq.process(block);

    for (std::size_t i = 0; i < input.size(); ++i) {
        REQUIRE(output[i] == Catch::Approx(input[i]).margin(1e-4));
    }
}

TEST_CASE("Equalizer peak boost raises RMS and peak cut lowers it at the band frequency", "[equalizer]") {
    std::vector<float> input = generateSine(1000.0f, 2048);

    std::vector<float> flatOutput = input;
    float* flatChannels[1] = { flatOutput.data() };
    AudioBlock flatBlock { flatChannels, 1, 2048 };
    Equalizer flatEq;
    flatEq.prepare(kSampleRate, 2048);
    flatEq.process(flatBlock);
    const float flatRms = rms(flatOutput);

    std::vector<float> boostOutput = input;
    float* boostChannels[1] = { boostOutput.data() };
    AudioBlock boostBlock { boostChannels, 1, 2048 };
    Equalizer boostEq;
    boostEq.prepare(kSampleRate, 2048);
    boostEq.setParameter(Equalizer::kPeakFreq, normalizedExponential(1000.0f, 20.0f, 20000.0f));
    boostEq.setParameter(Equalizer::kPeakGain, normalizedLinear(24.0f, -24.0f, 24.0f));
    boostEq.process(boostBlock);
    const float boostRms = rms(boostOutput);

    std::vector<float> cutOutput = input;
    float* cutChannels[1] = { cutOutput.data() };
    AudioBlock cutBlock { cutChannels, 1, 2048 };
    Equalizer cutEq;
    cutEq.prepare(kSampleRate, 2048);
    cutEq.setParameter(Equalizer::kPeakFreq, normalizedExponential(1000.0f, 20.0f, 20000.0f));
    cutEq.setParameter(Equalizer::kPeakGain, normalizedLinear(-24.0f, -24.0f, 24.0f));
    cutEq.process(cutBlock);
    const float cutRms = rms(cutOutput);

    REQUIRE(boostRms > flatRms);
    REQUIRE(cutRms < flatRms);
}

TEST_CASE("Equalizer peak band is selective, a narrow boost barely affects a distant frequency", "[equalizer]") {
    std::vector<float> input = generateSine(100.0f, 2048);

    std::vector<float> flatOutput = input;
    float* flatChannels[1] = { flatOutput.data() };
    AudioBlock flatBlock { flatChannels, 1, 2048 };
    Equalizer flatEq;
    flatEq.prepare(kSampleRate, 2048);
    flatEq.process(flatBlock);
    const float flatRms = rms(flatOutput);

    std::vector<float> boostOutput = input;
    float* boostChannels[1] = { boostOutput.data() };
    AudioBlock boostBlock { boostChannels, 1, 2048 };
    Equalizer boostEq;
    boostEq.prepare(kSampleRate, 2048);
    boostEq.setParameter(Equalizer::kPeakFreq, normalizedExponential(1000.0f, 20.0f, 20000.0f));
    boostEq.setParameter(Equalizer::kPeakGain, normalizedLinear(24.0f, -24.0f, 24.0f));
    boostEq.setParameter(Equalizer::kPeakQ, normalizedExponential(10.0f, 0.3f, 10.0f));
    boostEq.process(boostBlock);
    const float boostRms = rms(boostOutput);

    REQUIRE(boostRms == Catch::Approx(flatRms).epsilon(0.10));
}

TEST_CASE("Equalizer stays finite over a long block of boosted white noise", "[equalizer]") {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    Equalizer eq;
    eq.prepare(kSampleRate, 512);
    eq.setParameter(Equalizer::kLowGain, normalizedLinear(24.0f, -24.0f, 24.0f));
    eq.setParameter(Equalizer::kPeakGain, normalizedLinear(24.0f, -24.0f, 24.0f));
    eq.setParameter(Equalizer::kHighGain, normalizedLinear(24.0f, -24.0f, 24.0f));

    const int totalFrames = static_cast<int>(kSampleRate) * 10;
    const int blockSize = 512;
    std::vector<float> block(static_cast<std::size_t>(blockSize));

    for (int processed = 0; processed < totalFrames; processed += blockSize) {
        const int frames = std::min(blockSize, totalFrames - processed);

        for (int frame = 0; frame < frames; ++frame) {
            block[static_cast<std::size_t>(frame)] = dist(rng);
        }

        float* channels[1] = { block.data() };
        AudioBlock audioBlock { channels, 1, frames };
        eq.process(audioBlock);

        for (int frame = 0; frame < frames; ++frame) {
            REQUIRE(std::isfinite(block[static_cast<std::size_t>(frame)]));
        }
    }
}
