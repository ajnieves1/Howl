// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: ResizeMidiClipCommand round-trip and the renderer's clip-length note cutoff

#include "engine/Instrument.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/Commands.h"
#include "model/MidiTrackRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::engine::Instrument;
using howl::engine::Transport;
using howl::model::Arrangement;
using howl::model::kTicksPerQuarter;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::MidiTrackRenderer;
using howl::model::Note;
using howl::model::ResizeMidiClipCommand;
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

TEST_CASE("ResizeMidiClipCommand sets a placed clip's length and undo restores the old length", "[model]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    MidiClip clip;
    clip.setLengthTicks(kTicksPerQuarter * 4);
    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 0, clip });

    ResizeMidiClipCommand command(arrangement, trackIndex, 0, kTicksPerQuarter * 4, kTicksPerQuarter * 2);
    command.execute();
    REQUIRE(arrangement.track(trackIndex).midiClips[0].clip.lengthTicks() == kTicksPerQuarter * 2);

    command.undo();
    REQUIRE(arrangement.track(trackIndex).midiClips[0].clip.lengthTicks() == kTicksPerQuarter * 4);
}

TEST_CASE("MidiTrackRenderer drops notes starting past the clip end and keeps ones straddling it", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 65536; // long enough to cover 4 beats at 120 bpm

    MidiClip clip;
    clip.setLengthTicks(kTicksPerQuarter * 4); // room for both notes while building the clip
    clip.addNote(Note { 60, 1.0f, 0, kTicksPerQuarter * 3 }); // starts well before the cut, straddles it
    clip.addNote(Note { 61, 1.0f, kTicksPerQuarter * 2, kTicksPerQuarter }); // starts exactly at the cut
    clip.setLengthTicks(kTicksPerQuarter * 2); // shorten after placing notes, per the gesture rule

    Track track;
    track.name = "Lead";
    track.kind = TrackKind::Midi;
    track.midiClips.push_back(MidiClipPlacement { 0, clip });

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    ProbeInstrument instrument;
    instrument.prepare(sampleRate, blockSize);

    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(sampleRate);
    renderer.setInstrument(&instrument);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };
    AudioBlock block { channels, 1, blockSize };

    const SampleCount pos = transport.advance(blockSize);
    REQUIRE(pos == 0);
    renderer.process(block, pos);

    REQUIRE(std::find(instrument.onKeys.begin(), instrument.onKeys.end(), 60) != instrument.onKeys.end());
    REQUIRE(std::find(instrument.onKeys.begin(), instrument.onKeys.end(), 61) == instrument.onKeys.end());
}
