// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: MidiTrackRenderer clip-placement timing test

#include "dsp/SubtractiveSynth.h"
#include "engine/Instrument.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/MidiTrackRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::dsp::SubtractiveSynth;
using howl::engine::Instrument;
using howl::engine::Transport;
using howl::model::kTicksPerQuarter;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::MidiTrackRenderer;
using howl::model::Note;
using howl::model::Track;
using howl::model::TrackKind;

namespace {

// Records every noteOn call it receives, otherwise a silent no-op instrument
class ProbeInstrument : public Instrument {
public:
    void prepare(double, int) override {
    }

    void noteOn(int key, float) noexcept override {
        onKeys.push_back(key);
    }

    void noteOff(int) noexcept override {
    }

    void render(AudioBlock& audio) noexcept override {
        for (int channel = 0; channel < audio.numChannels; ++channel) {
            for (int frame = 0; frame < audio.numFrames; ++frame) {
                audio.channels[channel][frame] = 0.0f;
            }
        }
    }

    int numParameters() const override {
        return 0;
    }

    const char* parameterName(int) const override {
        return "";
    }

    void setParameter(int, float) noexcept override {
    }

    float getParameter(int) const noexcept override {
        return 0.0f;
    }

    std::vector<int> onKeys;
};

} // namespace

TEST_CASE("MidiTrackRenderer fires noteOn at the placement's absolute sample offset", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 32768;

    MidiClip clip;
    clip.setLengthTicks(kTicksPerQuarter * 4); // P10-T2's renderer trims notes past this, give it room
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

TEST_CASE("MidiTrackRenderer fires a parked note once but never retriggers it while the transport stays stopped", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 512;

    MidiClip clip;
    clip.setLengthTicks(kTicksPerQuarter * 4);
    clip.addNote(Note { 60, 1.0f, 0, kTicksPerQuarter }); // sits exactly at tick 0

    Track track;
    track.name = "Lead";
    track.kind = TrackKind::Midi;
    track.midiClips.push_back(MidiClipPlacement { 0, clip });

    Transport transport;
    transport.setTempo(120.0);
    // Deliberately never calling play(): the playhead is parked at 0, exactly on the note

    ProbeInstrument instrument;
    instrument.prepare(sampleRate, blockSize);

    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(sampleRate);
    renderer.setInstrument(&instrument);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };
    AudioBlock block { channels, 1, blockSize };

    // A stopped transport's advance() returns the same frozen position every call, exactly
    // what every real audio callback sees while paused; process() fires the note the first
    // time it lands there, but must not retrigger it on the repeated calls that follow at
    // the same pos
    for (int i = 0; i < 5; ++i) {
        const SampleCount pos = transport.advance(blockSize);
        REQUIRE(pos == 0);
        renderer.process(block, pos);
    }

    REQUIRE(instrument.onKeys.size() == 1);

    // Pressing Play right when parked exactly on the note must still fire it: the first
    // playing block can report the same pos as the last frozen one, and must not be skipped
    transport.play();
    const SampleCount playingPos = transport.advance(blockSize);
    REQUIRE(playingPos == 0);
    renderer.process(block, playingPos);

    REQUIRE(instrument.onKeys.size() == 2);
}
