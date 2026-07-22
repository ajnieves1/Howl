// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: metronome clicks land on beat boundaries and stay silent between them

#include "engine/Metronome.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using howl::AudioBlock;
using howl::engine::Metronome;

namespace {

// Sums the absolute output of the left channel over a window
float energyAround(const std::vector<float>& channel, int start, int count) {
    float energy = 0.0f;
    for (int i = start; i < start + count && i < static_cast<int>(channel.size()); ++i) {
        energy += std::abs(channel[i]);
    }
    return energy;
}

} // namespace

TEST_CASE("Metronome clicks on every beat boundary and is silent between", "[engine]") {
    Metronome metronome;
    metronome.reset();

    const double tempo = 120.0; // 24000 samples per beat at 48 kHz
    const double sampleRate = 48000.0;
    const int blockLength = 48000; // one second, two beats

    std::vector<float> left(static_cast<std::size_t>(blockLength), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(blockLength), 0.0f);
    float* channels[2] = { left.data(), right.data() };
    AudioBlock block { channels, 2, blockLength };

    metronome.process(block, 0, blockLength, tempo, sampleRate);

    // A click fires at the downbeat and at the next beat
    REQUIRE(energyAround(left, 0, 400) > 0.1f);
    REQUIRE(energyAround(left, 24000, 400) > 0.1f);

    // Mid beat, the click has decayed to silence
    REQUIRE(energyAround(left, 12000, 400) < 0.001f);
}

TEST_CASE("Metronome does not click when playback starts mid beat", "[engine]") {
    Metronome metronome;
    metronome.reset();

    const double tempo = 120.0;
    const double sampleRate = 48000.0;
    const int blockLength = 400;

    std::vector<float> left(static_cast<std::size_t>(blockLength), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(blockLength), 0.0f);
    float* channels[2] = { left.data(), right.data() };
    AudioBlock block { channels, 2, blockLength };

    // Start halfway through a beat, no boundary is crossed, so no click
    metronome.process(block, 12000, blockLength, tempo, sampleRate);

    REQUIRE(energyAround(left, 0, blockLength) < 0.001f);
}
