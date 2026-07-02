// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Reverb tail, decay, stability, and reset tests

#include "dsp/Reverb.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using howl::AudioBlock;
using howl::dsp::Reverb;

namespace {

constexpr double kSampleRate = 44100.0;

// Computes the root-mean-square of a range of a buffer
float rms(const std::vector<float>& samples, std::size_t begin, std::size_t end) {
    double sumSquares = 0.0;

    for (std::size_t i = begin; i < end; ++i) {
        sumSquares += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(end - begin)));
}

} // namespace

TEST_CASE("Reverb produces a decaying tail from an impulse", "[reverb]") {
    constexpr int kNumFrames = 88200;

    std::vector<float> samples(static_cast<std::size_t>(kNumFrames), 0.0f);
    samples[0] = 1.0f;
    float* channels[1] = { samples.data() };
    AudioBlock block { channels, 1, kNumFrames };

    Reverb reverb;
    reverb.prepare(kSampleRate, 512);
    reverb.process(block);

    bool tailFound = false;
    for (int frame = 1; frame < 4410; ++frame) {
        if (samples[static_cast<std::size_t>(frame)] != 0.0f) {
            tailFound = true;
            break;
        }
    }
    REQUIRE(tailFound);

    const float rmsFirstSecond = rms(samples, 0, 44100);
    const float rmsSecondSecond = rms(samples, 44100, 88200);
    REQUIRE(rmsSecondSecond < rmsFirstSecond);
}

TEST_CASE("Reverb stays finite over 10 seconds after an impulse", "[reverb]") {
    constexpr int kTotalFrames = static_cast<int>(kSampleRate) * 10;
    constexpr int kBlockSize = 512;

    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize);

    std::vector<float> block(static_cast<std::size_t>(kBlockSize), 0.0f);
    block[0] = 1.0f;

    for (int processed = 0; processed < kTotalFrames; processed += kBlockSize) {
        const int frames = std::min(kBlockSize, kTotalFrames - processed);
        float* channels[1] = { block.data() };
        AudioBlock audioBlock { channels, 1, frames };
        reverb.process(audioBlock);

        for (int frame = 0; frame < frames; ++frame) {
            REQUIRE(std::isfinite(block[static_cast<std::size_t>(frame)]));
        }

        std::fill(block.begin(), block.end(), 0.0f);
    }
}

TEST_CASE("Reverb.reset silences the tail", "[reverb]") {
    constexpr int kNumFrames = 8820;

    std::vector<float> samples(static_cast<std::size_t>(kNumFrames), 0.0f);
    samples[0] = 1.0f;
    float* channels[1] = { samples.data() };
    AudioBlock block { channels, 1, kNumFrames };

    Reverb reverb;
    reverb.prepare(kSampleRate, 512);
    reverb.process(block);

    reverb.reset();

    std::vector<float> silence(static_cast<std::size_t>(kNumFrames), 0.0f);
    float* silenceChannels[1] = { silence.data() };
    AudioBlock silenceBlock { silenceChannels, 1, kNumFrames };
    reverb.process(silenceBlock);

    for (float sample : silence) {
        REQUIRE(sample == 0.0f);
    }
}
