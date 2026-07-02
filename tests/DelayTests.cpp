// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Delay impulse-response and reset tests

#include "dsp/Delay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using howl::AudioBlock;
using howl::dsp::Delay;

TEST_CASE("Delay taps a wet impulse at the expected sample offset and decays by feedback", "[delay]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int kNumFrames = 24000;
    constexpr int kDelaySamples = 11025; // 250 ms default at 44.1 kHz

    std::vector<float> samples(static_cast<std::size_t>(kNumFrames), 0.0f);
    samples[0] = 1.0f;
    float* channels[1] = { samples.data() };
    AudioBlock block { channels, 1, kNumFrames };

    Delay delay;
    delay.prepare(kSampleRate, 512);
    delay.process(block);

    REQUIRE(samples[0] == Catch::Approx(0.5f).margin(1e-4));
    REQUIRE(samples[kDelaySamples] == Catch::Approx(0.5f).margin(1e-4));
    REQUIRE(samples[2 * kDelaySamples] == Catch::Approx(0.175f).margin(1e-4));

    REQUIRE(samples[1] == Catch::Approx(0.0f).margin(1e-4));
    REQUIRE(samples[kDelaySamples - 1] == Catch::Approx(0.0f).margin(1e-4));
    REQUIRE(samples[kDelaySamples + 1] == Catch::Approx(0.0f).margin(1e-4));
}

TEST_CASE("Delay.reset silences the tail", "[delay]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int kNumFrames = 24000;

    std::vector<float> samples(static_cast<std::size_t>(kNumFrames), 0.0f);
    samples[0] = 1.0f;
    float* channels[1] = { samples.data() };
    AudioBlock block { channels, 1, kNumFrames };

    Delay delay;
    delay.prepare(kSampleRate, 512);
    delay.process(block);

    delay.reset();

    std::vector<float> silence(static_cast<std::size_t>(kNumFrames), 0.0f);
    float* silenceChannels[1] = { silence.data() };
    AudioBlock silenceBlock { silenceChannels, 1, kNumFrames };
    delay.process(silenceBlock);

    for (float sample : silence) {
        REQUIRE(sample == 0.0f);
    }
}
