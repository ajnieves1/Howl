// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: MidiTrackRenderer plays pattern placements, live linked to the pattern's own notes

#include "engine/Instrument.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/MidiTrackRenderer.h"
#include "model/Pattern.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::engine::Instrument;
using howl::engine::Transport;
using howl::model::kTicksPerQuarter;
using howl::model::MidiTrackRenderer;
using howl::model::Note;
using howl::model::PatternBank;
using howl::model::PatternPlacement;
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

TEST_CASE("MidiTrackRenderer fires both tracks' pattern lanes at the placement's offset", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 65536;

    PatternBank patterns;
    const std::size_t patternIndex = patterns.addPattern("Beat", 2);
    patterns.pattern(patternIndex).trackClips[0].setLengthTicks(kTicksPerQuarter * 4);
    patterns.pattern(patternIndex).trackClips[0].addNote(Note { 60, 1.0f, 0, kTicksPerQuarter });
    patterns.pattern(patternIndex).trackClips[1].setLengthTicks(kTicksPerQuarter * 4);
    patterns.pattern(patternIndex).trackClips[1].addNote(Note { 61, 1.0f, 0, kTicksPerQuarter });
    patterns.addPlacement(PatternPlacement { patternIndex, kTicksPerQuarter * 2 });

    Track trackA;
    trackA.name = "Kick";
    trackA.kind = TrackKind::Midi;
    Track trackB;
    trackB.name = "Hat";
    trackB.kind = TrackKind::Midi;

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    ProbeInstrument instrumentA;
    instrumentA.prepare(sampleRate, blockSize);
    ProbeInstrument instrumentB;
    instrumentB.prepare(sampleRate, blockSize);

    MidiTrackRenderer rendererA(transport, trackA);
    rendererA.prepare(sampleRate);
    rendererA.setInstrument(&instrumentA);
    rendererA.setPatternSource(&patterns, 0);

    MidiTrackRenderer rendererB(transport, trackB);
    rendererB.prepare(sampleRate);
    rendererB.setInstrument(&instrumentB);
    rendererB.setPatternSource(&patterns, 1);

    std::vector<float> bufferA(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> bufferB(static_cast<std::size_t>(blockSize), 0.0f);
    float* channelsA[1] = { bufferA.data() };
    float* channelsB[1] = { bufferB.data() };
    AudioBlock blockA { channelsA, 1, blockSize };
    AudioBlock blockB { channelsB, 1, blockSize };

    const SampleCount pos = transport.advance(blockSize);
    REQUIRE(pos == 0);
    rendererA.process(blockA, pos);
    rendererB.process(blockB, pos);

    REQUIRE(std::find(instrumentA.onKeys.begin(), instrumentA.onKeys.end(), 60) != instrumentA.onKeys.end());
    REQUIRE(std::find(instrumentB.onKeys.begin(), instrumentB.onKeys.end(), 61) != instrumentB.onKeys.end());
}

TEST_CASE("MidiTrackRenderer fires two placements of the same pattern independently", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 65536;

    PatternBank patterns;
    const std::size_t patternIndex = patterns.addPattern("Beat", 1);
    patterns.pattern(patternIndex).trackClips[0].setLengthTicks(kTicksPerQuarter * 4);
    patterns.pattern(patternIndex).trackClips[0].addNote(Note { 60, 1.0f, 0, kTicksPerQuarter });
    patterns.addPlacement(PatternPlacement { patternIndex, 0 });
    patterns.addPlacement(PatternPlacement { patternIndex, kTicksPerQuarter * 2 });

    Track track;
    track.name = "Kick";
    track.kind = TrackKind::Midi;

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    ProbeInstrument instrument;
    instrument.prepare(sampleRate, blockSize);

    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(sampleRate);
    renderer.setInstrument(&instrument);
    renderer.setPatternSource(&patterns, 0);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };
    AudioBlock block { channels, 1, blockSize };

    const SampleCount pos = transport.advance(blockSize);
    REQUIRE(pos == 0);
    renderer.process(block, pos);

    REQUIRE(std::count(instrument.onKeys.begin(), instrument.onKeys.end(), 60) == 2);
}

TEST_CASE("A muted pattern placement is silent while an unmuted one beside it still plays", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 65536;

    PatternBank patterns;
    const std::size_t patternIndex = patterns.addPattern("Beat", 1);
    patterns.pattern(patternIndex).trackClips[0].setLengthTicks(kTicksPerQuarter * 4);
    patterns.pattern(patternIndex).trackClips[0].addNote(Note { 60, 1.0f, 0, kTicksPerQuarter });
    patterns.addPlacement(PatternPlacement { patternIndex, 0, 0, true, 0 });
    patterns.addPlacement(PatternPlacement { patternIndex, kTicksPerQuarter * 2, 0, false, 0 });

    Track track;
    track.name = "Kick";
    track.kind = TrackKind::Midi;

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    ProbeInstrument instrument;
    instrument.prepare(sampleRate, blockSize);

    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(sampleRate);
    renderer.setInstrument(&instrument);
    renderer.setPatternSource(&patterns, 0);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };
    AudioBlock block { channels, 1, blockSize };

    const SampleCount pos = transport.advance(blockSize);
    renderer.process(block, pos);

    REQUIRE(std::count(instrument.onKeys.begin(), instrument.onKeys.end(), 60) == 1);
}

TEST_CASE("A placement's lane index never filters playback, every channel lane still fires", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 65536;

    // The placement is drawn on lane 1, yet the renderer reading lane 0 must still hear it
    PatternBank patterns;
    const std::size_t patternIndex = patterns.addPattern("Beat", 2);
    patterns.pattern(patternIndex).trackClips[0].setLengthTicks(kTicksPerQuarter * 4);
    patterns.pattern(patternIndex).trackClips[0].addNote(Note { 60, 1.0f, 0, kTicksPerQuarter });
    patterns.addPlacement(PatternPlacement { patternIndex, 0, 1, false, 0 });

    Track track;
    track.name = "Kick";
    track.kind = TrackKind::Midi;

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    ProbeInstrument instrument;
    instrument.prepare(sampleRate, blockSize);

    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(sampleRate);
    renderer.setInstrument(&instrument);
    renderer.setPatternSource(&patterns, 0);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };
    AudioBlock block { channels, 1, blockSize };

    const SampleCount pos = transport.advance(blockSize);
    renderer.process(block, pos);

    REQUIRE(std::find(instrument.onKeys.begin(), instrument.onKeys.end(), 60) != instrument.onKeys.end());
}

TEST_CASE("Editing a pattern between passes changes the next render, proving live linkage", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 65536;

    PatternBank patterns;
    const std::size_t patternIndex = patterns.addPattern("Beat", 1);
    patterns.pattern(patternIndex).trackClips[0].setLengthTicks(kTicksPerQuarter * 4);
    patterns.pattern(patternIndex).trackClips[0].addNote(Note { 60, 1.0f, 0, kTicksPerQuarter });
    patterns.addPlacement(PatternPlacement { patternIndex, 0 });

    Track track;
    track.name = "Kick";
    track.kind = TrackKind::Midi;

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    ProbeInstrument firstPass;
    firstPass.prepare(sampleRate, blockSize);

    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(sampleRate);
    renderer.setInstrument(&firstPass);
    renderer.setPatternSource(&patterns, 0);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };
    AudioBlock block { channels, 1, blockSize };

    SampleCount pos = transport.advance(blockSize);
    renderer.process(block, pos);
    REQUIRE(std::find(firstPass.onKeys.begin(), firstPass.onKeys.end(), 60) != firstPass.onKeys.end());
    REQUIRE(std::find(firstPass.onKeys.begin(), firstPass.onKeys.end(), 67) == firstPass.onKeys.end());

    // Edit the pattern directly, exactly what a live linked instance means: every placement follows
    patterns.pattern(patternIndex).trackClips[0].addNote(Note { 67, 1.0f, 0, kTicksPerQuarter });

    transport.stop();
    transport.setPosition(0);
    transport.play();

    ProbeInstrument secondPass;
    secondPass.prepare(sampleRate, blockSize);
    renderer.setInstrument(&secondPass);

    pos = transport.advance(blockSize);
    renderer.process(block, pos);
    REQUIRE(std::find(secondPass.onKeys.begin(), secondPass.onKeys.end(), 60) != secondPass.onKeys.end());
    REQUIRE(std::find(secondPass.onKeys.begin(), secondPass.onKeys.end(), 67) != secondPass.onKeys.end());
}
