// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: SequencerNode timing and audio-through-instrument tests

#include "dsp/SubtractiveSynth.h"
#include "engine/Transport.h"
#include "model/MidiClip.h"
#include "model/SequencerNode.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using hearth::AudioBlock;
using hearth::SampleCount;
using hearth::dsp::SubtractiveSynth;
using hearth::engine::Transport;
using hearth::model::kTicksPerQuarter;
using hearth::model::MidiClip;
using hearth::model::Note;
using hearth::model::SequencerNode;

namespace {

// Root-mean-square of a flat sample buffer
double rmsOf(const float* samples, int numFrames) {
    double sumSquares = 0.0;
    for (int i = 0; i < numFrames; ++i) {
        sumSquares += static_cast<double>(samples[i]) * samples[i];
    }
    return std::sqrt(sumSquares / numFrames);
}

} // namespace

TEST_CASE("SequencerNode plays a note then goes silent after it ends", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 512;

    MidiClip clip;
    clip.addNote(Note { 69, 1.0f, 0, kTicksPerQuarter }); // A4, one beat long
    clip.setLengthTicks(kTicksPerQuarter);

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    SubtractiveSynth synth;
    synth.prepare(sampleRate, blockSize);

    SequencerNode sequencer(transport, clip, synth);
    sequencer.prepare(sampleRate);

    float buffer[blockSize] = {};
    float* channels[1] = { buffer };

    bool sawNonSilenceDuringNote = false;
    const int numBlocks = 70; // covers the one-beat note (22050 samples) plus release

    for (int i = 0; i < numBlocks; ++i) {
        const SampleCount pos = transport.advance(blockSize);
        AudioBlock block { channels, 1, blockSize };
        sequencer.process(block, pos);

        // Well inside the note's sounding duration, past the attack and decay
        if (pos > 5000 && pos < 15000) {
            if (rmsOf(buffer, blockSize) > 0.01) {
                sawNonSilenceDuringNote = true;
            }
        }
    }

    REQUIRE(sawNonSilenceDuringNote);

    // The last rendered block is well after the note ends and the release finishes
    REQUIRE(rmsOf(buffer, blockSize) < 0.001);
}

TEST_CASE("SequencerNode fires noteOn at the correct sample offset within a block", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 1024;

    MidiClip clip;
    clip.addNote(Note { 69, 1.0f, 13, kTicksPerQuarter }); // starts a few ticks in, not at tick 0

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    SubtractiveSynth synth;
    synth.prepare(sampleRate, blockSize);

    SequencerNode sequencer(transport, clip, synth);
    sequencer.prepare(sampleRate);

    float buffer[blockSize] = {};
    float* channels[1] = { buffer };

    const SampleCount pos = transport.advance(blockSize);
    REQUIRE(pos == 0);

    AudioBlock block { channels, 1, blockSize };
    sequencer.process(block, pos);

    // samplesPerTick at 120 bpm, 44100 Hz is 22.96875, so tick 13 lands at sample 298
    const int expectedOffset = 298;

    int firstNonSilentIndex = -1;
    for (int i = 0; i < blockSize; ++i) {
        if (std::abs(buffer[i]) > 1e-7f) {
            firstNonSilentIndex = i;
            break;
        }
    }

    REQUIRE(firstNonSilentIndex >= 0);
    REQUIRE(firstNonSilentIndex >= expectedOffset);
    REQUIRE(std::abs(firstNonSilentIndex - expectedOffset) <= 1);
}
