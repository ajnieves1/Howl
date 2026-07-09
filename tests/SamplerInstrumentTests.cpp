// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: SamplerInstrument pitch, voice stealing, note off, and level tests

#include "dsp/SamplerInstrument.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using howl::AudioBlock;
using howl::dsp::SamplerInstrument;

namespace {

// Renders one frame at a time and returns the summed output samples, one per frame
std::vector<float> renderFrames(SamplerInstrument& sampler, int numFrames) {
    std::vector<float> output(static_cast<std::size_t>(numFrames), 0.0f);
    for (int i = 0; i < numFrames; ++i) {
        float sample = 0.0f;
        float* channels[1] = { &sample };
        AudioBlock block { channels, 1, 1 };
        sampler.render(block);
        output[static_cast<std::size_t>(i)] = sample;
    }
    return output;
}

} // namespace

TEST_CASE("SamplerInstrument key 60 reproduces the buffer at unity rate", "[dsp]") {
    SamplerInstrument sampler;
    sampler.prepare(44100.0, 512);

    std::vector<std::vector<float>> channels { { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f } };
    sampler.setSample(channels, "test.wav");
    sampler.setParameter(0, 1.0f); // isolate pitch from the default 0.8 level scaling

    sampler.noteOn(60, 1.0f);
    const std::vector<float> output = renderFrames(sampler, 4);

    REQUIRE(output[0] == Catch::Approx(0.2f).margin(0.001));
    REQUIRE(output[1] == Catch::Approx(0.4f).margin(0.001));
    REQUIRE(output[2] == Catch::Approx(0.6f).margin(0.001));
    REQUIRE(output[3] == Catch::Approx(0.8f).margin(0.001));
}

TEST_CASE("SamplerInstrument key 72 finishes in roughly half the samples of key 60", "[dsp]") {
    std::vector<std::vector<float>> channels { std::vector<float>(200, 1.0f) };

    SamplerInstrument unityRate;
    unityRate.prepare(44100.0, 512);
    unityRate.setSample(channels, "test.wav");
    unityRate.setParameter(0, 1.0f);
    unityRate.noteOn(60, 1.0f);
    const std::vector<float> unityOutput = renderFrames(unityRate, 200);

    SamplerInstrument doubleRate;
    doubleRate.prepare(44100.0, 512);
    doubleRate.setSample(channels, "test.wav");
    doubleRate.setParameter(0, 1.0f);
    doubleRate.noteOn(72, 1.0f);
    const std::vector<float> doubleOutput = renderFrames(doubleRate, 200);

    const auto countNonSilent = [](const std::vector<float>& output) {
        std::size_t count = 0;
        for (float sample : output) {
            if (sample != 0.0f) {
                ++count;
            }
        }
        return count;
    };

    const std::size_t unityFrames = countNonSilent(unityOutput);
    const std::size_t doubleFrames = countNonSilent(doubleOutput);

    REQUIRE(doubleFrames < unityFrames);
    const double ratio = static_cast<double>(doubleFrames) / static_cast<double>(unityFrames);
    REQUIRE(ratio == Catch::Approx(0.5).margin(0.05));
}

TEST_CASE("SamplerInstrument a 17th noteOn steals the voice that has played the longest", "[dsp]") {
    SamplerInstrument sampler;
    sampler.prepare(44100.0, 512);

    std::vector<std::vector<float>> channels { std::vector<float>(50, 1.0f) };
    sampler.setSample(channels, "test.wav");
    sampler.setParameter(0, 1.0f);

    // Fill all 16 voices, staggering a render between each so voice 0 is furthest along
    // (has played the longest) and voice 15 is the freshest when the 17th note arrives
    for (int i = 0; i < 16; ++i) {
        sampler.noteOn(60, 0.5f);
        renderFrames(sampler, 1);
    }

    // Sum of 16 active voices at gain 0.5 each
    const std::vector<float> beforeSteal = renderFrames(sampler, 1);
    REQUIRE(beforeSteal[0] == Catch::Approx(16 * 0.5f).margin(0.001));

    // A distinct, larger velocity marks the stolen voice's contribution
    sampler.noteOn(60, 2.0f);
    const std::vector<float> afterSteal = renderFrames(sampler, 1);

    // If voice 0 (oldest) was stolen: 15 voices at 0.5 plus the new one at 2.0
    REQUIRE(afterSteal[0] == Catch::Approx(15 * 0.5f + 2.0f).margin(0.001));
}

TEST_CASE("SamplerInstrument noteOff is a no-op, the voice plays to its own end", "[dsp]") {
    SamplerInstrument sampler;
    sampler.prepare(44100.0, 512);

    std::vector<std::vector<float>> channels { { 1.0f, 1.0f, 1.0f } };
    sampler.setSample(channels, "test.wav");
    sampler.setParameter(0, 1.0f);

    sampler.noteOn(60, 1.0f);
    sampler.noteOff(60);

    const std::vector<float> output = renderFrames(sampler, 2);
    REQUIRE(output[0] == Catch::Approx(1.0f).margin(0.001));
    REQUIRE(output[1] == Catch::Approx(1.0f).margin(0.001));
}

TEST_CASE("SamplerInstrument level scales amplitude linearly", "[dsp]") {
    SamplerInstrument sampler;
    sampler.prepare(44100.0, 512);

    std::vector<std::vector<float>> channels { { 1.0f, 1.0f } };
    sampler.setSample(channels, "test.wav");

    sampler.setParameter(0, 0.5f);
    sampler.noteOn(60, 1.0f);
    const std::vector<float> halfLevel = renderFrames(sampler, 1);

    sampler.setParameter(0, 1.0f);
    sampler.noteOn(60, 1.0f);
    const std::vector<float> fullLevel = renderFrames(sampler, 1);

    REQUIRE(halfLevel[0] == Catch::Approx(0.5f).margin(0.001));
    REQUIRE(fullLevel[0] == Catch::Approx(1.0f).margin(0.001));
    REQUIRE(sampler.getParameter(0) == Catch::Approx(1.0f));
}
