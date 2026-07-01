// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: ClapAdapter MIDI-in/audio-out and state round-trip tests

#include "plugins/ClapAdapter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <iostream>
#include <optional>

using hearth::AudioBlock;
using hearth::plugins::ClapAdapter;
using hearth::plugins::ClapPluginInfo;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;
constexpr int kNumBlocks = 20;

// Returns the first CLAP instrument found on this machine, if any
std::optional<ClapPluginInfo> findClapInstrument() {
    for (const auto& info : ClapAdapter::scan()) {
        if (info.isInstrument) {
            return info;
        }
    }
    return std::nullopt;
}

// Feeds a note-on then renders kNumBlocks blocks, returns the RMS of everything rendered
double renderAndMeasureRms(ClapAdapter& adapter) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    float* channels[2] = { left, right };
    AudioBlock block { channels, 2, kBlockSize };

    double sumSquares = 0.0;
    int sampleCount = 0;

    for (int i = 0; i < kNumBlocks; ++i) {
        adapter.process(block, &midi);
        midi.clear();

        for (int frame = 0; frame < kBlockSize; ++frame) {
            sumSquares += static_cast<double>(left[frame]) * left[frame];
            sumSquares += static_cast<double>(right[frame]) * right[frame];
            sampleCount += 2;
        }
    }

    return sampleCount > 0 ? std::sqrt(sumSquares / sampleCount) : 0.0;
}

} // namespace

TEST_CASE("ClapAdapter feeds a MIDI note to a CLAP synth and gets non-silent audio back", "[plugins][clap]") {
    const auto instrument = findClapInstrument();
    if (!instrument.has_value()) {
        // No CLAP instrument installed here, the adapter path is exercised
        // whenever a real synth is present, this environment cannot prove it
        std::cout << "Hearth: no CLAP instrument found, skipping ClapAdapter audio check\n";
        return;
    }

    auto adapter = ClapAdapter::load(*instrument);
    REQUIRE(adapter != nullptr);

    adapter->prepare(kSampleRate, kBlockSize);
    const double rms = renderAndMeasureRms(*adapter);
    adapter->release();

    REQUIRE(rms > 0.0);
}

TEST_CASE("ClapAdapter state round-trips, a fresh instance renders the same after loadState", "[plugins][clap]") {
    const auto instrument = findClapInstrument();
    if (!instrument.has_value()) {
        std::cout << "Hearth: no CLAP instrument found, skipping ClapAdapter state check\n";
        return;
    }

    auto original = ClapAdapter::load(*instrument);
    REQUIRE(original != nullptr);
    original->prepare(kSampleRate, kBlockSize);

    const auto& params = original->params();
    for (std::size_t i = 0; i < params.size() && i < 4; ++i) {
        original->setParamNormalized(params[i].id, 0.75f);
    }

    const double rmsBeforeSave = renderAndMeasureRms(*original);
    const auto state = original->saveState();
    original->release();

    auto restored = ClapAdapter::load(*instrument);
    REQUIRE(restored != nullptr);
    restored->prepare(kSampleRate, kBlockSize);
    restored->loadState(state);

    const double rmsAfterLoad = renderAndMeasureRms(*restored);
    restored->release();

    REQUIRE(rmsAfterLoad == Catch::Approx(rmsBeforeSave).margin(0.01));
}
