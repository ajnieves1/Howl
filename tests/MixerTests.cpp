// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Mixer summing, mute, and solo tests

#include "model/Mixer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using howl::AudioBlock;
using howl::model::Mixer;
using howl::model::Send;

TEST_CASE("Mixer sums two default track strips into the output", "[mixer]") {
    Mixer mixer;
    mixer.prepare(2, 44100.0, 512, 1);

    float trackASamples[2] = { 1.0f, 2.0f };
    float* trackAChannels[1] = { trackASamples };
    AudioBlock trackA { trackAChannels, 1, 2 };

    float trackBSamples[2] = { 0.5f, 0.5f };
    float* trackBChannels[1] = { trackBSamples };
    AudioBlock trackB { trackBChannels, 1, 2 };

    std::vector<AudioBlock> trackBuffers { trackA, trackB };

    float outputSamples[2] = { 0.0f, 0.0f };
    float* outputChannels[1] = { outputSamples };
    AudioBlock output { outputChannels, 1, 2 };

    mixer.process(trackBuffers, output, 0);

    REQUIRE(outputSamples[0] == Catch::Approx(1.5f));
    REQUIRE(outputSamples[1] == Catch::Approx(2.5f));
}

TEST_CASE("Mixer drops a muted track from the sum", "[mixer]") {
    Mixer mixer;
    mixer.prepare(2, 44100.0, 512, 1);
    mixer.trackStrip(0).setMuted(true);

    float trackASamples[1] = { 1.0f };
    float* trackAChannels[1] = { trackASamples };
    AudioBlock trackA { trackAChannels, 1, 1 };

    float trackBSamples[1] = { 0.5f };
    float* trackBChannels[1] = { trackBSamples };
    AudioBlock trackB { trackBChannels, 1, 1 };

    std::vector<AudioBlock> trackBuffers { trackA, trackB };

    float outputSamples[1] = { 0.0f };
    float* outputChannels[1] = { outputSamples };
    AudioBlock output { outputChannels, 1, 1 };

    mixer.process(trackBuffers, output, 0);

    REQUIRE(outputSamples[0] == Catch::Approx(0.5f));
}

TEST_CASE("Mixer soloing one track drops the other from the sum", "[mixer]") {
    Mixer mixer;
    mixer.prepare(2, 44100.0, 512, 1);
    mixer.trackStrip(0).setSoloed(true);

    float trackASamples[1] = { 1.0f };
    float* trackAChannels[1] = { trackASamples };
    AudioBlock trackA { trackAChannels, 1, 1 };

    float trackBSamples[1] = { 0.5f };
    float* trackBChannels[1] = { trackBSamples };
    AudioBlock trackB { trackBChannels, 1, 1 };

    std::vector<AudioBlock> trackBuffers { trackA, trackB };

    float outputSamples[1] = { 0.0f };
    float* outputChannels[1] = { outputSamples };
    AudioBlock output { outputChannels, 1, 1 };

    mixer.process(trackBuffers, output, 0);

    REQUIRE(outputSamples[0] == Catch::Approx(1.0f));
}

TEST_CASE("Mixer applies the master strip to the summed output", "[mixer]") {
    Mixer mixer;
    mixer.prepare(1, 44100.0, 512, 1);
    mixer.masterStrip().setGainDb(-6.0f);

    float trackSamples[1] = { 1.0f };
    float* trackChannels[1] = { trackSamples };
    AudioBlock track { trackChannels, 1, 1 };

    std::vector<AudioBlock> trackBuffers { track };

    float outputSamples[1] = { 0.0f };
    float* outputChannels[1] = { outputSamples };
    AudioBlock output { outputChannels, 1, 1 };

    mixer.process(trackBuffers, output, 0);

    REQUIRE(outputSamples[0] == Catch::Approx(0.5f).margin(0.01f));
}

TEST_CASE("Mixer a track routed to a muted bus produces silence at master", "[mixer]") {
    Mixer mixer;
    mixer.prepare(1, 44100.0, 512, 1);
    const std::size_t bus = mixer.addBus("Bus A");
    mixer.setTrackOutput(0, bus);
    mixer.busStrip(bus).setMuted(true);

    float trackSamples[1] = { 1.0f };
    float* trackChannels[1] = { trackSamples };
    AudioBlock track { trackChannels, 1, 1 };

    std::vector<AudioBlock> trackBuffers { track };

    float outputSamples[1] = { 1.0f };
    float* outputChannels[1] = { outputSamples };
    AudioBlock output { outputChannels, 1, 1 };

    mixer.process(trackBuffers, output, 0);

    REQUIRE(outputSamples[0] == 0.0f);
}

TEST_CASE("Mixer a post-fader send duplicates the track's post-gain signal in a second bus", "[mixer]") {
    Mixer mixer;
    mixer.prepare(1, 44100.0, 512, 1);
    const std::size_t busA = mixer.addBus("Bus A");
    const std::size_t busB = mixer.addBus("Bus B");
    mixer.setTrackOutput(0, busA);
    mixer.addSend(0, Send { busB, 1.0f, false });

    float trackSamples[1] = { 2.0f };
    float* trackChannels[1] = { trackSamples };
    AudioBlock track { trackChannels, 1, 1 };

    std::vector<AudioBlock> trackBuffers { track };

    float outputSamples[1] = { 0.0f };
    float* outputChannels[1] = { outputSamples };
    AudioBlock output { outputChannels, 1, 1 };

    mixer.process(trackBuffers, output, 0);

    // Both busA (main output) and busB (post-fader send) receive the same
    // post-gain signal, so master sees it duplicated
    REQUIRE(outputSamples[0] == Catch::Approx(4.0f));
}

TEST_CASE("Mixer a pre-fader send ignores the track's gain change", "[mixer]") {
    Mixer mixer;
    mixer.prepare(1, 44100.0, 512, 1);
    const std::size_t bus = mixer.addBus("Bus A");
    mixer.setTrackOutput(0, Mixer::kMaster);
    mixer.addSend(0, Send { bus, 1.0f, true });
    mixer.trackStrip(0).setGainDb(-24.0f);

    float trackSamples[1] = { 1.0f };
    float* trackChannels[1] = { trackSamples };
    AudioBlock track { trackChannels, 1, 1 };

    std::vector<AudioBlock> trackBuffers { track };

    float outputSamples[1] = { 0.0f };
    float* outputChannels[1] = { outputSamples };
    AudioBlock output { outputChannels, 1, 1 };

    mixer.process(trackBuffers, output, 0);

    // Main path applies the -24 dB gain, the pre-fader send does not
    const float expected = std::pow(10.0f, -24.0f / 20.0f) + 1.0f;
    REQUIRE(outputSamples[0] == Catch::Approx(expected).margin(0.01f));
}
