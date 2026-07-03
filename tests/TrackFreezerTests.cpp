// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: TrackFreezer offline render through instrument and strip FX

#include "dsp/GainEffect.h"
#include "dsp/SubtractiveSynth.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/MidiClip.h"
#include "model/Mixer.h"
#include "model/Note.h"
#include "model/TrackFreezer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

using howl::dsp::GainEffect;
using howl::dsp::SubtractiveSynth;
using howl::engine::Transport;
using howl::model::Arrangement;
using howl::model::kTicksPerQuarter;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::Mixer;
using howl::model::Note;
using howl::model::TrackFreezer;
using howl::model::TrackKind;

namespace {

double rmsOf(const std::vector<float>& channel) {
    double sumSquares = 0.0;
    for (float sample : channel) {
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return std::sqrt(sumSquares / static_cast<double>(channel.size()));
}

} // namespace

TEST_CASE("TrackFreezer renders a MIDI track through its instrument and a -6 dB strip effect", "[trackfreezer]") {
    const double sampleRate = 44100.0;
    const int blockSize = 512;

    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    MidiClip clip;
    clip.setLengthTicks(kTicksPerQuarter * 4);
    clip.addNote(Note { 69, 1.0f, 0, kTicksPerQuarter * 4 });
    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 0, clip });

    Transport transport;
    transport.setTempo(120.0);

    Mixer mixerNoFx;
    mixerNoFx.prepare(1, sampleRate, blockSize, 1);

    SubtractiveSynth synthNoFx;
    synthNoFx.prepare(sampleRate, blockSize);

    auto rendered = TrackFreezer::renderTrack(arrangement, mixerNoFx, transport, trackIndex, &synthNoFx,
        sampleRate, blockSize, 1);

    REQUIRE(rendered.size() == 1);
    const double rmsNoFx = rmsOf(rendered[0]);
    REQUIRE(rmsNoFx > 0.0);

    // 1 bar at 120 bpm is 88200 samples, plus one second of tail, rounded up to whole blocks
    REQUIRE(rendered[0].size() >= static_cast<std::size_t>(88200.0 + sampleRate));

    Mixer mixerWithFx;
    mixerWithFx.prepare(1, sampleRate, blockSize, 1);
    auto gain = std::make_unique<GainEffect>();
    gain->prepare(sampleRate, blockSize);
    gain->setParameter(0, 54.0f / 72.0f); // -6 dB in the -60..12 dB range
    mixerWithFx.trackStrip(0).effects().add(std::move(gain));

    SubtractiveSynth synthWithFx;
    synthWithFx.prepare(sampleRate, blockSize);

    auto renderedWithFx = TrackFreezer::renderTrack(arrangement, mixerWithFx, transport, trackIndex, &synthWithFx,
        sampleRate, blockSize, 1);

    REQUIRE(renderedWithFx.size() == 1);
    const double rmsWithFx = rmsOf(renderedWithFx[0]);

    REQUIRE(rmsWithFx == Catch::Approx(rmsNoFx * 0.5).margin(rmsNoFx * 0.05));
}

TEST_CASE("TrackFreezer returns empty for a track with no clips", "[trackfreezer]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Empty", TrackKind::Midi);

    Transport transport;
    transport.setTempo(120.0);

    Mixer mixer;
    mixer.prepare(1, 44100.0, 512, 1);

    SubtractiveSynth synth;
    synth.prepare(44100.0, 512);

    auto rendered = TrackFreezer::renderTrack(arrangement, mixer, transport, trackIndex, &synth, 44100.0, 512, 1);
    REQUIRE(rendered.empty());
}
