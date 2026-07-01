// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: MidiTrackRenderer clip-placement timing test

#include "dsp/SubtractiveSynth.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/MidiTrackRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::dsp::SubtractiveSynth;
using howl::engine::Transport;
using howl::model::kTicksPerQuarter;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::MidiTrackRenderer;
using howl::model::Note;
using howl::model::Track;
using howl::model::TrackKind;

TEST_CASE("MidiTrackRenderer fires noteOn at the placement's absolute sample offset", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 32768;

    MidiClip clip;
    clip.addNote(Note { 69, 1.0f, 0, kTicksPerQuarter }); // note right at the clip's own start

    Track track;
    track.name = "Lead";
    track.kind = TrackKind::Midi;
    track.midiClips.push_back(MidiClipPlacement { kTicksPerQuarter, clip }); // clip placed one beat in

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    SubtractiveSynth synth;
    synth.prepare(sampleRate, blockSize);

    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(sampleRate);
    renderer.setInstrument(&synth);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };

    const SampleCount pos = transport.advance(blockSize);
    REQUIRE(pos == 0);

    AudioBlock block { channels, 1, blockSize };
    renderer.process(block, pos);

    // 120 bpm at 44100 Hz: one beat (960 ticks) is 22050 samples exactly
    const int expectedOffset = 22050;

    int firstNonSilentIndex = -1;
    for (int i = 0; i < blockSize; ++i) {
        if (std::abs(buffer[static_cast<std::size_t>(i)]) > 1e-7f) {
            firstNonSilentIndex = i;
            break;
        }
    }

    REQUIRE(firstNonSilentIndex >= 0);
    REQUIRE(firstNonSilentIndex >= expectedOffset);
    REQUIRE(std::abs(firstNonSilentIndex - expectedOffset) <= 1);
}
