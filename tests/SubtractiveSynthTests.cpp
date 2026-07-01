// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: SubtractiveSynth pitch, amplitude, and release decay tests

#include "dsp/SubtractiveSynth.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using howl::AudioBlock;
using howl::dsp::SubtractiveSynth;

namespace {

// Counts rising zero-crossings across the rendered signal
int countRisingZeroCrossings(const float* samples, int numFrames) {
    int crossings = 0;
    for (int i = 1; i < numFrames; ++i) {
        if (samples[i - 1] <= 0.0f && samples[i] > 0.0f) {
            ++crossings;
        }
    }
    return crossings;
}

} // namespace

TEST_CASE("SubtractiveSynth renders A4 at approximately 440 Hz", "[dsp]") {
    const double sampleRate = 44100.0;
    const int blockSize = 512;
    const int numBlocks = 20;

    SubtractiveSynth synth;
    synth.prepare(sampleRate, blockSize);
    synth.noteOn(69, 1.0f);

    std::vector<float> rendered;
    rendered.reserve(static_cast<std::size_t>(blockSize) * static_cast<std::size_t>(numBlocks));

    float buffer[blockSize] = {};
    float* channels[1] = { buffer };

    for (int i = 0; i < numBlocks; ++i) {
        AudioBlock block { channels, 1, blockSize };
        synth.render(block);
        rendered.insert(rendered.end(), buffer, buffer + blockSize);
    }

    // Skip the attack and decay ramp, measure frequency in the sustain region
    const int skipFrames = 4000;
    const int measureFrames = static_cast<int>(rendered.size()) - skipFrames;
    REQUIRE(measureFrames > 0);

    double sumSquares = 0.0;
    for (int i = skipFrames; i < static_cast<int>(rendered.size()); ++i) {
        sumSquares += static_cast<double>(rendered[static_cast<std::size_t>(i)])
                    * rendered[static_cast<std::size_t>(i)];
    }
    const double rms = std::sqrt(sumSquares / measureFrames);
    REQUIRE(rms > 0.0);

    const int crossings = countRisingZeroCrossings(rendered.data() + skipFrames, measureFrames);
    const double durationSeconds = measureFrames / sampleRate;
    const double estimatedFrequency = crossings / durationSeconds;

    REQUIRE(estimatedFrequency == Catch::Approx(440.0).margin(15.0));
}

TEST_CASE("SubtractiveSynth decays to near-silence after noteOff and the release time", "[dsp]") {
    const double sampleRate = 44100.0;
    const int blockSize = 512;

    SubtractiveSynth synth;
    synth.prepare(sampleRate, blockSize);
    synth.noteOn(69, 1.0f);

    float buffer[blockSize] = {};
    float* channels[1] = { buffer };

    // Render past the attack and decay into sustain
    for (int i = 0; i < 10; ++i) {
        AudioBlock block { channels, 1, blockSize };
        synth.render(block);
    }

    synth.noteOff(69);

    // Render well past the 200 ms release time
    for (int i = 0; i < 30; ++i) {
        AudioBlock block { channels, 1, blockSize };
        synth.render(block);
    }

    double sumSquares = 0.0;
    for (float sample : buffer) {
        sumSquares += static_cast<double>(sample) * sample;
    }
    const double rms = std::sqrt(sumSquares / blockSize);

    REQUIRE(rms < 0.001);
}
