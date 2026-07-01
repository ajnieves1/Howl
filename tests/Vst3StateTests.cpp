// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: Vst3Adapter state save and restore round-trip test

#include "plugins/PluginHost.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <iostream>

using hearth::AudioBlock;
using hearth::plugins::IPluginInstance;
using hearth::plugins::PluginDescriptor;
using hearth::plugins::PluginHost;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;
constexpr int kNumBlocks = 10;

// Feeds a fresh note-on then renders kNumBlocks blocks, returns the RMS of everything rendered
double renderAndMeasureRms(IPluginInstance& instance) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    float* channels[2] = { left, right };
    AudioBlock block { channels, 2, kBlockSize };

    double sumSquares = 0.0;
    int sampleCount = 0;

    for (int i = 0; i < kNumBlocks; ++i) {
        instance.process(block, &midi);
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

TEST_CASE("Vst3Adapter state round-trips, a fresh instance renders the same after loadState", "[plugins][vst3]") {
    PluginHost host;
    host.rescan();
    host.waitForScanToFinish();

    const auto descriptors = host.list();

    const PluginDescriptor* synth = nullptr;
    for (const auto& descriptor : descriptors) {
        if (descriptor.isInstrument) {
            synth = &descriptor;
            break;
        }
    }

    if (synth == nullptr) {
        // Same environment limitation as the P2-T2 test, no VST3 instrument
        // is installed here to actually prove the round trip against
        std::cout << "Hearth: no VST3 instrument found, skipping state round-trip check\n";
        return;
    }

    auto original = host.instantiate(*synth);
    REQUIRE(original != nullptr);
    original->prepare(kSampleRate, kBlockSize);

    // Nudge a few parameters away from their defaults so the state actually differs
    const auto& params = original->params();
    for (std::size_t i = 0; i < params.size() && i < 4; ++i) {
        original->setParamNormalized(params[i].id, 0.75f);
    }

    const double rmsBeforeSave = renderAndMeasureRms(*original);
    const auto state = original->saveState();
    original->release();

    auto restored = host.instantiate(*synth);
    REQUIRE(restored != nullptr);
    restored->prepare(kSampleRate, kBlockSize);
    restored->loadState(state);

    const double rmsAfterLoad = renderAndMeasureRms(*restored);
    restored->release();

    REQUIRE(rmsAfterLoad == Catch::Approx(rmsBeforeSave).margin(0.01));
}
